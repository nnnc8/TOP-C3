import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from openpilot.selfdrive.modeld.modeld import DEFAULT_MODEL_DIR, resolve_model_dir


class TestModeldCache(unittest.TestCase):
  def test_resolve_model_dir_falls_back_to_repo_models(self):
    with tempfile.TemporaryDirectory() as temp_dir, patch.dict(os.environ, {"OPENPILOT_MODEL_CACHE": temp_dir}):
      self.assertEqual(resolve_model_dir(), DEFAULT_MODEL_DIR)

  def test_resolve_model_dir_accepts_split_bundle(self):
    with tempfile.TemporaryDirectory() as temp_dir:
      active = Path(temp_dir) / "active"
      active.mkdir(parents=True)
      for name in (
        "driving_vision_tinygrad.pkl",
        "driving_vision_metadata.pkl",
        "driving_policy_tinygrad.pkl",
        "driving_policy_metadata.pkl",
      ):
        (active / name).touch()

      with patch.dict(os.environ, {"OPENPILOT_MODEL_CACHE": temp_dir}):
        self.assertEqual(resolve_model_dir(), active)

  def test_resolve_model_dir_accepts_supercombo_bundle(self):
    with tempfile.TemporaryDirectory() as temp_dir:
      active = Path(temp_dir) / "active"
      active.mkdir(parents=True)
      (active / "driving_supercombo_tinygrad.pkl").touch()
      (active / "driving_supercombo_metadata.pkl").touch()

      with patch.dict(os.environ, {"OPENPILOT_MODEL_CACHE": temp_dir}):
        self.assertEqual(resolve_model_dir(), active)


if __name__ == "__main__":
  unittest.main()
