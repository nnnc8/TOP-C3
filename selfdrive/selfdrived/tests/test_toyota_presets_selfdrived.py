import unittest
from unittest.mock import Mock, patch
from openpilot.selfdrive.selfdrived.selfdrived import SelfdriveD

class TestSelfdriveDToyotaPresets(unittest.TestCase):
  @patch("openpilot.selfdrive.selfdrived.selfdrived.Params")
  @patch("openpilot.selfdrive.selfdrived.selfdrived.messaging")
  @patch("openpilot.selfdrive.selfdrived.selfdrived.VisionIpcClient")
  def test_toyota_presets_selfdrived(self, mock_vip_cls, mock_messaging, mock_params_cls):
    mock_params_inst = Mock()
    mock_params_cls.return_value = mock_params_inst

    with patch("openpilot.selfdrive.selfdrived.selfdrived.config_realtime_process"), \
         patch("openpilot.selfdrive.selfdrived.selfdrived.StateMachine"), \
         patch("openpilot.selfdrive.selfdrived.selfdrived.AlertManager"), \
         patch("openpilot.selfdrive.selfdrived.selfdrived.Events"), \
         patch("openpilot.selfdrive.selfdrived.selfdrived.Ratekeeper"):
      s = SelfdriveD()

    s.initialized = True
    s.toyota_drive_mode = True
    s.friday_toyota_scene_presets = True
    s.last_preset_accel_profile = None
    s.params = mock_params_inst

    CS = Mock()
    CS.accelProfile = 0
    s.data_sample = Mock(return_value=CS)
    s.update_events = Mock()
    s.update_alerts = Mock()
    s.publish_selfdriveState = Mock()

    # 1. First step should write params
    s.step()
    mock_params_inst.put_nonblocking.assert_any_call("AccelPersonality", 2)
    mock_params_inst.put_nonblocking.assert_any_call("LongitudinalPersonality", 1)
    self.assertEqual(s.last_preset_accel_profile, 0)

    # Same profile again -> should NOT write params
    mock_params_inst.put_nonblocking.reset_mock()
    s.step()
    mock_params_inst.put_nonblocking.assert_not_called()

    # 2. Toggle off -> should reset last_preset_accel_profile
    s.friday_toyota_scene_presets = False
    mock_params_inst.put_nonblocking.reset_mock()
    s.step()
    mock_params_inst.put_nonblocking.assert_not_called()
    self.assertIsNone(s.last_preset_accel_profile)

    # 3. ToyotaDriveMode disabled -> should not write params
    s.toyota_drive_mode = False
    s.friday_toyota_scene_presets = True
    CS.accelProfile = 1
    mock_params_inst.put_nonblocking.reset_mock()
    s.step()
    mock_params_inst.put_nonblocking.assert_not_called()

    # 4. No accelProfile -> should not write params
    s.toyota_drive_mode = True
    s.friday_toyota_scene_presets = True
    CS.accelProfile = None
    mock_params_inst.put_nonblocking.reset_mock()
    s.step()
    mock_params_inst.put_nonblocking.assert_not_called()

if __name__ == "__main__":
  unittest.main()
