from openpilot.selfdrive.test.process_replay.process_replay import CONFIGS


def test_plannerd_process_replay_knows_brake_assist_topics():
  plannerd_config = next(cfg for cfg in CONFIGS if cfg.proc_name == "plannerd")

  assert "fridayTrafficIntent" in plannerd_config.pubs
  assert "longitudinalPlanTOP" in plannerd_config.subs
