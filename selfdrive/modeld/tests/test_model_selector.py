import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from openpilot.tools.model_selector import (
  install_model,
  load_cached_manifest,
  SELECTOR_VERSION,
  list_installable_models,
  model_file_plan,
  sync_manifest,
  verify_manifest_signature,
)


TEST_PUBLIC_KEY = """-----BEGIN PUBLIC KEY-----
MCowBQYDK2VwAyEASxeyf4IxaLi/0qFr1jPLoeZmWYMAWdFbGz9W/PEGiXQ=
-----END PUBLIC KEY-----"""
TEST_SIGNATURE = "IMfhXUU/TBmkMTi5OKy9j3PBcHhDnyaqhQ411Hdwy5zUDUU6YHlmA2o/J16K4+42t0MfmIx2pgeYq6rakbbFBA=="


def signed_manifest() -> tuple[dict, str]:
  manifest = {
    "version": 1,
    "updated_at": "2026-06-08T00:00:00Z",
    "models": [],
    "key_id": "test_key",
    "signature": TEST_SIGNATURE,
  }
  return manifest, TEST_PUBLIC_KEY


def model(model_id: str, files: dict[str, dict], minimum_selector_version: int = SELECTOR_VERSION) -> dict:
  return {
    "id": model_id,
    "name": model_id,
    "base_url": f"https://example.test/models/{model_id}",
    "files": files,
    "minimum_selector_version": minimum_selector_version,
    "added_at": "2026-06-08",
  }


def file_spec(size: int = 4096, sha256: str = "0" * 64) -> dict:
  return {"size": size, "sha256": sha256}


class TestModelSelector(unittest.TestCase):
  def test_verify_manifest_signature_accepts_valid_manifest(self):
    manifest, public_key = signed_manifest()

    verify_manifest_signature(manifest, {"test_key": public_key})

  def test_verify_manifest_signature_rejects_tampering(self):
    manifest, public_key = signed_manifest()
    manifest["updated_at"] = "tampered"

    with self.assertRaises(ValueError):
      verify_manifest_signature(manifest, {"test_key": public_key})

  def test_sync_manifest_keeps_a_verified_manifest_for_offline_reads(self):
    manifest, _ = signed_manifest()

    with tempfile.TemporaryDirectory() as temp_dir:
      cache_root = Path(temp_dir) / "cache"
      with patch("openpilot.tools.model_selector.load_manifest", return_value=manifest), \
           patch("openpilot.tools.model_selector.verify_manifest_signature"):
        sync_manifest("https://example.test/models_v4.json", cache_root=cache_root)
        cached = load_cached_manifest(cache_root)

      self.assertEqual(cached, manifest)
      self.assertEqual((cache_root / "manifest_status.json").read_text().count("example.test"), 1)

  def test_list_installable_models_filters_by_selector_version_and_bundle_shape(self):
    manifest = {
      "version": 1,
      "models": [
        model("opv7", {
          "driving_vision.onnx": file_spec(),
          "driving_policy.onnx": file_spec(),
        }),
        model("policy-alias", {
          "driving_vision.onnx": file_spec(),
          "driving_on_policy.onnx": file_spec(),
        }),
        model("missing-policy", {
          "driving_vision.onnx": file_spec(),
        }),
        model("future", {
          "driving_vision.onnx": file_spec(),
          "driving_policy.onnx": file_spec(),
        }, minimum_selector_version=SELECTOR_VERSION + 1),
      ],
    }

    installable_ids = [entry["id"] for entry in list_installable_models(manifest)]

    self.assertEqual(installable_ids, ["opv7", "policy-alias"])

  def test_model_file_plan_maps_on_policy_alias_to_driving_policy_artifact(self):
    plan = model_file_plan(model("policy-alias", {
      "driving_vision.onnx": file_spec(),
      "driving_on_policy.onnx": file_spec(),
    }))

    self.assertEqual([item.install_name for item in plan], [
      "driving_vision.onnx",
      "driving_policy.onnx",
    ])
    self.assertEqual([item.remote_name for item in plan], [
      "driving_vision.onnx",
      "driving_on_policy.onnx",
    ])

  def test_model_file_plan_accepts_single_supercombo_artifact(self):
    plan = model_file_plan(model("revert-rebellious-hope", {
      "driving_supercombo.onnx": file_spec(),
    }))

    self.assertEqual([item.install_name for item in plan], ["driving_supercombo.onnx"])

  def test_install_model_activates_cache_without_touching_repo_models(self):
    manifest = {"models": [model("opv7", {
      "driving_vision.onnx": file_spec(),
      "driving_policy.onnx": file_spec(),
    })]}

    def stage_files(_model, staging_dir: Path):
      for name in ("driving_vision.onnx", "driving_policy.onnx"):
        (staging_dir / name).write_bytes(f"new {name}".encode())
      return model_file_plan(_model)

    with tempfile.TemporaryDirectory() as temp_dir:
      root = Path(temp_dir)
      models_dir = root / "models"
      models_dir.mkdir()
      for name in ("driving_vision.onnx", "driving_policy.onnx"):
        (models_dir / name).write_bytes(f"old {name}".encode())

      with patch("openpilot.tools.model_selector.load_manifest", return_value=manifest), \
           patch("openpilot.tools.model_selector.verify_manifest_signature"), \
           patch("openpilot.tools.model_selector.stage_model_files", side_effect=stage_files), \
           patch("openpilot.tools.model_selector.inspect_staged_bundle", return_value="top01013-two-onnx"), \
           patch("openpilot.tools.model_selector.set_current_model_param") as set_param:
        result = install_model(
          "opv7",
          repo_root=root,
          models_dir=models_dir,
          cache_root=root / "cache",
          python_bin="python3",
          skip_rebuild=True,
        )

      self.assertEqual(result.layout, "top01013-two-onnx")
      self.assertIsNone(result.previous_dir)
      self.assertTrue((root / "cache" / "active").is_symlink())
      self.assertEqual((result.bundle_dir / "driving_vision.onnx").read_bytes(), b"new driving_vision.onnx")
      self.assertEqual((models_dir / "driving_vision.onnx").read_bytes(), b"old driving_vision.onnx")
      set_param.assert_called_once_with("opv7", "opv7")

  def test_install_model_keeps_active_model_when_rebuild_fails(self):
    manifest = {"models": [model("opv7", {
      "driving_vision.onnx": file_spec(),
      "driving_policy.onnx": file_spec(),
    })]}

    def stage_files(_model, staging_dir: Path):
      for name in ("driving_vision.onnx", "driving_policy.onnx"):
        (staging_dir / name).write_bytes(f"new {name}".encode())
      return model_file_plan(_model)

    with tempfile.TemporaryDirectory() as temp_dir:
      root = Path(temp_dir)
      models_dir = root / "models"
      models_dir.mkdir()
      for name in ("driving_vision.onnx", "driving_policy.onnx"):
        (models_dir / name).write_bytes(f"old {name}".encode())

      with patch("openpilot.tools.model_selector.load_manifest", return_value=manifest), \
           patch("openpilot.tools.model_selector.verify_manifest_signature"), \
           patch("openpilot.tools.model_selector.stage_model_files", side_effect=stage_files), \
           patch("openpilot.tools.model_selector.inspect_staged_bundle", return_value="top01013-two-onnx"), \
           patch("openpilot.tools.model_selector.rebuild_artifacts", side_effect=RuntimeError("compile failed")), \
           patch("openpilot.tools.model_selector.set_current_model_param"):
        with self.assertRaises(RuntimeError):
          install_model(
            "opv7",
            repo_root=root,
            models_dir=models_dir,
            cache_root=root / "cache",
            python_bin="python3",
          )

      self.assertEqual((models_dir / "driving_vision.onnx").read_bytes(), b"old driving_vision.onnx")
      self.assertFalse((root / "cache" / "active").exists())


if __name__ == "__main__":
  unittest.main()
