#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path


OFFROAD_REQUIRED_PROCS = ("manager.py", "ui", "pandad", "hardwared")
ONROAD_REQUIRED_PROCS = (*OFFROAD_REQUIRED_PROCS, "card", "controlsd", "plannerd")
LEGACY_PARAM_KEYS = (
  "DrivingModel",
  "DrivingModelName",
  "toyotaautolock",
  "toyotaautounlock",
  "toyota_bsm",
  "ToyotaTune",
  "topsng",
  "sng_e2e",
)


def process_failures(processes: dict[str, bool], requested_mode: str) -> list[str]:
  mode = resolve_process_mode(processes, requested_mode)
  required = ONROAD_REQUIRED_PROCS if mode == "onroad" else OFFROAD_REQUIRED_PROCS
  return [name for name in required if not processes.get(name, False)]


def resolve_process_mode(processes: dict[str, bool], requested_mode: str) -> str:
  if requested_mode != "auto":
    return requested_mode
  if processes.get("controlsd", False) or processes.get("plannerd", False) or processes.get("card", False):
    return "onroad"
  return "offroad"


def submodule_status_issue(status: str) -> str | None:
  status = status.strip("\n")
  if not status:
    return "opendbc_repo status unavailable"
  marker = status[0]
  if marker == "-":
    return "opendbc_repo is not initialized"
  if marker == "+":
    return "opendbc_repo is not at the recorded commit"
  if marker == "U":
    return "opendbc_repo has merge conflicts"
  return None


def error_log_issue(path: Path) -> str | None:
  if not path.exists() or path.stat().st_size == 0:
    return None
  return f"{path} is non-empty"


def legacy_param_issues(param_root: Path) -> list[str]:
  if not param_root.exists():
    return []
  return [f"legacy param still exists: {param_root / key}" for key in LEGACY_PARAM_KEYS if (param_root / key).exists()]


def default_repo_root() -> Path:
  return Path(__file__).resolve().parents[1]


def default_error_log_paths() -> list[Path]:
  paths = [Path("/data/community/crashes/error.txt")]
  home_error_log = Path.home() / ".comma" / "community" / "crashes" / "error.txt"
  if home_error_log not in paths:
    paths.append(home_error_log)
  return paths


def default_param_roots() -> list[Path]:
  roots = [Path("/data/params/d")]
  home_params = Path.home() / ".comma" / "params" / "d"
  if home_params not in roots:
    roots.append(home_params)
  return roots


def run_git(repo: Path, args: list[str]) -> tuple[int, str]:
  completed = subprocess.run(
    ["git", *args],
    cwd=repo,
    text=True,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    check=False,
  )
  return completed.returncode, completed.stdout.strip()


def git_issues(repo: Path, expected_branch: str) -> list[str]:
  issues = []

  returncode, branch = run_git(repo, ["rev-parse", "--abbrev-ref", "HEAD"])
  if returncode != 0:
    issues.append(f"git branch check failed: {branch}")
  elif branch != expected_branch:
    issues.append(f"expected branch {expected_branch}, got {branch}")

  returncode, status = run_git(repo, ["status", "--short"])
  if returncode != 0:
    issues.append(f"git status failed: {status}")
  elif status:
    issues.append("git worktree is dirty")

  returncode, submodule_status = run_git(repo, ["submodule", "status", "opendbc_repo"])
  if returncode != 0:
    issues.append(f"opendbc_repo status failed: {submodule_status}")
  else:
    issue = submodule_status_issue(submodule_status)
    if issue is not None:
      issues.append(issue)

  return issues


def collect_process_status(manager_timeout: float) -> dict[str, bool]:
  processes = {"manager.py": process_matches("manager.py")}
  processes.update(read_manager_state_processes(manager_timeout))
  return processes


def process_matches(pattern: str) -> bool:
  try:
    completed = subprocess.run(["pgrep", "-f", pattern], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    if completed.returncode == 0:
      return True
  except OSError:
    pass

  try:
    completed = subprocess.run(["ps", "-axo", "command="], text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=False)
  except OSError:
    return False
  return any(pattern in line and "c3_stability_check.py" not in line for line in completed.stdout.splitlines())


def read_manager_state_processes(timeout: float) -> dict[str, bool]:
  try:
    import cereal.messaging as messaging
  except Exception:
    return {}

  sm = messaging.SubMaster(["managerState"])
  deadline = time.monotonic() + timeout
  while time.monotonic() < deadline:
    sm.update(1000)
    if sm.updated["managerState"]:
      return {process.name: process.running for process in sm["managerState"].processes}
  return {}


def print_result(label: str, issues: list[str]) -> None:
  if issues:
    print(f"FAIL {label}")
    for issue in issues:
      print(f"  - {issue}")
  else:
    print(f"PASS {label}")


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(description="C3 stability smoke check for the top-c3 branch.")
  parser.add_argument("--repo", type=Path, default=default_repo_root())
  parser.add_argument("--branch", default="top-c3")
  parser.add_argument("--process-mode", choices=("auto", "offroad", "onroad", "skip"), default="auto")
  parser.add_argument("--manager-timeout", type=float, default=8.0)
  parser.add_argument("--skip-git", action="store_true")
  parser.add_argument("--skip-crash-log", action="store_true")
  parser.add_argument("--skip-legacy-params", action="store_true")
  return parser.parse_args()


def main() -> int:
  args = parse_args()
  checks: list[tuple[str, list[str]]] = []

  if not args.skip_git:
    checks.append(("git", git_issues(args.repo, args.branch)))

  if not args.skip_crash_log:
    checks.append(("crash logs", [issue for path in default_error_log_paths() if (issue := error_log_issue(path)) is not None]))

  if not args.skip_legacy_params:
    issues = []
    for param_root in default_param_roots():
      issues.extend(legacy_param_issues(param_root))
    checks.append(("legacy params", issues))

  if args.process_mode != "skip":
    processes = collect_process_status(args.manager_timeout)
    mode = resolve_process_mode(processes, args.process_mode)
    checks.append((f"{mode} processes", process_failures(processes, args.process_mode)))

  failed = False
  for label, issues in checks:
    print_result(label, issues)
    failed = failed or bool(issues)

  return 1 if failed else 0


if __name__ == "__main__":
  sys.exit(main())
