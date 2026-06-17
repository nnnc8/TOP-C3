"""
Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.

This file is part of sunnypilot and is licensed under the MIT License.
See the LICENSE.md file in the root directory for more details.
"""

from cereal import messaging, custom
from opendbc.car import structs
from openpilot.common.params import Params
from openpilot.top.selfdrive.controls.lib.acc_integrated_brake_assist.controller import (
  AccIntegratedBrakeAssistController,
  BrakeAssistConfig,
  BrakeAssistOutput,
  IntersectionDistance,
  TrafficIntent,
  fill_brake_assist_message,
  intersection_from_msg,
  traffic_intent_from_msg,
)
from openpilot.top.selfdrive.controls.lib.accel_personality.accel_controller import AccelController
from openpilot.top.selfdrive.controls.lib.smart_cruise_control.smart_cruise_control import SmartCruiseControl

LongitudinalPlanSource = custom.LongitudinalPlanTOP.LongitudinalPlanSource


def _sm_get(sm: messaging.SubMaster, name: str, default=None):
  try:
    return sm[name]
  except (KeyError, AttributeError):
    return default


class LongitudinalPlannerTOP:
  def __init__(self, CP: structs.CarParams):
    _ = CP
    self.params = Params()
    self.accel_controller = AccelController()
    self.scc = SmartCruiseControl()
    self.brake_assist = AccIntegratedBrakeAssistController(BrakeAssistConfig.from_params(self.params))
    self.brake_assist_output = BrakeAssistOutput()
    self.source = LongitudinalPlanSource.cruise

    self.output_v_target = 0.
    self.output_a_target = 0.
  def update_targets(self, sm: messaging.SubMaster, v_ego: float, a_ego: float, v_cruise: float) -> tuple[float, float]:
    long_enabled = sm['carControl'].enabled
    long_override = sm['carControl'].cruiseControl.override

    # Smart Cruise Control
    self.scc.update(sm, long_enabled, long_override, v_ego, a_ego, v_cruise)
    self.brake_assist.config = BrakeAssistConfig.from_params(self.params)
    carstate = sm['carState']
    traffic_msg = _sm_get(sm, 'fridayTrafficIntent')
    distance_msg = _sm_get(sm, 'fridayIntersectionDistance')
    traffic_intent = traffic_intent_from_msg(traffic_msg) if traffic_msg is not None else TrafficIntent()
    intersection = intersection_from_msg(distance_msg) if distance_msg is not None else IntersectionDistance()
    self.brake_assist_output = self.brake_assist.update(
      v_ego=v_ego,
      a_ego=a_ego,
      long_enabled=long_enabled,
      long_override=long_override,
      gas_pressed=bool(carstate.gasPressed),
      brake_pressed=bool(carstate.brakePressed),
      traffic_intent=traffic_intent,
      intersection=intersection,
    )

    targets = {
      LongitudinalPlanSource.cruise: (v_cruise, a_ego),
      LongitudinalPlanSource.sccVision: (self.scc.vision.output_v_target, self.scc.vision.output_a_target),
    }
    if self.brake_assist_output.active:
      targets[LongitudinalPlanSource.brakeAssist] = (
        self.brake_assist_output.v_target if self.brake_assist_output.v_target is not None else v_cruise,
        self.brake_assist_output.a_target,
      )

    self.source = min(targets, key=lambda k: targets[k][0])
    self.output_v_target, self.output_a_target = targets[self.source]
    return self.output_v_target, self.output_a_target

  def update(self, sm: messaging.SubMaster) -> None:
    if hasattr(sm, 'updated') and sm.updated['carState']:
      carstate = sm['carState']
      self.accel_controller.update(carstate)
    else:
      self.accel_controller.update()

  def publish_longitudinal_plan_top(self, sm: messaging.SubMaster, pm: messaging.PubMaster) -> None:
    plan_top_send = messaging.new_message('longitudinalPlanTOP')

    plan_top_send.valid = sm.all_checks(service_list=['carState', 'controlsState'])

    longitudinalPlanTOP = plan_top_send.longitudinalPlanTOP
    longitudinalPlanTOP.longitudinalPlanSource = self.source
    longitudinalPlanTOP.vTarget = float(self.output_v_target)
    longitudinalPlanTOP.aTarget = float(self.output_a_target)
    fill_brake_assist_message(longitudinalPlanTOP.brakeAssist, self.brake_assist_output)

    # Smart Cruise Control
    smartCruiseControl = longitudinalPlanTOP.smartCruiseControl
    # Vision Control
    sccVision = smartCruiseControl.vision
    sccVision.state = self.scc.vision.state
    sccVision.vTarget = float(self.scc.vision.output_v_target)
    sccVision.aTarget = float(self.scc.vision.output_a_target)
    sccVision.currentLateralAccel = float(self.scc.vision.current_lat_acc)
    sccVision.maxPredictedLateralAccel = float(self.scc.vision.max_pred_lat_acc)
    sccVision.enabled = self.scc.vision.is_enabled
    sccVision.active = self.scc.vision.is_active

    plan_top_send.longitudinalPlanTOP.accelPersonality = self.accel_controller.personality
    pm.send('longitudinalPlanTOP', plan_top_send)
