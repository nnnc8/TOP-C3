#include "selfdrive/ui/qt/offroad/timpilot.h"

#include <QAbstractItemView>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QDir>
#include <QScroller>
#include <QListWidget>
#include <QProcess>
#include <QTextStream>

#include "common/params.h"

#include "selfdrive/ui/ui.h"
#include "selfdrive/ui/qt/widgets/input.h"

DrivingModelSelectorControl::DrivingModelSelectorControl(QWidget *parent) : ButtonControl(
    tr("Driving Model"),
    tr("SELECT"),
    tr("Download, verify, compile, and install a signed driving model from happymaj11r. Install only while offroad."),
    "../assets/icons/friday_drive_mode.svg",
    parent) {
  process = new QProcess(this);
  process->setProcessChannelMode(QProcess::MergedChannels);
  connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          this, &DrivingModelSelectorControl::processFinished);
  connect(process, &QProcess::readyRead, this, &DrivingModelSelectorControl::processOutput);
  connect(process, &QProcess::errorOccurred, this, &DrivingModelSelectorControl::processError);
  connect(this, &ButtonControl::clicked, this, &DrivingModelSelectorControl::selectModel);
  refresh();
}

void DrivingModelSelectorControl::showEvent(QShowEvent *event) {
  refresh();
  QFrame::showEvent(event);
}

void DrivingModelSelectorControl::refresh() {
  const QString name = QString::fromStdString(params.get("DrivingModelName"));
  setValue(name.isEmpty() ? tr("Stock") : name);
  setDescription(tr("Source: happymaj11r/openpilot-models\nManifest and cache status are checked offroad."));
}

QString DrivingModelSelectorControl::openpilotRoot() const {
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

QString DrivingModelSelectorControl::cacheRoot() const {
  const QString configured = qEnvironmentVariable("OPENPILOT_MODEL_CACHE");
  if (!configured.isEmpty()) {
    return configured;
  }
  return QFile::exists("/data") ? "/data/model-cache" : QDir(root).filePath(".model-cache");
}

QString DrivingModelSelectorControl::logPath(const QString &root_path) const {
  return QFile::exists("/data") ? "/data/model-selector.log" : QDir(root_path).filePath("model-selector.log");
}

void DrivingModelSelectorControl::startList() {
  operation = Operation::List;
  operation_output.clear();
  pending_output.clear();
  setEnabled(false);
  setValue(tr("Loading..."));
  process->setWorkingDirectory(root);
  process->start("python3", QStringList()
    << "tools/model_selector.py"
    << "--cache-root" << cacheRoot()
    << "list" << "--format" << "json" << "--offline");
}

void DrivingModelSelectorControl::startSync() {
  operation = Operation::Sync;
  operation_output.clear();
  pending_output.clear();
  setEnabled(false);
  setValue(tr("Syncing..."));
  process->setWorkingDirectory(root);
  process->start("python3", QStringList()
    << "tools/model_selector.py"
    << "--cache-root" << cacheRoot()
    << "sync" << "--format" << "json");
}

void DrivingModelSelectorControl::selectModel() {
  if (uiState()->scene.started || uiState()->engaged()) {
    ConfirmationDialog::alert(tr("Disengage and go offroad before installing a driving model."), this);
    return;
  }
  if (process->state() != QProcess::NotRunning) {
    return;
  }

  root = openpilotRoot();
  if (!QFile::exists(QDir(root).filePath("tools/model_selector.py"))) {
    ConfirmationDialog::alert(tr("Model selector is unavailable."), this);
    return;
  }
  log_file = logPath(root);
  startList();
}

void DrivingModelSelectorControl::startInstall(const QString &model_id, const QString &model_name) {
  if (uiState()->scene.started || uiState()->engaged()) {
    ConfirmationDialog::alert(tr("Disengage and go offroad before installing a driving model."), this);
    return;
  }

  pending_model_name = model_name;
  operation = Operation::Install;
  operation_output.clear();
  pending_output.clear();
  setEnabled(false);
  setValue(tr("Preparing..."));
  process->setWorkingDirectory(root);
  process->start("python3", QStringList()
    << "tools/model_selector.py"
    << "--cache-root" << cacheRoot()
    << "install" << model_id
    << "--repo-root" << root
    << "--python-bin" << "python3");
}

void DrivingModelSelectorControl::writeLog(const QByteArray &output) {
  QFile log_file_handle(log_file);
  if (log_file_handle.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
    log_file_handle.write("\n--- model selector operation ---\n");
    log_file_handle.write(output);
    if (!output.endsWith('\n')) {
      log_file_handle.write("\n");
    }
  }
}

void DrivingModelSelectorControl::processOutput() {
  const QByteArray chunk = process->readAll();
  if (chunk.isEmpty()) {
    return;
  }

  operation_output += chunk;
  pending_output += chunk;
  while (true) {
    const int newline = pending_output.indexOf('\n');
    if (newline < 0) {
      break;
    }
    const QByteArray line = pending_output.left(newline).trimmed();
    pending_output.remove(0, newline + 1);
    if (!line.isEmpty()) {
      handleProgressLine(line);
    }
  }
}

void DrivingModelSelectorControl::handleProgressLine(const QByteArray &line) {
  const QString text = QString::fromUtf8(line).trimmed();
  if (text.startsWith("MODEL_PROGRESS ")) {
    const QStringList parts = text.split(' ', QString::SkipEmptyParts);
    if (parts.size() >= 4) {
      bool downloaded_ok = false;
      bool total_ok = false;
      const qint64 downloaded = parts[2].toLongLong(&downloaded_ok);
      const qint64 total = parts[3].toLongLong(&total_ok);
      if (downloaded_ok && total_ok && total > 0) {
        const int percent = qBound(0, static_cast<int>((downloaded * 100) / total), 100);
        setValue(tr("Downloading %1%").arg(percent));
      }
    }
    return;
  }

  const QString status_prefix = "MODEL_STATUS ";
  if (!text.startsWith(status_prefix)) {
    return;
  }

  const QString status = text.mid(status_prefix.size()).trimmed();
  if (status.startsWith("downloading ")) {
    setValue(tr("Downloading %1...").arg(status.mid(QString("downloading ").size())));
  } else if (status.startsWith("verified ")) {
    setValue(tr("Verified %1").arg(status.mid(QString("verified ").size())));
  } else if (status == "validating") {
    setValue(tr("Validating model..."));
  } else if (status.startsWith("validated")) {
    setValue(tr("Validated %1").arg(status.mid(QString("validated").size()).trimmed()));
  } else if (status.startsWith("compiling ")) {
    setValue(tr("Compiling %1...").arg(status.mid(QString("compiling ").size())));
  } else if (status.startsWith("compiled ")) {
    setValue(tr("Compiled %1").arg(status.mid(QString("compiled ").size())));
  } else if (status == "activating") {
    setValue(tr("Activating model..."));
  } else if (status.startsWith("using_cached")) {
    setValue(tr("Using cached model..."));
  }
}

void DrivingModelSelectorControl::processFinished(int exit_code, QProcess::ExitStatus exit_status) {
  processOutput();
  if (!pending_output.isEmpty()) {
    handleProgressLine(pending_output.trimmed());
  }
  pending_output.clear();
  const QByteArray output = operation_output;
  writeLog(output);
  operation_output.clear();
  const Operation finished_operation = operation;
  operation = Operation::None;

  if (finished_operation == Operation::Sync) {
    if (exit_status == QProcess::NormalExit && exit_code == 0) {
      startList();
    } else {
      setEnabled(true);
      setValue(tr("Unavailable"));
      ConfirmationDialog::alert(tr("Unable to sync model list. Log: %1").arg(log_file), this);
    }
    return;
  }

  if (finished_operation == Operation::List) {
    if (exit_status != QProcess::NormalExit || exit_code != 0) {
      startSync();
      return;
    }

    QJsonParseError parse_error;
    const QJsonDocument doc = QJsonDocument::fromJson(output, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !doc.isArray()) {
      setEnabled(true);
      setValue(tr("Unavailable"));
      ConfirmationDialog::alert(tr("Model list response was invalid. Log: %1").arg(log_file), this);
      return;
    }

    const QJsonArray models = doc.array();
    if (models.isEmpty()) {
      setEnabled(true);
      setValue(tr("No models"));
      ConfirmationDialog::alert(tr("No installable driving models were found."), this);
      return;
    }

    const QJsonObject manifest = models.first().toObject();
    const QString source = manifest["source"].toString();
    const QString manifest_updated_at = manifest["manifest_updated_at"].toString();
    const QString manifest_error = manifest["manifest_error"].toString();
    QString status_description = tr("Source: %1\nManifest: %2").arg(source, manifest_updated_at);
    if (!manifest_error.isEmpty()) {
      status_description += tr("\nLast sync error: %1").arg(manifest_error);
    }
    setDescription(status_description);

    QStringList labels;
    QMap<QString, QString> ids_by_label;
    QMap<QString, QString> names_by_label;
    QString current_label;
    const QString current_id = QString::fromStdString(params.get("DrivingModel"));
    for (const QJsonValue &value : models) {
      const QJsonObject model = value.toObject();
      const QString id = model["id"].toString();
      const QString name = model["name"].toString(id);
      const QString size_mb = QString::number(model["size_mb"].toDouble(), 'f', 1);
      const QString added_at = model["added_at"].toString();
      const bool compiled = model["compiled"].toBool();
      const bool cached = model["cached"].toBool();
      const QString state = compiled ? tr("offline ready") : cached ? tr("downloaded") : tr("online");
      const QString label = QString("%1 [%2] (%3 MB, %4, %5)").arg(name, id, size_mb, added_at, state);
      labels.append(label);
      ids_by_label[label] = id;
      names_by_label[label] = name;
      if (id == current_id || model["active"].toBool()) {
        current_label = label;
      }
    }

    setEnabled(true);
    const QString selection = MultiOptionDialog::getSelection(tr("Select Driving Model"), labels, current_label, this);
    if (selection.isEmpty()) {
      refresh();
      return;
    }

    const QString model_id = ids_by_label.value(selection);
    const QString model_name = names_by_label.value(selection);
    if (!ConfirmationDialog::confirm(tr("Install %1? openpilot will restart after installation.").arg(model_name), tr("Install"), this)) {
      refresh();
      return;
    }
    startInstall(model_id, model_name);
    return;
  }

  if (finished_operation == Operation::Install) {
    setEnabled(true);
    if (exit_status == QProcess::NormalExit && exit_code == 0) {
      params.putBool("OnroadCycleRequested", true);
      refresh();
      ConfirmationDialog::alert(tr("Installed %1. Log: %2").arg(pending_model_name, log_file), this);
    } else {
      setValue(tr("Failed"));
      ConfirmationDialog::alert(tr("Model installation failed. Log: %1").arg(log_file), this);
    }
  }
}

void DrivingModelSelectorControl::processError(QProcess::ProcessError error) {
  if (error != QProcess::FailedToStart || operation == Operation::None) {
    return;
  }
  operation = Operation::None;
  setEnabled(true);
  setValue(tr("Unavailable"));
  ConfirmationDialog::alert(tr("Unable to start model selector: %1").arg(process->errorString()), this);
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
  list->setStyleSheet("QListView {padding: 40px; background-color: #0D111B; border: 1px solid rgba(122, 174, 206, 72); border-radius: 15px; height: 140px; color: #F4F6FA;} QListView::item{height: 100px}");
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
