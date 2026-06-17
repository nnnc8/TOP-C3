#pragma once

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "common/params.h"
#include "selfdrive/ui/qt/widgets/controls.h"

class FridayHealthPanel : public QWidget {
  Q_OBJECT

public:
  explicit FridayHealthPanel(QWidget *parent = nullptr);
  void showEvent(QShowEvent *event) override;

private:
  void refresh();
  void manualBackup();
  void restoreLatest();
  void resetHealthData();

  Params params;

  QLabel *lbl_score;
  QLabel *lbl_level;
  QLabel *lbl_reasons;

  // Trip stats
  QLabel *lbl_trip_temp;
  QLabel *lbl_trip_space;
  QLabel *lbl_trip_events;

  // 30 days stats
  QLabel *lbl_30d_temp;
  QLabel *lbl_30d_space;
  QLabel *lbl_30d_reboots;
  QLabel *lbl_30d_errors;

  // Backups / Settings Status
  QLabel *lbl_backups_status;
};
