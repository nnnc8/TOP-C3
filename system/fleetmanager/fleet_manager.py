#!/usr/bin/env python3
from html import escape
import secrets
import traceback

from flask import Flask, Response, jsonify, render_template, request, send_file, stream_with_context

from openpilot.common.realtime import set_core_affinity
from openpilot.common.swaglog import cloudlog
from openpilot.system.fleetmanager import helpers as fleet

app = Flask(__name__)


@app.route("/")
def home_page():
  recent_routes = fleet.get_route_summaries(limit=4)
  return render_template("index.html", recent_routes=recent_routes)


@app.errorhandler(500)
def internal_error(exception):
  _ = exception
  tberror = traceback.format_exc()
  if request.path.startswith("/api/"):
    return jsonify({"error": tberror}), 500
  return render_template("error.html", error=tberror), 500


@app.route("/api/routes")
def api_routes():
  return jsonify({"routes": fleet.get_route_summaries()})


@app.route("/api/routes/<route_id>")
def api_route(route_id):
  try:
    manifest = fleet.get_route_manifest(route_id)
  except ValueError:
    return _api_error("invalid route", 404)
  except FileNotFoundError:
    return _api_error("route not found", 404)
  return jsonify(manifest)


@app.route("/api/routes/<route_id>/preview")
def api_route_preview(route_id):
  if not fleet.is_valid_route(route_id):
    return _api_error("route not found", 404)
  try:
    preview_path = fleet.get_route_preview_image(route_id)
  except ValueError:
    return _api_error("route not found", 404)
  except FileNotFoundError:
    return _preview_placeholder_response(route_id)
  mimetype = "image/png" if preview_path.endswith(".png") else "image/jpeg"
  return send_file(preview_path, mimetype=mimetype)


@app.route("/api/segments/<segment_id>/stream/<camera>")
def api_segment_stream(segment_id, camera):
  quality = request.args.get("quality", "preview")
  download = request.args.get("download", "").lower() in {"1", "true", "yes"}
  return _segment_stream_response(segment_id, camera, quality, download)


@app.route("/api/segments/<segment_id>/download/<target>")
def api_segment_download(segment_id, target):
  if target == fleet.MERGED_CAMERA:
    if not fleet.is_valid_segment(segment_id):
      return _api_error("segment not found", 404)
    try:
      composite_path = fleet.get_segment_composite_path(segment_id)
    except FileNotFoundError:
      return _api_error("segment not found", 404)
    except RuntimeError as exc:
      return _api_error(str(exc), 500)
    return send_file(
      composite_path,
      mimetype="video/mp4",
      as_attachment=True,
      download_name=fleet.get_segment_download_name(segment_id, target),
    )

  return _segment_stream_response(segment_id, target, "full", True)


def _segment_stream_response(segment_id, camera, quality, download):

  if not fleet.is_valid_camera(camera):
    return _api_error("invalid camera", 404)
  if quality not in {"preview", "full"}:
    return _api_error("invalid quality", 400)
  if not fleet.is_valid_segment(segment_id):
    return _api_error("segment not found", 404)

  try:
    if quality == "preview":
      preview_path = fleet.get_preview_stream_path(segment_id, camera)
      return send_file(
        preview_path,
        mimetype="video/mp4",
        as_attachment=download,
        download_name=fleet.get_segment_download_name(segment_id, camera),
      )

    process = fleet.build_segment_stream_process(segment_id, camera)
  except FileNotFoundError:
    return _api_error("camera stream not found", 404)
  except RuntimeError as exc:
    return _api_error(str(exc), 500)

  headers = {}
  if download:
    headers["Content-Disposition"] = f'attachment; filename="{fleet.get_segment_download_name(segment_id, camera)}"'
  return Response(stream_with_context(fleet.iter_process_output(process)), mimetype="video/mp4", headers=headers)


@app.route("/api/routes/<route_id>/download/<camera>")
def api_route_download(route_id, camera):
  if camera == fleet.MERGED_CAMERA:
    if not fleet.is_valid_route(route_id):
      return _api_error("route not found", 404)
    try:
      composite_path = fleet.get_route_composite_path(route_id)
    except FileNotFoundError:
      return _api_error("route not found", 404)
    except RuntimeError as exc:
      return _api_error(str(exc), 500)
    return send_file(
      composite_path,
      mimetype="video/mp4",
      as_attachment=True,
      download_name=fleet.get_route_download_name(route_id, camera),
    )

  if not fleet.is_valid_camera(camera):
    return _api_error("invalid camera", 404)
  if not fleet.is_valid_route(route_id):
    return _api_error("route not found", 404)

  start_sec, end_sec, error = _clip_args()
  if error is not None:
    return _api_error(error, 400)

  try:
    process = fleet.build_route_download_process(route_id, camera, start_sec=start_sec, end_sec=end_sec)
  except FileNotFoundError:
    return _api_error("camera stream not found", 404)

  headers = {
    "Content-Disposition": f'attachment; filename="{fleet.get_route_download_name(route_id, camera, start_sec, end_sec)}"',
  }
  return Response(stream_with_context(fleet.iter_process_output(process)), mimetype="video/mp4", headers=headers)


@app.route("/footage")
@app.route("/footage/")
def footage():
  return render_template("footage.html")


@app.route("/footage/<route_id>")
def route(route_id):
  if not fleet.is_valid_route(route_id):
    return render_template("error.html", error="找不到這條路線"), 404
  return render_template("route.html", route_id=route_id)


@app.route("/footage/full/<camera>/<route_id>")
def full(camera, route_id):
  return api_route_download(route_id, camera)


@app.route("/footage/<camera>/<segment_id>")
def segment_stream(camera, segment_id):
  return _segment_stream_response(segment_id, camera, "full", False)


@app.route("/about")
def about():
  return render_template("about.html")


@app.route("/error_logs")
def error_logs():
  rows = fleet.list_file(fleet.ERROR_LOGS_PATH)
  if not rows:
    return render_template("error.html", error="找不到錯誤日誌：<br><br>" + fleet.ERROR_LOGS_PATH), 404
  return render_template("error_logs.html", rows=rows)


@app.route("/error_logs/<file_name>")
def open_error_log(file_name):
  try:
    with open(fleet.ERROR_LOGS_PATH + file_name, "r", encoding="utf-8", errors="ignore") as handle:
      error = handle.read()
  except OSError:
    return render_template("error.html", error="找不到這份錯誤日誌"), 404
  return render_template("error_log.html", file_name=file_name, file_content=error)


@app.route("/previewgif/<path:file_path>", methods=["GET"])
def find_previewgif(file_path):
  route_id = file_path.split("--0/")[0]
  if not fleet.is_valid_route(route_id):
    return render_template("error.html", error="找不到這條路線"), 404
  try:
    preview_path = fleet.get_route_preview_image(route_id)
  except FileNotFoundError:
    return _preview_placeholder_response(route_id)
  mimetype = "image/png" if preview_path.endswith(".png") else "image/jpeg"
  return send_file(preview_path, mimetype=mimetype)


def _clip_args():
  start_raw = request.args.get("startSec")
  end_raw = request.args.get("endSec")
  if start_raw is None and end_raw is None:
    return None, None, None
  if start_raw is None or end_raw is None:
    return None, None, "both startSec and endSec are required"

  try:
    start_sec = float(start_raw)
    end_sec = float(end_raw)
  except ValueError:
    return None, None, "invalid clip range"
  if start_sec < 0 or end_sec <= start_sec:
    return None, None, "invalid clip range"
  return start_sec, end_sec, None


def _api_error(message, status):
  return jsonify({"error": message}), status


def _preview_placeholder_response(route_id):
  safe_route_id = escape(route_id)
  svg = f"""
  <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1280 720">
    <defs>
      <linearGradient id="bg" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0%" stop-color="#20242b"/>
        <stop offset="55%" stop-color="#111317"/>
        <stop offset="100%" stop-color="#08090b"/>
      </linearGradient>
    </defs>
    <rect width="1280" height="720" rx="42" fill="url(#bg)"/>
    <rect x="36" y="36" width="124" height="58" rx="29" fill="rgba(0,0,0,0.38)" stroke="rgba(255,255,255,0.08)"/>
    <text x="62" y="74" fill="#d9dde3" font-size="24" font-family="Avenir Next, Helvetica Neue, sans-serif">預覽圖</text>
    <text x="64" y="566" fill="#f3f4f6" font-size="72" font-weight="700" font-family="Avenir Next, Helvetica Neue, sans-serif">四鏡頭路線檢視</text>
    <text x="64" y="620" fill="#a0a7b2" font-size="28" font-family="Avenir Next, Helvetica Neue, sans-serif">這條路線目前沒有可用的封面預覽，點進去仍可直接播放與下載。</text>
    <text x="64" y="664" fill="#cbd1d9" font-size="24" font-family="Avenir Next, Helvetica Neue, sans-serif">{safe_route_id}</text>
    <line x1="320" y1="120" x2="320" y2="420" stroke="rgba(255,255,255,0.08)"/>
    <line x1="960" y1="120" x2="960" y2="420" stroke="rgba(255,255,255,0.08)"/>
    <line x1="36" y1="420" x2="1244" y2="420" stroke="rgba(255,255,255,0.08)"/>
  </svg>
  """.strip()
  return Response(svg, mimetype="image/svg+xml")


def main():
  try:
    set_core_affinity([0, 1, 2, 3])
  except Exception:
    cloudlog.exception("fleet_manager: failed to set core affinity")
  app.secret_key = secrets.token_hex(32)
  app.run(host="0.0.0.0", port=8082)


if __name__ == "__main__":
  main()
