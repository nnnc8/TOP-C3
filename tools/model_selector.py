#!/usr/bin/env python3
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from urllib.parse import quote

from Crypto.PublicKey import ECC
from Crypto.Signature import eddsa


SELECTOR_VERSION = 4
MANIFEST_URL = "https://raw.githubusercontent.com/nnnc8/openpilot-models/main/models_v4.json"
TRUSTED_PUBLIC_KEYS = {
  "key_2025_01": """-----BEGIN PUBLIC KEY-----
MCowBQYDK2VwAyEAyFPR4om9LyYvQjzRzSiyyso9wc2bP1egmg/PjKa79fg=
-----END PUBLIC KEY-----""",
}
MODEL_NAMES = ("driving_vision", "driving_policy", "driving_supercombo")
MODEL_ARTIFACT_SUFFIXES = (".onnx", "_metadata.pkl", "_tinygrad.pkl")
MIN_MODEL_BYTES = 1024 * 1024


@dataclass(frozen=True)
class ModelFilePlan:
  remote_name: str
  install_name: str
  url: str
  size: int
  sha256: str


@dataclass(frozen=True)
class InstallResult:
  model_id: str
  model_name: str
  backup_dir: Path
  layout: str


def canonical_json(obj) -> str:
  if isinstance(obj, dict):
    return "{" + ",".join(
      json.dumps(k, ensure_ascii=False) + ":" + canonical_json(v)
      for k, v in sorted(obj.items())
    ) + "}"
  if isinstance(obj, list):
    return "[" + ",".join(canonical_json(v) for v in obj) + "]"
  return json.dumps(obj, ensure_ascii=False)


def verify_manifest_signature(manifest: dict, public_keys: dict[str, str] | None = None) -> None:
  public_keys = public_keys or TRUSTED_PUBLIC_KEYS
  key_id = manifest.get("key_id")
  signature_b64 = manifest.get("signature")
  if not key_id or not signature_b64:
    raise ValueError("manifest is missing key_id or signature")
  if key_id not in public_keys:
    raise ValueError(f"manifest key_id is not trusted: {key_id}")

  payload = {k: v for k, v in manifest.items() if k not in ("key_id", "signature")}
  message = canonical_json(payload).encode("utf-8")
  signature = base64.b64decode(signature_b64)
  public_key = ECC.import_key(public_keys[key_id])

  try:
    eddsa.new(public_key, "rfc8032").verify(message, signature)
  except ValueError as exc:
    raise ValueError("manifest signature verification failed") from exc


def model_file_url(model: dict, remote_name: str) -> str:
  base_url = str(model["base_url"]).rstrip("/")
  return quote(f"{base_url}/{remote_name}", safe=":/%")


def model_file_plan(model: dict) -> list[ModelFilePlan]:
  files = model.get("files", {})
  if "driving_supercombo.onnx" in files:
    spec = files["driving_supercombo.onnx"]
    return [ModelFilePlan(
      remote_name="driving_supercombo.onnx",
      install_name="driving_supercombo.onnx",
      url=model_file_url(model, "driving_supercombo.onnx"),
      size=int(spec["size"]),
      sha256=str(spec["sha256"]),
    )]

  if "driving_vision.onnx" not in files:
    raise ValueError("missing driving_vision.onnx")
  policy_remote_name = "driving_policy.onnx" if "driving_policy.onnx" in files else "driving_on_policy.onnx" if "driving_on_policy.onnx" in files else None
  if policy_remote_name is None:
    raise ValueError("missing driving_policy.onnx or driving_on_policy.onnx")

  plan_names = [
    ("driving_vision.onnx", "driving_vision.onnx"),
    (policy_remote_name, "driving_policy.onnx"),
  ]
  plan: list[ModelFilePlan] = []
  for remote_name, install_name in plan_names:
    spec = files[remote_name]
    plan.append(ModelFilePlan(
      remote_name=remote_name,
      install_name=install_name,
      url=model_file_url(model, remote_name),
      size=int(spec["size"]),
      sha256=str(spec["sha256"]),
    ))
  return plan


def model_is_installable(model: dict, selector_version: int = SELECTOR_VERSION) -> bool:
  if int(model.get("minimum_selector_version", 1)) > selector_version:
    return False
  try:
    model_file_plan(model)
  except ValueError:
    return False
  return True


def list_installable_models(manifest: dict, selector_version: int = SELECTOR_VERSION) -> list[dict]:
  return [
    model
    for model in manifest.get("models", [])
    if model_is_installable(model, selector_version=selector_version)
  ]


def load_manifest(source: str = MANIFEST_URL) -> dict:
  if source.startswith("http://") or source.startswith("https://"):
    with urllib.request.urlopen(source, timeout=30) as response:
      return json.loads(response.read().decode("utf-8"))

  with open(source, encoding="utf-8") as manifest_file:
    return json.load(manifest_file)


def find_model(manifest: dict, model_id: str, selector_version: int = SELECTOR_VERSION) -> dict:
  for model in manifest.get("models", []):
    if model.get("id") == model_id:
      if not model_is_installable(model, selector_version=selector_version):
        raise ValueError(f"model is not installable by selector v{selector_version}: {model_id}")
      return model
  raise ValueError(f"unknown model id: {model_id}")


def verify_downloaded_file(path: Path, expected_size: int, expected_sha256: str) -> None:
  size = path.stat().st_size
  if size < MIN_MODEL_BYTES:
    raise ValueError(f"{path.name} is unexpectedly small: {size} bytes")
  if size != expected_size:
    raise ValueError(f"{path.name} size mismatch: got {size}, expected {expected_size}")

  sha256 = hashlib.sha256()
  with path.open("rb") as file:
    for chunk in iter(lambda: file.read(1024 * 1024), b""):
      sha256.update(chunk)
  actual_sha256 = sha256.hexdigest()
  if actual_sha256 != expected_sha256:
    raise ValueError(f"{path.name} sha256 mismatch: got {actual_sha256}, expected {expected_sha256}")


def download_file(url: str, output_path: Path) -> None:
  with urllib.request.urlopen(url, timeout=120) as response, output_path.open("wb") as output:
    shutil.copyfileobj(response, output)


def stage_model_files(model: dict, staging_dir: Path) -> list[ModelFilePlan]:
  plan = model_file_plan(model)
  staging_dir.mkdir(parents=True, exist_ok=True)

  for item in plan:
    output_path = staging_dir / item.install_name
    print(f"Downloading {item.remote_name} -> {item.install_name}")
    download_file(item.url, output_path)
    verify_downloaded_file(output_path, item.size, item.sha256)

  return plan


def repo_root_from_file() -> Path:
  return Path(__file__).resolve().parents[1]


def run_checked(cmd: list[str], cwd: Path, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
  proc = subprocess.run(cmd, cwd=cwd, text=True, capture_output=True, env=env)
  if proc.returncode != 0:
    output = proc.stderr.strip() or proc.stdout.strip()
    raise RuntimeError(output or f"command failed: {' '.join(cmd)}")
  return proc


def inspect_staged_bundle(repo_root: Path, staging_dir: Path, python_bin: str) -> str:
  proc = run_checked([
    python_bin,
    str(repo_root / "tools/inspect_model_outputs.py"),
    "--bundle-dir",
    str(staging_dir),
    "--validate",
  ], repo_root)

  for line in proc.stdout.splitlines():
    line = line.strip()
    if line.startswith("Detected layout:"):
      return line.removeprefix("Detected layout:").strip()
  return "validated"


def backup_current_bundle(models_dir: Path, model_id: str) -> Path:
  safe_id = "".join(ch if ch.isalnum() or ch in ("-", "_") else "_" for ch in model_id)
  backup_dir = models_dir / "backups" / f"{time.strftime('%Y%m%d-%H%M%S')}-{safe_id}"
  backup_dir.mkdir(parents=True, exist_ok=False)

  for model_name in MODEL_NAMES:
    for suffix in MODEL_ARTIFACT_SUFFIXES:
      source = models_dir / f"{model_name}{suffix}"
      if source.is_file():
        shutil.copy2(source, backup_dir / source.name)

  return backup_dir


def restore_bundle_from_backup(models_dir: Path, backup_dir: Path) -> None:
  for model_name in MODEL_NAMES:
    for suffix in MODEL_ARTIFACT_SUFFIXES:
      source = backup_dir / f"{model_name}{suffix}"
      if source.is_file():
        shutil.copy2(source, models_dir / source.name)


def clear_model_artifacts(models_dir: Path) -> None:
  for model_name in MODEL_NAMES:
    for suffix in MODEL_ARTIFACT_SUFFIXES:
      (models_dir / f"{model_name}{suffix}").unlink(missing_ok=True)


def copy_staged_onnx(staging_dir: Path, models_dir: Path, plan: list[ModelFilePlan]) -> None:
  models_dir.mkdir(parents=True, exist_ok=True)
  for item in plan:
    shutil.copy2(staging_dir / item.install_name, models_dir / item.install_name)


def compile_env(repo_root: Path) -> dict[str, str]:
  env = dict(os.environ)
  env["PYTHONPATH"] = f"{repo_root / 'tinygrad_repo'}" + (f":{env['PYTHONPATH']}" if env.get("PYTHONPATH") else "")

  system = platform.system()
  machine = platform.machine().lower()
  if system == "Darwin":
    env.update({
      "DEV": "CPU",
      "IMAGE": "0",
      "HOME": env.get("HOME", str(repo_root)),
    })
  elif machine in ("aarch64", "arm64"):
    env["DEV"] = "QCOM"
  else:
    env.update({
      "DEV": "CPU",
      "CPU_LLVM": "1",
      "IMAGE": "0",
    })
  return env


def rebuild_artifacts(repo_root: Path, models_dir: Path, python_bin: str, model_names: tuple[str, ...]) -> None:
  env = compile_env(repo_root)
  compile_script = repo_root / "tinygrad_repo/examples/openpilot/compile3.py"
  metadata_script = repo_root / "selfdrive/modeld/get_model_metadata.py"

  if not compile_script.is_file():
    raise RuntimeError(f"missing tinygrad compiler: {compile_script}")
  if not metadata_script.is_file():
    raise RuntimeError(f"missing metadata tool: {metadata_script}")

  for model_name in model_names:
    model_path = models_dir / f"{model_name}.onnx"
    if not model_path.is_file():
      raise RuntimeError(f"missing model file: {model_path}")

    run_checked([python_bin, str(metadata_script), str(model_path)], repo_root, env=env)
    run_checked([python_bin, str(compile_script), str(model_path), str(models_dir / f"{model_name}_tinygrad.pkl")], repo_root, env=env)


def set_current_model_param(model_id: str, model_name: str) -> None:
  try:
    from openpilot.common.params import Params
  except Exception as exc:
    print(f"Warning: unable to update DrivingModel param: {exc}", file=sys.stderr)
    return

  params = Params()
  params.put("DrivingModel", model_id)
  params.put("DrivingModelName", model_name)


def install_model(
  model_id: str,
  manifest_source: str = MANIFEST_URL,
  repo_root: Path | None = None,
  models_dir: Path | None = None,
  python_bin: str | None = None,
  selector_version: int = SELECTOR_VERSION,
  skip_rebuild: bool = False,
  update_params: bool = True,
) -> InstallResult:
  repo_root = repo_root or repo_root_from_file()
  models_dir = models_dir or repo_root / "selfdrive/modeld/models"
  python_bin = python_bin or sys.executable

  manifest = load_manifest(manifest_source)
  verify_manifest_signature(manifest)
  model = find_model(manifest, model_id, selector_version=selector_version)

  with tempfile.TemporaryDirectory(prefix="openpilot-model-") as tmp:
    staging_dir = Path(tmp)
    plan = stage_model_files(model, staging_dir)
    layout = inspect_staged_bundle(repo_root, staging_dir, python_bin)
    backup_dir = backup_current_bundle(models_dir, model_id)
    previous_model_names = tuple(path.stem for path in backup_dir.glob("*.onnx"))
    model_names = tuple(item.install_name.removesuffix(".onnx") for item in plan)

    try:
      clear_model_artifacts(models_dir)
      copy_staged_onnx(staging_dir, models_dir, plan)
      if not skip_rebuild:
        rebuild_artifacts(repo_root, models_dir, python_bin, model_names)
    except Exception:
      clear_model_artifacts(models_dir)
      restore_bundle_from_backup(models_dir, backup_dir)
      if not skip_rebuild and previous_model_names:
        try:
          rebuild_artifacts(repo_root, models_dir, python_bin, previous_model_names)
        except Exception as restore_exc:
          print(f"Warning: backup restored but artifact rebuild failed: {restore_exc}", file=sys.stderr)
      raise

  if update_params:
    set_current_model_param(str(model["id"]), str(model["name"]))
  return InstallResult(str(model["id"]), str(model["name"]), backup_dir, layout)


def model_summary(model: dict) -> dict:
  size = sum(int(file_info["size"]) for file_info in model.get("files", {}).values())
  return {
    "id": model["id"],
    "name": model["name"],
    "added_at": model.get("added_at", ""),
    "minimum_selector_version": int(model.get("minimum_selector_version", 1)),
    "size": size,
    "size_mb": round(size / (1024 * 1024), 1),
  }


def cmd_list(args: argparse.Namespace) -> int:
  manifest = load_manifest(args.manifest)
  verify_manifest_signature(manifest)
  models = [model_summary(model) for model in list_installable_models(manifest, selector_version=args.selector_version)]

  if args.format == "json":
    print(json.dumps(models, ensure_ascii=False, indent=2))
  else:
    for model in models:
      print(f"{model['id']}\t{model['name']}\t{model['size_mb']:.1f}MB\t{model['added_at']}")
  return 0


def cmd_install(args: argparse.Namespace) -> int:
  result = install_model(
    args.model_id,
    manifest_source=args.manifest,
    repo_root=Path(args.repo_root).resolve(),
    models_dir=Path(args.models_dir).resolve() if args.models_dir else None,
    python_bin=args.python_bin,
    selector_version=args.selector_version,
    skip_rebuild=args.skip_rebuild,
    update_params=not args.no_param,
  )
  print(f"Installed {result.model_name} ({result.model_id})")
  print(f"Detected layout: {result.layout}")
  print(f"Backup: {result.backup_dir}")
  print("Recommended checks:")
  print("  tools/c3_postinstall_smoke_check.sh offroad")
  print("  tools/c3_postinstall_smoke_check.sh runtime --profile c3")
  return 0


def build_arg_parser() -> argparse.ArgumentParser:
  parser = argparse.ArgumentParser(description="List and install signed TOP01013-C3 driving models.")
  parser.add_argument("--manifest", default=MANIFEST_URL, help="models.json URL or local path")
  parser.add_argument("--selector-version", type=int, default=SELECTOR_VERSION)
  subparsers = parser.add_subparsers(dest="command", required=True)

  list_parser = subparsers.add_parser("list", help="List installable model bundles")
  list_parser.add_argument("--format", choices=("names", "json"), default="names")
  list_parser.set_defaults(func=cmd_list)

  install_parser = subparsers.add_parser("install", help="Download, verify, compile, and install a model bundle")
  install_parser.add_argument("model_id")
  install_parser.add_argument("--repo-root", default=str(repo_root_from_file()))
  install_parser.add_argument("--models-dir", default="")
  install_parser.add_argument("--python-bin", default=sys.executable)
  install_parser.add_argument("--skip-rebuild", action="store_true", help="Copy ONNX files without rebuilding tinygrad artifacts")
  install_parser.add_argument("--no-param", action="store_true", help="Do not update DrivingModel params after install")
  install_parser.set_defaults(func=cmd_install)
  return parser


def main() -> int:
  parser = build_arg_parser()
  args = parser.parse_args()
  try:
    return args.func(args)
  except Exception as exc:
    print(f"model_selector: {exc}", file=sys.stderr)
    return 1


if __name__ == "__main__":
  raise SystemExit(main())
