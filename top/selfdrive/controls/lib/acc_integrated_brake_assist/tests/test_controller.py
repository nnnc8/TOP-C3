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
from openpilot.common.params import Params
from cereal import custom
import cereal.messaging as messaging


def make_controller() -> AccIntegratedBrakeAssistController:
  return AccIntegratedBrakeAssistController(BrakeAssistConfig(enabled=True))


def test_config_from_params_uses_persisted_toggle_and_limits():
  params = Params()
  params.put_bool("FridayLongitudinalBrakeAssist", True)
  params.put("FridayBrakeAssistMaxDecelHigh", 1.7)
  params.put("FridayBrakeAssistStopM", 6.5)

  config = BrakeAssistConfig.from_params(params)

  assert config.enabled
  assert config.max_decel_high == 1.7
  assert config.stop_m == 6.5


def test_optional_distance_without_stop_intent_does_not_request_stop():
  controller = make_controller()

  output = controller.update(
    v_ego=11.0,
    a_ego=0.0,
    long_enabled=True,
    long_override=False,
    gas_pressed=False,
    brake_pressed=False,
    traffic_intent=TrafficIntent(color="unknown", confidence=0.0),
    intersection=IntersectionDistance(distance_m=18.0, confidence=0.95, source="optional_distance"),
  )

  assert not output.active
  assert not output.should_stop
  assert output.release_reason == "no_stop_intent"


def test_high_confidence_red_light_distance_requests_capped_hard_brake():
  controller = make_controller()

  output = controller.update(
    v_ego=10.0,
    a_ego=0.0,
    long_enabled=True,
    long_override=False,
    gas_pressed=False,
    brake_pressed=False,
    traffic_intent=TrafficIntent(color="red", confidence=0.9, source="camera"),
    intersection=IntersectionDistance(distance_m=12.0, confidence=0.95, source="optional_distance"),
  )

  assert output.active
  assert output.state == "hard_brake"
  assert output.confidence_band == "high"
  assert output.a_target == -2.0
  assert not output.should_stop


def test_red_light_stop_zone_requests_should_stop():
  controller = make_controller()

  output = controller.update(
    v_ego=2.0,
    a_ego=-0.4,
    long_enabled=True,
    long_override=False,
    gas_pressed=False,
    brake_pressed=False,
    traffic_intent=TrafficIntent(color="red", confidence=0.9, source="camera"),
    intersection=IntersectionDistance(distance_m=5.0, confidence=0.9, source="optional_distance"),
  )

  assert output.active
  assert output.state == "final_stop"
  assert output.should_stop
  assert output.a_target <= -0.4


def test_green_light_releases_latched_red_light_assist():
  controller = make_controller()
  controller.update(
    v_ego=7.0,
    a_ego=0.0,
    long_enabled=True,
    long_override=False,
    gas_pressed=False,
    brake_pressed=False,
    traffic_intent=TrafficIntent(color="red", confidence=0.85, source="camera"),
    intersection=IntersectionDistance(distance_m=20.0, confidence=0.9, source="optional_distance"),
  )

  output = controller.update(
    v_ego=7.0,
    a_ego=0.0,
    long_enabled=True,
    long_override=False,
    gas_pressed=False,
    brake_pressed=False,
    traffic_intent=TrafficIntent(color="green", confidence=0.9, source="camera"),
    intersection=IntersectionDistance(distance_m=18.0, confidence=0.9, source="optional_distance"),
  )

  assert not output.active
  assert not output.should_stop
  assert output.release_reason == "green_light"


def test_low_confidence_far_red_light_only_limits_acceleration():
  controller = make_controller()

  output = controller.update(
    v_ego=16.0,
    a_ego=0.1,
    long_enabled=True,
    long_override=False,
    gas_pressed=False,
    brake_pressed=False,
    traffic_intent=TrafficIntent(color="red", confidence=0.35, source="camera_range", range="far"),
    intersection=IntersectionDistance(),
  )

  assert output.active
  assert output.state == "prepare"
  assert output.confidence_band == "low"
  assert output.a_target == -0.15
  assert not output.should_stop


def test_camera_mid_range_red_light_uses_low_confidence_light_brake_without_map_distance():
  controller = make_controller()

  output = controller.update(
    v_ego=10.0,
    a_ego=0.0,
    long_enabled=True,
    long_override=False,
    gas_pressed=False,
    brake_pressed=False,
    traffic_intent=TrafficIntent(color="red", confidence=0.45, source="camera_range", range="mid"),
    intersection=IntersectionDistance(),
  )

  assert output.active
  assert output.state == "brake"
  assert output.confidence_band == "low"
  assert output.target_distance_m == 30.0
  assert output.a_target == -0.45
  assert not output.should_stop


def test_camera_near_range_red_light_uses_low_confidence_near_cap_without_map_distance():
  controller = make_controller()

  output = controller.update(
    v_ego=7.0,
    a_ego=0.0,
    long_enabled=True,
    long_override=False,
    gas_pressed=False,
    brake_pressed=False,
    traffic_intent=TrafficIntent(color="red", confidence=0.50, source="camera_range", range="near"),
    intersection=IntersectionDistance(),
  )

  assert output.active
  assert output.state == "hard_brake"
  assert output.confidence_band == "low"
  assert output.target_distance_m == 15.0
  assert output.a_target == -1.1
  assert not output.should_stop


def test_driver_gas_releases_assist():
  controller = make_controller()

  output = controller.update(
    v_ego=8.0,
    a_ego=-0.2,
    long_enabled=True,
    long_override=False,
    gas_pressed=True,
    brake_pressed=False,
    traffic_intent=TrafficIntent(color="red", confidence=0.9, source="camera"),
    intersection=IntersectionDistance(distance_m=12.0, confidence=0.9, source="optional_distance"),
  )

  assert not output.active
  assert not output.should_stop
  assert output.release_reason == "driver_override"


def test_fill_brake_assist_message_maps_output_to_capnp_enums():
  msg = messaging.new_message("longitudinalPlanTOP")
  output = BrakeAssistOutput(
    active=True,
    state="hard_brake",
    a_target=-1.5,
    v_target=0.0,
    should_stop=False,
    allow_throttle=False,
    confidence_band="high",
    target_distance_m=11.5,
    source="camera+optional_distance",
  )

  fill_brake_assist_message(msg.longitudinalPlanTOP.brakeAssist, output)

  brake_assist = msg.longitudinalPlanTOP.brakeAssist
  assert brake_assist.active
  assert brake_assist.state == custom.LongitudinalPlanTOP.BrakeAssist.State.hardBrake
  assert brake_assist.confidenceBand == custom.LongitudinalPlanTOP.BrakeAssist.ConfidenceBand.high
  assert brake_assist.aTarget == -1.5
  assert brake_assist.targetDistanceM == 11.5
  assert brake_assist.source == "camera+optional_distance"


def test_capnp_inputs_convert_to_controller_dataclasses():
  traffic_msg = messaging.new_message("fridayTrafficIntent")
  traffic_msg.fridayTrafficIntent.color = custom.FridayTrafficIntent.Color.red
  traffic_msg.fridayTrafficIntent.confidence = 0.8
  traffic_msg.fridayTrafficIntent.source = "camera"
  traffic_msg.fridayTrafficIntent.ageS = 0.2

  distance_msg = messaging.new_message("fridayIntersectionDistance")
  distance_msg.fridayIntersectionDistance.active = True
  distance_msg.fridayIntersectionDistance.distanceM = 21.0
  distance_msg.fridayIntersectionDistance.confidence = 0.9
  distance_msg.fridayIntersectionDistance.source = "optional_distance"

  traffic = traffic_intent_from_msg(traffic_msg.fridayTrafficIntent)
  distance = intersection_from_msg(distance_msg.fridayIntersectionDistance)

  assert traffic == TrafficIntent(color="red", confidence=0.8, source="camera", age_s=0.2)
  assert distance == IntersectionDistance(distance_m=21.0, confidence=0.9, source="optional_distance")
