#include "selfdrive/ui/qt/offroad/friday_health.h"

#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
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

FridayHealthPanel::FridayHealthPanel(QWidget *parent) : QWidget(parent) {
  setObjectName("FridayHealthPanel");
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);
  main_layout->setSpacing(28);

  // Hero Card (Health Score)
  QFrame *hero = makeCard("heroCard", this);
  QVBoxLayout *hero_layout = new QVBoxLayout(hero);
  hero_layout->setContentsMargins(42, 38, 42, 38);
  hero_layout->setSpacing(12);

  QLabel *kicker = makeLabel(tr("FRIDAY 系統狀態"), 34, 700, "#A6B0BE", hero);
  QLabel *title = makeLabel(tr("健康中心"), 60, 750, "#F4F6FA", hero);
  lbl_score = makeLabel("-分", 76, 750, "#F4F6FA", hero);
  lbl_level = makeLabel("-", 34, 600, "#8A95A5", hero);

  hero_layout->addWidget(kicker);
  hero_layout->addWidget(title);
  hero_layout->addWidget(lbl_score);
  hero_layout->addWidget(lbl_level);
  main_layout->addWidget(hero);

  // Reasons Card
  QFrame *reasons_card = makeSectionCard(tr("狀態原因與建議措施"), this);
  lbl_reasons = makeLabel("-", 34, 500, "#FFFFFF", reasons_card);
  reasons_card->layout()->addWidget(lbl_reasons);
  main_layout->addWidget(reasons_card);

  // Trip summary
  QFrame *trip_card = makeSectionCard(tr("上一趟健康摘要"), this);
  QGridLayout *trip_grid = new QGridLayout();
  trip_grid->setHorizontalSpacing(40);
  trip_grid->setVerticalSpacing(30);

  makeMetricBlock(tr("最高溫度"), &lbl_trip_temp, trip_grid, 0, 0, trip_card);
  makeMetricBlock(tr("最低儲存空間"), &lbl_trip_space, trip_grid, 0, 1, trip_card);
  makeMetricBlock(tr("輔助事件摘要"), &lbl_trip_events, trip_grid, 0, 2, trip_card);

  trip_card->layout()->addItem(trip_grid);
  main_layout->addWidget(trip_card);

  // 30 days summary
  QFrame *trend_card = makeSectionCard(tr("30天老化趨勢監控"), this);
  QGridLayout *trend_grid = new QGridLayout();
  trend_grid->setHorizontalSpacing(40);
  trend_grid->setVerticalSpacing(30);

  makeMetricBlock(tr("30天最高溫"), &lbl_30d_temp, trend_grid, 0, 0, trend_card);
  makeMetricBlock(tr("30天最低儲存"), &lbl_30d_space, trend_grid, 0, 1, trend_card);
  makeMetricBlock(tr("30天里程數與重啟"), &lbl_30d_reboots, trend_grid, 1, 0, trend_card);
  makeMetricBlock(tr("30天最大錯誤數"), &lbl_30d_errors, trend_grid, 1, 1, trend_card);

  trend_card->layout()->addItem(trend_grid);
  main_layout->addWidget(trend_card);

  // Settings Backups Zone
  QFrame *backups_card = makeSectionCard(tr("參數設定自動備份"), this);
  QVBoxLayout *backups_layout = qobject_cast<QVBoxLayout *>(backups_card->layout());

  lbl_backups_status = makeLabel("-", 34, 500, "#A6B0BE", backups_card);
  backups_layout->addWidget(lbl_backups_status);

  QHBoxLayout *btn_lay = new QHBoxLayout();
  btn_lay->setSpacing(20);

  QPushButton *btn_backup = new QPushButton(tr("手動備份"), backups_card);
  QObject::connect(btn_backup, &QPushButton::clicked, [this]() { manualBackup(); });
  btn_lay->addWidget(btn_backup);

  QPushButton *btn_restore = new QPushButton(tr("還原最新"), backups_card);
  QObject::connect(btn_restore, &QPushButton::clicked, [this]() { restoreLatest(); });
  btn_lay->addWidget(btn_restore);

  backups_layout->addLayout(btn_lay);
  main_layout->addWidget(backups_card);

  // General Controls (Reset, etc.)
  QFrame *controls = makeCard("controlsCard", this);
  QVBoxLayout *controls_layout = new QVBoxLayout(controls);
  controls_layout->setContentsMargins(34, 18, 34, 18);
  controls_layout->setSpacing(10);

  if (params.get("FridayHealthCenter").empty()) {
    params.putBool("FridayHealthCenter", true);
  }

  controls_layout->addWidget(new ParamControl("FridayHealthCenter",
                                              tr("啟用健康中心"),
                                              tr("啟用本機健康監測系統，記錄老化趨勢與設定備份。"),
                                              "../assets/icons/friday_clean_km.svg",
                                              controls));

  ButtonControl *reset_btn = new ButtonControl(tr("重設健康中心數據"), tr("重設"),
                                               tr("清除本機儲存的所有 30 天健康與老化歷史趨勢資料。"),
                                               "../assets/icons/friday_reset.svg", controls);
  QObject::connect(reset_btn, &ButtonControl::clicked, [this]() { resetHealthData(); });
  controls_layout->addWidget(reset_btn);
  main_layout->addWidget(controls);

  main_layout->addStretch(1);

  setStyleSheet(R"(
    QWidget#FridayHealthPanel {
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
    QPushButton {
      height: 96px;
      font-size: 40px;
      font-weight: 500;
      color: #FFFFFF;
      background-color: #1F232D;
      border: 1px solid rgba(255, 255, 255, 30);
      border-radius: 12px;
    }
    QPushButton:pressed {
      background-color: #313745;
    }
  )");

  refresh();
}

void FridayHealthPanel::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  refresh();
}

void FridayHealthPanel::refresh() {
  std::string val = params.get("FridayHealthState");
  QJsonDocument doc = QJsonDocument::fromJson(QString::fromStdString(val).toUtf8());
  QJsonObject obj = doc.object();

  int score = obj["score"].toInt(100);
  QString level = obj["level"].toString("normal");
  QJsonArray reasons_arr = obj["reasons"].toArray();
  QJsonArray critical_arr = obj["critical"].toArray();

  lbl_score->setText(tr("%1分").arg(score));
  
  if (level == "critical") {
    lbl_level->setText(tr("嚴重警告 (Critical)"));
    lbl_level->setStyleSheet("font-size: 34px; font-weight: 600; color: #FF5A60; background-color: transparent;");
  } else if (level == "warning") {
    lbl_level->setText(tr("提示警告 (Warning)"));
    lbl_level->setStyleSheet("font-size: 34px; font-weight: 600; color: #FFC107; background-color: transparent;");
  } else {
    lbl_level->setText(tr("狀態優良 (Normal)"));
    lbl_level->setStyleSheet("font-size: 34px; font-weight: 600; color: #4CAF50; background-color: transparent;");
  }

  // Actionable Reasons List
  QStringList reasons;
  for (const QJsonValue &r : critical_arr) {
    reasons.append("<font color='#FF5A60'><b>⚠️ " + r.toString() + "</b></font>");
  }
  for (const QJsonValue &r : reasons_arr) {
    reasons.append("• " + r.toString());
  }
  if (reasons.isEmpty()) {
    reasons.append(tr("系統狀態良好，無任何異常警告"));
  }
  lbl_reasons->setText(reasons.join("<br>"));

  // Last Trip Summaries
  QJsonObject trip = obj["trip"].toObject();
  QJsonObject last = trip["last"].toObject();

  double max_temp = last["max_temp"].toDouble(0.0);
  double min_space = last["min_space"].toDouble(100.0);
  int alerts = last["alert_count"].toInt(0);
  int interventions = last["manual_intervention_count"].toInt(0);
  int hard_brakes = last["hard_brake_count"].toInt(0);

  lbl_trip_temp->setText(tr("%1 °C").arg(QString::number(max_temp, 'f', 1)));
  lbl_trip_space->setText(tr("%1 %").arg(QString::number(min_space, 'f', 1)));
  lbl_trip_events->setText(tr("警告: %1 / 介入: %2 / 急煞: %3").arg(alerts).arg(interventions).arg(hard_brakes));

  // 30 Days trend Summary
  QJsonArray daily_buckets = obj["daily_buckets"].toArray();
  double max_temp_30d = 0.0;
  double min_space_30d = 100.0;
  int reboots_30d = 0;
  int route_count_30d = 0;
  int error_log_count_30d = 0;

  for (const QJsonValue &b : daily_buckets) {
    QJsonObject bucket = b.toObject();
    max_temp_30d = std::max(max_temp_30d, bucket["max_temp"].toDouble(0.0));
    min_space_30d = std::min(min_space_30d, bucket["min_space"].toDouble(100.0));
    reboots_30d += bucket["reboot_count"].toInt(0);
    route_count_30d += bucket["route_count"].toInt(0);
    error_log_count_30d = std::max(error_log_count_30d, bucket["error_log_count"].toInt(0));
  }

  lbl_30d_temp->setText(tr("%1 °C").arg(QString::number(max_temp_30d, 'f', 1)));
  lbl_30d_space->setText(tr("%1 %").arg(QString::number(min_space_30d, 'f', 1)));
  lbl_30d_reboots->setText(tr("%1 趟旅程 / %2 重啟").arg(route_count_30d).arg(reboots_30d));
  lbl_30d_errors->setText(tr("%1 次錯誤記錄").arg(error_log_count_30d));

  // Backups Status
  std::string backups_val = params.get("FridayHealthSettingsBackups");
  QJsonDocument backups_doc = QJsonDocument::fromJson(QString::fromStdString(backups_val).toUtf8());
  QJsonArray backups_arr = backups_doc.array();
  if (backups_arr.isEmpty()) {
    lbl_backups_status->setText(tr("尚無任何備份記錄"));
  } else {
    QJsonObject latest = backups_arr.last().toObject();
    qint64 t = latest["time"].toVariant().toLongLong();
    QString commit = latest["commit"].toString("unknown").left(7);
    QDateTime date = QDateTime::fromSecsSinceEpoch(t);
    lbl_backups_status->setText(tr("備份總數: %1 / 最新備份: %2 (%3)").arg(backups_arr.size()).arg(date.toString("yyyy-MM-dd hh:mm")).arg(commit));
  }
}

void FridayHealthPanel::manualBackup() {
  // Execute manual backup through python command call or directly in python helper (invoking python is cleaner for consistency)
  if (ConfirmationDialog::confirm(tr("確定要立即建立手動設定備份？"), tr("備份"), this)) {
    // We can run Python code inline via python -c
    std::string cmd = ".venv/bin/python -c 'from openpilot.common.params import Params; from openpilot.system.friday_health.model import check_and_create_backup; check_and_create_backup(Params(), force=True)'";
    system(cmd.c_str());
    refresh();
  }
}

void FridayHealthPanel::restoreLatest() {
  std::string backups_val = params.get("FridayHealthSettingsBackups");
  QJsonDocument backups_doc = QJsonDocument::fromJson(QString::fromStdString(backups_val).toUtf8());
  QJsonArray backups_arr = backups_doc.array();
  if (backups_arr.isEmpty()) {
    ConfirmationDialog::alert(tr("尚無備份記錄可供還原。"), this);
    return;
  }

  if (ConfirmationDialog::confirm(tr("確定要將設定還原到最新備份狀態？這將會覆蓋您目前的參數設定！"), tr("還原"), this)) {
    std::string cmd = ".venv/bin/python -c 'from openpilot.common.params import Params; from openpilot.system.friday_health.model import restore_backup, load_backups; p = Params(); backups = load_backups(p); restore_backup(p, len(backups)-1)'";
    system(cmd.c_str());
    refresh();
    ConfirmationDialog::alert(tr("設定還原完成！"), this);
  }
}

void FridayHealthPanel::resetHealthData() {
  if (!ConfirmationDialog::confirm(tr("確定要清除所有健康與老化歷史趨勢資料？"), tr("重設"), this)) {
    return;
  }
  std::string cmd = ".venv/bin/python -c 'from openpilot.common.params import Params; from openpilot.system.friday_health.model import get_default_health_state; import json; Params().put(\"FridayHealthState\", json.dumps(get_default_health_state()))'";
  system(cmd.c_str());
  refresh();
}
