#!/usr/bin/env python3
import time
from pathlib import Path

from cereal import log

from openpilot.common.swaglog import cloudlog
from openpilot.system.hardware import HARDWARE
from openpilot.tools.model_selector import (
  atomic_write_json,
  cache_status_path,
  default_cache_root,
  read_cache_status,
  sync_manifest,
)


SYNC_INTERVAL_SECONDS = 5 * 60


def record_sync_error(cache_root: Path, error: Exception) -> None:
  status = read_cache_status(cache_root)
  status["error"] = str(error)
  try:
    atomic_write_json(cache_status_path(cache_root), status)
  except Exception:
    cloudlog.exception("failed to persist model manifest error")


def main() -> None:
  cache_root = default_cache_root()
  network_none = log.DeviceState.NetworkType.none

  while True:
    started = time.monotonic()
    try:
      if HARDWARE.get_network_type() != network_none:
        sync_manifest(cache_root=cache_root)
        cloudlog.info("model manifest synced from happymaj11r")
    except Exception as exc:
      record_sync_error(cache_root, exc)
      cloudlog.warning("model manifest sync failed: %s", exc)

    elapsed = time.monotonic() - started
    time.sleep(max(1.0, SYNC_INTERVAL_SECONDS - elapsed))


if __name__ == "__main__":
  main()
