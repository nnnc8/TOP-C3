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

SELECTOR_VERSION = 4
MANIFEST_URL = "https://raw.githubusercontent.com/happymaj11r/openpilot-models/main/models_v4.json"
TRUSTED_PUBLIC_KEYS = {
  "key_2025_01": """-----BEGIN PUBLIC KEY-----
MCowBQYDK2VwAyEAyFPR4om9LyYvQjzRzSiyyso9wc2bP1egmg/PjKa79fg=
-----END PUBLIC KEY-----""",
}
MODEL_NAMES = ("driving_vision", "driving_policy", "driving_supercombo")
MODEL_ARTIFACT_SUFFIXES = (".onnx", "_metadata.pkl", "_tinygrad.pkl")
MIN_MODEL_BYTES = 1024 * 1024
CACHE_MANIFEST_NAME = "models_v4.json"
CACHE_STATUS_NAME = "manifest_status.json"
CACHE_BUNDLES_DIR = "bundles"
CACHE_TMP_DIR = "tmp"
CACHE_ACTIVE_NAME = "active"
CACHE_PREVIOUS_NAME = "previous"
CACHE_MAX_BYTES = 1024 * 1024 * 1024
CACHE_MIN_FREE_BYTES = 2 * 1024 * 1024 * 1024


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
  previous_dir: Path | None
  bundle_dir: Path
  layout: str

  @property
  def backup_dir(self) -> Path | None:
    """Compatibility alias for callers of the pre-cache selector."""
    return self.previous_dir


def canonical_json(obj) -> str:
  if isinstance(obj, dict):
    return "{" + ",".join(
      json.dumps(k, ensure_ascii=False) + ":" + canonical_json(v)
      for k, v in sorted(obj.items())
    ) + "}"
  if isinstance(obj, list):
    return "[" + ",".join(canonical_json(v) for v in obj) + "]"
  return json.dumps(obj, ensure_ascii=False)


def _verify_with_cryptography(message: bytes, signature: bytes, public_key_pem: str) -> None:
  from cryptography.hazmat.primitives import serialization

  public_key = serialization.load_pem_public_key(public_key_pem.encode("ascii"))
  public_key.verify(signature, message)


def _verify_with_pycryptodome(message: bytes, signature: bytes, public_key_pem: str) -> None:
  from Crypto.PublicKey import ECC
  from Crypto.Signature import eddsa

  public_key = ECC.import_key(public_key_pem)
  eddsa.new(public_key, "rfc8032").verify(message, signature)


def _verify_with_openssl(message: bytes, signature: bytes, public_key_pem: str, temp_dir: Path | None) -> None:
  temp_root = None
  if temp_dir is not None:
    temp_root = Path(temp_dir)
    temp_root.mkdir(parents=True, exist_ok=True)

  with tempfile.TemporaryDirectory(prefix="model-signature-", dir=str(temp_root) if temp_root else None) as work_dir:
    work = Path(work_dir)
    public_key_path = work / "public.pem"
    message_path = work / "message"
    signature_path = work / "signature"
    public_key_path.write_text(public_key_pem, encoding="ascii")
    message_path.write_bytes(message)
    signature_path.write_bytes(signature)
    subprocess.run([
      "openssl", "pkeyutl", "-verify", "-rawin", "-pubin",
      "-inkey", str(public_key_path), "-in", str(message_path),
      "-sigfile", str(signature_path),
    ], check=True, capture_output=True, timeout=5)


def verify_manifest_signature(
  manifest: dict,
  public_keys: dict[str, str] | None = None,
  temp_dir: Path | None = None,
) -> None:
  public_keys = public_keys or TRUSTED_PUBLIC_KEYS
  key_id = manifest.get("key_id")
  signature_b64 = manifest.get("signature")
  if not key_id or not signature_b64:
    raise ValueError("manifest is missing key_id or signature")
  if key_id not in public_keys:
    raise ValueError(f"manifest key_id is not trusted: {key_id}")

  payload = {k: v for k, v in manifest.items() if k not in ("key_id", "signature")}
  message = canonical_json(payload).encode("utf-8")
  try:
    signature = base64.b64decode(signature_b64, validate=True)
  except Exception as exc:
    raise ValueError("manifest signature is not valid base64") from exc

  public_key_pem = public_keys[key_id]
  backend_errors: list[Exception] = []
  available_backends = 0
  for verifier in (_verify_with_cryptography, _verify_with_pycryptodome):
    try:
      verifier(message, signature, public_key_pem)
      return
    except ModuleNotFoundError:
      continue
    except Exception as exc:
      available_backends += 1
      backend_errors.append(exc)

  try:
    _verify_with_openssl(message, signature, public_key_pem, temp_dir)
    return
  except FileNotFoundError:
    pass
  except Exception as exc:
    available_backends += 1
    backend_errors.append(exc)

  if available_backends == 0:
    raise RuntimeError("no Ed25519 verifier available; install cryptography or provide openssl")
  raise ValueError("manifest signature verification failed") from backend_errors[-1]


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


def default_cache_root() -> Path:
  configured = os.environ.get("OPENPILOT_MODEL_CACHE")
  if configured:
    return Path(configured)
  if Path("/data").is_dir():
    return Path("/data/model-cache")
  return Path(tempfile.gettempdir()) / "openpilot-model-cache"


def cache_manifest_path(cache_root: Path) -> Path:
  return cache_root / CACHE_MANIFEST_NAME


def cache_status_path(cache_root: Path) -> Path:
  return cache_root / CACHE_STATUS_NAME


def cache_tmp_root(cache_root: Path) -> Path:
  return cache_root / CACHE_TMP_DIR


def safe_model_id(model_id: str) -> str:
  return "".join(ch if ch.isalnum() or ch in ("-", "_") else "_" for ch in model_id)


def bundle_key(model: dict) -> str:
  plan = model_file_plan(model)
  fingerprint = hashlib.sha256(
    "|".join(item.sha256 for item in plan).encode("ascii")
  ).hexdigest()[:12]
  return f"{safe_model_id(str(model['id']))}-{fingerprint}"


def bundle_dir_for_model(cache_root: Path, model: dict) -> Path:
  return cache_root / CACHE_BUNDLES_DIR / bundle_key(model)


def atomic_write_json(path: Path, payload: dict) -> None:
  path.parent.mkdir(parents=True, exist_ok=True)
  with tempfile.NamedTemporaryFile(
    mode="w", encoding="utf-8", dir=path.parent, prefix=f".{path.name}.", delete=False,
  ) as output:
    json.dump(payload, output, ensure_ascii=False, indent=2)
    output.write("\n")
    temp_path = Path(output.name)
  os.replace(temp_path, path)


def load_cached_manifest(cache_root: Path) -> dict:
  path = cache_manifest_path(cache_root)
  with path.open(encoding="utf-8") as manifest_file:
    manifest = json.load(manifest_file)
  verify_manifest_signature(manifest, temp_dir=cache_tmp_root(cache_root))
  return manifest


def sync_manifest(
  manifest_source: str = MANIFEST_URL,
  cache_root: Path | None = None,
) -> dict:
  cache_root = cache_root or default_cache_root()
  cache_root.mkdir(parents=True, exist_ok=True)
  manifest = load_manifest(manifest_source)
  verify_manifest_signature(manifest, temp_dir=cache_tmp_root(cache_root))
  atomic_write_json(cache_manifest_path(cache_root), manifest)
  atomic_write_json(cache_status_path(cache_root), {
    "source": manifest_source,
    "updated_at": manifest.get("updated_at", ""),
    "synced_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    "model_count": len(list_installable_models(manifest)),
    "error": "",
  })
  return manifest


def read_cache_status(cache_root: Path) -> dict:
  try:
    with cache_status_path(cache_root).open(encoding="utf-8") as status_file:
      return json.load(status_file)
  except (FileNotFoundError, json.JSONDecodeError):
    return {}


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


def required_artifact_paths(bundle_dir: Path, plan: list[ModelFilePlan]) -> list[Path]:
  return [
    bundle_dir / f"{item.install_name.removesuffix('.onnx')}{suffix}"
    for item in plan
    for suffix in ("_metadata.pkl", "_tinygrad.pkl")
  ]


def bundle_files_are_valid(bundle_dir: Path, plan: list[ModelFilePlan]) -> bool:
  try:
    for item in plan:
      verify_downloaded_file(bundle_dir / item.install_name, item.size, item.sha256)
    return True
  except (OSError, ValueError):
    return False


def bundle_is_compiled(bundle_dir: Path, plan: list[ModelFilePlan]) -> bool:
  if not bundle_files_are_valid(bundle_dir, plan):
    return False
  try:
    return all(path.is_file() and path.stat().st_size > 0 for path in required_artifact_paths(bundle_dir, plan))
  except OSError:
    return False


def remove_cache_path(path: Path) -> None:
  if path.is_symlink() or path.is_file():
    path.unlink(missing_ok=True)
  elif path.is_dir():
    shutil.rmtree(path)


def cache_size(cache_root: Path) -> int:
  total = 0
  for path in cache_root.glob(f"{CACHE_BUNDLES_DIR}/*"):
    if path.is_dir() and not path.is_symlink():
      total += sum(file.stat().st_size for file in path.rglob("*") if file.is_file())
  return total


def ensure_cache_space(cache_root: Path, required_bytes: int) -> None:
  cache_root.mkdir(parents=True, exist_ok=True)
  free_bytes = shutil.disk_usage(cache_root).free
  if free_bytes < CACHE_MIN_FREE_BYTES:
    prune_cache(cache_root, force=True)
    raise RuntimeError(
      f"insufficient /data space: {free_bytes} bytes free; non-active model cache was pruned"
    )
  if free_bytes < required_bytes + CACHE_MIN_FREE_BYTES:
    raise RuntimeError(
      f"insufficient /data space: {free_bytes} bytes free, {required_bytes + CACHE_MIN_FREE_BYTES} required"
    )


def prune_cache(cache_root: Path, force: bool = False) -> None:
  bundles_root = cache_root / CACHE_BUNDLES_DIR
  if not bundles_root.is_dir() or (not force and cache_size(cache_root) <= CACHE_MAX_BYTES):
    return

  protected = set()
  for name in (CACHE_ACTIVE_NAME, CACHE_PREVIOUS_NAME):
    link = cache_root / name
    if link.is_symlink():
      protected.add(Path(os.path.realpath(link)))

  candidates = sorted(
    (path for path in bundles_root.iterdir() if path.is_dir() and not path.is_symlink()),
    key=lambda path: path.stat().st_mtime,
  )
  for path in candidates:
    if path.resolve() in protected:
      continue
    remove_cache_path(path)
    if not force and cache_size(cache_root) <= CACHE_MAX_BYTES:
      break


def active_bundle_target(cache_root: Path) -> Path | None:
  link = cache_root / CACHE_ACTIVE_NAME
  if not link.is_symlink():
    return None
  target = Path(os.path.realpath(link))
  return target if target.is_dir() else None


def activate_bundle(cache_root: Path, bundle_dir: Path) -> Path | None:
  cache_root.mkdir(parents=True, exist_ok=True)
  bundle_dir = bundle_dir.resolve()
  previous = active_bundle_target(cache_root)
  token = f"{os.getpid()}-{time.monotonic_ns()}"

  if previous is not None and previous != bundle_dir:
    previous_link = cache_root / f".{CACHE_PREVIOUS_NAME}.{token}"
    previous_link.symlink_to(previous, target_is_directory=True)
    os.replace(previous_link, cache_root / CACHE_PREVIOUS_NAME)

  active_link = cache_root / f".{CACHE_ACTIVE_NAME}.{token}"
  active_link.symlink_to(bundle_dir, target_is_directory=True)
  os.replace(active_link, cache_root / CACHE_ACTIVE_NAME)
  return previous


def prepare_bundle(
  model: dict,
  cache_root: Path,
  repo_root: Path,
  python_bin: str,
  offline: bool,
  skip_rebuild: bool,
) -> tuple[Path, list[ModelFilePlan], str]:
  cache_root.mkdir(parents=True, exist_ok=True)
  cache_tmp_root(cache_root).mkdir(parents=True, exist_ok=True)
  plan = model_file_plan(model)
  bundle_dir = bundle_dir_for_model(cache_root, model)

  if (skip_rebuild and bundle_files_are_valid(bundle_dir, plan)) or bundle_is_compiled(bundle_dir, plan):
    layout = "top01013-supercombo" if any(item.install_name == "driving_supercombo.onnx" for item in plan) else "top01013-two-onnx"
    return bundle_dir, plan, layout

  ensure_cache_space(cache_root, sum(item.size for item in plan))
  work_dir = Path(tempfile.mkdtemp(prefix="bundle-", dir=cache_tmp_root(cache_root)))
  try:
    if bundle_files_are_valid(bundle_dir, plan):
      for item in plan:
        shutil.copy2(bundle_dir / item.install_name, work_dir / item.install_name)
    elif offline:
      raise RuntimeError(f"model is not cached for offline install: {model['id']}")
    else:
      stage_model_files(model, work_dir)

    layout = inspect_staged_bundle(repo_root, work_dir, python_bin)
    model_names = tuple(item.install_name.removesuffix(".onnx") for item in plan)
    if not skip_rebuild:
      rebuild_artifacts(repo_root, work_dir, python_bin, model_names, temp_dir=cache_tmp_root(cache_root))
      if not bundle_is_compiled(work_dir, plan):
        raise RuntimeError("compiled model bundle is incomplete")

    atomic_write_json(work_dir / "bundle.json", {
      "id": model["id"],
      "name": model.get("name", model["id"]),
      "layout": layout,
      "source": MANIFEST_URL,
      "created_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    })
    if bundle_dir.exists() or bundle_dir.is_symlink():
      remove_cache_path(bundle_dir)
    bundle_dir.parent.mkdir(parents=True, exist_ok=True)
    os.replace(work_dir, bundle_dir)
  except Exception:
    remove_cache_path(work_dir)
    raise

  prune_cache(cache_root)
  return bundle_dir, plan, layout


def compile_env(repo_root: Path, temp_dir: Path | None = None) -> dict[str, str]:
  env = dict(os.environ)
  env["PYTHONPATH"] = f"{repo_root / 'tinygrad_repo'}" + (f":{env['PYTHONPATH']}" if env.get("PYTHONPATH") else "")
  if temp_dir is not None:
    Path(temp_dir).mkdir(parents=True, exist_ok=True)
    env["TMPDIR"] = str(temp_dir)
    env["TEMP"] = str(temp_dir)
    env["TMP"] = str(temp_dir)

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


def rebuild_artifacts(
  repo_root: Path,
  models_dir: Path,
  python_bin: str,
  model_names: tuple[str, ...],
  temp_dir: Path | None = None,
) -> None:
  env = compile_env(repo_root, temp_dir=temp_dir)
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
    repo_root = str(repo_root_from_file())
    if repo_root not in sys.path:
      sys.path.insert(0, repo_root)
    try:
      from openpilot.common.params import Params
    except ModuleNotFoundError:
      from common.params import Params
  except Exception as exc:
    print(f"Warning: unable to update DrivingModel param: {exc}", file=sys.stderr)
    return

  params = Params()
  params.put("DrivingModel", model_id)
  params.put("DrivingModelName", model_name)


def manifest_for_install(manifest_source: str, cache_root: Path, offline: bool) -> dict:
  if manifest_source == MANIFEST_URL:
    try:
      return load_cached_manifest(cache_root)
    except (FileNotFoundError, json.JSONDecodeError, ValueError):
      if offline:
        raise RuntimeError("signed model manifest is not available offline")

  manifest = load_manifest(manifest_source)
  verify_manifest_signature(manifest, temp_dir=cache_tmp_root(cache_root))
  atomic_write_json(cache_manifest_path(cache_root), manifest)
  return manifest


def install_model(
  model_id: str,
  manifest_source: str = MANIFEST_URL,
  repo_root: Path | None = None,
  models_dir: Path | None = None,
  cache_root: Path | None = None,
  python_bin: str | None = None,
  selector_version: int = SELECTOR_VERSION,
  skip_rebuild: bool = False,
  update_params: bool = True,
  offline: bool = False,
) -> InstallResult:
  repo_root = repo_root or repo_root_from_file()
  if cache_root is None:
    cache_root = (models_dir.parent / ".model-cache") if models_dir is not None else default_cache_root()
  cache_root.mkdir(parents=True, exist_ok=True)
  python_bin = python_bin or sys.executable

  manifest = manifest_for_install(manifest_source, cache_root, offline)
  model = find_model(manifest, model_id, selector_version=selector_version)

  bundle_dir, _, layout = prepare_bundle(
    model,
    cache_root,
    repo_root,
    python_bin,
    offline=offline,
    skip_rebuild=skip_rebuild,
  )
  previous_dir = activate_bundle(cache_root, bundle_dir)

  if update_params:
    set_current_model_param(str(model["id"]), str(model["name"]))
  return InstallResult(str(model["id"]), str(model["name"]), previous_dir, bundle_dir, layout)


def model_summary(model: dict, cache_root: Path | None = None, manifest_status: dict | None = None) -> dict:
  size = sum(int(file_info["size"]) for file_info in model.get("files", {}).values())
  summary = {
    "id": model["id"],
    "name": model["name"],
    "added_at": model.get("added_at", ""),
    "minimum_selector_version": int(model.get("minimum_selector_version", 1)),
    "size": size,
    "size_mb": round(size / (1024 * 1024), 1),
  }
  if cache_root is not None:
    plan = model_file_plan(model)
    bundle_dir = bundle_dir_for_model(cache_root, model)
    active = active_bundle_target(cache_root)
    summary.update({
      "cached": bundle_files_are_valid(bundle_dir, plan),
      "compiled": bundle_is_compiled(bundle_dir, plan),
      "active": active == bundle_dir.resolve() if active else False,
      "source": MANIFEST_URL,
      "manifest_updated_at": (manifest_status or {}).get("updated_at", ""),
      "manifest_error": (manifest_status or {}).get("error", ""),
    })
  return summary


def load_manifest_for_list(args: argparse.Namespace, cache_root: Path) -> tuple[dict, dict]:
  if args.offline:
    manifest = load_cached_manifest(cache_root)
    return manifest, read_cache_status(cache_root)

  try:
    manifest = sync_manifest(args.manifest, cache_root)
    return manifest, read_cache_status(cache_root)
  except Exception as exc:
    try:
      manifest = load_cached_manifest(cache_root)
    except Exception:
      raise exc
    status = read_cache_status(cache_root)
    status["error"] = str(exc)
    try:
      atomic_write_json(cache_status_path(cache_root), status)
    except OSError:
      pass
    return manifest, status


def cmd_sync(args: argparse.Namespace) -> int:
  cache_root = Path(args.cache_root).resolve()
  manifest = sync_manifest(args.manifest, cache_root)
  status = read_cache_status(cache_root)
  result = {
    "source": args.manifest,
    "updated_at": manifest.get("updated_at", ""),
    "synced_at": status.get("synced_at", ""),
    "model_count": len(list_installable_models(manifest, selector_version=args.selector_version)),
  }
  if args.format == "json":
    print(json.dumps(result, ensure_ascii=False, indent=2))
  else:
    print(f"Synced {result['model_count']} models from {result['source']}")
  return 0


def cmd_list(args: argparse.Namespace) -> int:
  cache_root = Path(args.cache_root).resolve()
  manifest, status = load_manifest_for_list(args, cache_root)
  models = [
    model_summary(model, cache_root=cache_root, manifest_status=status)
    for model in list_installable_models(manifest, selector_version=args.selector_version)
  ]

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
    cache_root=Path(args.cache_root).resolve(),
    python_bin=args.python_bin,
    selector_version=args.selector_version,
    skip_rebuild=args.skip_rebuild,
    update_params=not args.no_param,
    offline=args.offline,
  )
  print(f"Installed {result.model_name} ({result.model_id})")
  print(f"Detected layout: {result.layout}")
  print(f"Bundle: {result.bundle_dir}")
  print(f"Previous: {result.previous_dir or 'none'}")
  return 0


def build_arg_parser() -> argparse.ArgumentParser:
  parser = argparse.ArgumentParser(description="List and install signed TOP01013-C3 driving models.")
  parser.add_argument("--manifest", default=MANIFEST_URL, help="models.json URL or local path")
  parser.add_argument("--selector-version", type=int, default=SELECTOR_VERSION)
  parser.add_argument("--cache-root", default=str(default_cache_root()))
  subparsers = parser.add_subparsers(dest="command", required=True)

  list_parser = subparsers.add_parser("list", help="List installable model bundles")
  list_parser.add_argument("--format", choices=("names", "json"), default="names")
  list_parser.add_argument("--offline", action="store_true", help="Use only the verified cached manifest")
  list_parser.set_defaults(func=cmd_list)

  sync_parser = subparsers.add_parser("sync", help="Fetch and verify the signed model manifest")
  sync_parser.add_argument("--format", choices=("text", "json"), default="text")
  sync_parser.set_defaults(func=cmd_sync)

  install_parser = subparsers.add_parser("install", help="Download, verify, compile, and install a model bundle")
  install_parser.add_argument("model_id")
  install_parser.add_argument("--repo-root", default=str(repo_root_from_file()))
  install_parser.add_argument("--python-bin", default=sys.executable)
  install_parser.add_argument("--skip-rebuild", action="store_true", help="Copy ONNX files without rebuilding tinygrad artifacts")
  install_parser.add_argument("--no-param", action="store_true", help="Do not update DrivingModel params after install")
  install_parser.add_argument("--offline", action="store_true", help="Install only from the verified cache")
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
