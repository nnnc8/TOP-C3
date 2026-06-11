#include "common/aegis_achievements.h"

#include <cassert>
#include <cmath>

static void test_parse_defaults() {
  AegisAchievementState state = parse_aegis_achievements("");

  assert(state.xp == 0);
  assert(state.level == 1);
  assert(state.assisted_time_s == 0.0);
  assert(state.assisted_distance_m == 0.0);
  assert(state.route_count_snapshot == 0);
  assert(state.unlocked_badges.empty());
}

static void test_accrue_only_clean_enabled_assist() {
  AegisAchievementState state = parse_aegis_achievements("");
  state.route_count_snapshot = 7;

  AegisAchievementSample sample = {};
  sample.dt_s = 60.0;
  sample.enabled = true;
  sample.has_alert = false;
  sample.v_ego = 10.0;
  sample.route_count = 7;
  sample.day_key = 20260611;

  AegisAchievementUpdate update = update_aegis_achievements(state, sample);

  assert(state.assisted_time_s == 60.0);
  assert(state.assisted_distance_m == 600.0);
  assert(state.xp > 0);
  assert(state.level == 1);
  assert(update.xp_awarded == state.xp);
  assert(update.new_badges.size() == 1);
  assert(update.new_badges[0].id == "first_assist");

  const int xp_after_enabled = state.xp;
  sample.enabled = false;
  sample.dt_s = 60.0;
  update_aegis_achievements(state, sample);
  assert(state.xp == xp_after_enabled);
  assert(state.assisted_time_s == 60.0);

  sample.enabled = true;
  sample.has_alert = true;
  update_aegis_achievements(state, sample);
  assert(state.xp == xp_after_enabled);
  assert(state.assisted_time_s == 60.0);
}

static void test_daily_xp_cap() {
  AegisAchievementState state = parse_aegis_achievements("");

  AegisAchievementSample sample = {};
  sample.dt_s = 60.0;
  sample.enabled = true;
  sample.v_ego = 30.0;
  sample.day_key = 20260611;

  for (int i = 0; i < 100; ++i) {
    update_aegis_achievements(state, sample);
  }

  assert(state.daily_xp_day == 20260611);
  assert(state.daily_xp == AEGIS_DAILY_XP_CAP);
  assert(state.xp >= AEGIS_DAILY_XP_CAP);

  const int capped_day_xp = state.xp;
  update_aegis_achievements(state, sample);
  assert(state.xp == capped_day_xp);

  sample.day_key = 20260612;
  update_aegis_achievements(state, sample);
  assert(state.daily_xp_day == 20260612);
  assert(state.daily_xp > 0);
  assert(state.xp > capped_day_xp);
}

static void test_persist_and_reset() {
  AegisAchievementState state = parse_aegis_achievements("");

  AegisAchievementSample sample = {};
  sample.dt_s = 120.0;
  sample.enabled = true;
  sample.v_ego = 20.0;
  sample.route_count = 3;
  sample.day_key = 20260611;
  sample.vision_turn_control_active = true;
  sample.brake_hold_active = true;
  update_aegis_achievements(state, sample);

  const std::string encoded = serialize_aegis_achievements(state);
  AegisAchievementState decoded = parse_aegis_achievements(encoded);

  assert(decoded.xp == state.xp);
  assert(decoded.level == state.level);
  assert(decoded.unlocked_badges == state.unlocked_badges);
  assert(decoded.assisted_time_s == state.assisted_time_s);
  assert(decoded.assisted_distance_m == state.assisted_distance_m);
  assert(decoded.route_count_snapshot == 3);

  AegisAchievementState reset = reset_aegis_achievements(9);
  assert(reset.xp == 0);
  assert(reset.level == 1);
  assert(reset.route_count_snapshot == 9);
  assert(reset.unlocked_badges.empty());
}

int main() {
  test_parse_defaults();
  test_accrue_only_clean_enabled_assist();
  test_daily_xp_cap();
  test_persist_and_reset();
  return 0;
}
