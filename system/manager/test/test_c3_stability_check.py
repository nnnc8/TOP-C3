import importlib.util
from pathlib import Path


SCRIPT_PATH = Path(__file__).resolve().parents[3] / "scripts" / "c3_stability_check.py"
SPEC = importlib.util.spec_from_file_location("c3_stability_check", SCRIPT_PATH)
c3_stability_check = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(c3_stability_check)


def test_process_failures_offroad_does_not_require_onroad_processes():
  processes = {
    "manager.py": True,
    "ui": True,
    "pandad": True,
    "hardwared": True,
    "controlsd": False,
    "plannerd": False,
  }

  assert c3_stability_check.process_failures(processes, "offroad") == []


def test_process_failures_onroad_requires_controlsd_and_plannerd():
  processes = {
    "manager.py": True,
    "ui": True,
    "pandad": True,
    "hardwared": True,
    "card": True,
    "controlsd": True,
    "plannerd": False,
  }

  assert c3_stability_check.process_failures(processes, "onroad") == ["plannerd"]


def test_process_mode_auto_promotes_to_onroad_when_controlsd_is_running():
  processes = {
    "manager.py": True,
    "ui": True,
    "pandad": True,
    "hardwared": True,
    "card": True,
    "controlsd": True,
    "plannerd": False,
  }

  assert c3_stability_check.resolve_process_mode(processes, "auto") == "onroad"


def test_submodule_status_rejects_uninitialized_or_mismatched_pointer():
  assert c3_stability_check.submodule_status_issue("-abc123 opendbc_repo") == "opendbc_repo is not initialized"
  assert c3_stability_check.submodule_status_issue("+abc123 opendbc_repo") == "opendbc_repo is not at the recorded commit"
  assert c3_stability_check.submodule_status_issue(" abc123 opendbc_repo") is None


def test_error_log_issue_rejects_nonempty_error_log(tmp_path):
  missing = tmp_path / "missing.txt"
  empty = tmp_path / "empty.txt"
  error_log = tmp_path / "error.txt"
  empty.write_text("")
  error_log.write_text("traceback")

  assert c3_stability_check.error_log_issue(missing) is None
  assert c3_stability_check.error_log_issue(empty) is None
  assert c3_stability_check.error_log_issue(error_log) == f"{error_log} is non-empty"
