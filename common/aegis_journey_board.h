#pragma once

#include <string>
#include <vector>

#include "common/params.h"

struct AegisJourneyTotals {
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

struct AegisJourneyTrip {
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

struct AegisJourneyDailyBucket {
  int day_key = 0;
  int trip_count = 0;
  double moving_distance_m = 0.0;
  double assisted_time_s = 0.0;
  double assisted_distance_m = 0.0;
  int alert_count = 0;
  int intervention_count = 0;
};

struct AegisJourneyBoardState {
  AegisJourneyTotals totals;
  AegisJourneyTrip current_trip;
  AegisJourneyTrip last_trip;
  std::vector<AegisJourneyDailyBucket> daily_buckets;
};

struct AegisJourneySample {
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

AegisJourneyBoardState parse_aegis_journey_board(const std::string &json);
std::string serialize_aegis_journey_board(const AegisJourneyBoardState &state);
AegisJourneyBoardState reset_aegis_journey_board(int route_count_snapshot = 0);
void update_aegis_journey_board(AegisJourneyBoardState &state, const AegisJourneySample &sample, bool &overriding_prev, bool &has_alert_prev);
void close_aegis_journey_trip(AegisJourneyBoardState &state, int day_key);
double get_assist_ratio(double assisted_moving_time, double moving_time);
void normalize_journey_board(AegisJourneyBoardState &state);
AegisJourneyBoardState load_journey_board(Params &params);
