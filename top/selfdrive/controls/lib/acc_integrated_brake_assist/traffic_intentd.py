#!/usr/bin/env python3
from __future__ import annotations

import json
import math
import os
import time
from pathlib import Path
from typing import NoReturn

from cereal import custom
import cereal.messaging as messaging

from openpilot.common.params import Params
from openpilot.common.realtime import Ratekeeper
from openpilot.top.selfdrive.controls.lib.acc_integrated_brake_assist.controller import TrafficIntent

STOP_ACTION_CONFIDENCE = 0.55
EXTERNAL_STATE_MAX_AGE_S = 5.0
DEFAULT_EXTERNAL_STATE_FILES = (
  Path(os.environ.get("FRIDAY_TRAFFIC_LIGHT_STATE_FILE", "/data/params/d/FridayTrafficLightState")),
  Path(os.environ.get("SIENNA_TRAFFIC_LIGHT_STATE_FILE", "/data/params/d/SiennaTrafficLightState")),
)


def _as_float(value) -> float | None:
  if value is None or value == "":
    return None
  try:
    out = float(value)
  except (TypeError, ValueError):
    return None
  return out if math.isfinite(out) else None


def _clamp(value: float, lo: float, hi: float) -> float:
  return max(lo, min(hi, value))


def load_external_traffic_light_state(path: Path | None = None) -> dict:
  paths = (path,) if path is not None else DEFAULT_EXTERNAL_STATE_FILES
  for candidate in paths:
    try:
      data = json.loads(candidate.read_text(encoding="utf-8"))
    except Exception:
      continue
    if isinstance(data, dict):
      return data
  return {}


def _normalize_range(value) -> str:
  raw = str(value or "").strip().lower().replace("-", "_")
  if raw in ("far", "mid", "near"):
    return raw
  if raw in ("long", "distant"):
    return "far"
  if raw in ("middle", "medium"):
    return "mid"
  if raw in ("close", "short"):
    return "near"
  return "unknown"


def _intent_from_external_state(external_state: dict | None, now_ms: int) -> TrafficIntent | None:
  if not external_state:
    return None
  updated_at_ms = _as_float(external_state.get("updated_at_ms", external_state.get("last_seen_ms")))
  if updated_at_ms is not None and (now_ms - updated_at_ms) / 1000.0 > EXTERNAL_STATE_MAX_AGE_S:
    return None

  confidence = _as_float(external_state.get("confidence"))
  confidence = _clamp(confidence if confidence is not None else 0.0, 0.0, 1.0)
  source = str(external_state.get("source", "traffic_light_state"))
  color = str(external_state.get("color", external_state.get("signal_color", ""))).lower()
  range_name = _normalize_range(external_state.get(
    "target_signal_range",
    external_state.get("signal_range", external_state.get("range", external_state.get("distance_range"))),
  ))

  if bool(external_state.get("red_present")) or color == "red":
    return TrafficIntent(color="red", confidence=confidence, source=source, range=range_name)
  if bool(external_state.get("mixed_signal")) or color == "mixed":
    return TrafficIntent(color="mixed", confidence=confidence, source=source, range=range_name)
  if bool(external_state.get("green_present")) or color == "green":
    return TrafficIntent(color="green", confidence=confidence, source=source, range=range_name)
  return None


def build_traffic_intent(
  model_msg=None,
  *,
  enabled: bool,
  external_state: dict | None = None,
  now_ms: int | None = None,
) -> TrafficIntent:
  if not enabled:
    return TrafficIntent(color="unknown", confidence=0.0, source="disabled")
  now_ms = int(time.time() * 1000) if now_ms is None else now_ms
  external_intent = _intent_from_external_state(external_state, now_ms)
  if external_intent is not None:
    return external_intent
  if model_msg is None:
    return TrafficIntent(color="unknown", confidence=0.0, source="missing_model", stale=True)

  if bool(model_msg.action.shouldStop):
    return TrafficIntent(
      color="mixed",
      confidence=STOP_ACTION_CONFIDENCE,
      source="model_action_should_stop",
      range="mid",
    )

  return TrafficIntent(color="unknown", confidence=0.0, source="model_action")


def fill_traffic_intent_message(traffic_msg, intent: TrafficIntent) -> None:
  color_map = {
    "unknown": custom.FridayTrafficIntent.Color.unknown,
    "red": custom.FridayTrafficIntent.Color.red,
    "mixed": custom.FridayTrafficIntent.Color.mixed,
    "green": custom.FridayTrafficIntent.Color.green,
  }
  range_name = intent.range
  if range_name == "unknown" and intent.color in ("red", "mixed") and intent.confidence >= 0.45:
    range_name = "mid"
  range_map = {
    "unknown": custom.FridayTrafficIntent.Range.unknown,
    "far": custom.FridayTrafficIntent.Range.far,
    "mid": custom.FridayTrafficIntent.Range.mid,
    "near": custom.FridayTrafficIntent.Range.near,
  }

  traffic_msg.color = color_map.get(intent.color, custom.FridayTrafficIntent.Color.unknown)
  traffic_msg.confidence = float(intent.confidence)
  traffic_msg.source = intent.source
  traffic_msg.ageS = float(intent.age_s)
  traffic_msg.stale = bool(intent.stale)
  traffic_msg.range = range_map[range_name]
  traffic_msg.reason = "model action requested stop" if intent.source == "model_action_should_stop" else intent.source


def main() -> NoReturn:
  params = Params()
  pm = messaging.PubMaster(["fridayTrafficIntent"])
  sm = messaging.SubMaster(["modelV2"])
  rk = Ratekeeper(20, print_delay_threshold=None)

  while True:
    sm.update(0)
    intent = build_traffic_intent(
      sm["modelV2"],
      enabled=params.get_bool("FridayLongitudinalBrakeAssist"),
      external_state=load_external_traffic_light_state(),
    )
    msg = messaging.new_message("fridayTrafficIntent", valid=True)
    fill_traffic_intent_message(msg.fridayTrafficIntent, intent)
    pm.send("fridayTrafficIntent", msg)
    rk.keep_time()


if __name__ == "__main__":
  main()
