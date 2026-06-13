import unittest
from unittest.mock import Mock, patch
from openpilot.top.selfdrive.controls.lib.accel_personality.accel_controller import AccelController, AccelPersonality

class TestAccelControllerPresets(unittest.TestCase):
  @patch("openpilot.top.selfdrive.controls.lib.accel_personality.accel_controller.Params")
  def test_toyota_drive_mode_no_presets(self, mock_params_cls):
    mock_params_inst = Mock()
    mock_params_cls.return_value = mock_params_inst

    mock_params_inst.get_bool.side_effect = lambda key: {
      "ToyotaDriveMode": True,
      "AegisToyotaScenePresets": False
    }[key]

    controller = AccelController()
    self.assertTrue(controller.toyota_drive_mode_enabled)
    self.assertFalse(controller.aegis_toyota_scene_presets_enabled)

    carstate = Mock()
    carstate.accelProfile = 0

    updated = controller.update_from_carstate(carstate)
    self.assertTrue(updated)
    mock_params_inst.put_nonblocking.assert_called_with('AccelPersonality', 0)
    self.assertEqual(controller.personality, 0)

  @patch("openpilot.top.selfdrive.controls.lib.accel_personality.accel_controller.Params")
  def test_toyota_scene_presets_enabled(self, mock_params_cls):
    mock_params_inst = Mock()
    mock_params_cls.return_value = mock_params_inst

    mock_params_inst.get_bool.side_effect = lambda key: {
      "ToyotaDriveMode": True,
      "AegisToyotaScenePresets": True
    }[key]

    controller = AccelController()
    self.assertTrue(controller.toyota_drive_mode_enabled)
    self.assertTrue(controller.aegis_toyota_scene_presets_enabled)

    carstate = Mock()
    carstate.accelProfile = 0

    updated = controller.update_from_carstate(carstate)
    self.assertFalse(updated)
    mock_params_inst.put_nonblocking.assert_not_called()

if __name__ == "__main__":
  unittest.main()
