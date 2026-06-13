#include "common/aegis_journey_board.h"

#include <cassert>
#include <cmath>
#include <iostream>

static void test_parse_defaults() {
  AegisJourneyBoardState state = parse_aegis_journey_board("");

  assert(state.totals.drive_time_s == 0.0);
  assert(state.totals.moving_time_s == 0.0);
  assert(state.totals.standstill_time_s == 0.0);
  assert(state.totals.moving_distance_m == 0.0);
  assert(state.totals.assisted_time_s == 0.0);
  assert(state.totals.assisted_moving_time_s == 0.0);
  assert(state.totals.assisted_distance_m == 0.0);
  assert(state.totals.top_speed_mps == 0.0);
  assert(state.totals.assisted_top_speed_mps == 0.0);
  assert(state.totals.trip_count == 0);
  assert(state.totals.route_count_snapshot == 0);
  assert(state.totals.alert_count == 0);
  assert(state.totals.intervention_count == 0);

  assert(state.daily_buckets.empty());
}

static void test_assist_ratio_safety() {
  assert(get_assist_ratio(10.0, 0.0) == 0.0);
  assert(get_assist_ratio(0.0, 0.0) == 0.0);
  assert(get_assist_ratio(50.0, 100.0) == 0.5);
}

static void test_migration() {
  Params params("/tmp/test_params_migration");

  // Write legacy AegisAchievements param
  std::string legacy_json = R"({
    "xp": 500,
    "level": 6,
    "unlocked_badges": ["first_assist", "assisted_10km"],
    "drive_time_s": 3600.0,
    "moving_time_s": 3000.0,
    "moving_distance_m": 45000.0,
    "top_speed_mps": 25.0,
    "assisted_time_s": 2400.0,
    "assisted_distance_m": 35000.0,
    "assisted_top_speed_mps": 24.0,
    "route_count_snapshot": 15,
    "daily_xp_day": 20260611,
    "daily_xp": 50
  })";
  params.put("AegisAchievements", legacy_json);
  params.remove("AegisJourneyBoard");

  // Call load_journey_board
  AegisJourneyBoardState state = load_journey_board(params);

  // Check transferred totals
  assert(state.totals.drive_time_s == 3600.0);
  assert(state.totals.moving_time_s == 3000.0);
  assert(state.totals.moving_distance_m == 45000.0);
  assert(state.totals.top_speed_mps == 25.0);
  assert(state.totals.assisted_time_s == 2400.0);
  assert(state.totals.assisted_distance_m == 35000.0);
  assert(state.totals.assisted_top_speed_mps == 24.0);
  assert(state.totals.route_count_snapshot == 15);

  // Check non-transferred (XP/Badges/Level should be gone or not in state)
  // Ensure the brand new parameters exist and are set
  std::string saved_val = params.get("AegisJourneyBoard");
  assert(!saved_val.empty());
}

static void test_rising_edges() {
  AegisJourneyBoardState state;
  bool overriding_prev = false;
  bool has_alert_prev = false;

  AegisJourneySample sample = {};
  sample.dt_s = 1.0;
  sample.enabled = true;
  sample.standstill = false;
  sample.v_ego = 10.0;

  // No alert, no override
  update_aegis_journey_board(state, sample, overriding_prev, has_alert_prev);
  assert(state.totals.alert_count == 0);
  assert(state.totals.intervention_count == 0);

  // Alert rising edge
  sample.has_alert = true;
  update_aegis_journey_board(state, sample, overriding_prev, has_alert_prev);
  assert(state.totals.alert_count == 1);
  assert(state.current_trip.alert_count == 1);

  // Alert level/stays true (no edge)
  update_aegis_journey_board(state, sample, overriding_prev, has_alert_prev);
  assert(state.totals.alert_count == 1);

  // Alert falling edge
  sample.has_alert = false;
  update_aegis_journey_board(state, sample, overriding_prev, has_alert_prev);
  assert(state.totals.alert_count == 1);

  // Alert rising edge again
  sample.has_alert = true;
  update_aegis_journey_board(state, sample, overriding_prev, has_alert_prev);
  assert(state.totals.alert_count == 2);
  assert(state.current_trip.alert_count == 2);

  // Override rising edge (gas pressed)
  sample.gas_pressed = true;
  update_aegis_journey_board(state, sample, overriding_prev, has_alert_prev);
  assert(state.totals.intervention_count == 1);
  assert(state.current_trip.intervention_count == 1);

  // Override stays true
  update_aegis_journey_board(state, sample, overriding_prev, has_alert_prev);
  assert(state.totals.intervention_count == 1);

  // Override falling edge
  sample.gas_pressed = false;
  update_aegis_journey_board(state, sample, overriding_prev, has_alert_prev);
  assert(state.totals.intervention_count == 1);

  // Override rising edge (brake pressed)
  sample.brake_pressed = true;
  update_aegis_journey_board(state, sample, overriding_prev, has_alert_prev);
  assert(state.totals.intervention_count == 2);
  assert(state.current_trip.intervention_count == 2);
}

static void test_daily_buckets_trim() {
  AegisJourneyBoardState state;

  for (int d = 1; d < 10; ++d) {
    int day_key = 20260600 + d;
    state.current_trip.moving_distance_m = 1000.0;
    state.current_trip.assisted_time_s = 60.0;
    state.current_trip.assisted_distance_m = 800.0;
    state.current_trip.alert_count = 1;
    state.current_trip.intervention_count = 2;

    close_aegis_journey_trip(state, day_key);
  }

  // Should contain exactly 7 daily buckets (the last 7 days: 3 to 9)
  assert(state.daily_buckets.size() == 7);
  assert(state.daily_buckets[0].day_key == 20260603);
  assert(state.daily_buckets[6].day_key == 20260609);

  // Total trip count should be 9
  assert(state.totals.trip_count == 9);
}

int main() {
  test_parse_defaults();
  test_assist_ratio_safety();
  test_migration();
  test_rising_edges();
  test_daily_buckets_trim();
  std::cout << "All tests passed successfully!" << std::endl;
  return 0;
}
