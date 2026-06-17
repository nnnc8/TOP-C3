from __future__ import annotations

from dataclasses import dataclass
from cereal import custom


def _param_float(params, key: str, default: float) -> float:
  value = params.get(key, return_default=True)
  if value is None:
    return default
  return float(value)


@dataclass(frozen=True)
class BrakeAssistConfig:
  enabled: bool = False
  prepare_m: float = 80.0
  brake_m: float = 30.0
  hard_brake_m: float = 15.0
  stop_m: float = 8.0
  high_confidence_min: float = 0.70
  mid_confidence_min: float = 0.45
  max_decel_high: float = 2.0
  max_decel_mid: float = 1.3
  max_decel_low_far: float = 0.45
  max_decel_low_near: float = 1.1
  prepare_decel: float = 0.15
  min_stop_decel: float = 0.4

  @classmethod
  def from_params(cls, params) -> BrakeAssistConfig:
    default = cls()
    return cls(
      enabled=bool(params.get_bool("FridayLongitudinalBrakeAssist")),
      prepare_m=_param_float(params, "FridayBrakeAssistPrepareM", default.prepare_m),
      brake_m=_param_float(params, "FridayBrakeAssistBrakeM", default.brake_m),
      hard_brake_m=_param_float(params, "FridayBrakeAssistHardBrakeM", default.hard_brake_m),
      stop_m=_param_float(params, "FridayBrakeAssistStopM", default.stop_m),
      high_confidence_min=_param_float(params, "FridayBrakeAssistHighConfidenceMin", default.high_confidence_min),
      mid_confidence_min=_param_float(params, "FridayBrakeAssistMidConfidenceMin", default.mid_confidence_min),
      max_decel_high=_param_float(params, "FridayBrakeAssistMaxDecelHigh", default.max_decel_high),
      max_decel_mid=_param_float(params, "FridayBrakeAssistMaxDecelMid", default.max_decel_mid),
      max_decel_low_far=_param_float(params, "FridayBrakeAssistMaxDecelLowFar", default.max_decel_low_far),
      max_decel_low_near=_param_float(params, "FridayBrakeAssistMaxDecelLowNear", default.max_decel_low_near),
      prepare_decel=default.prepare_decel,
      min_stop_decel=default.min_stop_decel,
    )


@dataclass(frozen=True)
class TrafficIntent:
  color: str = "unknown"
  confidence: float = 0.0
  source: str = ""
  age_s: float = 0.0
  stale: bool = False
  range: str = "unknown"


@dataclass(frozen=True)
class IntersectionDistance:
  distance_m: float | None = None
  confidence: float = 0.0
  source: str = ""
  age_s: float = 0.0
  stale: bool = False
  fail_reason: str = ""


@dataclass(frozen=True)
class BrakeAssistOutput:
  active: bool = False
  state: str = "inactive"
  v_target: float | None = None
  a_target: float = 0.0
  should_stop: bool = False
  allow_throttle: bool = True
  confidence_band: str = "none"
  target_distance_m: float | None = None
  source: str = ""
  release_reason: str = ""


class AccIntegratedBrakeAssistController:
  def __init__(self, config: BrakeAssistConfig | None = None):
    self.config = config if config is not None else BrakeAssistConfig()
    self.latched = False

  def update(
    self,
    *,
    v_ego: float,
    a_ego: float,
    long_enabled: bool,
    long_override: bool,
    gas_pressed: bool,
    brake_pressed: bool,
    traffic_intent: TrafficIntent,
    intersection: IntersectionDistance,
  ) -> BrakeAssistOutput:
    if not self.config.enabled:
      return self._release("disabled")
    if not long_enabled:
      return self._release("longitudinal_inactive")
    if long_override or gas_pressed or brake_pressed:
      return self._release("driver_override")

    color = traffic_intent.color.lower()
    if color == "green":
      return self._release("green_light")
    if traffic_intent.stale or intersection.stale:
      return self._release("stale_input")

    stop_intent = color in ("red", "mixed") and traffic_intent.confidence > 0.0
    if not stop_intent:
      return self._release("no_stop_intent")

    self.latched = True
    distance_m = intersection.distance_m
    if distance_m is None:
      distance_m = self._range_distance(traffic_intent.range)
    band = self._confidence_band(traffic_intent, intersection)
    source = self._source(traffic_intent, intersection)

    if distance_m is None:
      return BrakeAssistOutput(
        active=True,
        state="prepare",
        v_target=v_ego,
        a_target=-self.config.prepare_decel,
        allow_throttle=False,
        confidence_band=band,
        source=source,
      )

    if distance_m <= self.config.stop_m:
      decel = max(self.config.min_stop_decel, min(self._required_decel(v_ego, max(distance_m, 1.0)), self._max_decel(band, distance_m)))
      return BrakeAssistOutput(
        active=True,
        state="final_stop",
        v_target=0.0,
        a_target=-decel,
        should_stop=True,
        allow_throttle=False,
        confidence_band=band,
        target_distance_m=distance_m,
        source=source,
      )

    if distance_m <= self.config.hard_brake_m:
      decel = min(self._required_decel(v_ego, distance_m), self._max_decel(band, distance_m))
      return BrakeAssistOutput(
        active=True,
        state="hard_brake",
        v_target=0.0,
        a_target=-decel,
        allow_throttle=False,
        confidence_band=band,
        target_distance_m=distance_m,
        source=source,
      )

    if distance_m <= self.config.brake_m:
      decel = min(self._required_decel(v_ego, distance_m), self._max_decel(band, distance_m))
      return BrakeAssistOutput(
        active=True,
        state="brake",
        v_target=max(0.0, v_ego - 2.0),
        a_target=-decel,
        allow_throttle=False,
        confidence_band=band,
        target_distance_m=distance_m,
        source=source,
      )

    return BrakeAssistOutput(
      active=True,
      state="prepare",
      v_target=v_ego,
      a_target=min(a_ego, -self.config.prepare_decel),
      allow_throttle=False,
      confidence_band=band,
      target_distance_m=distance_m,
      source=source,
    )

  def _release(self, reason: str) -> BrakeAssistOutput:
    self.latched = False
    return BrakeAssistOutput(release_reason=reason)

  def _confidence_band(self, traffic_intent: TrafficIntent, intersection: IntersectionDistance) -> str:
    confidence = min(max(traffic_intent.confidence, 0.0), max(intersection.confidence, 0.0))
    if confidence >= self.config.high_confidence_min:
      return "high"
    if confidence >= self.config.mid_confidence_min:
      return "mid"
    return "low"

  def _max_decel(self, band: str, distance_m: float) -> float:
    if band == "high":
      return self.config.max_decel_high
    if band == "mid":
      return self.config.max_decel_mid
    if distance_m <= self.config.hard_brake_m:
      return self.config.max_decel_low_near
    return self.config.max_decel_low_far

  @staticmethod
  def _required_decel(v_ego: float, distance_m: float) -> float:
    if distance_m <= 0.0:
      return 0.0
    return max(0.0, (max(v_ego, 0.0) ** 2) / (2.0 * distance_m))

  def _range_distance(self, range_name: str) -> float | None:
    range_key = str(range_name or "").lower()
    if range_key == "far":
      return self.config.prepare_m + 5.0
    if range_key == "mid":
      return self.config.brake_m
    if range_key == "near":
      return self.config.hard_brake_m
    return None

  @staticmethod
  def _source(traffic_intent: TrafficIntent, intersection: IntersectionDistance) -> str:
    if traffic_intent.source and intersection.source:
      return f"{traffic_intent.source}+{intersection.source}"
    return traffic_intent.source or intersection.source


def fill_brake_assist_message(brake_assist_msg, output: BrakeAssistOutput) -> None:
  state_map = {
    "inactive": custom.LongitudinalPlanTOP.BrakeAssist.State.inactive,
    "prepare": custom.LongitudinalPlanTOP.BrakeAssist.State.prepare,
    "brake": custom.LongitudinalPlanTOP.BrakeAssist.State.brake,
    "hard_brake": custom.LongitudinalPlanTOP.BrakeAssist.State.hardBrake,
    "final_stop": custom.LongitudinalPlanTOP.BrakeAssist.State.finalStop,
  }
  confidence_map = {
    "none": custom.LongitudinalPlanTOP.BrakeAssist.ConfidenceBand.none,
    "low": custom.LongitudinalPlanTOP.BrakeAssist.ConfidenceBand.low,
    "mid": custom.LongitudinalPlanTOP.BrakeAssist.ConfidenceBand.mid,
    "high": custom.LongitudinalPlanTOP.BrakeAssist.ConfidenceBand.high,
  }

  brake_assist_msg.active = bool(output.active)
  brake_assist_msg.state = state_map.get(output.state, custom.LongitudinalPlanTOP.BrakeAssist.State.inactive)
  brake_assist_msg.confidenceBand = confidence_map.get(output.confidence_band, custom.LongitudinalPlanTOP.BrakeAssist.ConfidenceBand.none)
  brake_assist_msg.vTarget = float(output.v_target if output.v_target is not None else 0.0)
  brake_assist_msg.aTarget = float(output.a_target)
  brake_assist_msg.shouldStop = bool(output.should_stop)
  brake_assist_msg.allowThrottle = bool(output.allow_throttle)
  brake_assist_msg.targetDistanceM = float(output.target_distance_m if output.target_distance_m is not None else 0.0)
  brake_assist_msg.source = output.source
  brake_assist_msg.releaseReason = output.release_reason


def traffic_intent_from_msg(msg) -> TrafficIntent:
  color_map = {
    "red": "red",
    "mixed": "mixed",
    "green": "green",
    "unknown": "unknown",
  }
  range_map = {
    "far": "far",
    "mid": "mid",
    "near": "near",
    "unknown": "unknown",
  }
  return TrafficIntent(
    color=color_map.get(str(msg.color), "unknown"),
    confidence=round(float(msg.confidence), 6),
    source=str(msg.source),
    age_s=round(float(msg.ageS), 6),
    stale=bool(msg.stale),
    range=range_map.get(str(msg.range), "unknown"),
  )


def intersection_from_msg(msg) -> IntersectionDistance:
  active = bool(msg.active)
  return IntersectionDistance(
    distance_m=round(float(msg.distanceM), 6) if active else None,
    confidence=round(float(msg.confidence), 6) if active else 0.0,
    source=str(msg.source),
    age_s=round(float(msg.ageS), 6),
    stale=bool(msg.stale),
    fail_reason=str(msg.failReason),
  )
