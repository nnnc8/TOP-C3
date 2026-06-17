from openpilot.top.selfdrive.controls.lib.acc_integrated_brake_assist.traffic_intentd import (
  build_traffic_intent,
  load_external_traffic_light_state,
)


def test_external_red_light_state_overrides_model_unknown():
  state = {
    "red_present": True,
    "confidence": 0.82,
    "source": "vision_sidecar",
    "updated_at_ms": 100000,
  }

  intent = build_traffic_intent(None, enabled=True, external_state=state, now_ms=101000)

  assert intent.color == "red"
  assert intent.confidence == 0.82
  assert intent.source == "vision_sidecar"


def test_external_green_light_state_can_release_when_fresh():
  state = {
    "green_present": True,
    "confidence": 0.76,
    "source": "vision_sidecar",
    "updated_at_ms": 100000,
  }

  intent = build_traffic_intent(None, enabled=True, external_state=state, now_ms=101000)

  assert intent.color == "green"
  assert intent.confidence == 0.76


def test_external_traffic_light_state_preserves_camera_range():
  state = {
    "red_present": True,
    "confidence": 0.52,
    "source": "traffic_light_sidecar",
    "target_signal_range": "NEAR",
    "updated_at_ms": 100000,
  }

  intent = build_traffic_intent(None, enabled=True, external_state=state, now_ms=101000)

  assert intent.color == "red"
  assert intent.range == "near"
  assert intent.source == "traffic_light_sidecar"


def test_stale_external_state_falls_back_to_model_stop_intent():
  class ModelAction:
    shouldStop = True

  class ModelMsg:
    action = ModelAction()

  state = {"red_present": True, "confidence": 0.9, "updated_at_ms": 100000}

  intent = build_traffic_intent(ModelMsg(), enabled=True, external_state=state, now_ms=120000)

  assert intent.color == "mixed"
  assert intent.source == "model_action_should_stop"


def test_load_external_traffic_light_state_returns_empty_for_missing_or_invalid_file(tmp_path):
  assert load_external_traffic_light_state(tmp_path / "missing.json") == {}

  invalid = tmp_path / "invalid.json"
  invalid.write_text("not json")

  assert load_external_traffic_light_state(invalid) == {}
