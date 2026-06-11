#include "selfdrive/ui/qt/onroad/onroad_home.h"

#include <chrono>
#include <cmath>
#include <algorithm>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDate>
#include <QMouseEvent>
#include <QPainter>
#include <QStackedLayout>
#include <QTimer>
#include <QVBoxLayout>

#include "common/timing.h"
#include "selfdrive/ui/qt/util.h"

namespace {

constexpr double AEGIS_ACHIEVEMENT_SAVE_INTERVAL_MS = 10000.0;
constexpr double AEGIS_ACHIEVEMENT_TOAST_MS = 3500.0;

int current_day_key() {
  return QDate::currentDate().toString("yyyyMMdd").toInt();
}

QString achievement_badge_title(const AegisBadge &badge) {
  if (badge.id == "first_assist") return QCoreApplication::translate("AchievementsPanel", "First Assist");
  if (badge.id == "assisted_10km") return QCoreApplication::translate("AchievementsPanel", "10 km Assisted");
  if (badge.id == "assisted_hour") return QCoreApplication::translate("AchievementsPanel", "1 Hour Assisted");
  if (badge.id == "route_memory") return QCoreApplication::translate("AchievementsPanel", "Route Memory");
  if (badge.id == "vtsc_companion") return QCoreApplication::translate("AchievementsPanel", "V-TSC Companion");
  if (badge.id == "brake_hold_companion") return QCoreApplication::translate("AchievementsPanel", "Brake Hold Companion");
  return QString::fromStdString(badge.title);
}

}  // namespace

OnroadWindow::OnroadWindow(QWidget *parent) : QWidget(parent) {
  QVBoxLayout *main_layout  = new QVBoxLayout(this);
  main_layout->setMargin(UI_BORDER_SIZE);
  QStackedLayout *stacked_layout = new QStackedLayout;
  stacked_layout->setStackingMode(QStackedLayout::StackAll);
  main_layout->addLayout(stacked_layout);

  nvg = new AnnotatedCameraWidget(VISION_STREAM_ROAD, this);

  QWidget * split_wrapper = new QWidget;
  split = new QHBoxLayout(split_wrapper);
  split->setContentsMargins(0, 0, 0, 0);
  split->setSpacing(0);
  split->addWidget(nvg);

  if (getenv("DUAL_CAMERA_VIEW")) {
    CameraWidget *arCam = new CameraWidget("camerad", VISION_STREAM_ROAD, this);
    split->insertWidget(0, arCam);
  }

  stacked_layout->addWidget(split_wrapper);

  alerts = new OnroadAlerts(this);
  alerts->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  stacked_layout->addWidget(alerts);

  achievement_toast_container = new QWidget(this);
  achievement_toast_container->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  QVBoxLayout *toast_layout = new QVBoxLayout(achievement_toast_container);
  toast_layout->setContentsMargins(0, 350, 0, 0);
  achievement_toast_card = new QFrame(achievement_toast_container);
  achievement_toast_card->setFixedSize(980, 142);
  QVBoxLayout *achievement_layout = new QVBoxLayout(achievement_toast_card);
  achievement_layout->setContentsMargins(36, 22, 36, 20);
  achievement_layout->setSpacing(4);
  achievement_toast_title = new QLabel(achievement_toast_card);
  achievement_toast_title->setAlignment(Qt::AlignCenter);
  achievement_toast_detail = new QLabel(achievement_toast_card);
  achievement_toast_detail->setAlignment(Qt::AlignCenter);
  achievement_layout->addWidget(achievement_toast_title);
  achievement_layout->addWidget(achievement_toast_detail);
  achievement_toast_card->setStyleSheet(R"(
    QFrame {
      background-color: rgba(21, 23, 28, 238);
      border: 2px solid #E82127;
      border-radius: 12px;
    }
    QLabel {
      background-color: transparent;
      border: none;
    }
  )");
  achievement_toast_title->setStyleSheet("font-size: 30px; font-weight: 700; color: #E82127; background-color: transparent; border: none;");
  achievement_toast_detail->setStyleSheet("font-size: 42px; font-weight: 700; color: #FFFFFF; background-color: transparent; border: none;");
  achievement_toast_card->hide();
  toast_layout->addWidget(achievement_toast_card, 0, Qt::AlignTop | Qt::AlignHCenter);
  toast_layout->addStretch(1);
  stacked_layout->addWidget(achievement_toast_container);

  // setup stacking order
  alerts->raise();
  achievement_toast_container->raise();

  setAttribute(Qt::WA_OpaquePaintEvent);
  QObject::connect(uiState(), &UIState::uiUpdate, this, &OnroadWindow::updateState);
  QObject::connect(uiState(), &UIState::offroadTransition, this, &OnroadWindow::offroadTransition);
}

void OnroadWindow::updateDpIndicatorSideState(bool blinker_state, bool bsm_state, bool &show, bool &show_prev, int &count, QColor &color) {
  if (!blinker_state && !bsm_state) {
    show = false;
    count = 0;
  } else {
    count += 1;
  }
  if (bsm_state && blinker_state) {
    show = count % DP_INDICATOR_BLINK_RATE_FAST == 0? !show : show;
    color = DP_INDICATOR_COLOR_BSM;
  } else if (blinker_state) {
    show = count % DP_INDICATOR_BLINK_RATE_STD == 0? !show : show;
    color = DP_INDICATOR_COLOR_BLINKER;
  } else if (bsm_state) {
    show = true;
    color = DP_INDICATOR_COLOR_BSM;
  } else {
    show = false;
  }
}

void OnroadWindow::updateDpIndicatorStates(const UIState &s) {
  const auto cs = (*s.sm)["carState"].getCarState();
  updateDpIndicatorSideState(cs.getLeftBlinker(), cs.getLeftBlindspot(), dp_indicator_show_left, dp_indicator_show_left_prev, dp_indicator_count_left, dp_indicator_color_left);
  updateDpIndicatorSideState(cs.getRightBlinker(), cs.getRightBlindspot(), dp_indicator_show_right, dp_indicator_show_right_prev, dp_indicator_count_right, dp_indicator_color_right);
}

void OnroadWindow::updateState(const UIState &s) {
  if (!s.scene.started) {
    return;
  }

  updateAchievements(s);

  dp_indicator_show_left_prev = dp_indicator_show_left;
  dp_indicator_show_right_prev = dp_indicator_show_right;
  updateDpIndicatorStates(s);
  bool indicator_states_changed = dp_indicator_show_left != dp_indicator_show_left_prev || dp_indicator_show_right != dp_indicator_show_right_prev;

  alerts->updateState(s);
  nvg->updateState(s);

  QColor bgColor = bg_colors[s.scene.alka_active && s.status == STATUS_DISENGAGED? STATUS_ALKA : s.status];
  if (bg != bgColor || indicator_states_changed) {
    // repaint border
    bg = bgColor;
    update();
  }
}

bool OnroadWindow::achievementToastsEnabled() {
  const std::string value = achievement_params.get("AegisAchievementToasts");
  return value.empty() || value == "1";
}

void OnroadWindow::saveAchievements(bool force) {
  if (!achievements_loaded || !achievement_dirty) {
    return;
  }

  const double now_millis = millis_since_boot();
  if (!force && (now_millis - last_achievement_save_millis) < AEGIS_ACHIEVEMENT_SAVE_INTERVAL_MS) {
    return;
  }

  const std::string serialized = serialize_aegis_achievements(achievement_state);
  if (force) {
    achievement_params.put("AegisAchievements", serialized);
  } else {
    achievement_params.putNonBlocking("AegisAchievements", serialized);
  }
  achievement_dirty = false;
  last_achievement_save_millis = now_millis;
}

void OnroadWindow::showAchievementToast(const QString &title, const QString &detail, double now_millis) {
  achievement_toast_title->setText(title);
  achievement_toast_detail->setText(detail);
  achievement_toast_card->show();
  achievement_toast_until_millis = now_millis + AEGIS_ACHIEVEMENT_TOAST_MS;
  toast_shown_this_trip = true;
}

void OnroadWindow::updateAchievements(const UIState &s) {
  const SubMaster &sm = *(s.sm);
  const double now_millis = millis_since_boot();

  if (!achievements_loaded) {
    achievement_state = parse_aegis_achievements(achievement_params.get("AegisAchievements"));
    achievements_loaded = true;
    last_achievement_update_millis = now_millis;
    last_achievement_save_millis = now_millis;
  }

  const double raw_dt_s = (now_millis - last_achievement_update_millis) / 1000.0;
  last_achievement_update_millis = now_millis;
  const double dt_s = std::isfinite(raw_dt_s) ? std::clamp(raw_dt_s, 0.0, 1.0) : 0.0;

  const bool selfdrive_ready = sm.rcv_frame("selfdriveState") >= s.scene.started_frame;
  const auto selfdrive_state = sm["selfdriveState"].getSelfdriveState();
  const auto car_state = sm["carState"].getCarState();
  const auto plan_top = sm["longitudinalPlanTOP"].getLongitudinalPlanTOP();

  const bool has_alert = !selfdrive_ready ||
                         selfdrive_state.getAlertSize() != cereal::SelfdriveState::AlertSize::NONE ||
                         selfdrive_state.getAlertStatus() != cereal::SelfdriveState::AlertStatus::NORMAL;
  const bool standstill = car_state.getStandstill() || std::abs(car_state.getVEgo()) < 0.01;

  AegisAchievementSample sample = {};
  sample.dt_s = dt_s;
  sample.enabled = selfdrive_state.getEnabled();
  sample.has_alert = has_alert;
  sample.standstill = standstill;
  sample.v_ego = car_state.getVEgo();
  sample.route_count = achievement_params.getInt("RouteCount");
  sample.day_key = current_day_key();
  sample.vision_turn_control_active = plan_top.getSmartCruiseControl().getVision().getActive();
  sample.brake_hold_active = car_state.getBrakeHoldActive();

  AegisAchievementUpdate achievement_update = update_aegis_achievements(achievement_state, sample);
  achievement_dirty = achievement_dirty || achievement_update.changed;

  if (!achievement_update.new_badges.empty() && pending_achievement_title.isEmpty() && !toast_shown_this_trip) {
    const AegisBadge &badge = achievement_update.new_badges.front();
    pending_achievement_title = tr("ACCOLADE UNLOCKED");
    pending_achievement_detail = tr("%1 secured").arg(achievement_badge_title(badge));
  }

  const bool can_show_toast = achievementToastsEnabled() && standstill && !has_alert;
  if (!pending_achievement_title.isEmpty() && can_show_toast && !toast_shown_this_trip) {
    showAchievementToast(pending_achievement_title, pending_achievement_detail, now_millis);
    pending_achievement_title.clear();
    pending_achievement_detail.clear();
  } else if (now_millis >= achievement_toast_until_millis) {
    achievement_toast_card->hide();
  }

  saveAchievements(!achievement_update.new_badges.empty());
}

void OnroadWindow::mousePressEvent(QMouseEvent* e) {
  const auto &scene = uiState()->scene;
  // const SubMaster &sm = *uiState()->sm;
  static auto params = Params();
  // const bool isDrivingPersonalitiesViaUI = scene.driving_personalities_ui_wheel;
  const bool isExperimentalModeViaUI = scene.experimental_mode_via_wheel && !scene.steering_wheel_car;
  static bool propagateEvent = false;
  static bool recentlyTapped = false;
  const bool isToyotaCar = scene.steering_wheel_car;
  const int y_offset = 70;
  // bool rightHandDM = sm["driverMonitoringState"].getDriverMonitoringState().getIsRHD();

  // Driving personalities button
  int x = rect().left() + (btn_size - 24) / 2 - (UI_BORDER_SIZE * 2) + 100;
  const int y = rect().bottom() - y_offset;
  // Give the button a 25% offset so it doesn't need to be clicked on perfectly
  bool isDrivingPersonalitiesClicked = (e->pos() - QPoint(x, y)).manhattanLength() <= btn_size * 2 && !isToyotaCar;

  // Check if the button was clicked
  if (isDrivingPersonalitiesClicked) {
    personalityProfile = (params.getInt("LongitudinalPersonality") + 2) % 3;
    params.putInt("LongitudinalPersonality", personalityProfile);
    propagateEvent = false;
  // If the click wasn't on the button for drivingPersonalities, change the value of "ExperimentalMode"
  } else if (recentlyTapped && isExperimentalModeViaUI) {
    bool experimentalMode = params.getBool("ExperimentalMode");
    params.putBool("ExperimentalMode", !experimentalMode);
    recentlyTapped = false;
    propagateEvent = true;
  } else {
    recentlyTapped = true;
    propagateEvent = true;
  }

  if (propagateEvent) {
    QWidget::mousePressEvent(e);
  }
}

void OnroadWindow::offroadTransition(bool offroad) {
  saveAchievements(true);
  if (!offroad) {
    toast_shown_this_trip = false;
    pending_achievement_title.clear();
    pending_achievement_detail.clear();
    achievement_toast_until_millis = 0.0;
    last_achievement_update_millis = millis_since_boot();
  } else {
    achievement_toast_card->hide();
  }
  alerts->clear();
}

void OnroadWindow::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  p.fillRect(rect(), QColor(bg.red(), bg.green(), bg.blue(), 180));
  if (dp_indicator_show_left) p.fillRect(QRect(0, 0, width() * 0.2, height()), dp_indicator_color_left);
  if (dp_indicator_show_right) p.fillRect(QRect(width() * 0.8, 0, width() * 0.2, height()), dp_indicator_color_right);
}
