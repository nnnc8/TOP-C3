#include "selfdrive/ui/qt/offroad/timpilot.h"

#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <QComboBox>
#include <QAbstractItemView>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QMetaObject>
#include <QScroller>
#include <QListView>
#include <QListWidget>
#include <QProcess>
#include <QDir>
#include <QTextStream>

#include "common/params.h"
#include "selfdrive/ui/qt/api.h"
#include "selfdrive/ui/qt/widgets/input.h"

#include "selfdrive/ui/ui.h"

namespace {

QString openpilotRoot() {
  if (QFile::exists("/data/openpilot/tools/model_selector.py")) {
    return "/data/openpilot";
  }

  const QString current_path = QDir::currentPath();
  if (QFile::exists(QDir(current_path).filePath("tools/model_selector.py"))) {
    return current_path;
  }

  QDir app_dir(QCoreApplication::applicationDirPath());
  if (app_dir.cdUp() && app_dir.cdUp() && QFile::exists(app_dir.filePath("tools/model_selector.py"))) {
    return app_dir.absolutePath();
  }

  return current_path;
}

QString modelSelectorLogPath(const QString &root) {
  return QFile::exists("/data") ? "/data/model_selector.log" : QDir(root).filePath("model_selector.log");
}

}  // namespace

DrivingModelSelectorControl::DrivingModelSelectorControl(QWidget *parent) : ButtonControl(
    QObject::tr("Driving Model"),
    QObject::tr("SELECT"),
    QObject::tr("Download, verify, compile, and install a signed driving model bundle from openpilot-models. Install only while offroad."),
    parent) {
  refresh();
  QObject::connect(this, &ButtonControl::clicked, [this]() { selectModel(); });
}

void DrivingModelSelectorControl::showEvent(QShowEvent *event) {
  refresh();
  QFrame::showEvent(event);
}

void DrivingModelSelectorControl::refresh() {
  const QString name = QString::fromStdString(params.get("DrivingModelName"));
  setValue(name.isEmpty() ? QObject::tr("Stock") : name);
}

QJsonArray DrivingModelSelectorControl::fetchInstallableModels(const QString &root) {
  QProcess process;
  process.setWorkingDirectory(root);
  process.start("python3", QStringList() << "tools/model_selector.py" << "list" << "--format" << "json");
  if (!process.waitForFinished(30000) || process.exitCode() != 0) {
    const QString error = QString::fromUtf8(process.readAllStandardError()).trimmed();
    throw std::runtime_error(error.isEmpty() ? "Unable to fetch model list." : error.toStdString());
  }

  QJsonParseError parse_error;
  QJsonDocument doc = QJsonDocument::fromJson(process.readAllStandardOutput(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !doc.isArray()) {
    throw std::runtime_error("Model list response was not valid JSON.");
  }
  return doc.array();
}

void DrivingModelSelectorControl::selectModel() {
  if (uiState()->engaged()) {
    ConfirmationDialog::alert(QObject::tr("Disengage to install a driving model."), this);
    return;
  }

  const QString root = openpilotRoot();
  QJsonArray models;
  try {
    models = fetchInstallableModels(root);
  } catch (const std::exception &exc) {
    ConfirmationDialog::alert(QObject::tr("Unable to load model list: %1").arg(exc.what()), this);
    return;
  }

  if (models.isEmpty()) {
    ConfirmationDialog::alert(QObject::tr("No installable driving models found."), this);
    return;
  }

  QStringList labels;
  QMap<QString, QString> ids_by_label;
  QMap<QString, QString> names_by_label;
  QString current_label;
  const QString current_id = QString::fromStdString(params.get("DrivingModel"));
  for (const QJsonValue &value : models) {
    const QJsonObject model = value.toObject();
    const QString id = model["id"].toString();
    const QString name = model["name"].toString(id);
    const double size_mb = model["size_mb"].toDouble();
    const QString added_at = model["added_at"].toString();
    const QString label = QString("%1 (%2 MB, %3)").arg(name, QString::number(size_mb, 'f', 1), added_at);
    labels.append(label);
    ids_by_label[label] = id;
    names_by_label[label] = name;
    if (id == current_id) {
      current_label = label;
    }
  }

  const QString selection = MultiOptionDialog::getSelection(QObject::tr("Select Driving Model"), labels, current_label, this);
  if (selection.isEmpty()) {
    return;
  }

  const QString model_id = ids_by_label.value(selection);
  const QString model_name = names_by_label.value(selection);
  if (!ConfirmationDialog::confirm(QObject::tr("Install %1? openpilot will restart after installation.").arg(model_name), QObject::tr("Install"), this)) {
    return;
  }

  installModel(root, model_id, model_name);
}

void DrivingModelSelectorControl::installModel(const QString &root, const QString &model_id, const QString &model_name) {
  setEnabled(false);
  setValue(QObject::tr("Installing..."));

  std::thread([this, root, model_id, model_name]() {
    QProcess process;
    process.setWorkingDirectory(root);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start("python3", QStringList() << "tools/model_selector.py" << "install" << model_id << "--repo-root" << root << "--python-bin" << "python3");
    const bool finished = process.waitForFinished(-1);
    const int exit_code = process.exitCode();
    const QString output = QString::fromUtf8(process.readAll());
    const QString log_path = modelSelectorLogPath(root);

    QFile log_file(log_path);
    if (log_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      QTextStream out(&log_file);
      out << output;
    }

    QMetaObject::invokeMethod(this, [this, finished, exit_code, model_name, log_path]() {
      setEnabled(true);
      if (finished && exit_code == 0) {
        params.put("DrivingModelName", model_name.toStdString());
        params.putBool("OnroadCycleRequested", true);
        refresh();
        ConfirmationDialog::alert(QObject::tr("Installed %1. Log: %2").arg(model_name, log_path), this);
      } else {
        setValue(QObject::tr("Failed"));
        ConfirmationDialog::alert(QObject::tr("Model installation failed. Log: %1").arg(log_path), this);
      }
    }, Qt::QueuedConnection);
  }).detach();
}

static QStringList get_list(const char* path)
{
  QStringList stringList;
  QFile textFile(path);
  if (!textFile.exists()) {
    qDebug() << "Cars file not found, generating...";

    QFile allCarsCheck("/data/openpilot/selfdrive/car/top_tmp/AllCars");
    if (!allCarsCheck.exists()) {
      qDebug() << "AllCars file not found, generating AllCars first...";

      QProcess process;
      process.setWorkingDirectory("/data/openpilot");
      process.start("python3", QStringList() << "opendbc_repo/opendbc/car/fingerprints.py");
      process.waitForFinished(8000);

      if (process.exitCode() == 0) {
        QString output = process.readAllStandardOutput();

        QFile allCarsFile("/data/openpilot/selfdrive/car/top_tmp/AllCars");
        if (allCarsFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
          QTextStream out(&allCarsFile);
          out << output;
          allCarsFile.close();
          qDebug() << "AllCars file generated successfully";
        } else {
          qDebug() << "Failed to write AllCars file";
          return stringList;
        }
      } else {
        qDebug() << "fingerprints.py failed with exit code:" << process.exitCode();
        return stringList;
      }
    } else {
      qDebug() << "AllCars file exists, skipping generation";
    }
    qDebug() << "Generating Cars file from AllCars...";
    QProcess forceProcess;
    forceProcess.setWorkingDirectory("/data/openpilot");
    forceProcess.start("python3", QStringList() << "force_car_recognition.py");
    forceProcess.waitForFinished(2000);

    if (forceProcess.exitCode() != 0) {
      qDebug() << "force_car_recognition.py failed";
    } else {
      qDebug() << "Cars file generated successfully";
    }
  }

  if (textFile.open(QIODevice::ReadOnly)) {
    QTextStream textStream(&textFile);
    while (true) {
      QString line = textStream.readLine();
      if (line.isNull())
        break;
      else
        stringList.append(line);
    }
  }

  return stringList;
}

ForceCarRecognition::ForceCarRecognition(QWidget* parent): QWidget(parent) {

  QVBoxLayout* main_layout = new QVBoxLayout(this);
  main_layout->setMargin(20);
  main_layout->setSpacing(20);

  QPushButton* back = new QPushButton(tr("Back"));
  back->setObjectName("backBtn");
  back->setFixedSize(500, 100);
  connect(back, &QPushButton::clicked, [=]() { emit backPress(); });
  main_layout->addWidget(back, 0, Qt::AlignLeft);

  QListWidget* list = new QListWidget(this);
  list->setStyleSheet("QListView {padding: 40px; background-color: #393939; border-radius: 15px; height: 140px;} QListView::item{height: 100px}");
  QScroller::grabGesture(list->viewport(), QScroller::LeftMouseButtonGesture);
  list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

  list->addItem(tr("[-Not selected-]"));

  QStringList items = get_list("/data/openpilot/selfdrive/car/top_tmp/Cars");
  list->addItems(items);
  list->setCurrentRow(0);

  QString set = QString::fromStdString(Params().get("CarModel"));

  int index = 0;
  for (QString item : items) {
    if (set == item) {
        list->setCurrentRow(index + 1);
        break;
    }
    index++;
  }

  QObject::connect(list, QOverload<QListWidgetItem*>::of(&QListWidget::itemClicked),
    [=](QListWidgetItem* item){

    if (list->currentRow() == 0)
        Params().remove("CarModel");
    else
        Params().put("CarModel", list->currentItem()->text().toStdString());

    emit selectedCar();
    });

  main_layout->addWidget(list);
}
