#pragma once

#include <string>
#include <vector>

constexpr int AEGIS_DAILY_XP_CAP = 120;
constexpr int AEGIS_LEVEL_XP = 100;

struct AegisBadge {
  std::string id;
  std::string title;
  std::string description;
  int xp = 0;

  bool operator==(const AegisBadge &other) const {
    return id == other.id && title == other.title && description == other.description && xp == other.xp;
  }
};

struct AegisAchievementState {
  int xp = 0;
  int level = 1;
  std::vector<std::string> unlocked_badges;
  double drive_time_s = 0.0;
  double moving_time_s = 0.0;
  double moving_distance_m = 0.0;
  double top_speed_mps = 0.0;
  double assisted_time_s = 0.0;
  double assisted_distance_m = 0.0;
  double assisted_top_speed_mps = 0.0;
  int route_count_snapshot = 0;
  int daily_xp_day = 0;
  int daily_xp = 0;
};

struct AegisAchievementSample {
  double dt_s = 0.0;
  bool enabled = false;
  bool has_alert = false;
  bool standstill = false;
  double v_ego = 0.0;
  int route_count = 0;
  int day_key = 0;
  bool vision_turn_control_active = false;
  bool brake_hold_active = false;
};

struct AegisAchievementUpdate {
  int xp_awarded = 0;
  bool changed = false;
  std::vector<AegisBadge> new_badges;
};

AegisAchievementState parse_aegis_achievements(const std::string &json);
std::string serialize_aegis_achievements(const AegisAchievementState &state);
AegisAchievementState reset_aegis_achievements(int route_count_snapshot = 0);
AegisAchievementUpdate update_aegis_achievements(AegisAchievementState &state, const AegisAchievementSample &sample);

const std::vector<AegisBadge> &aegis_badge_catalog();
bool aegis_has_badge(const AegisAchievementState &state, const std::string &badge_id);
