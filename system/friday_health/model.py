import json
import time
from openpilot.common.params import Params

WATCHED_KEYS = [
  "FridayQuietCabin",
  "FridayAchievements",
  "FridayAchievementToasts",
  "FridayJourneyBoard",
  "FridayToyotaScenePresets",
  "ToyotaDriveMode",
  "AleSato_AutomaticBrakeHold",
  "AccelPersonality",
  "LongitudinalPersonality",
  "Dynamic_Follow",
  "SmartCruiseControlVision",
  "ReverseAccChange",
  "AlwaysOnDM",
  "ExperimentalMode",
  "NudgelessLaneChange",
  "road_edge_detection",
  "TimSignals"
]

def get_default_health_state():
  return {
    "version": 1,
    "score": 100,
    "level": "normal",
    "reasons": [],
    "critical": [],
    "trip": {
      "current": {
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
      },
      "last": {
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
    },
    "daily_buckets": []
  }

def calculate_score_and_reasons(sample):
  """
  sample has:
    - cpu_temp (float)
    - free_space_percent (float)
    - panda_faults (int)
    - can_overflow (bool)
    - can_invalid (bool)
    - processes_crashed (list of strings)
    - camera_malfunction (bool)
    - gps_lost (bool)
    - active_alerts (list of dicts with 'text' and 'critical' bool)
  """
  score = 100
  reasons = []
  critical = []

  # 1. Storage
  free_space = sample.get("free_space_percent", 100.0)
  if free_space < 5.0:
    score -= 25
    critical.append("儲存空間嚴重不足 (<5%)")
  elif free_space < 15.0:
    score -= 10
    reasons.append("儲存空間不足 (<15%)")

  # 2. Thermal
  temp = sample.get("cpu_temp", 0.0)
  if temp > 95.0:
    score -= 25
    critical.append("車機溫度過高 (>95°C)")
  elif temp > 80.0:
    score -= 10
    reasons.append("車機溫度偏高 (>80°C)")

  # 3. Panda/CAN
  if sample.get("panda_faults", 0) > 0:
    score -= 20
    critical.append("偵測到 Panda 錯誤代碼")
  if sample.get("can_overflow", False):
    score -= 15
    reasons.append("CAN 緩衝區溢位 (Overflow)")
  if sample.get("can_invalid", False):
    score -= 10
    reasons.append("CAN 安全防護校驗異常")

  # 4. Process Crashes
  crashed = sample.get("processes_crashed", [])
  if crashed:
    score -= 20
    critical.append(f"系統進程異常崩潰: {', '.join(crashed)}")

  # 5. Camera / GPS / Alerts
  if sample.get("camera_malfunction", False):
    score -= 15
    critical.append("相機感光元件異常")
  if sample.get("gps_lost", False):
    score -= 10
    reasons.append("GPS 訊號遺失")

  # Active alerts
  for alert in sample.get("active_alerts", []):
    if alert.get("critical", False):
      score -= 15
      critical.append(f"嚴重警告: {alert.get('text', '')}")
    else:
      score -= 5
      reasons.append(f"提示: {alert.get('text', '')}")

  score = max(0, min(100, score))
  level = "normal"
  if score < 70:
    level = "critical"
  elif score < 85:
    level = "warning"

  return score, level, reasons, critical

def rotate_daily_buckets(buckets, new_bucket):
  # Keep only unique day keys
  day_key = new_bucket["day_key"]
  buckets = [b for b in buckets if b["day_key"] != day_key]
  buckets.append(new_bucket)
  buckets.sort(key=lambda x: x["day_key"])
  if len(buckets) > 30:
    buckets = buckets[-30:]
  return buckets

# Settings Backup/Diff/Restore Helpers
def load_backups(params):
  val = params.get("FridayHealthSettingsBackups")
  if not val:
    return []
  try:
    return json.loads(val)
  except Exception:
    return []

def save_backups(params, backups):
  params.put("FridayHealthSettingsBackups", json.dumps(backups[-20:])) # keep last 20 backups

def get_current_watched_params(params):
  res = {}
  for key in WATCHED_KEYS:
    val = params.get(key)
    res[key] = val.decode('utf-8') if val is not None else None
  return res

def check_and_create_backup(params, force=False):
  backups = load_backups(params)
  curr_params = get_current_watched_params(params)
  git_commit = params.get("GitCommit")
  git_commit = git_commit.decode('utf-8') if git_commit else "unknown"

  needed = force or len(backups) == 0
  if not needed:
    latest = backups[-1]
    if latest.get("commit") != git_commit:
      needed = True
    else:
      # Compare params
      latest_params = latest.get("params", {})
      for k, v in curr_params.items():
        if latest_params.get(k) != v:
          needed = True
          break

  if needed:
    new_backup = {
      "time": int(time.time()),
      "commit": git_commit,
      "params": curr_params,
      "manual": force
    }
    backups.append(new_backup)
    save_backups(params, backups)
    return True
  return False

def diff_backup_to_current(params, backup_index):
  backups = load_backups(params)
  if backup_index < 0 or backup_index >= len(backups):
    return {}
  b_params = backups[backup_index].get("params", {})
  curr_params = get_current_watched_params(params)

  diff = {}
  for k in WATCHED_KEYS:
    b_val = b_params.get(k)
    c_val = curr_params.get(k)
    if b_val != c_val:
      diff[k] = {"backup": b_val, "current": c_val}
  return diff

def restore_backup(params, backup_index):
  backups = load_backups(params)
  if backup_index < 0 or backup_index >= len(backups):
    return False
  b_params = backups[backup_index].get("params", {})
  for k, v in b_params.items():
    if v is None:
      params.remove(k)
    else:
      params.put(k, v)
  return True
