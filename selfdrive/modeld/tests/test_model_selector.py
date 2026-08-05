import base64
import copy
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from Crypto.PublicKey import ECC
from Crypto.Signature import eddsa

from openpilot.tools.model_selector import (
  install_model,
  SELECTOR_VERSION,
  canonical_json,
  list_installable_models,
  model_file_plan,
  verify_manifest_signature,
)


def signed_manifest(manifest_body: dict) -> tuple[dict, str]:
  key = ECC.generate(curve="Ed25519")
  manifest = copy.deepcopy(manifest_body)
  payload = {k: v for k, v in manifest.items() if k not in ("key_id", "signature")}
  signature = eddsa.new(key, "rfc8032").sign(canonical_json(payload).encode("utf-8"))
  manifest["key_id"] = "test_key"
  manifest["signature"] = base64.b64encode(signature).decode("ascii")
  return manifest, key.public_key().export_key(format="PEM")


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
    manifest, public_key = signed_manifest({
      "version": 1,
      "updated_at": "2026-06-08T00:00:00Z",
      "models": [model("OPv7", {
        "driving_vision.onnx": file_spec(),
        "driving_policy.onnx": file_spec(),
      })],
    })

    verify_manifest_signature(manifest, {"test_key": public_key})

  def test_verify_manifest_signature_rejects_tampering(self):
    manifest, public_key = signed_manifest({
      "version": 1,
      "updated_at": "2026-06-08T00:00:00Z",
      "models": [model("OPv7", {
        "driving_vision.onnx": file_spec(),
        "driving_policy.onnx": file_spec(),
      })],
    })
    manifest["models"][0]["name"] = "tampered"

    with self.assertRaises(ValueError):
      verify_manifest_signature(manifest, {"test_key": public_key})

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

  def test_install_model_backs_up_and_replaces_model_pair(self):
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
        result = install_model("opv7", repo_root=root, models_dir=models_dir, python_bin="python3", skip_rebuild=True)

      self.assertEqual(result.layout, "top01013-two-onnx")
      self.assertTrue(result.backup_dir.is_dir())
      self.assertEqual((models_dir / "driving_vision.onnx").read_bytes(), b"new driving_vision.onnx")
      self.assertEqual((result.backup_dir / "driving_vision.onnx").read_bytes(), b"old driving_vision.onnx")
      set_param.assert_called_once_with("opv7", "opv7")

  def test_install_model_restores_backup_when_rebuild_fails(self):
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
           patch("openpilot.tools.model_selector.rebuild_artifacts", side_effect=[RuntimeError("compile failed"), None]), \
           patch("openpilot.tools.model_selector.set_current_model_param"):
        with self.assertRaises(RuntimeError):
          install_model("opv7", repo_root=root, models_dir=models_dir, python_bin="python3")

      self.assertEqual((models_dir / "driving_vision.onnx").read_bytes(), b"old driving_vision.onnx")


if __name__ == "__main__":
  unittest.main()
