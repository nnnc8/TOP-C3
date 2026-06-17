import numpy as np
import inspect

from cereal import car, custom, log
import cereal.messaging as messaging
import openpilot.selfdrive.controls.plannerd as plannerd

from openpilot.common.params import Params
from openpilot.selfdrive.modeld.constants import ModelConstants
from openpilot.top.selfdrive.controls.lib.longitudinal_planner import LongitudinalPlannerTOP, LongitudinalPlanSource


def generate_model_v2(speed: float = 10.0):
  model = messaging.new_message("modelV2")
  position = log.XYZTData.new_message()
  position.x = [float(x) for x in (speed + 0.5) * np.array(ModelConstants.T_IDXS)]
  model.modelV2.position = position
  orientation_rate = log.XYZTData.new_message()
  orientation_rate.z = [0.001 for _ in ModelConstants.T_IDXS]
  model.modelV2.orientationRate = orientation_rate
  velocity = log.XYZTData.new_message()
  velocity.x = [float(x) for x in (speed + 0.5) * np.ones_like(ModelConstants.T_IDXS)]
  velocity.x[0] = float(speed)
  model.modelV2.velocity = velocity
  acceleration = log.XYZTData.new_message()
  acceleration.x = [0.0 for _ in ModelConstants.T_IDXS]
  model.modelV2.acceleration = acceleration
  return model.modelV2


def make_sm(red_light: bool = True, signal_range=custom.FridayTrafficIntent.Range.near):
  car_control = messaging.new_message("carControl").carControl
  car_control.enabled = True
  car_state = messaging.new_message("carState").carState
  car_state.vEgo = 10.0
  controls_state = messaging.new_message("controlsState").controlsState
  controls_state.curvature = 0.0

  traffic_msg = messaging.new_message("fridayTrafficIntent").fridayTrafficIntent
  traffic_msg.color = custom.FridayTrafficIntent.Color.red if red_light else custom.FridayTrafficIntent.Color.unknown
  traffic_msg.confidence = 0.9 if red_light else 0.0
  traffic_msg.source = "camera"
  traffic_msg.range = signal_range

  return {
    "carControl": car_control,
    "carState": car_state,
    "controlsState": controls_state,
    "modelV2": generate_model_v2(),
    "fridayTrafficIntent": traffic_msg,
  }


def test_planner_ignores_brake_assist_when_toggle_is_off():
  params = Params()
  params.put_bool("FridayLongitudinalBrakeAssist", False)
  params.put_bool("SmartCruiseControlVision", False)
  planner = LongitudinalPlannerTOP(car.CarParams.new_message())

  v_target, a_target = planner.update_targets(make_sm(), v_ego=10.0, a_ego=0.0, v_cruise=25.0)

  assert planner.source == LongitudinalPlanSource.cruise
  assert v_target == 25.0
  assert a_target == 0.0
  assert not planner.brake_assist_output.active


def test_planner_selects_brake_assist_when_red_light_range_is_active():
  params = Params()
  params.put_bool("FridayLongitudinalBrakeAssist", True)
  params.put_bool("SmartCruiseControlVision", False)
  planner = LongitudinalPlannerTOP(car.CarParams.new_message())

  v_target, a_target = planner.update_targets(make_sm(), v_ego=10.0, a_ego=0.0, v_cruise=25.0)

  assert planner.source == LongitudinalPlanSource.brakeAssist
  assert v_target == 0.0
  assert a_target == -1.1
  assert planner.brake_assist_output.state == "hard_brake"


def test_plannerd_subscribes_to_brake_assist_inputs():
  source = inspect.getsource(plannerd.main)

  assert "'fridayTrafficIntent'" in source
  assert "'fridayIntersectionDistance'" not in source
