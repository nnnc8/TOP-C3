#include "selfdrive/ui/qt/offroad/journey_board.h"

#include <algorithm>
#include <cmath>

#include <QGridLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QPixmap>
#include <QShowEvent>
#include <QSizePolicy>
#include <QVBoxLayout>

#include "selfdrive/ui/qt/util.h"

namespace {

QLabel *makeLabel(const QString &text, int font_size, int weight, const QString &color, QWidget *parent = nullptr) {
  QLabel *label = new QLabel(text, parent);
  label->setWordWrap(true);
  label->setStyleSheet(QString("font-size: %1px; font-weight: %2; color: %3; background-color: transparent;")
                           .arg(font_size)
                           .arg(weight)
                           .arg(color));
  return label;
}

QFrame *makeCard(const QString &object_name, QWidget *parent = nullptr) {
  QFrame *card = new QFrame(parent);
  card->setObjectName(object_name);
  card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  return card;
}

QFrame *makeSectionCard(const QString &title, QWidget *parent = nullptr) {
  QFrame *card = makeCard("sectionCard", parent);
  QVBoxLayout *layout = new QVBoxLayout(card);
  layout->setContentsMargins(36, 28, 36, 28);
  layout->setSpacing(24);

  QLabel *lbl_title = makeLabel(title, 44, 700, "#F4F6FA", card);
  layout->addWidget(lbl_title);
  return card;
}

void makeMetricBlock(const QString &name, QLabel **val_lbl, QGridLayout *grid, int row, int col, QWidget *parent = nullptr) {
  QVBoxLayout *layout = new QVBoxLayout();
  layout->setSpacing(6);
  layout->setContentsMargins(0, 0, 0, 0);

  QLabel *lbl_name = makeLabel(name, 32, 600, "#8A95A5", parent);
  *val_lbl = makeLabel("-", 44, 700, "#FFFFFF", parent);

  layout->addWidget(lbl_name);
  layout->addWidget(*val_lbl);
  grid->addLayout(layout, row, col);
}

}  // namespace

JourneyBoardPanel::JourneyBoardPanel(QWidget *parent) : QWidget(parent) {
  setObjectName("JourneyBoardPanel");
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);
  main_layout->setSpacing(28);

  // Hero Card
  QFrame *hero = makeCard("heroCard", this);
  QVBoxLayout *hero_layout = new QVBoxLayout(hero);
  hero_layout->setContentsMargins(42, 38, 42, 38);
  hero_layout->setSpacing(12);

  QLabel *kicker = makeLabel(tr("FRIDAY 旅程資料"), 34, 700, "#A6B0BE", hero);
  QLabel *title = makeLabel(tr("旅程看板"), 60, 750, "#F4F6FA", hero);
  QLabel *desc = makeLabel(tr("純旅程遙測與輔助品質追蹤系統"), 34, 500, "#8A95A5", hero);

  hero_layout->addWidget(kicker);
  hero_layout->addWidget(title);
  hero_layout->addWidget(desc);
  main_layout->addWidget(hero);

  // Zone 1: Totals
  QFrame *totals_card = makeSectionCard(tr("總累積數據"), this);
  QGridLayout *totals_grid = new QGridLayout();
  totals_grid->setHorizontalSpacing(40);
  totals_grid->setVerticalSpacing(30);

  makeMetricBlock(tr("路線數快照"), &total_routes_val, totals_grid, 0, 0, totals_card);
  makeMetricBlock(tr("總旅程數"), &total_trips_val, totals_grid, 0, 1, totals_card);
  makeMetricBlock(tr("總行駛時間"), &total_drive_time_val, totals_grid, 0, 2, totals_card);

  makeMetricBlock(tr("總移動時間"), &total_moving_time_val, totals_grid, 1, 0, totals_card);
  makeMetricBlock(tr("總移動里程"), &total_moving_dist_val, totals_grid, 1, 1, totals_card);
  makeMetricBlock(tr("總輔助時間"), &total_assist_time_val, totals_grid, 1, 2, totals_card);

  makeMetricBlock(tr("總輔助里程"), &total_assist_dist_val, totals_grid, 2, 0, totals_card);
  makeMetricBlock(tr("總輔助占比"), &total_assist_ratio_val, totals_grid, 2, 1, totals_card);
  makeMetricBlock(tr("總介入次數"), &total_interv_val, totals_grid, 2, 2, totals_card);

  makeMetricBlock(tr("總警告次數"), &total_alerts_val, totals_grid, 3, 0, totals_card);
  makeMetricBlock(tr("最高速度"), &total_top_speed_val, totals_grid, 3, 1, totals_card);
  makeMetricBlock(tr("輔助極速"), &total_assist_top_speed_val, totals_grid, 3, 2, totals_card);

  totals_card->layout()->addItem(totals_grid);
  main_layout->addWidget(totals_card);

  // Zone 2: Last Trip
  QFrame *last_card = makeSectionCard(tr("上一趟旅程"), this);
  QGridLayout *last_grid = new QGridLayout();
  last_grid->setHorizontalSpacing(40);
  last_grid->setVerticalSpacing(30);

  makeMetricBlock(tr("行駛時間"), &last_drive_time_val, last_grid, 0, 0, last_card);
  makeMetricBlock(tr("移動時間"), &last_moving_time_val, last_grid, 0, 1, last_card);
  makeMetricBlock(tr("移動里程"), &last_moving_dist_val, last_grid, 0, 2, last_card);

  makeMetricBlock(tr("輔助時間"), &last_assist_time_val, last_grid, 1, 0, last_card);
  makeMetricBlock(tr("輔助里程"), &last_assist_dist_val, last_grid, 1, 1, last_card);
  makeMetricBlock(tr("輔助占比"), &last_assist_ratio_val, last_grid, 1, 2, last_card);

  makeMetricBlock(tr("介入次數"), &last_interv_val, last_grid, 2, 0, last_card);
  makeMetricBlock(tr("警告次數"), &last_alerts_val, last_grid, 2, 1, last_card);
  makeMetricBlock(tr("最高速度"), &last_top_speed_val, last_grid, 2, 2, last_card);

  last_card->layout()->addItem(last_grid);
  main_layout->addWidget(last_card);

  // Zone 3: Last 7 Days
  QFrame *trend_card = makeSectionCard(tr("近七天趨勢"), this);
  QGridLayout *trend_grid = new QGridLayout();
  trend_grid->setHorizontalSpacing(40);
  trend_grid->setVerticalSpacing(30);

  makeMetricBlock(tr("總旅程數"), &trend_trips_val, trend_grid, 0, 0, trend_card);
  makeMetricBlock(tr("累積移動里程"), &trend_dist_val, trend_grid, 0, 1, trend_card);
  makeMetricBlock(tr("累積輔助時間"), &trend_assist_time_val, trend_grid, 0, 2, trend_card);

  makeMetricBlock(tr("累積輔助里程"), &trend_assist_dist_val, trend_grid, 1, 0, trend_card);
  makeMetricBlock(tr("輔助占比"), &trend_assist_ratio_val, trend_grid, 1, 1, trend_card);
  makeMetricBlock(tr("平均每趟介入"), &trend_avg_interv_val, trend_grid, 1, 2, trend_card);
  makeMetricBlock(tr("平均每趟警告"), &trend_avg_alerts_val, trend_grid, 1, 3, trend_card);

  trend_card->layout()->addItem(trend_grid);
  main_layout->addWidget(trend_card);

  // Controls Zone
  QFrame *controls = makeCard("controlsCard", this);
  QVBoxLayout *controls_layout = new QVBoxLayout(controls);
  controls_layout->setContentsMargins(34, 18, 34, 18);
  controls_layout->setSpacing(10);

  ButtonControl *reset_btn = new ButtonControl(tr("重置旅程看板"), tr("重置"),
                                               tr("清除所有旅程遙測與輔助累積資料，並重新建立目前路線數快照。"),
                                               "../assets/icons/friday_reset.svg", controls);
  QObject::connect(reset_btn, &ButtonControl::clicked, [this]() { resetJourneyBoard(); });
  controls_layout->addWidget(reset_btn);
  main_layout->addWidget(controls);
  main_layout->addStretch(1);

  setStyleSheet(R"(
    QWidget#JourneyBoardPanel {
      background-color: transparent;
    }
    QFrame#heroCard {
      background-color: #15171C;
      border: 1px solid rgba(255, 255, 255, 52);
      border-radius: 12px;
    }
    QFrame#sectionCard, QFrame#controlsCard {
      background-color: #15171C;
      border: 1px solid rgba(255, 255, 255, 42);
      border-radius: 12px;
    }
  )");

  refresh();
}

void JourneyBoardPanel::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  refresh();
}

QString JourneyBoardPanel::formatTime(double seconds) const {
  const int total_minutes = static_cast<int>(std::floor(std::max(0.0, seconds) / 60.0));
  const int hours = total_minutes / 60;
  const int minutes = total_minutes % 60;
  if (hours > 0) {
    return tr("%1 h %2 min").arg(hours).arg(minutes);
  }
  return tr("%1 min").arg(minutes);
}

QString JourneyBoardPanel::formatDistance(double meters) const {
  return tr("%1 km").arg(QString::number(std::max(0.0, meters) / 1000.0, 'f', 1));
}

QString JourneyBoardPanel::formatSpeed(double speed_mps) const {
  return tr("%1 km/h").arg(QString::number(std::max(0.0, speed_mps) * 3.6, 'f', 0));
}

QString JourneyBoardPanel::formatRatio(double ratio) const {
  return tr("%1%").arg(static_cast<int>(std::round(std::clamp(ratio, 0.0, 1.0) * 100.0)));
}

void JourneyBoardPanel::refresh() {
  FridayJourneyBoardState state = load_journey_board(params);

  // Totals
  total_routes_val->setText(QString::number(state.totals.route_count_snapshot));
  total_trips_val->setText(QString::number(state.totals.trip_count));
  total_drive_time_val->setText(formatTime(state.totals.drive_time_s));
  total_moving_time_val->setText(formatTime(state.totals.moving_time_s));
  total_moving_dist_val->setText(formatDistance(state.totals.moving_distance_m));
  total_assist_time_val->setText(formatTime(state.totals.assisted_time_s));
  total_assist_dist_val->setText(formatDistance(state.totals.assisted_distance_m));
  total_assist_ratio_val->setText(formatRatio(get_assist_ratio(state.totals.assisted_moving_time_s, state.totals.moving_time_s)));
  total_interv_val->setText(QString::number(state.totals.intervention_count));
  total_alerts_val->setText(QString::number(state.totals.alert_count));
  total_top_speed_val->setText(formatSpeed(state.totals.top_speed_mps));
  total_assist_top_speed_val->setText(formatSpeed(state.totals.assisted_top_speed_mps));

  // Last Trip
  last_drive_time_val->setText(formatTime(state.last_trip.drive_time_s));
  last_moving_time_val->setText(formatTime(state.last_trip.moving_time_s));
  last_moving_dist_val->setText(formatDistance(state.last_trip.moving_distance_m));
  last_assist_time_val->setText(formatTime(state.last_trip.assisted_time_s));
  last_assist_dist_val->setText(formatDistance(state.last_trip.assisted_distance_m));
  last_assist_ratio_val->setText(formatRatio(get_assist_ratio(state.last_trip.assisted_moving_time_s, state.last_trip.moving_time_s)));
  last_interv_val->setText(QString::number(state.last_trip.intervention_count));
  last_alerts_val->setText(QString::number(state.last_trip.alert_count));
  last_top_speed_val->setText(formatSpeed(state.last_trip.top_speed_mps));

  // Trend (Last 7 Days)
  int total_trips_7d = 0;
  double total_dist_7d = 0.0;
  double total_assist_time_7d = 0.0;
  double total_assist_dist_7d = 0.0;
  double total_moving_time_7d = 0.0;
  double total_assisted_moving_time_7d = 0.0;
  int total_alerts_7d = 0;
  int total_interventions_7d = 0;

  for (const auto &b : state.daily_buckets) {
    total_trips_7d += b.trip_count;
    total_dist_7d += b.moving_distance_m;
    total_assist_time_7d += b.assisted_time_s;
    total_assist_dist_7d += b.assisted_distance_m;
    total_moving_time_7d += b.moving_time_s;
    total_assisted_moving_time_7d += b.assisted_moving_time_s;
    total_alerts_7d += b.alert_count;
    total_interventions_7d += b.intervention_count;
  }

  double assist_ratio_7d = get_assist_ratio(total_assisted_moving_time_7d, total_moving_time_7d);

  double avg_interventions_7d = 0.0;
  double avg_alerts_7d = 0.0;
  if (total_trips_7d > 0) {
    avg_interventions_7d = static_cast<double>(total_interventions_7d) / total_trips_7d;
    avg_alerts_7d = static_cast<double>(total_alerts_7d) / total_trips_7d;
  }

  trend_trips_val->setText(QString::number(total_trips_7d));
  trend_dist_val->setText(formatDistance(total_dist_7d));
  trend_assist_time_val->setText(formatTime(total_assist_time_7d));
  trend_assist_dist_val->setText(formatDistance(total_assist_dist_7d));
  trend_assist_ratio_val->setText(formatRatio(assist_ratio_7d));
  trend_avg_interv_val->setText(QString::number(avg_interventions_7d, 'f', 1));
  trend_avg_alerts_val->setText(QString::number(avg_alerts_7d, 'f', 1));
}

void JourneyBoardPanel::resetJourneyBoard() {
  if (!ConfirmationDialog::confirm(tr("確定要重置旅程看板遙測數據？"), tr("重置"), this)) {
    return;
  }

  FridayJourneyBoardState state = reset_friday_journey_board(params.getInt("RouteCount"));
  params.put("FridayJourneyBoard", serialize_friday_journey_board(state));
  refresh();
}
