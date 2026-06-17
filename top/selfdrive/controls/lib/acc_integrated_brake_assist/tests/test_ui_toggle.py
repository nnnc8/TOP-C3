from pathlib import Path


def test_brake_assist_toggle_is_exposed_in_timpilot_panel():
  repo_root = Path(__file__).resolve().parents[6]
  settings_source = repo_root / "selfdrive/ui/qt/offroad/settings.cc"
  source = settings_source.read_text()

  assert 'ParamControl("FridayLongitudinalBrakeAssist"' in source
  assert "Longitudinal Brake Assist" in source
