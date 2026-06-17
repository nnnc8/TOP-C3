#pragma once

#include <QFrame>
#include <QLabel>

#include "common/friday_journey_board.h"
#include "common/params.h"
#include "selfdrive/ui/qt/widgets/controls.h"

class JourneyBoardPanel : public QWidget {
  Q_OBJECT

public:
  explicit JourneyBoardPanel(QWidget *parent = nullptr);
  void showEvent(QShowEvent *event) override;

private:
  QString formatTime(double seconds) const;
  QString formatDistance(double meters) const;
  QString formatSpeed(double speed_mps) const;
  QString formatRatio(double ratio) const;
  void refresh();
  void resetJourneyBoard();

  Params params;

  // Totals Zone
  QLabel *total_routes_val;
  QLabel *total_trips_val;
  QLabel *total_drive_time_val;
  QLabel *total_moving_time_val;
  QLabel *total_moving_dist_val;
  QLabel *total_assist_time_val;
  QLabel *total_assist_dist_val;
  QLabel *total_assist_ratio_val;
  QLabel *total_top_speed_val;
  QLabel *total_assist_top_speed_val;
  QLabel *total_alerts_val;
  QLabel *total_interv_val;

  // Last Trip Zone
  QLabel *last_drive_time_val;
  QLabel *last_moving_time_val;
  QLabel *last_moving_dist_val;
  QLabel *last_assist_time_val;
  QLabel *last_assist_dist_val;
  QLabel *last_assist_ratio_val;
  QLabel *last_top_speed_val;
  QLabel *last_assist_top_speed_val;
  QLabel *last_alerts_val;
  QLabel *last_interv_val;

  // Last 7 Days Zone
  QLabel *trend_trips_val;
  QLabel *trend_dist_val;
  QLabel *trend_assist_time_val;
  QLabel *trend_assist_dist_val;
  QLabel *trend_assist_ratio_val;
  QLabel *trend_avg_alerts_val;
  QLabel *trend_avg_interv_val;
};
