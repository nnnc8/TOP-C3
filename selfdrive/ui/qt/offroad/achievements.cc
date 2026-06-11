#include "selfdrive/ui/qt/offroad/achievements.h"

#include <algorithm>
#include <cmath>

#include <QGridLayout>
#include <QHBoxLayout>
#include <QPushButton>
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

QFrame *makeMetricCard(const QString &kicker, const QString &title, QLabel **value_label, QWidget *parent = nullptr) {
  QFrame *card = makeCard("metricCard", parent);
  QVBoxLayout *layout = new QVBoxLayout(card);
  layout->setContentsMargins(32, 28, 32, 28);
  layout->setSpacing(10);

  layout->addWidget(makeLabel(kicker, 28, 700, "#6FFFE9", card));
  layout->addWidget(makeLabel(title, 34, 600, "#FFFFFF", card));
  *value_label = makeLabel("", 48, 750, "#FFE66D", card);
  (*value_label)->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  layout->addWidget(*value_label);
  return card;
}

QString badgeCardStyle(bool unlocked) {
  if (unlocked) {
    return R"(
      QFrame {
        background-color: rgba(21, 36, 42, 235);
        border: 2px solid rgba(111, 255, 233, 210);
        border-radius: 18px;
      }
      QLabel { background-color: transparent; border: none; }
    )";
  }

  return R"(
    QFrame {
      background-color: rgba(22, 24, 30, 205);
      border: 2px solid rgba(110, 118, 130, 140);
      border-radius: 18px;
    }
    QLabel { background-color: transparent; border: none; }
  )";
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
  hero_layout->setSpacing(18);

  QLabel *kicker = makeLabel(tr("AEGIS Festival Playlist"), 32, 800, "#6FFFE9", hero);
  QLabel *title = makeLabel(tr("Journey Board"), 74, 850, "#FFFFFF", hero);
  hero_layout->addWidget(kicker);
  hero_layout->addWidget(title);

  QHBoxLayout *level_layout = new QHBoxLayout();
  level_layout->setSpacing(28);
  level_value = makeLabel("", 62, 850, "#FFE66D", hero);
  xp_progress_label = makeLabel("", 36, 650, "#FFFFFF", hero);
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
  metrics->addWidget(makeMetricCard(tr("Seat Time"), tr("Assisted Time"), &time_value, this), 0, 0);
  metrics->addWidget(makeMetricCard(tr("Clean KM"), tr("Assisted Distance"), &distance_value, this), 0, 1);
  metrics->addWidget(makeMetricCard(tr("Garage Log"), tr("Route Snapshot"), &routes_value, this), 1, 0);

  QFrame *daily_card = makeMetricCard(tr("Daily Cap"), tr("Today"), &daily_xp_value, this);
  QVBoxLayout *daily_layout = qobject_cast<QVBoxLayout *>(daily_card->layout());
  daily_xp_bar = makeProgressBar(AEGIS_DAILY_XP_CAP, daily_card);
  daily_layout->addWidget(daily_xp_bar);
  metrics->addWidget(daily_card, 1, 1);
  main_layout->addLayout(metrics);

  QFrame *accolades = makeCard("sectionCard", this);
  QVBoxLayout *accolade_layout = new QVBoxLayout(accolades);
  accolade_layout->setContentsMargins(34, 32, 34, 34);
  accolade_layout->setSpacing(22);

  QHBoxLayout *accolade_header = new QHBoxLayout();
  accolade_header->addWidget(makeLabel(tr("Accolades"), 48, 800, "#FFFFFF", accolades), 1);
  accolade_summary = makeLabel("", 32, 700, "#6FFFE9", accolades);
  accolade_summary->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  accolade_header->addWidget(accolade_summary, 1);
  accolade_layout->addLayout(accolade_header);

  QGridLayout *badge_grid = new QGridLayout();
  badge_grid->setHorizontalSpacing(20);
  badge_grid->setVerticalSpacing(20);
  int badge_index = 0;
  for (const AegisBadge &badge : aegis_badge_catalog()) {
    QFrame *badge_card = makeCard("badgeCard", accolades);
    badge_card->setStyleSheet(badgeCardStyle(false));
    QVBoxLayout *badge_layout = new QVBoxLayout(badge_card);
    badge_layout->setContentsMargins(28, 24, 28, 24);
    badge_layout->setSpacing(10);

    QLabel *badge_title = makeLabel(badgeTitle(badge), 35, 800, "#FFFFFF", badge_card);
    QLabel *badge_desc = makeLabel(badgeDescription(badge), 27, 500, "#B8C4CE", badge_card);
    QLabel *badge_state = makeLabel("", 26, 800, "#88939E", badge_card);
    badge_state->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    badge_layout->addWidget(badge_title);
    badge_layout->addWidget(badge_desc, 1);
    badge_layout->addWidget(badge_state);

    badge_cards[badge.id] = badge_card;
    badge_state_labels[badge.id] = badge_state;
    badge_grid->addWidget(badge_card, badge_index / 2, badge_index % 2);
    badge_index++;
  }
  accolade_layout->addLayout(badge_grid);
  main_layout->addWidget(accolades);

  QFrame *controls = makeCard("sectionCard", this);
  QVBoxLayout *controls_layout = new QVBoxLayout(controls);
  controls_layout->setContentsMargins(34, 18, 34, 18);
  controls_layout->setSpacing(10);
  if (params.get("AegisAchievementToasts").empty()) {
    params.putBool("AegisAchievementToasts", true);
  }
  controls_layout->addWidget(new ParamControl("AegisAchievementToasts",
                                              tr("Pit Wall Toasts"),
                                              tr("Show one compact accolade banner per trip while the car is stopped and no alert is visible."),
                                              "",
                                              controls));

  ButtonControl *reset_btn = new ButtonControl(tr("Reset Journey Board"), tr("RESET"),
                                               tr("Clear XP, accolades, assisted counters, and start a fresh RouteCount snapshot."),
                                               controls);
  QObject::connect(reset_btn, &ButtonControl::clicked, [this]() { resetAchievements(); });
  controls_layout->addWidget(reset_btn);
  main_layout->addWidget(controls);
  main_layout->addStretch(1);

  setStyleSheet(R"(
    QWidget#AchievementsPanel {
      background-color: transparent;
    }
    QFrame#heroCard {
      background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                        stop:0 rgba(19, 31, 46, 255),
                                        stop:0.52 rgba(16, 47, 50, 245),
                                        stop:1 rgba(49, 38, 77, 245));
      border: 2px solid rgba(111, 255, 233, 170);
      border-radius: 24px;
    }
    QFrame#metricCard, QFrame#sectionCard {
      background-color: rgba(14, 18, 24, 220);
      border: 2px solid rgba(255, 255, 255, 45);
      border-radius: 18px;
    }
    QProgressBar {
      background-color: rgba(255, 255, 255, 42);
      border: none;
      border-radius: 12px;
    }
    QProgressBar::chunk {
      background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                                        stop:0 #6FFFE9,
                                        stop:0.55 #FFE66D,
                                        stop:1 #FF4FD8);
      border-radius: 12px;
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

QString AchievementsPanel::badgeTitle(const AegisBadge &badge) const {
  if (badge.id == "first_assist") return tr("First Assist");
  if (badge.id == "assisted_10km") return tr("10 km Assisted");
  if (badge.id == "assisted_hour") return tr("1 Hour Assisted");
  if (badge.id == "route_memory") return tr("Route Memory");
  if (badge.id == "vtsc_companion") return tr("V-TSC Companion");
  if (badge.id == "brake_hold_companion") return tr("Brake Hold Companion");
  return QString::fromStdString(badge.title);
}

QString AchievementsPanel::badgeDescription(const AegisBadge &badge) const {
  if (badge.id == "first_assist") return tr("Completed the first clean assisted minute.");
  if (badge.id == "assisted_10km") return tr("Reached 10 km of estimated assisted distance.");
  if (badge.id == "assisted_hour") return tr("Reached 1 hour of clean assisted time.");
  if (badge.id == "route_memory") return tr("Recorded a new route count snapshot.");
  if (badge.id == "vtsc_companion") return tr("Observed vision turn control while assisted.");
  if (badge.id == "brake_hold_companion") return tr("Observed brake hold while assisted.");
  return QString::fromStdString(badge.description);
}

void AchievementsPanel::refresh() {
  AegisAchievementState state = parse_aegis_achievements(params.get("AegisAchievements"));
  const int level_xp = state.xp % AEGIS_LEVEL_XP;

  level_value->setText(tr("Level %1").arg(state.level));
  xp_progress_label->setText(tr("Festival XP %1 / %2").arg(level_xp).arg(AEGIS_LEVEL_XP));
  xp_bar->setValue(level_xp);
  daily_xp_value->setText(tr("%1 / %2 XP").arg(state.daily_xp).arg(AEGIS_DAILY_XP_CAP));
  daily_xp_bar->setValue(state.daily_xp);
  time_value->setText(formatAssistedTime(state.assisted_time_s));
  distance_value->setText(formatDistance(state.assisted_distance_m));
  routes_value->setText(QString::number(state.route_count_snapshot));
  accolade_summary->setText(tr("%1 of %2 unlocked").arg(state.unlocked_badges.size()).arg(aegis_badge_catalog().size()));

  for (const AegisBadge &badge : aegis_badge_catalog()) {
    const bool unlocked = aegis_has_badge(state, badge.id);
    if (auto card = badge_cards.find(badge.id); card != badge_cards.end()) {
      card->second->setStyleSheet(badgeCardStyle(unlocked));
    }
    if (auto label = badge_state_labels.find(badge.id); label != badge_state_labels.end()) {
      label->second->setText(unlocked ? tr("UNLOCKED") : tr("LOCKED"));
      label->second->setStyleSheet(QString("font-size: 26px; font-weight: 800; color: %1; background-color: transparent; border: none;")
                                       .arg(unlocked ? "#6FFFE9" : "#88939E"));
    }
  }
}

void AchievementsPanel::resetAchievements() {
  if (!ConfirmationDialog::confirm(tr("Reset AEGIS Journey Board?"), tr("Reset"), this)) {
    return;
  }

  AegisAchievementState state = reset_aegis_achievements(params.getInt("RouteCount"));
  params.put("AegisAchievements", serialize_aegis_achievements(state));
  refresh();
}
