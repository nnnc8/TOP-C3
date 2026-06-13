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

constexpr double AEGIS_JOURNEY_SAVE_INTERVAL_MS = 10000.0;

int current_day_key() {
  return QDate::currentDate().toString("yyyyMMdd").toInt();
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
  toast_layout->setContentsMargins(0, 40, 40, 0); // safe margin at top right

  achievement_toast_card = new QFrame(achievement_toast_container);
  achievement_toast_card->setFixedSize(400, 200);
  QVBoxLayout *achievement_layout = new QVBoxLayout(achievement_toast_card);
  achievement_layout->setContentsMargins(24, 16, 24, 16);
  achievement_layout->setSpacing(8);

  QLabel *onroad_card_title = new QLabel(tr("輔助品質"), achievement_toast_card);
  onroad_card_title->setStyleSheet("font-size: 22px; font-weight: 700; color: #A6B0BE; background-color: transparent; border: none;");
  achievement_layout->addWidget(onroad_card_title);

  QGridLayout *grid = new QGridLayout();
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(16);
  grid->setVerticalSpacing(10);

  auto makeMetricBlock = [](const QString &label, QLabel **val_lbl, QGridLayout *g, int row, int col) {
    QVBoxLayout *cell_lay = new QVBoxLayout();
    cell_lay->setSpacing(2);
    cell_lay->setContentsMargins(0, 0, 0, 0);

    QLabel *lbl_name = new QLabel(label);
    lbl_name->setStyleSheet("font-size: 16px; font-weight: 600; color: #8A95A5; background-color: transparent; border: none;");

    *val_lbl = new QLabel("-");
    (*val_lbl)->setStyleSheet("font-size: 26px; font-weight: 700; color: #FFFFFF; background-color: transparent; border: none;");

    cell_lay->addWidget(lbl_name);
    cell_lay->addWidget(*val_lbl);
    g->addLayout(cell_lay, row, col);
  };

  makeMetricBlock(tr("輔助時間"), &lbl_assist_time_val, grid, 0, 0);
  makeMetricBlock(tr("輔助里程"), &lbl_assist_dist_val, grid, 0, 1);
  makeMetricBlock(tr("輔助占比"), &lbl_assist_ratio_val, grid, 1, 0);
  makeMetricBlock(tr("介入次數"), &lbl_interv_val, grid, 1, 1);

  achievement_layout->addLayout(grid);

  achievement_toast_card->setStyleSheet(R"(
    QFrame {
      background-color: rgba(21, 23, 28, 220);
      border: 2px solid rgba(255, 255, 255, 42);
      border-radius: 12px;
    }
    QLabel {
      background-color: transparent;
      border: none;
    }
  )");

  achievement_toast_card->hide();
  toast_layout->addWidget(achievement_toast_card, 0, Qt::AlignTop | Qt::AlignRight);
  toast_layout->addStretch(1);
  stacked_layout->addWidget(achievement_toast_container);

  // setup stacking order
  alerts->raise();
  achievement_toast_container->raise();

  show_onroad_card = params.getBool("AegisJourneyBoardOnroadCard");

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

  updateJourneyBoard(s);

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

void OnroadWindow::saveJourneyBoard(bool force) {
  if (!journey_board_loaded || !journey_board_dirty) {
    return;
  }

  const double now_millis = millis_since_boot();
  if (!force && (now_millis - last_journey_save_millis) < AEGIS_JOURNEY_SAVE_INTERVAL_MS) {
    return;
  }

  const std::string serialized = serialize_aegis_journey_board(journey_state);
  if (force) {
    params.put("AegisJourneyBoard", serialized);
  } else {
    params.putNonBlocking("AegisJourneyBoard", serialized);
  }
  journey_board_dirty = false;
  last_journey_save_millis = now_millis;
}

void OnroadWindow::updateJourneyBoard(const UIState &s) {
  const SubMaster &sm = *(s.sm);
  const double now_millis = millis_since_boot();

  if (!journey_board_loaded) {
    journey_state = load_journey_board(params);
    journey_board_loaded = true;
    last_journey_update_millis = now_millis;
    last_journey_save_millis = now_millis;
  }

  const double raw_dt_s = (now_millis - last_journey_update_millis) / 1000.0;
  last_journey_update_millis = now_millis;
  const double dt_s = std::isfinite(raw_dt_s) ? std::clamp(raw_dt_s, 0.0, 1.0) : 0.0;

  const bool selfdrive_ready = sm.rcv_frame("selfdriveState") >= s.scene.started_frame;
  const auto selfdrive_state = sm["selfdriveState"].getSelfdriveState();
  const auto car_state = sm["carState"].getCarState();

  const bool has_alert = !selfdrive_ready ||
                         selfdrive_state.getAlertSize() != cereal::SelfdriveState::AlertSize::NONE ||
                         selfdrive_state.getAlertStatus() != cereal::SelfdriveState::AlertStatus::NORMAL;
  const bool standstill = car_state.getStandstill() || std::abs(car_state.getVEgo()) < 0.01;

  AegisJourneySample sample = {};
  sample.dt_s = dt_s;
  sample.enabled = selfdrive_state.getEnabled();
  sample.has_alert = has_alert;
  sample.standstill = standstill;
  sample.v_ego = car_state.getVEgo();
  sample.route_count = params.getInt("RouteCount");
  sample.day_key = current_day_key();
  sample.brake_pressed = car_state.getBrakePressed();
  sample.gas_pressed = car_state.getGasPressed();
  sample.steering_pressed = car_state.getSteeringPressed();

  update_aegis_journey_board(journey_state, sample, overriding_prev, has_alert_prev);
  if (dt_s > 0.0) {
    journey_board_dirty = true;
  }

  // Show card only when started, toggle is enabled, and there is no active alert
  if (s.scene.started && show_onroad_card && !has_alert) {
    auto format_time = [](double seconds) -> QString {
      int total_mins = static_cast<int>(seconds) / 60;
      int hours = total_mins / 60;
      int mins = total_mins % 60;
      if (hours > 0) {
        return QString("%1h %2m").arg(hours).arg(mins);
      } else if (mins > 0) {
        return QString("%1m").arg(mins);
      } else {
        return QString("%1s").arg(static_cast<int>(seconds));
      }
    };

    auto format_dist = [](double meters) -> QString {
      double km = meters / 1000.0;
      if (km >= 10.0) {
        return QString("%1 km").arg(QString::number(km, 'f', 0));
      } else {
        return QString("%1 km").arg(QString::number(km, 'f', 1));
      }
    };

    double ratio = get_assist_ratio(journey_state.current_trip.assisted_moving_time_s, journey_state.current_trip.moving_time_s);
    QString ratio_str = QString("%1%").arg(static_cast<int>(std::round(ratio * 100.0)));

    lbl_assist_time_val->setText(format_time(journey_state.current_trip.assisted_time_s));
    lbl_assist_dist_val->setText(format_dist(journey_state.current_trip.assisted_distance_m));
    lbl_assist_ratio_val->setText(ratio_str);
    lbl_interv_val->setText(tr("%1 次").arg(journey_state.current_trip.intervention_count));

    achievement_toast_card->show();
  } else {
    achievement_toast_card->hide();
  }

  saveJourneyBoard(false);
}

void OnroadWindow::mousePressEvent(QMouseEvent* e) {
  const auto &scene = uiState()->scene;
  const bool isExperimentalModeViaUI = scene.experimental_mode_via_wheel && !scene.steering_wheel_car;
  static bool propagateEvent = false;
  static bool recentlyTapped = false;
  const bool isToyotaCar = scene.steering_wheel_car;
  const int y_offset = 70;

  // Driving personalities button
  int x = rect().left() + (btn_size - 24) / 2 - (UI_BORDER_SIZE * 2) + 100;
  const int y = rect().bottom() - y_offset;
  bool isDrivingPersonalitiesClicked = (e->pos() - QPoint(x, y)).manhattanLength() <= btn_size * 2 && !isToyotaCar;

  if (isDrivingPersonalitiesClicked) {
    personalityProfile = (params.getInt("LongitudinalPersonality") + 2) % 3;
    params.putInt("LongitudinalPersonality", personalityProfile);
    propagateEvent = false;
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
  if (offroad) {
    if (journey_board_loaded) {
      close_aegis_journey_trip(journey_state, current_day_key());
      saveJourneyBoard(true);
    }
    achievement_toast_card->hide();
  } else {
    show_onroad_card = params.getBool("AegisJourneyBoardOnroadCard");
    last_journey_update_millis = millis_since_boot();
    last_journey_save_millis = millis_since_boot();
    overriding_prev = false;
    has_alert_prev = false;
  }
  alerts->clear();
}

void OnroadWindow::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  p.fillRect(rect(), QColor(bg.red(), bg.green(), bg.blue(), 180));
  if (dp_indicator_show_left) p.fillRect(QRect(0, 0, width() * 0.2, height()), dp_indicator_color_left);
  if (dp_indicator_show_right) p.fillRect(QRect(width() * 0.8, 0, width() * 0.2, height()), dp_indicator_color_right);
}
