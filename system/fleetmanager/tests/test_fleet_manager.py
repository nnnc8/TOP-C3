from openpilot.system.fleetmanager import helpers as fleet
from openpilot.system.fleetmanager.fleet_manager import app


def _write_segment(log_root, route_id, segment_num):
  segment_dir = log_root / f"{route_id}--{segment_num}"
  segment_dir.mkdir(parents=True, exist_ok=True)
  (segment_dir / "qcamera.ts").write_bytes(b"video")


def test_api_routes_returns_library_payload(tmp_path, monkeypatch):
  route_id = "2026-06-07--18-00-00"
  cache_root = tmp_path / "cache"
  monkeypatch.setattr(fleet.Paths, "log_root", lambda: str(tmp_path))
  monkeypatch.setattr(fleet.Paths, "download_cache_root", lambda: str(cache_root))
  _write_segment(tmp_path, route_id, 0)

  app.testing = True
  client = app.test_client()

  response = client.get("/api/routes")

  assert response.status_code == 200
  payload = response.get_json()
  assert payload["routes"][0]["routeId"] == route_id


def test_api_route_manifest_returns_viewer_payload(tmp_path, monkeypatch):
  route_id = "2026-06-07--19-00-00"
  cache_root = tmp_path / "cache"
  monkeypatch.setattr(fleet.Paths, "log_root", lambda: str(tmp_path))
  monkeypatch.setattr(fleet.Paths, "download_cache_root", lambda: str(cache_root))
  _write_segment(tmp_path, route_id, 0)

  app.testing = True
  client = app.test_client()

  response = client.get(f"/api/routes/{route_id}")

  assert response.status_code == 200
  payload = response.get_json()
  assert payload["routeId"] == route_id
  assert payload["segments"][0]["segmentId"] == f"{route_id}--0"


def test_api_route_preview_returns_png(tmp_path, monkeypatch):
  preview_path = tmp_path / "preview.png"
  preview_path.write_bytes(b"\x89PNG\r\n\x1a\n")
  monkeypatch.setattr(fleet, "get_route_preview_image", lambda route_id: str(preview_path))
  monkeypatch.setattr(fleet, "is_valid_route", lambda route_id: True)

  app.testing = True
  client = app.test_client()

  response = client.get("/api/routes/2026-06-07--19-00-00/preview")

  try:
    assert response.status_code == 200
    assert response.mimetype == "image/png"
  finally:
    response.close()


def test_api_route_preview_falls_back_to_placeholder_svg(monkeypatch):
  def missing_preview(route_id):
    raise FileNotFoundError(route_id)

  monkeypatch.setattr(fleet, "is_valid_route", lambda route_id: True)
  monkeypatch.setattr(fleet, "get_route_preview_image", missing_preview)

  app.testing = True
  client = app.test_client()

  response = client.get("/api/routes/2026-06-07--19-00-00/preview")

  try:
    assert response.status_code == 200
    assert response.mimetype == "image/svg+xml"
    assert "四鏡頭路線檢視" in response.get_data(as_text=True)
  finally:
    response.close()


def test_api_segment_merged_download_returns_mp4(tmp_path, monkeypatch):
  video_path = tmp_path / "segment-merged.mp4"
  video_path.write_bytes(b"mp4")
  monkeypatch.setattr(fleet, "is_valid_segment", lambda segment_id: True)
  monkeypatch.setattr(fleet, "get_segment_composite_path", lambda segment_id: str(video_path))

  app.testing = True
  client = app.test_client()

  response = client.get("/api/segments/2026-06-07--20-00-00--0/download/merged")

  try:
    assert response.status_code == 200
    assert response.mimetype == "video/mp4"
  finally:
    response.close()


def test_api_route_merged_download_returns_mp4(tmp_path, monkeypatch):
  video_path = tmp_path / "route-merged.mp4"
  video_path.write_bytes(b"mp4")
  monkeypatch.setattr(fleet, "is_valid_route", lambda route_id: True)
  monkeypatch.setattr(fleet, "get_route_composite_path", lambda route_id: str(video_path))

  app.testing = True
  client = app.test_client()

  response = client.get("/api/routes/2026-06-07--20-00-00/download/merged")

  try:
    assert response.status_code == 200
    assert response.mimetype == "video/mp4"
  finally:
    response.close()


def test_api_invalid_route_returns_404(tmp_path, monkeypatch):
  cache_root = tmp_path / "cache"
  monkeypatch.setattr(fleet.Paths, "log_root", lambda: str(tmp_path))
  monkeypatch.setattr(fleet.Paths, "download_cache_root", lambda: str(cache_root))

  app.testing = True
  client = app.test_client()

  response = client.get("/api/routes/not-a-route")

  assert response.status_code == 404


def test_api_invalid_segment_returns_404(tmp_path, monkeypatch):
  cache_root = tmp_path / "cache"
  monkeypatch.setattr(fleet.Paths, "log_root", lambda: str(tmp_path))
  monkeypatch.setattr(fleet.Paths, "download_cache_root", lambda: str(cache_root))

  app.testing = True
  client = app.test_client()

  response = client.get("/api/segments/not-a-segment/stream/qcamera")

  assert response.status_code == 404


def test_api_clip_download_rejects_invalid_ranges(tmp_path, monkeypatch):
  route_id = "2026-06-07--20-00-00"
  cache_root = tmp_path / "cache"
  monkeypatch.setattr(fleet.Paths, "log_root", lambda: str(tmp_path))
  monkeypatch.setattr(fleet.Paths, "download_cache_root", lambda: str(cache_root))
  _write_segment(tmp_path, route_id, 0)

  app.testing = True
  client = app.test_client()

  response = client.get(f"/api/routes/{route_id}/download/qcamera?startSec=30&endSec=10")

  assert response.status_code == 400

def test_api_health(tmp_path, monkeypatch):
  monkeypatch.setattr(fleet, "get_health_state", lambda: {"score": 100})
  app.testing = True
  client = app.test_client()
  response = client.get("/api/health")
  assert response.status_code == 200
  payload = response.get_json()
  assert payload["score"] == 100

def test_api_route_health_valid_and_invalid(tmp_path, monkeypatch):
  route_id = "2026-06-07--21-00-00"
  cache_root = tmp_path / "cache"
  monkeypatch.setattr(fleet.Paths, "log_root", lambda: str(tmp_path))
  monkeypatch.setattr(fleet.Paths, "download_cache_root", lambda: str(cache_root))
  _write_segment(tmp_path, route_id, 0)

  def mock_route_health(rid):
    if rid == "not-a-route":
      raise FileNotFoundError()
    return {"routeId": rid, "score": 100}
  monkeypatch.setattr(fleet, "get_route_health", mock_route_health)

  app.testing = True
  client = app.test_client()

  # Valid route
  response = client.get(f"/api/routes/{route_id}/health")
  assert response.status_code == 200
  payload = response.get_json()
  assert payload["score"] == 100

  # Invalid route
  response2 = client.get("/api/routes/not-a-route/health")
  assert response2.status_code == 404

def test_api_route_evidence_zip(tmp_path, monkeypatch):
  route_id = "2026-06-07--22-00-00"
  cache_root = tmp_path / "cache"
  monkeypatch.setattr(fleet.Paths, "log_root", lambda: str(tmp_path))
  monkeypatch.setattr(fleet.Paths, "download_cache_root", lambda: str(cache_root))
  _write_segment(tmp_path, route_id, 0)

  def fake_build(rid):
    p = cache_root / "evidence.zip"
    p.mkdir(parents=True, exist_ok=True) # just create parent dir
    p_file = cache_root / f"evidence-{rid}.zip"
    p_file.write_bytes(b"zip")
    return str(p_file)

  monkeypatch.setattr(fleet, "build_evidence_pack", fake_build)

  app.testing = True
  client = app.test_client()

  response = client.get(f"/api/routes/{route_id}/evidence.zip")
  try:
    assert response.status_code == 200
    assert response.mimetype == "application/zip"
  finally:
    response.close()
