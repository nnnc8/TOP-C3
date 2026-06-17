#pragma once

#include <string>
#include <vector>

#include "common/params.h"

struct FridayJourneyTotals {
  double drive_time_s = 0.0;
  double moving_time_s = 0.0;
  double standstill_time_s = 0.0;
  double moving_distance_m = 0.0;
  double assisted_time_s = 0.0;
  double assisted_moving_time_s = 0.0;
  double assisted_distance_m = 0.0;
  double top_speed_mps = 0.0;
  double assisted_top_speed_mps = 0.0;
  int trip_count = 0;
  int route_count_snapshot = 0;
  int alert_count = 0;
  int intervention_count = 0;
};

struct FridayJourneyTrip {
  double drive_time_s = 0.0;
  double moving_time_s = 0.0;
  double standstill_time_s = 0.0;
  double moving_distance_m = 0.0;
  double assisted_time_s = 0.0;
  double assisted_moving_time_s = 0.0;
  double assisted_distance_m = 0.0;
  double top_speed_mps = 0.0;
  double assisted_top_speed_mps = 0.0;
  int alert_count = 0;
  int intervention_count = 0;
};

struct FridayJourneyDailyBucket {
  int day_key = 0;
  int trip_count = 0;
  double moving_time_s = 0.0;
  double assisted_moving_time_s = 0.0;
  double moving_distance_m = 0.0;
  double assisted_time_s = 0.0;
  double assisted_distance_m = 0.0;
  int alert_count = 0;
  int intervention_count = 0;
};

struct FridayJourneyBoardState {
  FridayJourneyTotals totals;
  FridayJourneyTrip current_trip;
  FridayJourneyTrip last_trip;
  std::vector<FridayJourneyDailyBucket> daily_buckets;
};

struct FridayJourneySample {
  double dt_s = 0.0;
  bool enabled = false;
  bool has_alert = false;
  bool standstill = false;
  double v_ego = 0.0;
  int route_count = 0;
  int day_key = 0;
  bool brake_pressed = false;
  bool gas_pressed = false;
  bool steering_pressed = false;
};

FridayJourneyBoardState parse_friday_journey_board(const std::string &json);
std::string serialize_friday_journey_board(const FridayJourneyBoardState &state);
FridayJourneyBoardState reset_friday_journey_board(int route_count_snapshot = 0);
void update_friday_journey_board(FridayJourneyBoardState &state, const FridayJourneySample &sample, bool &overriding_prev, bool &has_alert_prev);
void close_friday_journey_trip(FridayJourneyBoardState &state, int day_key);
double get_assist_ratio(double assisted_moving_time, double moving_time);
void normalize_journey_board(FridayJourneyBoardState &state);
FridayJourneyBoardState load_journey_board(Params &params);
