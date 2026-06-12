#pragma once

#include <QFrame>
#include <QLabel>
#include <QProgressBar>

#include "common/aegis_achievements.h"
#include "common/params.h"
#include "selfdrive/ui/qt/widgets/controls.h"

class AchievementsPanel : public QWidget {
  Q_OBJECT

public:
  explicit AchievementsPanel(QWidget *parent = nullptr);
  void showEvent(QShowEvent *event) override;

private:
  QString formatAssistedTime(double seconds) const;
  QString formatDistance(double meters) const;
  QString formatSpeed(double speed_mps) const;
  QString formatAverageSpeed(double meters, double seconds) const;
  void refresh();
  void resetAchievements();

  Params params;
  QLabel *level_value;
  QLabel *xp_progress_label;
  QProgressBar *xp_bar;
  QLabel *daily_xp_value;
  QProgressBar *daily_xp_bar;
  QLabel *drive_time_value;
  QLabel *moving_time_value;
  QLabel *moving_distance_value;
  QLabel *average_speed_value;
  QLabel *top_speed_value;
  QLabel *assisted_time_value;
  QLabel *assisted_distance_value;
  QLabel *assisted_average_speed_value;
  QLabel *assisted_top_speed_value;
  QLabel *routes_value;
};
