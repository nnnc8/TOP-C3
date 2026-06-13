#include "common/aegis_journey_board.h"

#include <algorithm>
#include <cmath>

#include "third_party/json11/json11.hpp"

namespace {

int int_field(const json11::Json &obj, const std::string &key, int fallback = 0) {
  const json11::Json value = obj[key];
  return value.is_number() ? value.int_value() : fallback;
}

double number_field(const json11::Json &obj, const std::string &key, double fallback = 0.0) {
  const json11::Json value = obj[key];
  return value.is_number() ? value.number_value() : fallback;
}

AegisJourneyTotals parse_totals(const json11::Json &obj) {
  AegisJourneyTotals totals;
  if (!obj.is_object()) return totals;
  totals.drive_time_s = number_field(obj, "drive_time_s");
  totals.moving_time_s = number_field(obj, "moving_time_s");
  totals.standstill_time_s = number_field(obj, "standstill_time_s");
  totals.moving_distance_m = number_field(obj, "moving_distance_m");
  totals.assisted_time_s = number_field(obj, "assisted_time_s");
  totals.assisted_moving_time_s = number_field(obj, "assisted_moving_time_s");
  totals.assisted_distance_m = number_field(obj, "assisted_distance_m");
  totals.top_speed_mps = number_field(obj, "top_speed_mps");
  totals.assisted_top_speed_mps = number_field(obj, "assisted_top_speed_mps");
  totals.trip_count = int_field(obj, "trip_count");
  totals.route_count_snapshot = int_field(obj, "route_count_snapshot");
  totals.alert_count = int_field(obj, "alert_count");
  totals.intervention_count = int_field(obj, "intervention_count");
  return totals;
}

AegisJourneyTrip parse_trip(const json11::Json &obj) {
  AegisJourneyTrip trip;
  if (!obj.is_object()) return trip;
  trip.drive_time_s = number_field(obj, "drive_time_s");
  trip.moving_time_s = number_field(obj, "moving_time_s");
  trip.standstill_time_s = number_field(obj, "standstill_time_s");
  trip.moving_distance_m = number_field(obj, "moving_distance_m");
  trip.assisted_time_s = number_field(obj, "assisted_time_s");
  trip.assisted_moving_time_s = number_field(obj, "assisted_moving_time_s");
  trip.assisted_distance_m = number_field(obj, "assisted_distance_m");
  trip.top_speed_mps = number_field(obj, "top_speed_mps");
  trip.assisted_top_speed_mps = number_field(obj, "assisted_top_speed_mps");
  trip.alert_count = int_field(obj, "alert_count");
  trip.intervention_count = int_field(obj, "intervention_count");
  return trip;
}

AegisJourneyDailyBucket parse_bucket(const json11::Json &obj) {
  AegisJourneyDailyBucket bucket;
  if (!obj.is_object()) return bucket;
  bucket.day_key = int_field(obj, "day_key");
  bucket.trip_count = int_field(obj, "trip_count");
  bucket.moving_distance_m = number_field(obj, "moving_distance_m");
  bucket.assisted_time_s = number_field(obj, "assisted_time_s");
  bucket.assisted_distance_m = number_field(obj, "assisted_distance_m");
  bucket.alert_count = int_field(obj, "alert_count");
  bucket.intervention_count = int_field(obj, "intervention_count");
  return bucket;
}

json11::Json serialize_totals(const AegisJourneyTotals &totals) {
  return json11::Json::object{
    {"drive_time_s", totals.drive_time_s},
    {"moving_time_s", totals.moving_time_s},
    {"standstill_time_s", totals.standstill_time_s},
    {"moving_distance_m", totals.moving_distance_m},
    {"assisted_time_s", totals.assisted_time_s},
    {"assisted_moving_time_s", totals.assisted_moving_time_s},
    {"assisted_distance_m", totals.assisted_distance_m},
    {"top_speed_mps", totals.top_speed_mps},
    {"assisted_top_speed_mps", totals.assisted_top_speed_mps},
    {"trip_count", totals.trip_count},
    {"route_count_snapshot", totals.route_count_snapshot},
    {"alert_count", totals.alert_count},
    {"intervention_count", totals.intervention_count}
  };
}

json11::Json serialize_trip(const AegisJourneyTrip &trip) {
  return json11::Json::object{
    {"drive_time_s", trip.drive_time_s},
    {"moving_time_s", trip.moving_time_s},
    {"standstill_time_s", trip.standstill_time_s},
    {"moving_distance_m", trip.moving_distance_m},
    {"assisted_time_s", trip.assisted_time_s},
    {"assisted_moving_time_s", trip.assisted_moving_time_s},
    {"assisted_distance_m", trip.assisted_distance_m},
    {"top_speed_mps", trip.top_speed_mps},
    {"assisted_top_speed_mps", trip.assisted_top_speed_mps},
    {"alert_count", trip.alert_count},
    {"intervention_count", trip.intervention_count}
  };
}

json11::Json serialize_bucket(const AegisJourneyDailyBucket &bucket) {
  return json11::Json::object{
    {"day_key", bucket.day_key},
    {"trip_count", bucket.trip_count},
    {"moving_distance_m", bucket.moving_distance_m},
    {"assisted_time_s", bucket.assisted_time_s},
    {"assisted_distance_m", bucket.assisted_distance_m},
    {"alert_count", bucket.alert_count},
    {"intervention_count", bucket.intervention_count}
  };
}

}  // namespace

void normalize_journey_board(AegisJourneyBoardState &state) {
  auto clamp_totals = [](AegisJourneyTotals &t) {
    t.drive_time_s = std::max(0.0, t.drive_time_s);
    t.moving_time_s = std::max(0.0, t.moving_time_s);
    t.standstill_time_s = std::max(0.0, t.standstill_time_s);
    t.moving_distance_m = std::max(0.0, t.moving_distance_m);
    t.assisted_time_s = std::max(0.0, t.assisted_time_s);
    t.assisted_moving_time_s = std::max(0.0, t.assisted_moving_time_s);
    t.assisted_distance_m = std::max(0.0, t.assisted_distance_m);
    t.top_speed_mps = std::max(0.0, t.top_speed_mps);
    t.assisted_top_speed_mps = std::max(0.0, t.assisted_top_speed_mps);
    t.trip_count = std::max(0, t.trip_count);
    t.route_count_snapshot = std::max(0, t.route_count_snapshot);
    t.alert_count = std::max(0, t.alert_count);
    t.intervention_count = std::max(0, t.intervention_count);
  };
  auto clamp_trip = [](AegisJourneyTrip &t) {
    t.drive_time_s = std::max(0.0, t.drive_time_s);
    t.moving_time_s = std::max(0.0, t.moving_time_s);
    t.standstill_time_s = std::max(0.0, t.standstill_time_s);
    t.moving_distance_m = std::max(0.0, t.moving_distance_m);
    t.assisted_time_s = std::max(0.0, t.assisted_time_s);
    t.assisted_moving_time_s = std::max(0.0, t.assisted_moving_time_s);
    t.assisted_distance_m = std::max(0.0, t.assisted_distance_m);
    t.top_speed_mps = std::max(0.0, t.top_speed_mps);
    t.assisted_top_speed_mps = std::max(0.0, t.assisted_top_speed_mps);
    t.alert_count = std::max(0, t.alert_count);
    t.intervention_count = std::max(0, t.intervention_count);
  };
  clamp_totals(state.totals);
  clamp_trip(state.current_trip);
  clamp_trip(state.last_trip);
  for (auto &b : state.daily_buckets) {
    b.trip_count = std::max(0, b.trip_count);
    b.moving_distance_m = std::max(0.0, b.moving_distance_m);
    b.assisted_time_s = std::max(0.0, b.assisted_time_s);
    b.assisted_distance_m = std::max(0.0, b.assisted_distance_m);
    b.alert_count = std::max(0, b.alert_count);
    b.intervention_count = std::max(0, b.intervention_count);
  }
}

AegisJourneyBoardState parse_aegis_journey_board(const std::string &json) {
  AegisJourneyBoardState state;
  std::string err;
  const json11::Json obj = json11::Json::parse(json, err);
  if (!err.empty() || !obj.is_object()) {
    return state;
  }

  state.totals = parse_totals(obj["totals"]);
  state.current_trip = parse_trip(obj["current_trip"]);
  state.last_trip = parse_trip(obj["last_trip"]);

  const json11::Json buckets = obj["daily_buckets"];
  if (buckets.is_array()) {
    for (const json11::Json &b_obj : buckets.array_items()) {
      state.daily_buckets.push_back(parse_bucket(b_obj));
    }
  }

  normalize_journey_board(state);
  return state;
}

std::string serialize_aegis_journey_board(const AegisJourneyBoardState &state) {
  AegisJourneyBoardState normalized = state;
  normalize_journey_board(normalized);

  json11::Json::array buckets_arr;
  for (const auto &b : normalized.daily_buckets) {
    buckets_arr.push_back(serialize_bucket(b));
  }

  return json11::Json(json11::Json::object{
    {"totals", serialize_totals(normalized.totals)},
    {"current_trip", serialize_trip(normalized.current_trip)},
    {"last_trip", serialize_trip(normalized.last_trip)},
    {"daily_buckets", buckets_arr}
  }).dump();
}

AegisJourneyBoardState reset_aegis_journey_board(int route_count_snapshot) {
  AegisJourneyBoardState state;
  state.totals.route_count_snapshot = std::max(0, route_count_snapshot);
  return state;
}

void update_aegis_journey_board(AegisJourneyBoardState &state, const AegisJourneySample &sample, bool &overriding_prev, bool &has_alert_prev) {
  const double dt_s = sample.dt_s > 0.0 ? sample.dt_s : 0.0;
  const double speed_mps = std::max(0.0, sample.v_ego);

  if (dt_s > 0.0) {
    state.totals.drive_time_s += dt_s;
    state.current_trip.drive_time_s += dt_s;

    state.totals.top_speed_mps = std::max(state.totals.top_speed_mps, speed_mps);
    state.current_trip.top_speed_mps = std::max(state.current_trip.top_speed_mps, speed_mps);

    if (sample.standstill) {
      state.totals.standstill_time_s += dt_s;
      state.current_trip.standstill_time_s += dt_s;
    } else {
      state.totals.moving_time_s += dt_s;
      state.current_trip.moving_time_s += dt_s;
      
      const double dist_m = speed_mps * dt_s;
      state.totals.moving_distance_m += dist_m;
      state.current_trip.moving_distance_m += dist_m;
    }

    if (sample.enabled) {
      state.totals.assisted_time_s += dt_s;
      state.current_trip.assisted_time_s += dt_s;

      state.totals.assisted_top_speed_mps = std::max(state.totals.assisted_top_speed_mps, speed_mps);
      state.current_trip.assisted_top_speed_mps = std::max(state.current_trip.assisted_top_speed_mps, speed_mps);

      if (!sample.standstill) {
        state.totals.assisted_moving_time_s += dt_s;
        state.current_trip.assisted_moving_time_s += dt_s;
        
        const double dist_m = speed_mps * dt_s;
        state.totals.assisted_distance_m += dist_m;
        state.current_trip.assisted_distance_m += dist_m;
      }
    }
  }

  // Count alerts on rising edge of has_alert
  if (sample.has_alert && !has_alert_prev) {
    state.totals.alert_count++;
    state.current_trip.alert_count++;
  }
  has_alert_prev = sample.has_alert;

  // Count interventions on rising edge of overriding while enabled
  bool overriding = sample.enabled && (sample.brake_pressed || sample.gas_pressed || sample.steering_pressed);
  if (overriding && !overriding_prev) {
    state.totals.intervention_count++;
    state.current_trip.intervention_count++;
  }
  overriding_prev = overriding;

  if (sample.route_count > state.totals.route_count_snapshot) {
    state.totals.route_count_snapshot = sample.route_count;
  }

  normalize_journey_board(state);
}

void close_aegis_journey_trip(AegisJourneyBoardState &state, int day_key) {
  state.last_trip = state.current_trip;

  bool found = false;
  for (auto &bucket : state.daily_buckets) {
    if (bucket.day_key == day_key) {
      bucket.trip_count += 1;
      bucket.moving_distance_m += state.current_trip.moving_distance_m;
      bucket.assisted_time_s += state.current_trip.assisted_time_s;
      bucket.assisted_distance_m += state.current_trip.assisted_distance_m;
      bucket.alert_count += state.current_trip.alert_count;
      bucket.intervention_count += state.current_trip.intervention_count;
      found = true;
      break;
    }
  }

  if (!found && day_key > 0) {
    AegisJourneyDailyBucket bucket;
    bucket.day_key = day_key;
    bucket.trip_count = 1;
    bucket.moving_distance_m = state.current_trip.moving_distance_m;
    bucket.assisted_time_s = state.current_trip.assisted_time_s;
    bucket.assisted_distance_m = state.current_trip.assisted_distance_m;
    bucket.alert_count = state.current_trip.alert_count;
    bucket.intervention_count = state.current_trip.intervention_count;
    state.daily_buckets.push_back(bucket);
  }

  std::sort(state.daily_buckets.begin(), state.daily_buckets.end(), [](const AegisJourneyDailyBucket &a, const AegisJourneyDailyBucket &b) {
    return a.day_key < b.day_key;
  });

  if (state.daily_buckets.size() > 7) {
    state.daily_buckets.erase(state.daily_buckets.begin(), state.daily_buckets.end() - 7);
  }

  state.totals.trip_count += 1;
  state.current_trip = AegisJourneyTrip();

  normalize_journey_board(state);
}

double get_assist_ratio(double assisted_moving_time, double moving_time) {
  if (moving_time <= 0.0) {
    return 0.0;
  }
  return std::clamp(assisted_moving_time / moving_time, 0.0, 1.0);
}

AegisJourneyBoardState load_journey_board(Params &params) {
  std::string j_board_val = params.get("AegisJourneyBoard");
  if (!j_board_val.empty()) {
    return parse_aegis_journey_board(j_board_val);
  }

  AegisJourneyBoardState state;
  std::string achievements_val = params.get("AegisAchievements");
  if (!achievements_val.empty()) {
    std::string err;
    auto obj = json11::Json::parse(achievements_val, err);
    if (err.empty() && obj.is_object()) {
      state.totals.drive_time_s = number_field(obj, "drive_time_s");
      state.totals.moving_time_s = number_field(obj, "moving_time_s");
      state.totals.moving_distance_m = number_field(obj, "moving_distance_m");
      state.totals.top_speed_mps = number_field(obj, "top_speed_mps");
      state.totals.assisted_time_s = number_field(obj, "assisted_time_s");
      state.totals.assisted_distance_m = number_field(obj, "assisted_distance_m");
      state.totals.assisted_top_speed_mps = number_field(obj, "assisted_top_speed_mps");
      state.totals.route_count_snapshot = int_field(obj, "route_count_snapshot");
    }
  } else {
    state.totals.route_count_snapshot = params.getInt("RouteCount");
  }

  params.put("AegisJourneyBoard", serialize_aegis_journey_board(state));
  return state;
}
