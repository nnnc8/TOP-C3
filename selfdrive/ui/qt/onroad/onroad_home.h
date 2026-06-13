#pragma once

#include <QColor>
#include "common/aegis_journey_board.h"
#include "common/params.h"
#include "selfdrive/ui/qt/onroad/alerts.h"
#include "selfdrive/ui/qt/onroad/annotated_camera.h"

static int personalityProfile;
class OnroadWindow : public QWidget {
  Q_OBJECT

  const int DP_INDICATOR_BLINK_RATE_STD = 8;
  const int DP_INDICATOR_BLINK_RATE_FAST = 4;
  const QColor DP_INDICATOR_COLOR_BLINKER = QColor(0, 0xff, 0, 255);
  const QColor DP_INDICATOR_COLOR_BSM = QColor(0xff, 0xff, 0, 255);

public:
  OnroadWindow(QWidget* parent = 0);

private:
  void paintEvent(QPaintEvent *event);
  void mousePressEvent(QMouseEvent* e) override;
  void updateJourneyBoard(const UIState &s);
  void saveJourneyBoard(bool force = false);

  OnroadAlerts *alerts;
  AnnotatedCameraWidget *nvg;
  QColor bg = bg_colors[STATUS_DISENGAGED];
  QHBoxLayout* split;
  Params params;
  AegisJourneyBoardState journey_state;
  bool journey_board_loaded = false;
  bool journey_board_dirty = false;
  double last_journey_update_millis = 0.0;
  double last_journey_save_millis = 0.0;
  bool overriding_prev = false;
  bool has_alert_prev = false;

  void updateDpIndicatorSideState(bool blinker_state, bool bsm_state, bool &show, bool &show_prev, int &count, QColor &color);
  void updateDpIndicatorStates(const UIState &s);
  // left
  int dp_indicator_count_left = 0;
  QColor dp_indicator_color_left = DP_INDICATOR_COLOR_BLINKER;
  bool dp_indicator_show_left = false;
  bool dp_indicator_show_left_prev = false;
  // right
  int dp_indicator_count_right = 0;
  QColor dp_indicator_color_right = DP_INDICATOR_COLOR_BLINKER;
  bool dp_indicator_show_right = false;
  bool dp_indicator_show_right_prev = false;

private slots:
  void offroadTransition(bool offroad);
  void updateState(const UIState &s);
};
