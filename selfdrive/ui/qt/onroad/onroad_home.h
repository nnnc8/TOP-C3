#pragma once

#include <QColor>
#include <QFrame>
#include <QLabel>

#include "common/aegis_achievements.h"
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
  void updateAchievements(const UIState &s);
  void saveAchievements(bool force = false);
  bool achievementToastsEnabled();
  void showAchievementToast(const QString &title, const QString &detail, double now_millis);

  OnroadAlerts *alerts;
  AnnotatedCameraWidget *nvg;
  QWidget *achievement_toast_container;
  QFrame *achievement_toast_card;
  QLabel *achievement_toast_title;
  QLabel *achievement_toast_detail;
  QColor bg = bg_colors[STATUS_DISENGAGED];
  QHBoxLayout* split;
  Params achievement_params;
  AegisAchievementState achievement_state;
  bool achievements_loaded = false;
  bool achievement_dirty = false;
  bool toast_shown_this_trip = false;
  double last_achievement_update_millis = 0.0;
  double last_achievement_save_millis = 0.0;
  double achievement_toast_until_millis = 0.0;
  QString pending_achievement_title;
  QString pending_achievement_detail;

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
