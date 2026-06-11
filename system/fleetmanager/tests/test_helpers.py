import os

import cereal.messaging as messaging
import zstandard as zstd

from openpilot.system.fleetmanager import helpers as fleet


def _write_qlog(path, samples):
  payload = bytearray()
  for idx, sample in enumerate(samples):
    msg = messaging.new_message("carState")
    msg.logMonoTime = idx * 100_000_000
    msg.carState.aEgo = sample.get("aEgo", 0.0)
    msg.carState.standstill = sample.get("standstill", False)
    msg.carState.steeringPressed = sample.get("steeringPressed", False)
    msg.carState.vEgo = sample.get("vEgo", 0.0)
    payload.extend(msg.to_bytes())
  with open(path, "wb") as handle:
    handle.write(zstd.compress(bytes(payload), 10))


def _touch(path, content=b"test"):
  os.makedirs(os.path.dirname(path), exist_ok=True)
  with open(path, "wb") as handle:
    handle.write(content)


def _write_segment(log_root, route_id, segment_num, cameras=None, qlog_samples=None):
  cameras = cameras or ["qcamera", "fcamera", "ecamera", "dcamera"]
  segment_dir = os.path.join(log_root, f"{route_id}--{segment_num}")
  os.makedirs(segment_dir, exist_ok=True)

  extension_map = {
    "qcamera": ".ts",
    "fcamera": ".hevc",
    "ecamera": ".hevc",
    "dcamera": ".hevc",
  }
  for camera in cameras:
    _touch(os.path.join(segment_dir, f"{camera}{extension_map[camera]}"))

  if qlog_samples is not None:
    _write_qlog(os.path.join(segment_dir, "qlog.zst"), qlog_samples)


def test_get_route_summaries_builds_library_cards(tmp_path, monkeypatch):
  route_id = "2026-06-07--12-00-00"
  cache_root = tmp_path / "cache"
  monkeypatch.setattr(fleet.Paths, "log_root", lambda: str(tmp_path))
  monkeypatch.setattr(fleet.Paths, "download_cache_root", lambda: str(cache_root))

  _write_segment(
    str(tmp_path),
    route_id,
    0,
    qlog_samples=[
      {"aEgo": 0.0, "vEgo": 8.0, "standstill": False},
      {"aEgo": -3.1, "vEgo": 8.0, "standstill": False},
      {"aEgo": -3.2, "vEgo": 7.5, "standstill": False},
      {"aEgo": -3.3, "vEgo": 7.0, "standstill": False},
      {"aEgo": -3.4, "vEgo": 6.5, "standstill": False},
      {"aEgo": 0.0, "vEgo": 0.0, "standstill": True},
      {"aEgo": 0.0, "vEgo": 5.0, "standstill": False},
      {"aEgo": 0.0, "vEgo": 5.0, "standstill": False, "steeringPressed": True},
    ],
  )
  _write_segment(str(tmp_path), route_id, 1, qlog_samples=[{"aEgo": 0.0, "vEgo": 3.0, "standstill": False}])

  summaries = fleet.get_route_summaries()

  summary = summaries[0]
  assert summary["routeId"] == route_id
  assert summary["segmentCount"] == 2
  assert summary["durationSec"] == 120
  assert summary["cameras"] == ["qcamera", "fcamera", "ecamera", "dcamera"]
  assert summary["previewUrl"].endswith(f"/api/routes/{route_id}/preview")
  assert summary["eventCounts"] == {
    "hard_brake": 0,
    "stop": 0,
    "start": 0,
    "manual_steer": 0,
  }


def test_get_route_summaries_does_not_scan_qlogs(tmp_path, monkeypatch):
  route_id = "2026-06-07--13-00-00"
  cache_root = tmp_path / "cache"
  monkeypatch.setattr(fleet.Paths, "log_root", lambda: str(tmp_path))
  monkeypatch.setattr(fleet.Paths, "download_cache_root", lambda: str(cache_root))

  _write_segment(
    str(tmp_path),
    route_id,
    0,
    qlog_samples=[
      {"aEgo": 0.0, "vEgo": 8.0, "standstill": False},
      {"aEgo": -3.1, "vEgo": 8.0, "standstill": False},
    ],
  )

  def fail_on_event_scan(_qlog_paths):
    raise AssertionError("route summaries must not scan qlogs")

  monkeypatch.setattr(fleet, "extract_route_events", fail_on_event_scan)

  summaries = fleet.get_route_summaries()

  assert summaries[0]["routeId"] == route_id
  assert summaries[0]["eventCounts"] == {
    "hard_brake": 0,
    "stop": 0,
    "start": 0,
    "manual_steer": 0,
  }


def test_get_route_summaries_limit_skips_old_route_materialization(tmp_path, monkeypatch):
  cache_root = tmp_path / "cache"
  monkeypatch.setattr(fleet.Paths, "log_root", lambda: str(tmp_path))
  monkeypatch.setattr(fleet.Paths, "download_cache_root", lambda: str(cache_root))

  route_ids = [
    "2026-06-07--10-00-00",
    "2026-06-07--11-00-00",
    "2026-06-07--12-00-00",
    "2026-06-07--13-00-00",
  ]
  for route_id in route_ids:
    _write_segment(str(tmp_path), route_id, 0, cameras=["qcamera"])
    _write_segment(str(tmp_path), route_id, 1, cameras=["qcamera"])

  real_segment_record_from_entry = fleet._segment_record_from_entry
  materialized_entries = []

  def tracked_segment_record_from_entry(entry):
    materialized_entries.append(entry)
    return real_segment_record_from_entry(entry)

  monkeypatch.setattr(fleet, "_segment_record_from_entry", tracked_segment_record_from_entry)

  summaries = fleet.get_route_summaries(limit=2)

  assert [summary["routeId"] for summary in summaries] == route_ids[-1:-3:-1]
  assert all(entry.startswith(tuple(route_ids[-2:])) for entry in materialized_entries)
  assert not any(entry.startswith(tuple(route_ids[:2])) for entry in materialized_entries)


def test_extract_route_events_detects_thresholds_and_transitions(tmp_path, monkeypatch):
  route_id = "2026-06-07--15-00-00"
  cache_root = tmp_path / "cache"
  monkeypatch.setattr(fleet.Paths, "log_root", lambda: str(tmp_path))
  monkeypatch.setattr(fleet.Paths, "download_cache_root", lambda: str(cache_root))

  _write_segment(
    str(tmp_path),
    route_id,
    0,
    qlog_samples=[
      {"aEgo": 0.0, "vEgo": 10.0, "standstill": False},
      {"aEgo": -2.6, "vEgo": 10.0, "standstill": False},
      {"aEgo": -2.7, "vEgo": 9.7, "standstill": False},
      {"aEgo": -2.8, "vEgo": 9.4, "standstill": False},
      {"aEgo": -2.9, "vEgo": 9.1, "standstill": False},
      {"aEgo": 0.0, "vEgo": 0.0, "standstill": True},
      {"aEgo": 0.0, "vEgo": 4.0, "standstill": False},
      {"aEgo": 0.0, "vEgo": 4.5, "standstill": False, "steeringPressed": True},
    ],
  )

  manifest = fleet.get_route_manifest(route_id)
  event_types = [event["type"] for event in manifest["events"]]

  assert event_types == ["hard_brake", "stop", "start", "manual_steer"]


def test_get_route_manifest_falls_back_without_qlogs(tmp_path, monkeypatch):
  route_id = "2026-06-07--17-00-00"
  cache_root = tmp_path / "cache"
  monkeypatch.setattr(fleet.Paths, "log_root", lambda: str(tmp_path))
  monkeypatch.setattr(fleet.Paths, "download_cache_root", lambda: str(cache_root))

  _write_segment(str(tmp_path), route_id, 0, cameras=["qcamera", "fcamera"])
  _write_segment(str(tmp_path), route_id, 1, cameras=["qcamera"])

  manifest = fleet.get_route_manifest(route_id)

  assert manifest["routeId"] == route_id
  assert manifest["events"] == []
  assert manifest["cameras"] == ["qcamera", "fcamera"]
  assert manifest["segments"][1]["cameras"] == ["qcamera"]
  assert manifest["downloads"]["full"]["merged"].endswith("/api/routes/2026-06-07--17-00-00/download/merged")


def test_build_segment_stream_command_switches_between_preview_and_full(tmp_path):
  segment_file = tmp_path / "fcamera.hevc"
  segment_file.write_bytes(b"video")

  preview_command = fleet.build_segment_stream_command(str(segment_file), "fcamera", "preview")
  full_command = fleet.build_segment_stream_command(str(segment_file), "fcamera", "full")

  assert "libx264" in preview_command
  assert "scale=640:-2" in preview_command
  assert preview_command[-1] == "-"
  assert full_command.count("copy") == 1
  assert "hvc1" in full_command


def test_build_segment_composite_command_creates_four_camera_wall(tmp_path):
  qcamera = tmp_path / "qcamera.ts"
  dcamera = tmp_path / "dcamera.hevc"
  qcamera.write_bytes(b"video")
  dcamera.write_bytes(b"video")

  command = fleet.build_segment_composite_command({
    "qcamera": str(qcamera),
    "dcamera": str(dcamera),
  }, output_path="out.mp4")
  joined = " ".join(command)

  assert "xstack=inputs=4" in joined
  assert "color=c=black" in joined
  assert command[-1] == "out.mp4"


def test_build_concat_mp4_command_reencodes_route_wall():
  command = fleet.build_concat_mp4_command("/tmp/route.txt", output_path="out.mp4")

  assert "libx264" in command
  assert "veryfast" in command
  assert command[-1] == "out.mp4"


def test_get_route_preview_image_uses_png_cache_and_preview_fallback(tmp_path, monkeypatch):
  route_id = "2026-06-07--21-00-00"
  cache_root = tmp_path / "cache"
  monkeypatch.setattr(fleet.Paths, "log_root", lambda: str(tmp_path))
  monkeypatch.setattr(fleet.Paths, "download_cache_root", lambda: str(cache_root))
  _write_segment(str(tmp_path), route_id, 0, cameras=["qcamera"])

  calls = []

  def fake_build(source_path, output_path):
    calls.append((source_path, output_path))
    if len(calls) == 1:
      raise RuntimeError("initial decode failed")
    _touch(output_path, b"png")

  monkeypatch.setattr(fleet, "_build_preview_image", fake_build)
  monkeypatch.setattr(fleet, "get_preview_stream_path", lambda segment_id, camera: f"/tmp/{segment_id}-{camera}-preview.mp4")

  preview_path = fleet.get_route_preview_image(route_id)

  assert preview_path.endswith(".png")
  assert calls[0][0].endswith("qcamera.ts")
  assert calls[1][0].endswith("-preview.mp4")


def test_get_route_summaries_supports_openpilot_segment_names(tmp_path, monkeypatch):
  route_id = "00000002--7547aa40af"
  cache_root = tmp_path / "cache"
  monkeypatch.setattr(fleet.Paths, "log_root", lambda: str(tmp_path))
  monkeypatch.setattr(fleet.Paths, "download_cache_root", lambda: str(cache_root))

  _write_segment(str(tmp_path), route_id, 18, cameras=["qcamera", "fcamera"])
  _write_segment(str(tmp_path), route_id, 19, cameras=["qcamera"])

  summaries = fleet.get_route_summaries()

  assert len(summaries) == 1
  summary = summaries[0]
  assert summary["routeId"] == route_id
  assert summary["segmentCount"] == 2
  assert summary["durationSec"] == 120
  assert summary["cameras"] == ["qcamera", "fcamera"]

  manifest = fleet.get_route_manifest(route_id)
  assert manifest["segments"][0]["segmentId"] == f"{route_id}--18"
  assert manifest["segments"][1]["segmentId"] == f"{route_id}--19"
