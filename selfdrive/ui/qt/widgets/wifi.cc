#include "selfdrive/ui/qt/widgets/wifi.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>

WiFiPromptWidget::WiFiPromptWidget(QWidget *parent) : QFrame(parent) {
  // Setup Firehose Mode
  QVBoxLayout *main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(56, 40, 56, 40);
  main_layout->setSpacing(42);  
  
  QLabel *title = new QLabel(tr("Firehose Mode"));
  title->setStyleSheet("font-size: 64px; font-weight: 650; color: #F4F6FA; background-color: transparent;");
  main_layout->addWidget(title);

  QLabel *desc = new QLabel(tr("Maximize your training data uploads to improve openpilot's driving models."));
  desc->setStyleSheet("font-size: 40px; font-weight: 400; color: #A6B0BE; background-color: transparent;");
  desc->setWordWrap(true);
  main_layout->addWidget(desc);

  QPushButton *settings_btn = new QPushButton(tr("Open"));
  connect(settings_btn, &QPushButton::clicked, [=]() { emit openSettings(1, "FirehosePanel"); });
  settings_btn->setStyleSheet(R"(
    QPushButton {
      font-size: 48px;
      font-weight: 650;
      border-radius: 12px;
      color: #07101A;
      background-color: #7AAECE;
      padding: 32px;
    }
    QPushButton:pressed {
      background-color: #497C9C;
    }
  )");
  main_layout->addWidget(settings_btn);

  setStyleSheet(R"(
    WiFiPromptWidget {
      background-color: #0D111B;
      border: 1px solid rgba(122, 174, 206, 72);
      border-radius: 14px;
    }
  )");
}
