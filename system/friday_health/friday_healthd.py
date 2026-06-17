#!/usr/bin/env python3
import os
import json
import time
import glob as pyglob
from openpilot.common.realtime import Ratekeeper
from openpilot.common.params import Params
import cereal.messaging as messaging
from openpilot.system.friday_health.model import (
  get_default_health_state,
  calculate_score_and_reasons,
  rotate_daily_buckets,
  check_and_create_backup
)

class FridayHealthDaemon:
  def __init__(self):
    self.params = Params()
    self.rk = Ratekeeper(1, print_delay_threshold=None) # run at 1Hz, but we throttle writes to 10s
    self.sm = messaging.SubMaster([
      'deviceState', 'pandaStates', 'managerState', 'selfdriveState', 'carState'
    ])
    
    # Load or initialize state
    state_val = self.params.get("FridayHealthState")
    if state_val:
      try:
        self.state = json.loads(state_val)
      except Exception:
        self.state = get_default_health_state()
    else:
      self.state = get_default_health_state()

    # Ensure schema conformance
    if "version" not in self.state or self.state.get("version", 0) < 1:
      self.state = get_default_health_state()

    # Initial settings backup check
    check_and_create_backup(self.params, force=False)

    self.last_write_time = 0
    self.started_prev = False
    
    # Edge detection states
    self.overriding_prev = False
    self.hard_braking_prev = False
    
    # Trip temp stats accumulated during onroad
    self.trip_alerts = 0
    self.trip_interventions = 0
    self.trip_hard_brakes = 0
    self.trip_panda_faults = 0
    self.trip_can_overflow = 0
    self.trip_can_invalid = 0

    # Temperature timing stats for daily buckets
    self.yellow_temp_start = None
    self.red_temp_start = None
    self.day_yellow_mins = 0.0
    self.day_red_mins = 0.0

  def get_day_key(self):
    return int(time.strftime("%Y%m%d"))

  def get_error_log_count(self):
    # Count tombstones and crash logs
    tombstones = len(pyglob.glob("/data/tombstones/tombstone_*"))
    crashes = len(pyglob.glob("/data/community/crashes/*"))
    flogs = len(pyglob.glob("/data/media/0/flogs/*"))
    return tombstones + crashes + flogs

  def update_trip_on_transition(self, to_offroad):
    # Transitioning to offroad: save current trip to last trip
    curr = self.state["trip"]["current"]
    
    # Finalize last trip with accumulated metrics
    last = {
      "max_temp": curr["max_temp"],
      "min_space": curr["min_space"],
      "panda_fault_count": self.trip_panda_faults,
      "can_overflow_count": self.trip_can_overflow,
      "can_invalid_count": self.trip_can_invalid,
      "alert_count": self.trip_alerts,
      "manual_intervention_count": self.trip_interventions,
      "hard_brake_count": self.trip_hard_brakes,
      "min_score": curr["min_score"],
      "max_score": curr["max_score"]
    }
    self.state["trip"]["last"] = last
    
    # Aggregate into daily bucket
    day_key = self.get_day_key()
    
    # Find existing or create new daily bucket
    bucket = None
    for b in self.state["daily_buckets"]:
      if b["day_key"] == day_key:
        bucket = b
        break
    
    if bucket is None:
      bucket = {
        "day_key": day_key,
        "max_temp": last["max_temp"],
        "yellow_temp_mins": self.day_yellow_mins,
        "red_temp_mins": self.day_red_mins,
        "min_space": last["min_space"],
        "max_fan": 0.0,
        "panda_fault_count": last["panda_fault_count"],
        "can_overflow_count": last["can_overflow_count"],
        "can_invalid_count": last["can_invalid_count"],
        "reboot_count": 0,
        "route_count": 1,
        "error_log_count": self.get_error_log_count()
      }
    else:
      bucket["max_temp"] = max(bucket["max_temp"], last["max_temp"])
      bucket["yellow_temp_mins"] += self.day_yellow_mins
      bucket["red_temp_mins"] += self.day_red_mins
      bucket["min_space"] = min(bucket["min_space"], last["min_space"])
      bucket["panda_fault_count"] += last["panda_fault_count"]
      bucket["can_overflow_count"] += last["can_overflow_count"]
      bucket["can_invalid_count"] += last["can_invalid_count"]
      bucket["route_count"] += 1
      bucket["error_log_count"] = self.get_error_log_count()

    self.state["daily_buckets"] = rotate_daily_buckets(self.state["daily_buckets"], bucket)
    
    # Reset current trip
    self.state["trip"]["current"] = {
      "max_temp": 0.0,
      "min_space": 100.0,
      "panda_fault_count": 0,
      "can_overflow_count": 0,
      "can_invalid_count": 0,
      "alert_count": 0,
      "manual_intervention_count": 0,
      "hard_brake_count": 0,
      "min_score": 100,
      "max_score": 100
    }
    
    # Reset trip accumulator fields
    self.trip_alerts = 0
    self.trip_interventions = 0
    self.trip_hard_brakes = 0
    self.trip_panda_faults = 0
    self.trip_can_overflow = 0
    self.trip_can_invalid = 0
    self.day_yellow_mins = 0.0
    self.day_red_mins = 0.0

  def step(self):
    self.sm.update(0)
    
    # Only run if FridayHealthCenter is enabled
    if not self.params.get_bool("FridayHealthCenter"):
      time.sleep(1.0)
      return

    # Check start/stop transition
    started = self.sm['deviceState'].started if self.sm.updated['deviceState'] else False
    if started and not self.started_prev:
      # Transitioned to onroad: count a new route start
      pass
    elif not started and self.started_prev:
      # Transitioned to offroad: finalize and flush
      self.update_trip_on_transition(to_offroad=True)
      self.params.put("FridayHealthState", json.dumps(self.state))
      self.last_write_time = time.time()

    self.started_prev = started

    # Extract current metrics
    cpu_temp = 0.0
    free_space = 100.0
    fan_power = 0
    if self.sm.updated['deviceState']:
      ds = self.sm['deviceState']
      cpu_temp = max(ds.cpuTempC, default=0.0)
      free_space = ds.freeSpacePercent
      fan_power = ds.fanSpeedPercentDeprecated if hasattr(ds, 'fanSpeedPercentDeprecated') else 0

    panda_faults = 0
    can_overflow = False
    can_invalid = False
    if self.sm.updated['pandaStates']:
      p_states = self.sm['pandaStates']
      panda_faults = sum(len(ps.faults) for ps in p_states)
      can_overflow = any(ps.rxBufferOverflow > 0 or ps.txBufferOverflow > 0 for ps in p_states)
      can_invalid = any(ps.safetyRxInvalid > 0 for ps in p_states)
      
      # Accumulate trip counters
      self.trip_panda_faults += panda_faults
      if can_overflow:
        self.trip_can_overflow += 1
      if can_invalid:
        self.trip_can_invalid += 1

    processes_crashed = []
    if self.sm.updated['managerState']:
      processes_crashed = [p.name for p in self.sm['managerState'].processes if p.shouldBeRunning and not p.running]

    active_alerts = []
    if self.sm.updated['selfdriveState']:
      ss = self.sm['selfdriveState']
      if ss.alertSize != messaging.log.SelfdriveState.AlertSize.none:
        is_critical = ss.alertStatus == messaging.log.SelfdriveState.AlertStatus.critical
        active_alerts.append({
          "text": ss.alertText1,
          "critical": is_critical
        })
        if is_critical:
          self.trip_alerts += 1

    # Edge detection of interventions and hard brakes from carState
    if self.sm.updated['carState'] and self.sm.updated['selfdriveState']:
      cs = self.sm['carState']
      ss = self.sm['selfdriveState']
      
      # Intervention count
      overriding = ss.enabled and (cs.brakePressed or cs.gasPressed or cs.steeringPressed)
      if overriding and not self.overriding_prev:
        self.trip_interventions += 1
      self.overriding_prev = overriding
      
      # Hard brake count
      hard_braking = not cs.standstill and cs.aEgo < -2.5
      if hard_braking and not self.hard_braking_prev:
        self.trip_hard_brakes += 1
      self.hard_braking_prev = hard_braking

    # Build evaluation sample
    sample = {
      "cpu_temp": cpu_temp,
      "free_space_percent": free_space,
      "panda_faults": panda_faults,
      "can_overflow": can_overflow,
      "can_invalid": can_invalid,
      "processes_crashed": processes_crashed,
      "camera_malfunction": any("相機" in a["text"] or "Camera" in a["text"] for a in active_alerts),
      "gps_lost": any("GPS" in a["text"] for a in active_alerts),
      "active_alerts": active_alerts
    }

    # Evaluate Score
    score, level, reasons, critical = calculate_score_and_reasons(sample)
    
    # Update state fields
    self.state["score"] = score
    self.state["level"] = level
    self.state["reasons"] = reasons
    self.state["critical"] = critical

    # Update current trip health if onroad
    if started:
      curr = self.state["trip"]["current"]
      curr["max_temp"] = max(curr["max_temp"], cpu_temp)
      curr["min_space"] = min(curr["min_space"], free_space)
      curr["min_score"] = min(curr["min_score"], score)
      curr["max_score"] = max(curr["max_score"], score)
      curr["alert_count"] = self.trip_alerts
      curr["manual_intervention_count"] = self.trip_interventions
      curr["hard_brake_count"] = self.trip_hard_brakes
      curr["panda_fault_count"] = self.trip_panda_faults
      curr["can_overflow_count"] = self.trip_can_overflow
      curr["can_invalid_count"] = self.trip_can_invalid

    # Accumulate daily temperature statistics
    if cpu_temp > 95.0:
      if self.red_temp_start is None:
        self.red_temp_start = time.time()
      else:
        self.day_red_mins += (time.time() - self.red_temp_start) / 60.0
        self.red_temp_start = time.time()
    else:
      self.red_temp_start = None

    if cpu_temp > 80.0:
      if self.yellow_temp_start is None:
        self.yellow_temp_start = time.time()
      else:
        self.day_yellow_mins += (time.time() - self.yellow_temp_start) / 60.0
        self.yellow_temp_start = time.time()
    else:
      self.yellow_temp_start = None

    # Update daily bucket max fan power if onroad
    if started:
      day_key = self.get_day_key()
      bucket = None
      for b in self.state["daily_buckets"]:
        if b["day_key"] == day_key:
          bucket = b
          break
      if bucket:
        bucket["max_fan"] = max(bucket["max_fan"], float(fan_power))

    # Periodic write throttling (every 10 seconds)
    now = time.time()
    if (now - self.last_write_time) >= 10.0:
      self.params.put("FridayHealthState", json.dumps(self.state))
      self.last_write_time = now

  def run(self):
    while True:
      try:
        self.step()
        self.rk.keep_time()
      except Exception as e:
        print(f"FridayHealthDaemon error: {e}")
        time.sleep(1.0)

def main():
  daemon = FridayHealthDaemon()
  daemon.run()

if __name__ == "__main__":
  main()
