from openpilot.selfdrive.controls.lib.longitudinal_planner import (
  apply_brake_assist_allow_throttle,
  apply_brake_assist_output,
)
from openpilot.top.selfdrive.controls.lib.acc_integrated_brake_assist.controller import BrakeAssistOutput


def test_brake_assist_caps_final_accel_and_requests_stop():
  output = BrakeAssistOutput(active=True, a_target=-1.4, should_stop=True)

  a_target, should_stop = apply_brake_assist_output(0.2, False, output)

  assert a_target == -1.4
  assert should_stop


def test_inactive_brake_assist_leaves_final_output_unchanged():
  output = BrakeAssistOutput(active=False, a_target=-1.4, should_stop=True)

  a_target, should_stop = apply_brake_assist_output(0.2, False, output)

  assert a_target == 0.2
  assert not should_stop


def test_active_brake_assist_blocks_allow_throttle():
  output = BrakeAssistOutput(active=True, allow_throttle=False)

  assert not apply_brake_assist_allow_throttle(True, output)


def test_inactive_brake_assist_leaves_allow_throttle_unchanged():
  output = BrakeAssistOutput(active=False, allow_throttle=False)

  assert apply_brake_assist_allow_throttle(True, output)
