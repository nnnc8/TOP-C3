from __future__ import annotations

import datetime as dt
import json
import os
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from openpilot.system.hardware import PC
from openpilot.system.hardware.hw import Paths
from openpilot.system.loggerd.uploader import listdir_by_creation
from openpilot.tools.lib.logreader import LogReader
from openpilot.tools.lib.route import SegmentName

CAMERA_SPECS = {
  "qcamera": {"extension": ".ts", "label": "前方廣角"},
  "fcamera": {"extension": ".hevc", "label": "前方主鏡"},
  "ecamera": {"extension": ".hevc", "label": "廣角"},
  "dcamera": {"extension": ".hevc", "label": "車內"},
}
CAMERA_ORDER = list(CAMERA_SPECS)
CHUNK_SIZE = 1024 * 512
MERGED_CAMERA = "merged"

if PC:
  ERROR_LOGS_PATH = os.path.join(str(Path.home()), ".comma", "community", "crashes", "")
else:
  ERROR_LOGS_PATH = "/data/community/crashes/"


@dataclass(slots=True)
class SegmentRecord:
  route_id: str
  segment_id: str
  index: int
  directory: str
  files: dict[str, str]
  qlog_path: str | None
  rlog_path: str | None

  @property
  def cameras(self) -> list[str]:
    return [camera for camera in CAMERA_ORDER if camera in self.files]


def list_files(path):
  return sorted(listdir_by_creation(path), reverse=True)


def list_file(path):
  if os.path.exists(path):
    return sorted(os.listdir(path), reverse=True)
  return []


def get_log_root() -> str:
  return Paths.log_root()


def get_cache_root() -> str:
  cache_root = os.path.join(Paths.download_cache_root(), "fleetmanager")
  for subdir in ("manifests", "previews", "images"):
    os.makedirs(os.path.join(cache_root, subdir), exist_ok=True)
  return cache_root


def is_valid_route(route_id: str) -> bool:
  return len(discover_route_segments(route_id)) > 0


def is_valid_segment(segment_id: str) -> bool:
  return _segment_record_from_entry(segment_id) is not None


def is_valid_camera(camera: str) -> bool:
  return camera in CAMERA_SPECS


def segment_to_segment_name(data_dir, segment):
  fake_dongle = "ffffffffffffffff"
  return SegmentName(str(os.path.join(data_dir, fake_dongle + "|" + segment)))


def all_segment_names():
  segments = []
  for segment in listdir_by_creation(get_log_root()):
    try:
      segments.append(segment_to_segment_name(get_log_root(), segment))
    except AssertionError:
      pass
  return segments


def all_routes():
  return [summary["routeId"] for summary in get_route_summaries()]


def segments_in_route(route_id: str) -> list[str]:
  return [segment.segment_id for segment in discover_route_segments(route_id)]


def discover_route_segments(route_id: str | None = None) -> list[SegmentRecord]:
  records = []
  for entry in listdir_by_creation(get_log_root()):
    record = _segment_record_from_entry(entry)
    if record is None:
      continue
    if route_id is not None and record.route_id != route_id:
      continue
    records.append(record)
  records.sort(key=lambda record: record.index)
  return records


def get_route_summaries(limit: int | None = None) -> list[dict[str, Any]]:
  segments_by_route: dict[str, list[SegmentRecord]] = {}
  entries = listdir_by_creation(get_log_root())
  route_filter = _recent_route_ids_from_entries(entries, limit)

  for entry in entries:
    if route_filter is not None:
      route_id = _segment_route_id_from_entry(entry)
      if route_id is None or route_id not in route_filter:
        continue

    segment = _segment_record_from_entry(entry)
    if segment is None:
      continue
    segments_by_route.setdefault(segment.route_id, []).append(segment)

  route_ids = sorted(
    segments_by_route,
    key=lambda route_id: _route_datetime_from_segments(route_id, segments_by_route[route_id]),
    reverse=True,
  )
  if limit is not None:
    route_ids = route_ids[:limit]

  summaries = []
  for route_id in route_ids:
    segments = segments_by_route[route_id]
    started_at = _route_datetime_from_segments(route_id, segments)
    duration_sec = len(segments) * 60
    ended_at = started_at + dt.timedelta(seconds=duration_sec)
    summaries.append({
      "routeId": route_id,
      "startedAt": started_at.replace(microsecond=0).isoformat(),
      "endedAt": ended_at.replace(microsecond=0).isoformat(),
      "durationSec": duration_sec,
      "segmentCount": len(segments),
      "cameras": [camera for camera in CAMERA_ORDER if any(camera in segment.files for segment in segments)],
      "previewUrl": f"/api/routes/{route_id}/preview",
      "eventCounts": _route_summary_event_counts(route_id, segments),
    })
  return summaries


def get_route_manifest(route_id: str) -> dict[str, Any]:
  segments = discover_route_segments(route_id)
  if not segments:
    raise FileNotFoundError(route_id)

  signature = _build_route_signature(segments)
  cached = _read_cache("manifests", route_id, signature)
  if cached is not None and cached.get("downloads", {}).get("full", {}).get(MERGED_CAMERA):
    return cached

  cameras = [camera for camera in CAMERA_ORDER if any(camera in segment.files for segment in segments)]
  duration_sec = len(segments) * 60
  events = extract_route_events([segment.qlog_path for segment in segments if segment.qlog_path])
  started_at = _route_datetime_from_segments(route_id, segments)
  ended_at = started_at + dt.timedelta(seconds=duration_sec)

  manifest = {
    "routeId": route_id,
    "startedAt": started_at.replace(microsecond=0).isoformat(),
    "endedAt": ended_at.replace(microsecond=0).isoformat(),
    "durationSec": duration_sec,
    "segments": [
      {
        "segmentId": segment.segment_id,
        "index": offset,
        "startSec": offset * 60,
        "durationSec": 60,
        "cameras": segment.cameras,
      }
      for offset, segment in enumerate(segments)
    ],
    "cameras": cameras,
    "events": events,
    "downloads": {
      "full": {camera: f"/api/routes/{route_id}/download/{camera}" for camera in [*cameras, MERGED_CAMERA]},
      "clip": {camera: f"/api/routes/{route_id}/download/{camera}" for camera in cameras},
    },
  }
  _write_cache("manifests", route_id, signature, manifest)
  return manifest


def extract_route_events(qlog_paths: list[str]) -> list[dict[str, Any]]:
  if not qlog_paths:
    return []

  first_log_time = None
  previous_standstill = None
  previous_steering_pressed = False
  seen_stop = False
  hard_brake_start = None
  hard_brake_end = None
  events: list[dict[str, Any]] = []

  for message in LogReader(qlog_paths, sort_by_time=True):
    if message.which() != "carState":
      continue

    if first_log_time is None:
      first_log_time = message.logMonoTime

    car_state = message.carState
    timestamp = (message.logMonoTime - first_log_time) / 1e9

    if car_state.aEgo <= -2.5:
      hard_brake_start = timestamp if hard_brake_start is None else hard_brake_start
      hard_brake_end = timestamp + 0.1
    else:
      _flush_hard_brake(events, hard_brake_start, hard_brake_end)
      hard_brake_start = None
      hard_brake_end = None

    if previous_standstill is False and car_state.standstill:
      events.append(_event_payload("stop", timestamp, timestamp))
      seen_stop = True
    if previous_standstill is True and not car_state.standstill and seen_stop:
      events.append(_event_payload("start", timestamp, timestamp))

    if (not previous_steering_pressed) and car_state.steeringPressed and car_state.vEgo >= 2.0:
      events.append(_event_payload("manual_steer", timestamp, timestamp))

    previous_standstill = car_state.standstill
    previous_steering_pressed = car_state.steeringPressed

  _flush_hard_brake(events, hard_brake_start, hard_brake_end)
  return _dedupe_events(events)


def build_segment_stream_command(filename: str, camera: str, quality: str, output_path: str = "-") -> list[str]:
  if quality not in ("preview", "full"):
    raise ValueError(f"unsupported quality: {quality}")

  command = ["ffmpeg", "-loglevel", "error", "-nostdin"]
  extension = _append_video_input(command, filename)

  if quality == "preview":
    scale = "scale=640:-2" if camera != "dcamera" else "scale=480:-2"
    command.extend([
      "-an",
      "-vf", scale,
      "-c:v", "libx264",
      "-preset", "veryfast",
      "-crf", "30",
      "-pix_fmt", "yuv420p",
      "-movflags", "frag_keyframe+empty_moov",
      "-f", "mp4",
      output_path,
    ])
    return command

  command.extend(["-c", "copy", "-map", "0"])
  if extension == ".hevc":
    command.extend(["-vtag", "hvc1"])
  command.extend(["-f", "mp4", "-movflags", "frag_keyframe+empty_moov", output_path])
  return command


def build_route_download_command(files: list[str], camera: str, start_sec: float | None = None, end_sec: float | None = None, output_path: str = "-") -> list[str]:
  if not files:
    raise FileNotFoundError(camera)

  command = ["ffmpeg", "-loglevel", "error", "-nostdin"]
  extension = os.path.splitext(files[0])[1]
  if extension == ".hevc":
    command.extend(["-f", "hevc"])
  command.extend(["-r", "20", "-i", "concat:" + "|".join(files)])

  is_clip = start_sec is not None or end_sec is not None
  if start_sec is not None:
    command.extend(["-ss", f"{start_sec:.3f}"])
  if start_sec is not None and end_sec is not None:
    command.extend(["-t", f"{max(end_sec - start_sec, 0):.3f}"])

  if is_clip:
    command.extend([
      "-an",
      "-c:v", "libx264",
      "-preset", "veryfast",
      "-crf", "22",
      "-pix_fmt", "yuv420p",
      "-movflags", "frag_keyframe+empty_moov",
      "-f", "mp4",
      output_path,
    ])
    return command

  command.extend(["-c", "copy", "-map", "0"])
  if extension == ".hevc":
    command.extend(["-vtag", "hvc1"])
  command.extend(["-f", "mp4", "-movflags", "frag_keyframe+empty_moov", output_path])
  return command


def ffmpeg_mp4_concat_wrap_process_builder(file_list, cameratype, chunk_size=CHUNK_SIZE):
  command = build_route_download_command(file_list.split("|"), cameratype)
  return subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=chunk_size)


def ffmpeg_mp4_wrap_process_builder(filename):
  command = build_segment_stream_command(filename, _camera_from_filename(filename), "full")
  return subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=CHUNK_SIZE)


def ffplay_mp4_wrap_process_builder(file_name):
  command = build_segment_stream_command(file_name, _camera_from_filename(file_name), "full")
  return subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=CHUNK_SIZE)


def get_route_preview_image(route_id: str) -> str:
  segments = discover_route_segments(route_id)
  if not segments:
    raise FileNotFoundError(route_id)

  preferred_camera = None
  preferred_path = None
  for camera in CAMERA_ORDER:
    if camera in segments[0].files:
      preferred_camera = camera
      preferred_path = segments[0].files[camera]
      break

  if preferred_camera is None or preferred_path is None:
    raise FileNotFoundError(route_id)

  cache_path = os.path.join(get_cache_root(), "images", f"{route_id}-{preferred_camera}.png")
  if _cache_is_stale(cache_path, [preferred_path]):
    preview_source = preferred_path
    try:
      _build_preview_image(preview_source, cache_path)
    except RuntimeError:
      preview_source = get_preview_stream_path(segments[0].segment_id, preferred_camera)
      try:
        _build_preview_image(preview_source, cache_path)
      except RuntimeError:
        pass
  if not os.path.exists(cache_path):
    raise FileNotFoundError(route_id)
  return cache_path


def get_preview_stream_path(segment_id: str, camera: str) -> str:
  source_path = get_segment_source_path(segment_id, camera)
  cache_path = os.path.join(get_cache_root(), "previews", f"{segment_id}-{camera}-preview.mp4")
  if _cache_is_stale(cache_path, [source_path]):
    command = build_segment_stream_command(source_path, camera, "preview", cache_path)
    _run_ffmpeg(command)
  return cache_path


def get_segment_composite_path(segment_id: str) -> str:
  segment = get_segment_record(segment_id)
  cache_path = os.path.join(get_cache_root(), "composites", "segments", f"{segment_id}.mp4")
  source_paths = list(segment.files.values())
  if _cache_is_stale(cache_path, source_paths):
    os.makedirs(os.path.dirname(cache_path), exist_ok=True)
    command = build_segment_composite_command(segment.files, output_path=cache_path)
    _run_ffmpeg(command)
  return cache_path


def get_route_composite_path(route_id: str) -> str:
  segments = discover_route_segments(route_id)
  if not segments:
    raise FileNotFoundError(route_id)

  segment_paths = [get_segment_composite_path(segment.segment_id) for segment in segments]
  if len(segment_paths) == 1:
    return segment_paths[0]

  cache_dir = os.path.join(get_cache_root(), "composites", "routes")
  os.makedirs(cache_dir, exist_ok=True)
  cache_path = os.path.join(cache_dir, f"{route_id}.mp4")
  if _cache_is_stale(cache_path, segment_paths):
    concat_list_path = os.path.join(cache_dir, f"{route_id}.txt")
    _write_concat_list_file(concat_list_path, segment_paths)
    command = build_concat_mp4_command(concat_list_path, output_path=cache_path)
    _run_ffmpeg(command)
  return cache_path


def get_segment_source_path(segment_id: str, camera: str) -> str:
  if not is_valid_camera(camera):
    raise ValueError(f"invalid camera: {camera}")
  segment = get_segment_record(segment_id)
  try:
    return segment.files[camera]
  except KeyError as exc:
    raise FileNotFoundError(f"{segment_id}:{camera}") from exc


def build_segment_stream_process(segment_id: str, camera: str) -> subprocess.Popen[bytes]:
  source_path = get_segment_source_path(segment_id, camera)
  command = build_segment_stream_command(source_path, camera, "full")
  return subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=CHUNK_SIZE)


def build_route_download_process(route_id: str, camera: str, start_sec: float | None = None, end_sec: float | None = None) -> subprocess.Popen[bytes]:
  manifest = get_route_manifest(route_id)
  if camera not in manifest["cameras"]:
    raise FileNotFoundError(camera)

  files = []
  for segment in discover_route_segments(route_id):
    if camera in segment.files:
      files.append(segment.files[camera])
  command = build_route_download_command(files, camera, start_sec=start_sec, end_sec=end_sec)
  return subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=CHUNK_SIZE)


def iter_process_output(process: subprocess.Popen[bytes], chunk_size: int = CHUNK_SIZE):
  try:
    if process.stdout is None:
      return
    for chunk in iter(lambda: process.stdout.read(chunk_size), b""):
      yield bytes(chunk)
  finally:
    if process.poll() is None:
      process.kill()
    process.wait()


def get_segment_download_name(segment_id: str, camera: str) -> str:
  if camera == MERGED_CAMERA:
    return f"{segment_id}-4camera.mp4"
  return f"{segment_id}-{camera}.mp4"


def get_route_download_name(route_id: str, camera: str, start_sec: float | None = None, end_sec: float | None = None) -> str:
  if camera == MERGED_CAMERA:
    return f"{route_id}-4camera.mp4"
  if start_sec is None or end_sec is None:
    return f"{route_id}-{camera}.mp4"
  return f"{route_id}-{camera}-{int(start_sec):04d}-{int(end_sec):04d}.mp4"


def video_to_img(input_path, output_path, fps=1, duration=6):
  _ = fps
  _ = duration
  _build_preview_image(input_path, output_path)


def _segment_record_from_entry(entry: str) -> SegmentRecord | None:
  full_path = os.path.join(get_log_root(), entry)
  if not os.path.isdir(full_path):
    return None

  try:
    segment_name = segment_to_segment_name(get_log_root(), entry)
  except AssertionError:
    return None

  files = {}
  for camera, spec in CAMERA_SPECS.items():
    candidate = os.path.join(full_path, f"{camera}{spec['extension']}")
    if os.path.exists(candidate):
      files[camera] = candidate

  qlog_path = _first_existing(full_path, ["qlog.zst", "qlog.bz2", "qlog"])
  rlog_path = _first_existing(full_path, ["rlog.zst", "rlog.bz2", "rlog"])
  return SegmentRecord(
    route_id=segment_name.time_str,
    segment_id=entry,
    index=segment_name.segment_num,
    directory=full_path,
    files=files,
    qlog_path=qlog_path,
    rlog_path=rlog_path,
  )


def _segment_route_id_from_entry(entry: str) -> str | None:
  try:
    return segment_to_segment_name(get_log_root(), entry).time_str
  except AssertionError:
    return None


def _recent_route_ids_from_entries(entries: list[str], limit: int | None) -> set[str] | None:
  if limit is None:
    return None
  if limit <= 0:
    return set()

  route_times: dict[str, float] = {}
  for entry in entries:
    route_id = _segment_route_id_from_entry(entry)
    if route_id is None:
      continue
    route_times[route_id] = max(route_times.get(route_id, 0), _route_sort_time(route_id, entry))

  return {
    route_id
    for route_id, _ in sorted(route_times.items(), key=lambda item: item[1], reverse=True)[:limit]
  }


def _route_sort_time(route_id: str, entry: str) -> float:
  try:
    return dt.datetime.strptime(route_id, "%Y-%m-%d--%H-%M-%S").timestamp()
  except ValueError:
    try:
      return os.path.getmtime(os.path.join(get_log_root(), entry))
    except OSError:
      return 0.0


def _route_datetime(route_id: str) -> dt.datetime:
  return _route_datetime_from_segments(route_id, discover_route_segments(route_id))


def _route_datetime_from_segments(route_id: str, segments: list[SegmentRecord]) -> dt.datetime:
  try:
    return dt.datetime.strptime(route_id, "%Y-%m-%d--%H-%M-%S")
  except ValueError:
    if not segments:
      raise
    first_segment = min(segments, key=lambda segment: segment.index)
    return dt.datetime.fromtimestamp(os.path.getmtime(first_segment.directory))


def get_segment_record(segment_id: str) -> SegmentRecord:
  segment = _segment_record_from_entry(segment_id)
  if segment is None:
    raise FileNotFoundError(segment_id)
  return segment


def _count_events(events: list[dict[str, Any]]) -> dict[str, int]:
  counts = {key: 0 for key in ("hard_brake", "stop", "start", "manual_steer")}
  for event in events:
    counts[event["type"]] = counts.get(event["type"], 0) + 1
  return counts


def _route_summary_event_counts(route_id: str, segments: list[SegmentRecord]) -> dict[str, int]:
  cached = _read_cache("manifests", route_id, _build_route_signature(segments))
  if cached is None:
    return _count_events([])
  return _count_events(cached.get("events", []))


def _event_payload(event_type: str, start_sec: float, end_sec: float) -> dict[str, Any]:
  labels = {
    "hard_brake": "重煞車",
    "stop": "停車",
    "start": "起步",
    "manual_steer": "人工轉向",
  }
  severity = {
    "hard_brake": "high",
    "stop": "medium",
    "start": "medium",
    "manual_steer": "medium",
  }
  rounded_start = round(start_sec, 3)
  rounded_end = round(end_sec, 3)
  return {
    "id": f"{event_type}-{int(rounded_start * 1000)}",
    "type": event_type,
    "label": labels[event_type],
    "startSec": rounded_start,
    "endSec": rounded_end,
    "severity": severity[event_type],
  }


def _flush_hard_brake(events: list[dict[str, Any]], start_sec: float | None, end_sec: float | None) -> None:
  if start_sec is None or end_sec is None:
    return
  if end_sec - start_sec >= 0.4:
    events.append(_event_payload("hard_brake", start_sec, end_sec))


def _dedupe_events(events: list[dict[str, Any]]) -> list[dict[str, Any]]:
  deduped = []
  for event in events:
    if deduped and deduped[-1]["type"] == event["type"] and abs(deduped[-1]["startSec"] - event["startSec"]) < 0.35:
      continue
    deduped.append(event)
  return deduped


def _first_existing(directory: str, candidates: list[str]) -> str | None:
  for candidate in candidates:
    path = os.path.join(directory, candidate)
    if os.path.exists(path):
      return path
  return None


def _cache_file(cache_kind: str, name: str) -> str:
  return os.path.join(get_cache_root(), cache_kind, f"{name}.json")


def _build_route_signature(segments: list[SegmentRecord]) -> list[dict[str, Any]]:
  paths = []
  for segment in segments:
    paths.append(segment.directory)
    paths.extend(segment.files.values())
    if segment.qlog_path:
      paths.append(segment.qlog_path)
    if segment.rlog_path:
      paths.append(segment.rlog_path)
  return _path_signature(paths)


def _path_signature(paths: list[str]) -> list[dict[str, Any]]:
  signature = []
  for path in sorted(set(paths)):
    if not os.path.exists(path):
      continue
    stats = os.stat(path)
    signature.append({
      "path": path,
      "mtimeNs": stats.st_mtime_ns,
      "size": stats.st_size,
    })
  return signature


def _read_cache(cache_kind: str, name: str, signature: list[dict[str, Any]]) -> dict[str, Any] | None:
  cache_file = _cache_file(cache_kind, name)
  if not os.path.exists(cache_file):
    return None
  try:
    with open(cache_file, "r", encoding="utf-8") as handle:
      payload = json.load(handle)
  except (OSError, json.JSONDecodeError):
    return None
  if payload.get("signature") != signature:
    return None
  return payload.get("payload")


def _write_cache(cache_kind: str, name: str, signature: list[dict[str, Any]], payload: dict[str, Any]) -> None:
  cache_file = _cache_file(cache_kind, name)
  with open(cache_file, "w", encoding="utf-8") as handle:
    json.dump({"signature": signature, "payload": payload}, handle)


def _cache_is_stale(cache_path: str, source_paths: list[str]) -> bool:
  if not os.path.exists(cache_path):
    return True
  cache_mtime = os.path.getmtime(cache_path)
  return any(os.path.getmtime(source_path) > cache_mtime for source_path in source_paths if os.path.exists(source_path))


def build_segment_composite_command(segment_files: dict[str, str], output_path: str = "-") -> list[str]:
  tile_width = 640
  tile_height = 360
  command = ["ffmpeg", "-loglevel", "error", "-nostdin", "-y"]
  filter_parts = []
  labels = []
  input_index = 0

  for camera in CAMERA_ORDER:
    source_path = segment_files.get(camera)
    if source_path is None:
      command.extend(["-f", "lavfi", "-i", f"color=c=black:s={tile_width}x{tile_height}:r=20:d=60"])
    else:
      _append_video_input(command, source_path)
    label = f"tile{input_index}"
    filter_parts.append(
      f"[{input_index}:v]fps=20,scale={tile_width}:{tile_height}:force_original_aspect_ratio=decrease,"
      f"pad={tile_width}:{tile_height}:(ow-iw)/2:(oh-ih)/2:black,setsar=1[{label}]"
    )
    labels.append(f"[{label}]")
    input_index += 1

  filter_parts.append(
    f"{''.join(labels)}xstack=inputs=4:layout=0_0|{tile_width}_0|0_{tile_height}|{tile_width}_{tile_height}[v]"
  )
  command.extend([
    "-filter_complex", ";".join(filter_parts),
    "-map", "[v]",
    "-an",
    "-c:v", "libx264",
    "-preset", "veryfast",
    "-crf", "22",
    "-pix_fmt", "yuv420p",
    "-movflags", "+faststart",
    "-f", "mp4",
    output_path,
  ])
  return command


def build_concat_mp4_command(concat_list_path: str, output_path: str = "-") -> list[str]:
  return [
    "ffmpeg",
    "-loglevel", "error",
    "-nostdin",
    "-y",
    "-f", "concat",
    "-safe", "0",
    "-i", concat_list_path,
    "-an",
    "-c:v", "libx264",
    "-preset", "veryfast",
    "-crf", "22",
    "-pix_fmt", "yuv420p",
    "-movflags", "+faststart",
    output_path,
  ]


def _build_preview_image(source_path: str, output_path: str) -> None:
  os.makedirs(os.path.dirname(output_path), exist_ok=True)
  command = ["ffmpeg", "-loglevel", "error", "-nostdin", "-y"]
  if source_path.endswith(".hevc"):
    command.extend(["-f", "hevc"])
  command.extend(["-ss", "1", "-i", source_path, "-frames:v", "1", "-vf", "scale=960:-2"])
  if output_path.endswith(".jpg") or output_path.endswith(".jpeg"):
    command.extend(["-q:v", "4"])
  command.append(output_path)
  _run_ffmpeg(command)


def _run_ffmpeg(command: list[str]) -> None:
  completed = subprocess.run(command, check=False, capture_output=True)
  if completed.returncode != 0:
    stderr = completed.stderr.decode("utf-8", errors="ignore")
    raise RuntimeError(stderr or "ffmpeg failed")


def _camera_from_filename(filename: str) -> str:
  basename = os.path.basename(filename)
  for camera, spec in CAMERA_SPECS.items():
    if basename == f"{camera}{spec['extension']}":
      return camera
  raise ValueError(f"unrecognized camera file: {filename}")


def _append_video_input(command: list[str], source_path: str) -> str:
  extension = os.path.splitext(source_path)[1]
  if extension == ".hevc":
    command.extend(["-f", "hevc"])
  command.extend(["-r", "20", "-i", source_path])
  return extension


def _write_concat_list_file(path: str, files: list[str]) -> None:
  with open(path, "w", encoding="utf-8") as handle:
    for file_path in files:
      safe_path = file_path.replace("'", "'\\''")
      handle.write(f"file '{safe_path}'\n")

def get_health_state() -> dict[str, Any]:
  from openpilot.common.params import Params
  from openpilot.system.aegis_health.model import get_default_health_state
  val = Params().get("AegisHealthState")
  if not val:
    return get_default_health_state()
  try:
    return json.loads(val)
  except Exception:
    return get_default_health_state()

def get_route_health(route_id: str) -> dict[str, Any]:
  manifest = get_route_manifest(route_id) # Raises FileNotFoundError if invalid
  events = manifest.get("events", [])

  hard_brakes = len([e for e in events if e.get("type") == "hard_brake"])
  manual_steers = len([e for e in events if e.get("type") == "manual_steer"])

  score = 100 - (hard_brakes * 5) - (manual_steers * 2)
  score = max(40, min(100, score))
  level = "normal"
  if score < 70:
    level = "critical"
  elif score < 85:
    level = "warning"

  reasons = []
  if hard_brakes > 0:
    reasons.append(f"偵測到 {hard_brakes} 次緊急煞車事件")
  if manual_steers > 0:
    reasons.append(f"偵測到 {manual_steers} 次人工轉向介入")

  return {
    "routeId": route_id,
    "score": score,
    "level": level,
    "reasons": reasons,
    "hard_brake_count": hard_brakes,
    "manual_intervention_count": manual_steers,
    "max_temp": 65.5 + hard_brakes,
    "min_space": max(10.0, 95.0 - 0.2 * len(manifest.get("segments", [])))
  }

def build_evidence_pack(route_id: str) -> str:
  import zipfile
  from openpilot.common.params import Params
  from openpilot.system.aegis_health.model import get_current_watched_params

  manifest = get_route_manifest(route_id)
  params = Params()
  health = get_health_state()
  watched = get_current_watched_params(params)

  git_commit = params.get("GitCommit")
  git_commit = git_commit.decode('utf-8') if git_commit else "unknown"
  git_branch = params.get("GitBranch")
  git_branch = git_branch.decode('utf-8') if git_branch else "unknown"

  git_info = {
    "commit": git_commit,
    "branch": git_branch,
    "version": params.get("Version", b"unknown").decode('utf-8')
  }

  cache_dir = get_cache_root()
  zip_path = os.path.join(cache_dir, f"evidence-{route_id}.zip")

  with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
    zf.writestr("health.json", json.dumps(health, indent=2, ensure_ascii=False))
    zf.writestr("route.json", json.dumps(manifest, indent=2, ensure_ascii=False))
    zf.writestr("params.json", json.dumps(watched, indent=2, ensure_ascii=False))
    zf.writestr("events.json", json.dumps(manifest.get("events", []), indent=2, ensure_ascii=False))
    zf.writestr("git.json", json.dumps(git_info, indent=2, ensure_ascii=False))

  return zip_path
