import json
import unittest
from unittest.mock import Mock, patch
from openpilot.common.params import Params
from openpilot.system.friday_health.model import (
  get_default_health_state,
  calculate_score_and_reasons,
  rotate_daily_buckets,
  check_and_create_backup,
  diff_backup_to_current,
  restore_backup,
  WATCHED_KEYS
)

class TestFridayHealthModel(unittest.TestCase):
  def test_default_health_state(self):
    state = get_default_health_state()
    self.assertEqual(state["version"], 1)
    self.assertEqual(state["score"], 100)
    self.assertEqual(state["level"], "normal")
    self.assertEqual(len(state["reasons"]), 0)
    self.assertEqual(len(state["critical"]), 0)

  def test_calculate_score_and_reasons(self):
    # Perfect score scenario
    sample = {
      "cpu_temp": 50.0,
      "free_space_percent": 80.0,
      "panda_faults": 0,
      "can_overflow": False,
      "can_invalid": False,
      "processes_crashed": [],
      "camera_malfunction": False,
      "gps_lost": False,
      "active_alerts": []
    }
    score, level, reasons, critical = calculate_score_and_reasons(sample)
    self.assertEqual(score, 100)
    self.assertEqual(level, "normal")

    # High temp + low space warn
    sample["cpu_temp"] = 85.0
    sample["free_space_percent"] = 10.0
    score, level, reasons, critical = calculate_score_and_reasons(sample)
    self.assertEqual(score, 80) # 100 - 10 (temp) - 10 (space) = 80
    self.assertEqual(level, "warning")
    self.assertIn("車機溫度偏高 (>80°C)", reasons)
    self.assertIn("儲存空間不足 (<15%)", reasons)

    # Critical failures (temp >95, space <5, process crashed)
    sample["cpu_temp"] = 98.0
    sample["free_space_percent"] = 3.0
    sample["processes_crashed"] = ["controlsd"]
    score, level, reasons, critical = calculate_score_and_reasons(sample)
    self.assertEqual(score, 30) # 100 - 25 (temp) - 25 (space) - 20 (crashed) = 30
    self.assertEqual(level, "critical")
    self.assertIn("車機溫度過高 (>95°C)", critical)
    self.assertIn("儲存空間嚴重不足 (<5%)", critical)
    self.assertIn("系統進程異常崩潰: controlsd", critical)

  def test_rotate_daily_buckets(self):
    buckets = []
    for i in range(1, 40):
      new_bucket = {"day_key": 20260600 + i, "max_temp": 50.0 + i}
      buckets = rotate_daily_buckets(buckets, new_bucket)

    # Must contain exactly 30 buckets
    self.assertEqual(len(buckets), 30)
    # The oldest should be 10, the newest should be 39
    self.assertEqual(buckets[0]["day_key"], 20260610)
    self.assertEqual(buckets[-1]["day_key"], 20260639)

  @patch("openpilot.system.friday_health.model.Params")
  def test_settings_backups_diff_restore(self, mock_params_cls):
    mock_params_inst = Mock()
    mock_params_cls.return_value = mock_params_inst

    # Mock parameters storage values
    stored_params = {
      "GitCommit": b"commit_v1",
      "FridayToyotaScenePresets": b"0",
      "LongitudinalPersonality": b"1"
    }
    mock_params_inst.get.side_effect = lambda key, **kwargs: stored_params.get(key)

    # 1. Create first backup
    created = check_and_create_backup(mock_params_inst, force=True)
    self.assertTrue(created)

    # Verify that backups are saved
    self.assertTrue(mock_params_inst.put.called)
    call_args = mock_params_inst.put.call_args_list[0]
    backups_json = call_args[0][1]
    backups = json.loads(backups_json)
    self.assertEqual(len(backups), 1)
    self.assertEqual(backups[0]["commit"], "commit_v1")
    self.assertEqual(backups[0]["params"]["FridayToyotaScenePresets"], "0")

    # Check secret keys are NOT backed up
    for k in backups[0]["params"].keys():
      self.assertIn(k, WATCHED_KEYS)

    # 2. Modify param and check diff
    mock_params_inst.get.side_effect = lambda key, **kwargs: {
      "FridayHealthSettingsBackups": backups_json.encode('utf-8'),
      "GitCommit": b"commit_v1",
      "FridayToyotaScenePresets": b"1", # changed!
      "LongitudinalPersonality": b"1"
    }.get(key)

    diff = diff_backup_to_current(mock_params_inst, 0)
    self.assertIn("FridayToyotaScenePresets", diff)
    self.assertEqual(diff["FridayToyotaScenePresets"]["backup"], "0")
    self.assertEqual(diff["FridayToyotaScenePresets"]["current"], "1")

    # 3. Restore backup
    restored = restore_backup(mock_params_inst, 0)
    self.assertTrue(restored)

if __name__ == "__main__":
  unittest.main()
