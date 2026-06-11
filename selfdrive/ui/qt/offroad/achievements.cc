#include "selfdrive/ui/qt/offroad/achievements.h"

#include <cmath>

#include <QShowEvent>

#include "selfdrive/ui/qt/util.h"

namespace {

QString format_assisted_time(double seconds) {
  const int total_minutes = static_cast<int>(std::floor(std::max(0.0, seconds) / 60.0));
  const int hours = total_minutes / 60;
  const int minutes = total_minutes % 60;
  if (hours > 0) {
    return QObject::tr("%1 h %2 min").arg(hours).arg(minutes);
  }
  return QObject::tr("%1 min").arg(minutes);
}

QString format_distance(double meters) {
  return QObject::tr("%1 km").arg(QString::number(std::max(0.0, meters) / 1000.0, 'f', 1));
}

QString format_level_progress(int xp) {
  return QObject::tr("%1 / %2 XP").arg(xp % AEGIS_LEVEL_XP).arg(AEGIS_LEVEL_XP);
}

}  // namespace

AchievementsPanel::AchievementsPanel(QWidget *parent) : ListWidget(parent) {
  setSpacing(35);

  level_label = new LabelControl(tr("Level"), "", tr("AEGIS journey level based on capped achievement XP."), this);
  xp_label = new LabelControl(tr("Level Progress"), "", tr("Progress toward the next level. XP has a daily cap."), this);
  daily_xp_label = new LabelControl(tr("Today"), "", tr("XP earned today against the daily cap."), this);
  time_label = new LabelControl(tr("Assisted Time"), "", tr("Clean assisted time counted only while openpilot is enabled and no alert is active."), this);
  distance_label = new LabelControl(tr("Assisted Distance"), "", tr("Estimated from live vehicle speed while openpilot is enabled."), this);
  routes_label = new LabelControl(tr("Route Snapshot"), "", tr("The latest RouteCount snapshot stored with achievements."), this);
  badges_label = new LabelControl(tr("Badges"), "", tr("Unlocked journey badges."), this);

  addItem(level_label);
  addItem(xp_label);
  addItem(daily_xp_label);
  addItem(time_label);
  addItem(distance_label);
  addItem(routes_label);
  addItem(badges_label);
  addItem(horizontal_line());

  if (params.get("AegisAchievementToasts").empty()) {
    params.putBool("AegisAchievementToasts", true);
  }
  addItem(new ParamControl("AegisAchievementToasts",
                           tr("Achievement Toasts"),
                           tr("Show one small achievement toast per trip while the car is stopped and no alert is visible."),
                           "../assets/icons/road.png",
                           this));

  ButtonControl *reset_btn = new ButtonControl(tr("Reset Achievements"), tr("RESET"),
                                               tr("Clear XP, badges, assisted counters, and start a fresh RouteCount snapshot."),
                                               this);
  QObject::connect(reset_btn, &ButtonControl::clicked, [this]() { resetAchievements(); });
  addItem(reset_btn);
  addItem(horizontal_line());

  for (const AegisBadge &badge : aegis_badge_catalog()) {
    LabelControl *row = new LabelControl(tr(badge.title.c_str()), "", tr(badge.description.c_str()), this);
    badge_rows[badge.id] = row;
    addItem(row);
  }

  refresh();
}

void AchievementsPanel::showEvent(QShowEvent *event) {
  ListWidget::showEvent(event);
  refresh();
}

void AchievementsPanel::refresh() {
  AegisAchievementState state = parse_aegis_achievements(params.get("AegisAchievements"));

  level_label->setText(tr("Level %1").arg(state.level));
  xp_label->setText(format_level_progress(state.xp));
  daily_xp_label->setText(tr("%1 / %2 XP").arg(state.daily_xp).arg(AEGIS_DAILY_XP_CAP));
  time_label->setText(format_assisted_time(state.assisted_time_s));
  distance_label->setText(format_distance(state.assisted_distance_m));
  routes_label->setText(QString::number(state.route_count_snapshot));
  badges_label->setText(tr("%1 / %2 unlocked").arg(state.unlocked_badges.size()).arg(aegis_badge_catalog().size()));

  for (const AegisBadge &badge : aegis_badge_catalog()) {
    auto row = badge_rows.find(badge.id);
    if (row == badge_rows.end()) {
      continue;
    }
    row->second->setText(aegis_has_badge(state, badge.id) ? tr("Unlocked") : tr("Locked"));
  }
}

void AchievementsPanel::resetAchievements() {
  if (!ConfirmationDialog::confirm(tr("Reset AEGIS achievements?"), tr("Reset"), this)) {
    return;
  }

  AegisAchievementState state = reset_aegis_achievements(params.getInt("RouteCount"));
  params.put("AegisAchievements", serialize_aegis_achievements(state));
  refresh();
}
