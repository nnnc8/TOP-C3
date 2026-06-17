from cereal import custom
import cereal.messaging as messaging
import pytest

from openpilot.top.selfdrive.controls.lib.acc_integrated_brake_assist.traffic_intentd import (
  build_traffic_intent,
  fill_traffic_intent_message,
)


def test_model_should_stop_builds_mixed_traffic_intent():
  model_msg = messaging.new_message("modelV2").modelV2
  model_msg.action.shouldStop = True

  intent = build_traffic_intent(model_msg, enabled=True)

  assert intent.color == "mixed"
  assert intent.confidence == 0.55
  assert intent.source == "model_action_should_stop"
  assert intent.range == "mid"


def test_traffic_intent_provider_is_unknown_when_disabled():
  model_msg = messaging.new_message("modelV2").modelV2
  model_msg.action.shouldStop = True

  intent = build_traffic_intent(model_msg, enabled=False)

  assert intent.color == "unknown"
  assert intent.confidence == 0.0
  assert intent.source == "disabled"


def test_fill_traffic_intent_message_maps_enums():
  msg = messaging.new_message("fridayTrafficIntent")
  model_msg = messaging.new_message("modelV2").modelV2
  model_msg.action.shouldStop = True

  fill_traffic_intent_message(msg.fridayTrafficIntent, build_traffic_intent(model_msg, enabled=True))

  assert msg.fridayTrafficIntent.color == custom.FridayTrafficIntent.Color.mixed
  assert msg.fridayTrafficIntent.confidence == pytest.approx(0.55)
  assert msg.fridayTrafficIntent.range == custom.FridayTrafficIntent.Range.mid


def test_fill_traffic_intent_message_preserves_visual_range():
  msg = messaging.new_message("fridayTrafficIntent")
  intent = build_traffic_intent(None, enabled=True, external_state={
    "red_present": True,
    "confidence": 0.51,
    "target_signal_range": "near",
    "updated_at_ms": 100000,
  }, now_ms=101000)

  fill_traffic_intent_message(msg.fridayTrafficIntent, intent)

  assert msg.fridayTrafficIntent.range == custom.FridayTrafficIntent.Range.near
