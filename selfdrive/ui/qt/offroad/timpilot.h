#pragma once

#include <QJsonArray>
#include <QProcess>
#include <QPushButton>
#include <QStackedWidget>
#include <QWidget>
#include <QStackedLayout>

#include "selfdrive/ui/qt/widgets/controls.h"

class DrivingModelSelectorControl : public ButtonControl {
  Q_OBJECT

public:
  explicit DrivingModelSelectorControl(QWidget *parent = nullptr);
  void showEvent(QShowEvent *event) override;
  void refresh();

private:
  enum class Operation { None, List, Sync, Install };

  void selectModel();
  void startList();
  void startSync();
  void startInstall(const QString &model_id, const QString &model_name);
  void processOutput();
  void handleProgressLine(const QByteArray &line);
  void processFinished(int exit_code, QProcess::ExitStatus exit_status);
  void processError(QProcess::ProcessError error);
  void writeLog(const QByteArray &output);
  QString openpilotRoot() const;
  QString cacheRoot() const;
  QString logPath(const QString &root) const;

  Params params;
  QProcess *process = nullptr;
  Operation operation = Operation::None;
  QString pending_model_name;
  QString root;
  QString log_file;
  QByteArray operation_output;
  QByteArray pending_output;
};

class ForceCarRecognition : public QWidget
{
  Q_OBJECT

public:
  explicit ForceCarRecognition(QWidget* parent = 0);

private:

signals:
  void backPress();
  void selectedCar();
};
