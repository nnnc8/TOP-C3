#pragma once

#include <QPushButton>
#include <QStackedWidget>
#include <QWidget>
#include <QStackedLayout>

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
