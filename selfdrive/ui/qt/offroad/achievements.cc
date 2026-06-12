#include "selfdrive/ui/qt/offroad/achievements.h"

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

QProgressBar *makeProgressBar(int maximum, QWidget *parent = nullptr) {
  QProgressBar *bar = new QProgressBar(parent);
  bar->setRange(0, maximum);
  bar->setTextVisible(false);
  bar->setFixedHeight(24);
  return bar;
}

QLabel *makeIconLabel(const QString &icon, int width, QWidget *parent = nullptr) {
  QLabel *label = new QLabel(parent);
  label->setFixedSize(width, width);
  label->setAlignment(Qt::AlignCenter);
  QPixmap pixmap(icon);
  if (!pixmap.isNull()) {
    label->setPixmap(pixmap.scaled(width, width, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  }
  return label;
}

QFrame *makeMetricCard(const QString &kicker, const QString &title, const QString &icon, QLabel **value_label, QWidget *parent = nullptr) {
  QFrame *card = makeCard("metricCard", parent);
  QVBoxLayout *layout = new QVBoxLayout(card);
  layout->setContentsMargins(32, 28, 32, 28);
  layout->setSpacing(10);

  QHBoxLayout *header = new QHBoxLayout();
  header->setSpacing(18);
  header->addWidget(makeIconLabel(icon, 56, card), 0, Qt::AlignTop);

  QVBoxLayout *label_stack = new QVBoxLayout();
  label_stack->setContentsMargins(0, 0, 0, 0);
  label_stack->setSpacing(4);
  label_stack->addWidget(makeLabel(kicker, 24, 650, "#A6B0BE", card));
  label_stack->addWidget(makeLabel(title, 32, 600, "#F4F6FA", card));
  header->addLayout(label_stack, 1);
  layout->addLayout(header);

  *value_label = makeLabel("", 44, 700, "#F4F6FA", card);
  (*value_label)->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  layout->addWidget(*value_label);
  return card;
}

}  // namespace

AchievementsPanel::AchievementsPanel(QWidget *parent) : QWidget(parent) {
  setObjectName("AchievementsPanel");
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);
  main_layout->setSpacing(28);

  QFrame *hero = makeCard("heroCard", this);
  QVBoxLayout *hero_layout = new QVBoxLayout(hero);
  hero_layout->setContentsMargins(42, 38, 42, 38);
  hero_layout->setSpacing(16);

  QLabel *kicker = makeLabel(tr("AEGIS 旅程資料"), 26, 700, "#A6B0BE", hero);
  QLabel *title = makeLabel(tr("遙測看板"), 56, 750, "#F4F6FA", hero);
  hero_layout->addWidget(kicker);
  hero_layout->addWidget(title);

  QHBoxLayout *level_layout = new QHBoxLayout();
  level_layout->setSpacing(28);
  level_value = makeLabel("", 48, 700, "#F4F6FA", hero);
  xp_progress_label = makeLabel("", 30, 550, "#A6B0BE", hero);
  xp_progress_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  level_layout->addWidget(level_value, 1);
  level_layout->addWidget(xp_progress_label, 1);
  hero_layout->addLayout(level_layout);

  xp_bar = makeProgressBar(AEGIS_LEVEL_XP, hero);
  hero_layout->addWidget(xp_bar);
  main_layout->addWidget(hero);

  QGridLayout *metrics = new QGridLayout();
  metrics->setHorizontalSpacing(24);
  metrics->setVerticalSpacing(24);
  metrics->addWidget(makeMetricCard(tr("累積行程"), tr("路線數"), "../assets/icons/aegis_route_snapshot.svg", &routes_value, this), 0, 0);
  metrics->addWidget(makeMetricCard(tr("累積里程"), tr("總移動里程"), "../assets/icons/aegis_clean_km.svg", &moving_distance_value, this), 0, 1);
  metrics->addWidget(makeMetricCard(tr("累積時間"), tr("總行駛時間"), "../assets/icons/aegis_seat_time.svg", &drive_time_value, this), 1, 0);
  metrics->addWidget(makeMetricCard(tr("移動時間"), tr("實際移動時間"), "../assets/icons/aegis_pit_wall.svg", &moving_time_value, this), 1, 1);
  metrics->addWidget(makeMetricCard(tr("平均速度"), tr("移動均速"), "../assets/icons/aegis_metric.svg", &average_speed_value, this), 2, 0);
  metrics->addWidget(makeMetricCard(tr("最高速度"), tr("峰值速度"), "../assets/icons/aegis_accel.svg", &top_speed_value, this), 2, 1);
  metrics->addWidget(makeMetricCard(tr("輔助里程"), tr("openpilot 里程"), "../assets/icons/aegis_enable.svg", &assisted_distance_value, this), 3, 0);
  metrics->addWidget(makeMetricCard(tr("輔助時間"), tr("openpilot 時間"), "../assets/icons/aegis_driver_monitor.svg", &assisted_time_value, this), 3, 1);
  metrics->addWidget(makeMetricCard(tr("輔助均速"), tr("openpilot 均速"), "../assets/icons/aegis_follow.svg", &assisted_average_speed_value, this), 4, 0);
  metrics->addWidget(makeMetricCard(tr("輔助極速"), tr("openpilot 最高"), "../assets/icons/aegis_vision_turn.svg", &assisted_top_speed_value, this), 4, 1);

  QFrame *daily_card = makeMetricCard(tr("今日上限"), tr("今日 XP"), "../assets/icons/aegis_daily_cap.svg", &daily_xp_value, this);
  QVBoxLayout *daily_layout = qobject_cast<QVBoxLayout *>(daily_card->layout());
  daily_xp_bar = makeProgressBar(AEGIS_DAILY_XP_CAP, daily_card);
  daily_layout->addWidget(daily_xp_bar);
  metrics->addWidget(daily_card, 5, 0, 1, 2);
  main_layout->addLayout(metrics);

  QFrame *controls = makeCard("sectionCard", this);
  QVBoxLayout *controls_layout = new QVBoxLayout(controls);
  controls_layout->setContentsMargins(34, 18, 34, 18);
  controls_layout->setSpacing(10);
  if (params.get("AegisAchievementToasts").empty()) {
    params.putBool("AegisAchievementToasts", true);
  }
  controls_layout->addWidget(new ParamControl("AegisAchievementToasts",
                                              tr("旅程提示"),
                                              tr("車輛停止且沒有警告時，每趟最多顯示一次小型旅程里程碑提示。"),
                                              "../assets/icons/aegis_pit_wall.svg",
                                              controls));

  ButtonControl *reset_btn = new ButtonControl(tr("重置遙測看板"), tr("重置"),
                                               tr("清除 XP、旅程遙測、輔助累積資料，並重新建立目前路線數快照。"),
                                               "../assets/icons/aegis_reset.svg", controls);
  QObject::connect(reset_btn, &ButtonControl::clicked, [this]() { resetAchievements(); });
  controls_layout->addWidget(reset_btn);
  main_layout->addWidget(controls);
  main_layout->addStretch(1);

  setStyleSheet(R"(
    QWidget#AchievementsPanel {
      background-color: transparent;
    }
    QFrame#heroCard {
      background-color: #15171C;
      border: 1px solid rgba(255, 255, 255, 52);
      border-radius: 12px;
    }
    QFrame#metricCard, QFrame#sectionCard {
      background-color: #15171C;
      border: 1px solid rgba(255, 255, 255, 42);
      border-radius: 12px;
    }
    QProgressBar {
      background-color: #161D2C;
      border: none;
      border-radius: 10px;
    }
    QProgressBar::chunk {
      background-color: #7AAECE;
      border-radius: 10px;
    }
  )");

  refresh();
}

void AchievementsPanel::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  refresh();
}

QString AchievementsPanel::formatAssistedTime(double seconds) const {
  const int total_minutes = static_cast<int>(std::floor(std::max(0.0, seconds) / 60.0));
  const int hours = total_minutes / 60;
  const int minutes = total_minutes % 60;
  if (hours > 0) {
    return tr("%1 h %2 min").arg(hours).arg(minutes);
  }
  return tr("%1 min").arg(minutes);
}

QString AchievementsPanel::formatDistance(double meters) const {
  return tr("%1 km").arg(QString::number(std::max(0.0, meters) / 1000.0, 'f', 1));
}

QString AchievementsPanel::formatSpeed(double speed_mps) const {
  return tr("%1 km/h").arg(QString::number(std::max(0.0, speed_mps) * 3.6, 'f', 0));
}

QString AchievementsPanel::formatAverageSpeed(double meters, double seconds) const {
  if (seconds <= 1.0) {
    return tr("0 km/h");
  }
  return formatSpeed(meters / seconds);
}

void AchievementsPanel::refresh() {
  AegisAchievementState state = parse_aegis_achievements(params.get("AegisAchievements"));
  const int level_xp = state.xp % AEGIS_LEVEL_XP;

  level_value->setText(tr("等級 %1").arg(state.level));
  xp_progress_label->setText(tr("旅程 XP %1 / %2").arg(level_xp).arg(AEGIS_LEVEL_XP));
  xp_bar->setValue(level_xp);
  daily_xp_value->setText(tr("%1 / %2 XP").arg(state.daily_xp).arg(AEGIS_DAILY_XP_CAP));
  daily_xp_bar->setValue(state.daily_xp);
  drive_time_value->setText(formatAssistedTime(state.drive_time_s));
  moving_time_value->setText(formatAssistedTime(state.moving_time_s));
  moving_distance_value->setText(formatDistance(state.moving_distance_m));
  average_speed_value->setText(formatAverageSpeed(state.moving_distance_m, state.moving_time_s));
  top_speed_value->setText(formatSpeed(state.top_speed_mps));
  assisted_time_value->setText(formatAssistedTime(state.assisted_time_s));
  assisted_distance_value->setText(formatDistance(state.assisted_distance_m));
  assisted_average_speed_value->setText(formatAverageSpeed(state.assisted_distance_m, state.assisted_time_s));
  assisted_top_speed_value->setText(formatSpeed(state.assisted_top_speed_mps));
  routes_value->setText(QString::number(state.route_count_snapshot));
}

void AchievementsPanel::resetAchievements() {
  if (!ConfirmationDialog::confirm(tr("Reset AEGIS telemetry board?"), tr("Reset"), this)) {
    return;
  }

  AegisAchievementState state = reset_aegis_achievements(params.getInt("RouteCount"));
  params.put("AegisAchievements", serialize_aegis_achievements(state));
  refresh();
}
