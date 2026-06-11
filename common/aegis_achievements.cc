#include "common/aegis_achievements.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "third_party/json11/json11.hpp"

namespace {

int compute_level(int xp) {
  return std::max(1, xp / AEGIS_LEVEL_XP + 1);
}

int int_field(const json11::Json &obj, const std::string &key, int fallback = 0) {
  const json11::Json value = obj[key];
  return value.is_number() ? value.int_value() : fallback;
}

double number_field(const json11::Json &obj, const std::string &key, double fallback = 0.0) {
  const json11::Json value = obj[key];
  return value.is_number() ? value.number_value() : fallback;
}

void normalize(AegisAchievementState &state) {
  state.xp = std::max(0, state.xp);
  state.level = compute_level(state.xp);
  state.assisted_time_s = std::max(0.0, state.assisted_time_s);
  state.assisted_distance_m = std::max(0.0, state.assisted_distance_m);
  state.route_count_snapshot = std::max(0, state.route_count_snapshot);
  state.daily_xp_day = std::max(0, state.daily_xp_day);
  state.daily_xp = std::clamp(state.daily_xp, 0, AEGIS_DAILY_XP_CAP);

  std::sort(state.unlocked_badges.begin(), state.unlocked_badges.end());
  state.unlocked_badges.erase(std::unique(state.unlocked_badges.begin(), state.unlocked_badges.end()), state.unlocked_badges.end());
}

bool unlock_badge(AegisAchievementState &state, const AegisBadge &badge, AegisAchievementUpdate &update) {
  if (aegis_has_badge(state, badge.id)) {
    return false;
  }
  state.unlocked_badges.push_back(badge.id);
  update.new_badges.push_back(badge);
  update.changed = true;
  return true;
}

int award_xp(AegisAchievementState &state, int requested_xp) {
  if (requested_xp <= 0) {
    return 0;
  }

  const int room = std::max(0, AEGIS_DAILY_XP_CAP - state.daily_xp);
  const int awarded = std::min(requested_xp, room);
  state.xp += awarded;
  state.daily_xp += awarded;
  state.level = compute_level(state.xp);
  return awarded;
}

}  // namespace

const std::vector<AegisBadge> &aegis_badge_catalog() {
  static const std::vector<AegisBadge> badges = {
    {"first_assist", "First Assist", "Completed the first clean assisted minute.", 10},
    {"assisted_10km", "10 km Assisted", "Reached 10 km of estimated assisted distance.", 20},
    {"assisted_hour", "1 Hour Assisted", "Reached 1 hour of clean assisted time.", 30},
    {"route_memory", "Route Memory", "Recorded a new route count snapshot.", 10},
    {"vtsc_companion", "V-TSC Companion", "Observed vision turn control while assisted.", 15},
    {"brake_hold_companion", "Brake Hold Companion", "Observed brake hold while assisted.", 15},
  };
  return badges;
}

bool aegis_has_badge(const AegisAchievementState &state, const std::string &badge_id) {
  return std::find(state.unlocked_badges.begin(), state.unlocked_badges.end(), badge_id) != state.unlocked_badges.end();
}

AegisAchievementState parse_aegis_achievements(const std::string &json) {
  AegisAchievementState state;

  std::string err;
  const json11::Json obj = json11::Json::parse(json, err);
  if (!err.empty() || !obj.is_object()) {
    return state;
  }

  state.xp = int_field(obj, "xp");
  state.level = int_field(obj, "level", compute_level(state.xp));
  state.assisted_time_s = number_field(obj, "assisted_time_s");
  state.assisted_distance_m = number_field(obj, "assisted_distance_m");
  state.route_count_snapshot = int_field(obj, "route_count_snapshot");
  state.daily_xp_day = int_field(obj, "daily_xp_day");
  state.daily_xp = int_field(obj, "daily_xp");

  const json11::Json badges = obj["unlocked_badges"];
  if (badges.is_array()) {
    for (const json11::Json &badge_id : badges.array_items()) {
      if (badge_id.is_string()) {
        state.unlocked_badges.push_back(badge_id.string_value());
      }
    }
  }

  normalize(state);
  return state;
}

std::string serialize_aegis_achievements(const AegisAchievementState &state) {
  AegisAchievementState normalized = state;
  normalize(normalized);

  json11::Json::array badges;
  for (const std::string &badge_id : normalized.unlocked_badges) {
    badges.push_back(badge_id);
  }

  return json11::Json(json11::Json::object{
    {"xp", normalized.xp},
    {"level", normalized.level},
    {"unlocked_badges", badges},
    {"assisted_time_s", normalized.assisted_time_s},
    {"assisted_distance_m", normalized.assisted_distance_m},
    {"route_count_snapshot", normalized.route_count_snapshot},
    {"daily_xp_day", normalized.daily_xp_day},
    {"daily_xp", normalized.daily_xp},
  }).dump();
}

AegisAchievementState reset_aegis_achievements(int route_count_snapshot) {
  AegisAchievementState state;
  state.route_count_snapshot = std::max(0, route_count_snapshot);
  return state;
}

AegisAchievementUpdate update_aegis_achievements(AegisAchievementState &state, const AegisAchievementSample &sample) {
  normalize(state);
  AegisAchievementUpdate update;

  if (sample.day_key > 0 && sample.day_key != state.daily_xp_day) {
    state.daily_xp_day = sample.day_key;
    state.daily_xp = 0;
    update.changed = true;
  }

  const double previous_time_s = state.assisted_time_s;
  const double previous_distance_m = state.assisted_distance_m;
  const int previous_route_count = state.route_count_snapshot;

  const bool clean_assist = sample.enabled && !sample.has_alert && sample.dt_s > 0.0;
  if (clean_assist) {
    const double dt_s = sample.dt_s;
    state.assisted_time_s += dt_s;
    state.assisted_distance_m += std::max(0.0, sample.v_ego) * dt_s;
    update.changed = true;
  }

  if (sample.route_count > state.route_count_snapshot) {
    state.route_count_snapshot = sample.route_count;
    update.changed = true;
  }

  int requested_xp = 0;
  if (clean_assist) {
    requested_xp += static_cast<int>(std::floor(state.assisted_time_s / 60.0) - std::floor(previous_time_s / 60.0));
    requested_xp += static_cast<int>(std::floor(state.assisted_distance_m / 1000.0) - std::floor(previous_distance_m / 1000.0));
  }

  const auto &badges = aegis_badge_catalog();
  auto find_badge = [&badges](const std::string &badge_id) -> const AegisBadge& {
    auto it = std::find_if(badges.begin(), badges.end(), [&badge_id](const AegisBadge &badge) {
      return badge.id == badge_id;
    });
    return *it;
  };

  if (state.assisted_time_s >= 60.0 && unlock_badge(state, find_badge("first_assist"), update)) {
    requested_xp += find_badge("first_assist").xp;
  }
  if (state.assisted_distance_m >= 10000.0 && unlock_badge(state, find_badge("assisted_10km"), update)) {
    requested_xp += find_badge("assisted_10km").xp;
  }
  if (state.assisted_time_s >= 3600.0 && unlock_badge(state, find_badge("assisted_hour"), update)) {
    requested_xp += find_badge("assisted_hour").xp;
  }
  if (previous_route_count > 0 && state.route_count_snapshot > previous_route_count && unlock_badge(state, find_badge("route_memory"), update)) {
    requested_xp += find_badge("route_memory").xp;
  }
  if (clean_assist && sample.vision_turn_control_active && unlock_badge(state, find_badge("vtsc_companion"), update)) {
    requested_xp += find_badge("vtsc_companion").xp;
  }
  if (clean_assist && sample.brake_hold_active && unlock_badge(state, find_badge("brake_hold_companion"), update)) {
    requested_xp += find_badge("brake_hold_companion").xp;
  }

  update.xp_awarded = award_xp(state, requested_xp);
  update.changed = update.changed || update.xp_awarded > 0;
  normalize(state);
  return update;
}
