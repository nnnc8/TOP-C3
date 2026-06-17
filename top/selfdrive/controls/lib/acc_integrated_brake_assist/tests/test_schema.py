from cereal import custom
import cereal.messaging as messaging
from cereal.services import SERVICE_LIST


def test_brake_assist_schema_and_services_are_available():
  plan_msg = messaging.new_message("longitudinalPlanTOP")
  brake_assist = plan_msg.longitudinalPlanTOP.brakeAssist
  brake_assist.active = True
  brake_assist.state = custom.LongitudinalPlanTOP.BrakeAssist.State.hardBrake
  brake_assist.confidenceBand = custom.LongitudinalPlanTOP.BrakeAssist.ConfidenceBand.high
  brake_assist.targetDistanceM = 12.0
  brake_assist.aTarget = -2.0

  assert brake_assist.active
  assert brake_assist.state == custom.LongitudinalPlanTOP.BrakeAssist.State.hardBrake
  assert brake_assist.confidenceBand == custom.LongitudinalPlanTOP.BrakeAssist.ConfidenceBand.high

  assert "fridayTrafficIntent" in SERVICE_LIST
  traffic_msg = messaging.new_message("fridayTrafficIntent")
  traffic_msg.fridayTrafficIntent.color = custom.FridayTrafficIntent.Color.red
  traffic_msg.fridayTrafficIntent.confidence = 0.9
  traffic_msg.fridayTrafficIntent.range = custom.FridayTrafficIntent.Range.near

  assert traffic_msg.fridayTrafficIntent.color == custom.FridayTrafficIntent.Color.red
  assert traffic_msg.fridayTrafficIntent.range == custom.FridayTrafficIntent.Range.near
