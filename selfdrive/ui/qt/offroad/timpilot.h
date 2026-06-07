#pragma once

#include <QPushButton>
#include <QJsonArray>
#include <QStackedWidget>
#include <QWidget>
#include <QStackedLayout>

#include "selfdrive/ui/qt/widgets/controls.h"

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

class DrivingModelSelectorControl : public ButtonControl {
public:
  explicit DrivingModelSelectorControl(QWidget *parent = nullptr);
  void showEvent(QShowEvent *event) override;

private:
  void refresh();
  QJsonArray fetchInstallableModels(const QString &root);
  void selectModel();
  void installModel(const QString &root, const QString &model_id, const QString &model_name);

  Params params;
};
