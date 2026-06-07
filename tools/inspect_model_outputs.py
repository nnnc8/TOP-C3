#!/usr/bin/env python3
from __future__ import annotations

import argparse
import codecs
import pathlib
import pickle
import sys

try:
  import onnx
except ModuleNotFoundError as exc:
  print("onnx is required to inspect model outputs.", file=sys.stderr)
  raise SystemExit(2) from exc


MODEL_NAMES = ("driving_vision", "driving_policy")


def get_name_and_shape(value_info: onnx.ValueInfoProto) -> tuple[str, tuple[int, ...]]:
  shape = tuple(int(dim.dim_value) if dim.dim_value else 0 for dim in value_info.type.tensor_type.shape.dim)
  return value_info.name, shape


def get_metadata_value_by_name(model: onnx.ModelProto, name: str) -> str | None:
  for prop in model.metadata_props:
    if prop.key == name:
      return prop.value
  return None


def load_model_metadata(model_path: pathlib.Path) -> dict:
  model = onnx.load(str(model_path))
  output_slices_raw = get_metadata_value_by_name(model, "output_slices")
  if output_slices_raw is None:
    raise ValueError(f"{model_path.name} is missing output_slices metadata")

  return {
    "input_shapes": dict(get_name_and_shape(x) for x in model.graph.input),
    "output_shapes": dict(get_name_and_shape(x) for x in model.graph.output),
    "output_slices": pickle.loads(codecs.decode(output_slices_raw.encode(), "base64")),
  }


def inspect_bundle(bundle_dir: pathlib.Path) -> tuple[str, list[str]]:
  bundle_metadata: dict[str, dict] = {}
  errors: list[str] = []

  for model_name in MODEL_NAMES:
    model_path = bundle_dir / f"{model_name}.onnx"
    if not model_path.is_file():
      errors.append(f"missing {model_path.name}")
      continue
    try:
      bundle_metadata[model_name] = load_model_metadata(model_path)
    except Exception as exc:
      errors.append(f"{model_path.name}: {exc}")

  print(f"Bundle directory: {bundle_dir}")
  for model_name, metadata in bundle_metadata.items():
    print(f"\n== {model_name}.onnx ==")
    print(f"inputs: {metadata['input_shapes']}")
    print(f"graph outputs: {metadata['output_shapes']}")
    print(f"slice keys: {sorted(k for k in metadata['output_slices'].keys() if k != 'pad')}")

  layout = "top01013-two-onnx" if not errors else "unsupported"
  print(f"\nDetected layout: {layout}")
  if errors:
    print("Validation errors:")
    for error in errors:
      print(f"  - {error}")
  else:
    print("Validation: OK")

  return layout, errors


def main() -> int:
  parser = argparse.ArgumentParser(description="Inspect TOP01013 driving model ONNX metadata.")
  parser.add_argument("--bundle-dir", default="selfdrive/modeld/models", help="Directory containing driving_vision/driving_policy ONNX files")
  parser.add_argument("--validate", action="store_true", help="Exit non-zero when the bundle is unsupported")
  args = parser.parse_args()

  layout, errors = inspect_bundle(pathlib.Path(args.bundle_dir))
  if args.validate and errors:
    return 1
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
