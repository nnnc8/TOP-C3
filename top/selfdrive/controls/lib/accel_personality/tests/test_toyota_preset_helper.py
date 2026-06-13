import unittest
from openpilot.top.selfdrive.controls.lib.accel_personality.toyota_preset_helper import ToyotaScenePresets

class TestToyotaPresetHelper(unittest.TestCase):
  def test_preset_mapping(self):
    # Power/Sport -> 山路 (accel eco=2, longitudinal standard=1)
    accel_p, long_p, label = ToyotaScenePresets.get_preset(0)
    self.assertEqual(accel_p, 2)
    self.assertEqual(long_p, 1)
    self.assertEqual(label, "山路")

    # Normal -> 市區 (accel normal=1, longitudinal aggressive=0)
    accel_p, long_p, label = ToyotaScenePresets.get_preset(1)
    self.assertEqual(accel_p, 1)
    self.assertEqual(long_p, 0)
    self.assertEqual(label, "市區")

    # Eco -> 高速 (accel eco=2, longitudinal aggressive=0)
    accel_p, long_p, label = ToyotaScenePresets.get_preset(2)
    self.assertEqual(accel_p, 2)
    self.assertEqual(long_p, 0)
    self.assertEqual(label, "高速")

    # Invalid/stock -> None
    accel_p, long_p, label = ToyotaScenePresets.get_preset(3)
    self.assertIsNone(accel_p)
    self.assertIsNone(long_p)
    self.assertIsNone(label)

if __name__ == "__main__":
  unittest.main()
