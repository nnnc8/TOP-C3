#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODELS_DIR="${1:-$ROOT/selfdrive/modeld/models}"
PYTHON_BIN="${PYTHON_BIN:-python3}"

if [ ! -d "$ROOT/tinygrad_repo" ] || [ -z "$(ls -A "$ROOT/tinygrad_repo" 2>/dev/null)" ]; then
  echo "tinygrad_repo is missing. Run: git submodule update --init --recursive tinygrad_repo" >&2
  exit 1
fi

if ! command -v "$PYTHON_BIN" >/dev/null 2>&1; then
  echo "python interpreter not found: $PYTHON_BIN" >&2
  exit 1
fi

if ! "$PYTHON_BIN" -c "import numpy, onnx" >/dev/null 2>&1; then
  echo "python dependencies missing. Install at least numpy and onnx in the active environment." >&2
  exit 1
fi

compile_flags=()
if [ "$(uname)" = "Darwin" ]; then
  compile_flags=("DEV=CPU" "IMAGE=0" "HOME=${HOME:-$ROOT}")
elif [ "$(uname -m)" = "aarch64" ] || [ "$(uname -m)" = "arm64" ]; then
  compile_flags=("DEV=QCOM")
else
  compile_flags=("DEV=CPU" "CPU_LLVM=1" "IMAGE=0")
fi

export PYTHONPATH="$ROOT/tinygrad_repo${PYTHONPATH:+:$PYTHONPATH}"

for model_name in driving_vision driving_policy; do
  model_path="$MODELS_DIR/${model_name}.onnx"
  metadata_path="$MODELS_DIR/${model_name}_metadata.pkl"
  tinygrad_path="$MODELS_DIR/${model_name}_tinygrad.pkl"

  if [ ! -f "$model_path" ]; then
    echo "missing model file: $model_path" >&2
    exit 1
  fi

  "$PYTHON_BIN" "$ROOT/selfdrive/modeld/get_model_metadata.py" "$model_path"
  env "${compile_flags[@]}" "$PYTHON_BIN" "$ROOT/tinygrad_repo/examples/openpilot/compile3.py" "$model_path" "$tinygrad_path"

  if [ ! -f "$metadata_path" ] || [ ! -f "$tinygrad_path" ]; then
    echo "failed to rebuild artifacts for $model_name" >&2
    exit 1
  fi
done
