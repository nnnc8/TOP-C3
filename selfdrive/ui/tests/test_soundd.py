from cereal import car
from cereal import messaging
from cereal.messaging import SubMaster, PubMaster
import numpy as np
from openpilot.selfdrive.ui.soundd import BRAKEHOLD_VOLUME_BOOST, SELFDRIVE_STATE_TIMEOUT, Soundd, check_selfdrive_timeout_alert

import time

AudibleAlert = car.CarControl.HUDControl.AudibleAlert


class TestSoundd:
  def test_check_selfdrive_timeout_alert(self):
    sm = SubMaster(['selfdriveState'])
    pm = PubMaster(['selfdriveState'])

    for _ in range(100):
      cs = messaging.new_message('selfdriveState')
      cs.selfdriveState.enabled = True

      pm.send("selfdriveState", cs)

      time.sleep(0.01)

      sm.update(0)

      assert not check_selfdrive_timeout_alert(sm)

    for _ in range(SELFDRIVE_STATE_TIMEOUT * 110):
      sm.update(0)
      time.sleep(0.01)

    assert check_selfdrive_timeout_alert(sm)

  # TODO: add test with micd for checking that soundd actually outputs sounds

  def test_engage_brakehold_has_extra_gain(self):
    soundd = Soundd.__new__(Soundd)
    soundd.loaded_sounds = {
      AudibleAlert.engage: np.array([0.25, -0.25], dtype=np.float32),
      AudibleAlert.engageBrakehold: np.array([0.7, -0.7], dtype=np.float32),
    }
    soundd.current_sound_frame = 0
    soundd.current_volume = 1.0
    soundd.quiet_drive = False

    soundd.current_alert = AudibleAlert.engage
    standard = soundd.get_sound_data(2)

    soundd.current_alert = AudibleAlert.engageBrakehold
    soundd.current_sound_frame = 0
    boosted = soundd.get_sound_data(2)

    assert np.allclose(standard, np.array([0.25, -0.25], dtype=np.float32))
    assert np.allclose(boosted, np.array([1.0, -1.0], dtype=np.float32))
    assert BRAKEHOLD_VOLUME_BOOST > 1.0
