#include <cassert>
#include <cmath>
#include <string>
#include <tuple>
#include <vector>

#include <QDebug>
#include <QIcon>

#include "common/watchdog.h"
#include "common/util.h"
#include "selfdrive/ui/qt/network/networking.h"
#include "selfdrive/ui/qt/offroad/journey_board.h"
#include "selfdrive/ui/qt/offroad/aegis_health.h"
#include "selfdrive/ui/qt/offroad/settings.h"
#include "selfdrive/ui/qt/qt_window.h"
#include "selfdrive/ui/qt/widgets/prime.h"
#include "selfdrive/ui/qt/widgets/scrollview.h"
#include "selfdrive/ui/qt/offroad/developer_panel.h"
#include "selfdrive/ui/qt/offroad/timpilot.h"

TogglesPanel::TogglesPanel(SettingsWindow *parent) : ListWidget(parent) {
  // param, title, desc, icon, restart needed
  std::vector<std::tuple<QString, QString, QString, QString, bool>> toggle_defs{
    {
      "OpenpilotEnabledToggle",
      tr("Enable openpilot"),
      tr("Use the openpilot system for adaptive cruise control and lane keep driver assistance. Your attention is required at all times to use this feature."),
      "../assets/icons/aegis_enable.svg",
      true,
    },
    {
      "ExperimentalMode",
      tr("Experimental Mode"),
      "",
      "../assets/icons/aegis_experimental.svg",
      false,
    },
    {
      "DisengageOnAccelerator",
      tr("Disengage on Accelerator Pedal"),
      tr("When enabled, pressing the accelerator pedal will disengage openpilot."),
      "../assets/icons/aegis_pedal.svg",
      false,
    },
    {
      "IsLdwEnabled",
      tr("Enable Lane Departure Warnings"),
      tr("Receive alerts to steer back into the lane when your vehicle drifts over a detected lane line without a turn signal activated while driving over 31 mph (50 km/h)."),
      "../assets/icons/aegis_lane_warning.svg",
      false,
    },
    {
      "AlwaysOnDM",
      tr("Always-On Driver Monitoring"),
      tr("Enable driver monitoring even when openpilot is not engaged."),
      "../assets/icons/aegis_driver_monitor.svg",
      false,
    },
    {
      "RecordFront",
      tr("Record and Upload Driver Camera"),
      tr("Upload data from the driver facing camera and help improve the driver monitoring algorithm."),
      "../assets/icons/aegis_camera.svg",
      true,
    },
    {
      "RecordAudio",
      tr("Record and Upload Microphone Audio"),
      tr("Record and store microphone audio while driving. The audio will be included in the dashcam video in comma connect."),
      "../assets/icons/aegis_microphone.svg",
      true,
    },
    {
      "IsMetric",
      tr("Use Metric System"),
      tr("Display speed in km/h instead of mph."),
      "../assets/icons/aegis_metric.svg",
      false,
    },
  };


  std::vector<QString> longi_button_texts{tr("Aggressive"), tr("Standard"), tr("Relaxed")};
  long_personality_setting = new ButtonParamControl("LongitudinalPersonality", tr("Driving Personality"),
                                          tr("Standard is recommended. In aggressive mode, openpilot will follow lead cars closer and be more aggressive with the gas and brake. "
                                             "In relaxed mode openpilot will stay further away from lead cars. On supported cars, you can cycle through these personalities with "
                                             "your steering wheel distance button."),
                                          "../assets/icons/aegis_follow.svg",
                                          longi_button_texts);

  // accel controller
  std::vector<QString> accel_personality_texts{tr("Sport"), tr("Normal"), tr("Eco"), tr("Stock")};
  accel_personality_setting = new ButtonParamControl("AccelPersonality", tr("Acceleration Personality"),
                                          tr("Normal is recommended. In sport mode, AEGIS will provide aggressive acceleration for a dynamic driving experience. "
                                             "In eco mode, AEGIS will apply smoother and more relaxed acceleration. On supported cars, you can cycle through these "
                                             "acceleration personality within Onroad Settings on the driving screen."),
                                          "../assets/icons/aegis_accel.svg",
                                          accel_personality_texts);
  accel_personality_setting->showDescription();

  // set up uiState update for personality setting
  QObject::connect(uiState(), &UIState::uiUpdate, this, &TogglesPanel::updateState);
  const bool lite = getenv("LITE");
  for (auto &[param, title, desc, icon, needs_restart] : toggle_defs) {
    if ((param == "AlwaysOnDM" || param == "RecordFront" || param == "RecordAudio" || param == "RecordAudioFeedback") && lite) {
      continue;
    }
    auto toggle = new ParamControl(param, title, desc, icon, this);

    bool locked = params.getBool((param + "Lock").toStdString());
    toggle->setEnabled(!locked);

    if (needs_restart && !locked) {
      toggle->setDescription(toggle->getDescription() + tr(" Changing this setting will restart openpilot if the car is powered on."));

      QObject::connect(uiState(), &UIState::engagedChanged, [toggle](bool engaged) {
        toggle->setEnabled(!engaged);
      });

      QObject::connect(toggle, &ParamControl::toggleFlipped, [=](bool state) {
        params.putBool("OnroadCycleRequested", true);
      });
    }

    addItem(toggle);
    toggles[param.toStdString()] = toggle;

    // insert longitudinal personality after NDOG toggle
    if (param == "DisengageOnAccelerator") {
      addItem(long_personality_setting);
      addItem(accel_personality_setting);
    }
  }

  // Toggles with confirmation dialogs
  toggles["ExperimentalMode"]->setActiveIcon("../assets/icons/aegis_experimental_active.svg");
  toggles["ExperimentalMode"]->setConfirmation(true, true);
}

void TogglesPanel::updateState(const UIState &s) {
  const SubMaster &sm = *(s.sm);

  if (sm.updated("selfdriveState")) {
    auto personality = sm["selfdriveState"].getSelfdriveState().getPersonality();
    if (personality != s.scene.personality && s.scene.started && isVisible()) {
      long_personality_setting->setCheckedButton(static_cast<int>(personality));
    }
    uiState()->scene.personality = personality;
  }
  if (sm.updated("longitudinalPlanTOP")) {
    auto accel_personality = sm["longitudinalPlanTOP"].getLongitudinalPlanTOP().getAccelPersonality();
    if (accel_personality != s.scene.accel_personality && s.scene.started && isVisible()) {
      accel_personality_setting->setCheckedButton(static_cast<int>(accel_personality));
    }
    uiState()->scene.accel_personality = accel_personality;
  }
}

void TogglesPanel::expandToggleDescription(const QString &param) {
  toggles[param.toStdString()]->showDescription();
}

void TogglesPanel::scrollToToggle(const QString &param) {
  if (auto it = toggles.find(param.toStdString()); it != toggles.end()) {
    auto scroll_area = qobject_cast<QScrollArea*>(parent()->parent());
    if (scroll_area) {
      scroll_area->ensureWidgetVisible(it->second);
    }
  }
}

void TogglesPanel::showEvent(QShowEvent *event) {
  updateToggles();
}

void TogglesPanel::updateToggles() {
  auto experimental_mode_toggle = toggles["ExperimentalMode"];
  const QString e2e_description = QString("%1<br>"
                                          "<h4>%2</h4><br>"
                                          "%3<br>"
                                          "<h4>%4</h4><br>"
                                          "%5<br>")
                                  .arg(tr("openpilot defaults to driving in <b>chill mode</b>. Experimental mode enables <b>alpha-level features</b> that aren't ready for chill mode. Experimental features are listed below:"))
                                  .arg(tr("End-to-End Longitudinal Control"))
                                  .arg(tr("Let the driving model control the gas and brakes. openpilot will drive as it thinks a human would, including stopping for red lights and stop signs. "
                                          "Since the driving model decides the speed to drive, the set speed will only act as an upper bound. This is an alpha quality feature; "
                                          "mistakes should be expected."))
                                  .arg(tr("New Driving Visualization"))
                                  .arg(tr("The driving visualization will transition to the road-facing wide-angle camera at low speeds to better show some turns. The Experimental mode logo will also be shown in the top right corner."));

  const bool is_release = params.getBool("IsReleaseBranch");
  auto cp_bytes = params.get("CarParamsPersistent");
  if (!cp_bytes.empty()) {
    AlignedBuffer aligned_buf;
    capnp::FlatArrayMessageReader cmsg(aligned_buf.align(cp_bytes.data(), cp_bytes.size()));
    cereal::CarParams::Reader CP = cmsg.getRoot<cereal::CarParams>();

    if (hasLongitudinalControl(CP)) {
      // normal description and toggle
      experimental_mode_toggle->setEnabled(true);
      experimental_mode_toggle->setDescription(e2e_description);
      long_personality_setting->setEnabled(true);
      long_personality_setting->refresh();
      accel_personality_setting->setEnabled(true);
      accel_personality_setting->refresh();
    } else {
      // no long for now
      experimental_mode_toggle->setEnabled(false);
      long_personality_setting->setEnabled(false);
      accel_personality_setting->setEnabled(true);
      params.remove("ExperimentalMode");

      const QString unavailable = tr("Experimental mode is currently unavailable on this car since the car's stock ACC is used for longitudinal control.");

      QString long_desc = unavailable + " " + \
                          tr("openpilot longitudinal control may come in a future update.");
      if (CP.getAlphaLongitudinalAvailable()) {
        if (is_release) {
          long_desc = unavailable + " " + tr("An alpha version of openpilot longitudinal control can be tested, along with Experimental mode, on non-release branches.");
        } else {
          long_desc = tr("Enable the openpilot longitudinal control (alpha) toggle to allow Experimental mode.");
        }
      }
      experimental_mode_toggle->setDescription("<b>" + long_desc + "</b><br><br>" + e2e_description);
    }

    experimental_mode_toggle->refresh();
  } else {
    experimental_mode_toggle->setDescription(e2e_description);
  }
}

DevicePanel::DevicePanel(SettingsWindow *parent) : ListWidget(parent), parentWindow(parent) {
  setSpacing(50);

  auto footagePopup = new MyFootagePopup(this);
  auto qrcodeBtn = new ButtonControl(tr("DashCam footage"), tr("QR-Code"),
                                     tr("Watch and/or download recordings from comma device cameras"),
                                     "../assets/icons/aegis_dashcam.svg", this);
  connect(qrcodeBtn, &ButtonControl::clicked, [=] {
      footagePopup->exec();
    });
  addItem(qrcodeBtn);

  addItem(new LabelControl(tr("Dongle ID"), getDongleId().value_or(tr("N/A")), "", "../assets/icons/aegis_dongle.svg", this));
  addItem(new LabelControl(tr("Serial"), params.get("HardwareSerial").c_str(), "", "../assets/icons/aegis_serial.svg", this));

  const bool lite = getenv("LITE");
  pair_device = new ButtonControl(tr("Pair Device"), tr("PAIR"),
                                  tr("Pair your device with comma connect (connect.comma.ai) and claim your comma prime offer."),
                                  "../assets/icons/aegis_pair.svg", this);
  connect(pair_device, &ButtonControl::clicked, [=]() {
    PairingPopup popup(this);
    popup.exec();
  });
  addItem(pair_device);

  // offroad-only buttons
  if (!lite) {
    auto dcamBtn = new ButtonControl(tr("Driver Camera"), tr("PREVIEW"),
                                     tr("Preview the driver facing camera to ensure that driver monitoring has good visibility. (vehicle must be off)"),
                                     "../assets/icons/aegis_driver_camera.svg", this);
    connect(dcamBtn, &ButtonControl::clicked, [=]() { emit showDriverView(); });
    addItem(dcamBtn);
  }
  resetCalibBtn = new ButtonControl(tr("Reset Calibration"), tr("RESET"), "", "../assets/icons/aegis_calibration.svg", this);
  connect(resetCalibBtn, &ButtonControl::showDescriptionEvent, this, &DevicePanel::updateCalibDescription);
  connect(resetCalibBtn, &ButtonControl::clicked, [&]() {
    if (!uiState()->engaged()) {
      if (ConfirmationDialog::confirm(tr("Are you sure you want to reset calibration?"), tr("Reset"), this)) {
        // Check engaged again in case it changed while the dialog was open
        if (!uiState()->engaged()) {
          params.remove("CalibrationParams");
          params.remove("LiveTorqueParameters");
          params.remove("LiveParameters");
          params.remove("LiveParametersV2");
          params.remove("LiveDelay");
          params.putBool("OnroadCycleRequested", true);
          updateCalibDescription();
        }
      }
    } else {
      ConfirmationDialog::alert(tr("Disengage to Reset Calibration"), this);
    }
  });
  addItem(resetCalibBtn);

  flashPandaBtn = new ButtonControl(tr("Flash Panda"), tr("FLASH"),
                                    tr("<b>Reinstall the Panda firmware</b> to fix connection or reliability issues."),
                                    "../assets/icons/aegis_panda.svg", this);
  connect(flashPandaBtn, &ButtonControl::clicked, this, &DevicePanel::flashPanda);
  addItem(flashPandaBtn);

  auto retrainingBtn = new ButtonControl(tr("Review Training Guide"), tr("REVIEW"), tr("Review the rules, features, and limitations of openpilot"),
                                         "../assets/icons/aegis_training.svg", this);
  connect(retrainingBtn, &ButtonControl::clicked, [=]() {
    if (ConfirmationDialog::confirm(tr("Are you sure you want to review the training guide?"), tr("Review"), this)) {
      emit reviewTrainingGuide();
    }
  });
  addItem(retrainingBtn);

  if (Hardware::TICI()) {
    auto regulatoryBtn = new ButtonControl(tr("Regulatory"), tr("VIEW"), "", "../assets/icons/aegis_regulatory.svg", this);
    connect(regulatoryBtn, &ButtonControl::clicked, [=]() {
      const std::string txt = util::read_file("../assets/offroad/fcc.html");
      ConfirmationDialog::rich(QString::fromStdString(txt), this);
    });
    addItem(regulatoryBtn);
  }

  auto translateBtn = new ButtonControl(tr("Change Language"), tr("CHANGE"), "", "../assets/icons/aegis_language.svg", this);
  connect(translateBtn, &ButtonControl::clicked, [=]() {
    QMap<QString, QString> langs = getSupportedLanguages();
    QString selection = MultiOptionDialog::getSelection(tr("Select a language"), langs.keys(), langs.key(uiState()->language), this);
    if (!selection.isEmpty()) {
      // put language setting, exit Qt UI, and trigger fast restart
      params.put("LanguageSetting", langs[selection].toStdString());
      qApp->exit(18);
      watchdog_kick(0);
    }
  });
  addItem(translateBtn);

  QObject::connect(uiState()->prime_state, &PrimeState::changed, [this] (PrimeState::Type type) {
    pair_device->setVisible(type == PrimeState::PRIME_TYPE_UNPAIRED);
  });
//  QObject::connect(uiState(), &UIState::offroadTransition, [=](bool offroad) {
//    for (auto btn : findChildren<ButtonControl *>()) {
//      if (btn != pair_device && btn != resetCalibBtn) {
//        btn->setEnabled(offroad);
//      }
//    }
//  });

  // power buttons
  QHBoxLayout *power_layout = new QHBoxLayout();
  power_layout->setSpacing(30);

  QPushButton *reboot_btn = new QPushButton(tr("Reboot"));
  reboot_btn->setObjectName("reboot_btn");
  reboot_btn->setIcon(QIcon("../assets/icons/aegis_reboot.svg"));
  reboot_btn->setIconSize(QSize(54, 54));
  power_layout->addWidget(reboot_btn);
  QObject::connect(reboot_btn, &QPushButton::clicked, this, &DevicePanel::reboot);

  QPushButton *poweroff_btn = new QPushButton(tr("Power Off"));
  poweroff_btn->setObjectName("poweroff_btn");
  poweroff_btn->setIcon(QIcon("../assets/icons/aegis_power.svg"));
  poweroff_btn->setIconSize(QSize(54, 54));
  power_layout->addWidget(poweroff_btn);
  QObject::connect(poweroff_btn, &QPushButton::clicked, this, &DevicePanel::poweroff);

//  if (!Hardware::PC()) {
//    connect(uiState(), &UIState::offroadTransition, poweroff_btn, &QPushButton::setVisible);
//  }

  setStyleSheet(R"(
    #reboot_btn { height: 112px; border-radius: 12px; background-color: #7AAECE; border: 1px solid rgba(255, 255, 255, 52); font-size: 42px; color: #07101A; font-weight: 650; }
    #reboot_btn:pressed { background-color: #497C9C; }
    #poweroff_btn { height: 112px; border-radius: 12px; background-color: #E82127; font-size: 42px; color: #FFFFFF; }
    #poweroff_btn:pressed { background-color: #FF2424; }
  )");
  addItem(power_layout);
}

void DevicePanel::updateCalibDescription() {
  QString desc = tr("openpilot requires the device to be mounted within 4° left or right and within 5° up or 9° down.");
  std::string calib_bytes = params.get("CalibrationParams");
  if (!calib_bytes.empty()) {
    try {
      AlignedBuffer aligned_buf;
      capnp::FlatArrayMessageReader cmsg(aligned_buf.align(calib_bytes.data(), calib_bytes.size()));
      auto calib = cmsg.getRoot<cereal::Event>().getLiveCalibration();
      if (calib.getCalStatus() != cereal::LiveCalibrationData::Status::UNCALIBRATED) {
        double pitch = calib.getRpyCalib()[1] * (180 / M_PI);
        double yaw = calib.getRpyCalib()[2] * (180 / M_PI);
        desc += tr(" Your device is pointed %1° %2 and %3° %4.")
                    .arg(QString::number(std::abs(pitch), 'g', 1), pitch > 0 ? tr("down") : tr("up"),
                         QString::number(std::abs(yaw), 'g', 1), yaw > 0 ? tr("left") : tr("right"));
      }
    } catch (kj::Exception) {
      qInfo() << "invalid CalibrationParams";
    }
  }

  int lag_perc = 0;
  std::string lag_bytes = params.get("LiveDelay");
  if (!lag_bytes.empty()) {
    try {
      AlignedBuffer aligned_buf;
      capnp::FlatArrayMessageReader cmsg(aligned_buf.align(lag_bytes.data(), lag_bytes.size()));
      lag_perc = cmsg.getRoot<cereal::Event>().getLiveDelay().getCalPerc();
    } catch (kj::Exception) {
      qInfo() << "invalid LiveDelay";
    }
  }
  if (lag_perc < 100) {
    desc += tr("\n\nSteering lag calibration is %1% complete.").arg(lag_perc);
  } else {
    desc += tr("\n\nSteering lag calibration is complete.");
  }

  std::string torque_bytes = params.get("LiveTorqueParameters");
  if (!torque_bytes.empty()) {
    try {
      AlignedBuffer aligned_buf;
      capnp::FlatArrayMessageReader cmsg(aligned_buf.align(torque_bytes.data(), torque_bytes.size()));
      auto torque = cmsg.getRoot<cereal::Event>().getLiveTorqueParameters();
      // don't add for non-torque cars
      if (torque.getUseParams()) {
        int torque_perc = torque.getCalPerc();
        if (torque_perc < 100) {
          desc += tr(" Steering torque response calibration is %1% complete.").arg(torque_perc);
        } else {
          desc += tr(" Steering torque response calibration is complete.");
        }
      }
    } catch (kj::Exception) {
      qInfo() << "invalid LiveTorqueParameters";
    }
  }

  desc += "\n\n";
  desc += tr("openpilot is continuously calibrating, resetting is rarely required. "
             "Resetting calibration will restart openpilot if the car is powered on.");
  resetCalibBtn->setDescription(desc);
}

void DevicePanel::reboot() {
  if (!uiState()->engaged()) {
    if (ConfirmationDialog::confirm(tr("Are you sure you want to reboot?"), tr("Reboot"), this)) {
      // Check engaged again in case it changed while the dialog was open
      if (!uiState()->engaged()) {
        params.putBool("DoReboot", true);
      }
    }
  } else {
    ConfirmationDialog::alert(tr("Disengage to Reboot"), this);
  }
}

void DevicePanel::poweroff() {
  if (!uiState()->engaged()) {
    if (ConfirmationDialog::confirm(tr("Are you sure you want to power off?"), tr("Power Off"), this)) {
      // Check engaged again in case it changed while the dialog was open
      if (!uiState()->engaged()) {
        params.putBool("DoShutdown", true);
      }
    }
  } else {
    ConfirmationDialog::alert(tr("Disengage to Power Off"), this);
  }
}

void DevicePanel::flashPanda() {
  if (!uiState()->engaged()) {
    if (ConfirmationDialog::confirm(tr("Are you sure you want to flash the Panda firmware?"), tr("Flash"), this)) {
      if (!uiState()->engaged()) {
        std::thread([this]() {
          parentWindow->keepScreenOn = true;

          flashPandaBtn->setEnabled(false);
          flashPandaBtn->setValue(tr("Flashing..."));

          int ret = std::system("cd /data/openpilot && python3 top/system/flash_panda.py > /data/flash_panda.log 2>&1");

          if (ret == 0) {
            flashPandaBtn->setValue(tr("Flashed!"));
            util::sleep_for(2500);

            flashPandaBtn->setValue(tr("Rebooting..."));
            util::sleep_for(2500);

            params.putBool("DoReboot", true);
          } else {
            flashPandaBtn->setValue(tr("Failed!"));
            util::sleep_for(3000);
            flashPandaBtn->setValue("");
            flashPandaBtn->setEnabled(true);
          }
        }).detach();
      }
    }
  } else {
    ConfirmationDialog::alert(tr("Disengage to Flash Panda"), this);
  }
}

void SettingsWindow::showEvent(QShowEvent *event) {
  setCurrentPanel(0);
}

void SettingsWindow::setCurrentPanel(int index, const QString &param) {
  if (!param.isEmpty()) {
    // Check if param ends with "Panel" to determine if it's a panel name
    if (param.endsWith("Panel")) {
      QString panelName = param;
      panelName.chop(5); // Remove "Panel" suffix

      // Find the panel by name
      for (int i = 0; i < nav_btns->buttons().size(); i++) {
        if (nav_btns->buttons()[i]->text() == tr(panelName.toStdString().c_str())) {
          index = i;
          break;
        }
      }
    } else {
      emit expandToggleDescription(param);
      emit scrollToToggle(param);
    }
  }

  panel_widget->setCurrentIndex(index);
  nav_btns->buttons()[index]->setChecked(true);
}

SettingsWindow::SettingsWindow(QWidget *parent) : QFrame(parent) {

  // setup two main layouts
  sidebar_widget = new QWidget;
  QVBoxLayout *sidebar_layout = new QVBoxLayout(sidebar_widget);
  panel_widget = new QStackedWidget();

  // close button
  QPushButton *close_btn = new QPushButton(tr("×"));
  close_btn->setStyleSheet(R"(
    QPushButton {
      font-size: 120px;
      padding-bottom: 20px;
      border-radius: 100px;
      background-color: #0D111B;
      border: 1px solid rgba(122, 174, 206, 118);
      color: #F4F6FA;
      font-weight: 400;
    }
    QPushButton:pressed {
      background-color: #161D2C;
    }
  )");
  close_btn->setFixedSize(140, 140);
  sidebar_layout->addSpacing(40);
  sidebar_layout->addWidget(close_btn, 0, Qt::AlignLeft);
  QObject::connect(close_btn, &QPushButton::clicked, this, &SettingsWindow::closeSettings);

  // setup panels
  DevicePanel *device = new DevicePanel(this);
  QObject::connect(device, &DevicePanel::reviewTrainingGuide, this, &SettingsWindow::reviewTrainingGuide);
  QObject::connect(device, &DevicePanel::showDriverView, this, &SettingsWindow::showDriverView);

  TogglesPanel *toggles = new TogglesPanel(this);
  QObject::connect(this, &SettingsWindow::expandToggleDescription, toggles, &TogglesPanel::expandToggleDescription);
  QObject::connect(this, &SettingsWindow::scrollToToggle, toggles, &TogglesPanel::scrollToToggle);

  auto networking = new Networking(this);
  QObject::connect(uiState()->prime_state, &PrimeState::changed, networking, &Networking::setPrimeType);

  struct PanelItem {
    QString name;
    QString icon;
    QWidget *panel;
  };
  QList<PanelItem> panels = {
    {tr("Device"), "../assets/icons/aegis_nav_device.svg", device},
    {tr("Network"), "../assets/icons/aegis_nav_network.svg", networking},
    {tr("Toggles"), "../assets/icons/aegis_nav_toggles.svg", toggles},
    {tr("Software"), "../assets/icons/aegis_nav_software.svg", new SoftwarePanel(this)},
    {tr("Journey Board"), "../assets/icons/aegis_nav_achievements.svg", new JourneyBoardPanel(this)},
    {tr("Health Center"), "../assets/icons/aegis_clean_km.svg", new AegisHealthPanel(this)},
    {tr("Developer"), "../assets/icons/aegis_nav_developer.svg", new DeveloperPanel(this)},
    {tr("AEGIS"), "../assets/icons/aegis_nav_aegis.svg", new TimpilotPanel(this)},
  };

  nav_btns = new QButtonGroup(this);
  for (auto &[name, icon, panel] : panels) {
    QPushButton *btn = new QPushButton(name);
    btn->setObjectName("navButton");
    btn->setCheckable(true);
    btn->setChecked(nav_btns->buttons().size() == 0);
    btn->setIcon(QIcon(icon));
    btn->setIconSize(QSize(48, 48));
    btn->setStyleSheet(R"(
      QPushButton#navButton {
        color: #A6B0BE;
        border: 1px solid transparent;
        border-left: 5px solid transparent;
        border-radius: 10px;
        background-color: transparent;
        font-size: 46px;
        font-weight: 650;
        text-align: left;
        padding-left: 18px;
      }
      QPushButton#navButton:checked {
        color: #F4F6FA;
        background-color: rgba(122, 174, 206, 32);
        border: 1px solid rgba(122, 174, 206, 82);
        border-left: 5px solid #7AAECE;
      }
      QPushButton#navButton:pressed {
        color: #FFFFFF;
        background-color: rgba(122, 174, 206, 52);
      }
    )");
    btn->setMinimumHeight(108);
    btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    nav_btns->addButton(btn);
    sidebar_layout->addWidget(btn, 0, Qt::AlignLeft);

    const int lr_margin = name != tr("Network") ? 50 : 0;  // Network panel handles its own margins
    panel->setContentsMargins(lr_margin, 25, lr_margin, 25);

    ScrollView *panel_frame = new ScrollView(panel, this);
    panel_widget->addWidget(panel_frame);

    QObject::connect(btn, &QPushButton::clicked, [=, w = panel_frame]() {
      btn->setChecked(true);
      panel_widget->setCurrentWidget(w);
    });
  }
  sidebar_layout->setContentsMargins(50, 50, 100, 50);

  // main settings layout, sidebar + main panel
  QHBoxLayout *main_layout = new QHBoxLayout(this);

  sidebar_widget->setFixedWidth(500);
  main_layout->addWidget(sidebar_widget);
  main_layout->addWidget(panel_widget);

  setStyleSheet(R"(
    * {
      color: #F4F6FA;
      font-size: 46px;
      font-family: "Cubic 11", "[Cubic 11]", Inter, "Noto Sans CJK TC";
    }
    SettingsWindow {
      background-color: #03060E;
    }
    QWidget {
      background-color: transparent;
    }
    QStackedWidget, ScrollView {
      background-color: #0D111B;
      border: 1px solid rgba(122, 174, 206, 56);
      border-radius: 18px;
    }
  )");
}

TimpilotPanel::TimpilotPanel(QWidget* parent) : QWidget(parent) {
  main_layout = new QStackedLayout(this);
  home = new QWidget(this);
  QVBoxLayout* fcr_layout = new QVBoxLayout(home);
  fcr_layout->setContentsMargins(0, 20, 0, 20);

  QString set = QString::fromStdString(Params().get("CarModel"));

  QPushButton* setCarBtn = new QPushButton(set.length() ? set : tr("Select Car"));
  setCarBtn->setObjectName("setCarBtn");
  setCarBtn->setIcon(QIcon("../assets/icons/aegis_car_select.svg"));
  setCarBtn->setIconSize(QSize(46, 46));
  setCarBtn->setStyleSheet("margin-right: 30px;");
  connect(setCarBtn, &QPushButton::clicked, [=]() { main_layout->setCurrentWidget(setCar); });
  fcr_layout->addSpacing(10);
  fcr_layout->addWidget(setCarBtn, 0, Qt::AlignRight);
  fcr_layout->addSpacing(10);

  home_widget = new QWidget(this);
  QVBoxLayout* toggle_layout = new QVBoxLayout(home_widget);
  home_widget->setObjectName("homeWidget");

  ScrollView *scroller = new ScrollView(home_widget, this);
  scroller->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  fcr_layout->addWidget(scroller, 1);

  main_layout->addWidget(home);

  setCar = new ForceCarRecognition(this);
  connect(setCar, &ForceCarRecognition::backPress, [=]() { main_layout->setCurrentWidget(home); });
  connect(setCar, &ForceCarRecognition::selectedCar, [=]() {
    QString set = QString::fromStdString(Params().get("CarModel"));
    setCarBtn->setText(set.length() ? set : tr("Select your car"));
    main_layout->setCurrentWidget(home);
  });
  main_layout->addWidget(setCar);

  QPalette pal = palette();
  pal.setColor(QPalette::Background, QColor(0x03, 0x06, 0x0e));
  setAutoFillBackground(true);
  setPalette(pal);

  setStyleSheet(R"(
    #backBtn, #setCarBtn {
      font-size: 44px;
      margin: 0px;
      padding: 20px;
      border: 1px solid rgba(122, 174, 206, 72);
      border-radius: 12px;
      color: #F4F6FA;
      background-color: #0D111B;
    }
    #homeWidget {
      background-color: #03060E;
    }
    AbstractControl {
      background-color: #0D111B;
      border: 1px solid rgba(122, 174, 206, 52);
      border-radius: 8px;
    }
    AbstractControl QPushButton {
      color: #F4F6FA;
    }
    AbstractControl QLabel {
      color: #A6B0BE;
    }
  )");

  QList<ParamControl*> toggles;

  toggles.append(new ParamControl("QuietDrive",
                                  tr("Quiet Drive"),
                                  tr("AEGIS will display alerts but only play the most important warning sounds. This feature can be toggled while the car is on."),
                                  "../assets/icons/aegis_quiet.svg",
                                  this));

  toggles.append(new ParamControl("AegisQuietCabin",
                                  tr("Quiet Cabin"),
                                  tr("Suppress Toyota's overlapping double beep chimes while maintaining essential safety and openpilot alerts."),
                                  "../assets/icons/aegis_quiet.svg",
                                  this));

  toggles.append(new ParamControl("OnroadScreenOff",
                                  tr("Driving Screen Off"),
                                  tr("Turn off the device screen to protect the OLED panel after driving starts. It automatically brightens or turns on when a touch or event occurs."),
                                  "../assets/icons/aegis_screen.svg",
                                  this));

  toggles.append(new ParamControl("dp_atl",
                                  tr("Lateral Controls Always On"),
                                  tr("Lateral control will always be on and will not be interrupted by braking."),
                                  "../assets/icons/aegis_lateral.svg",
                                  this));

  toggles.append(new ParamControl("NNFF",
                                  tr("NNFF Torque Control"),
                                  tr("Use Twilsonco's Neural Network Feedforward torque system for more precise lateral control."),
                                  "../assets/icons/aegis_nnff.svg",
                                  this));

  toggles.append(new ParamControl("Dynamic_Follow",
                                  tr("Dynamic Distance Adjustment"),
                                  tr("The distance to the lead car will no longer be a fixed reaction time, but will be dynamically adjusted based on the speed of the vehicle."),
                                  "../assets/icons/aegis_dynamic_follow.svg",
                                  this));

  toggles.append(new ParamControl("NudgelessLaneChange",
                                  tr("Blinker Lane Change"),
                                  tr("Change lanes without the need to nudge the steering wheel first.\nDisabled: Need to nudge the steering wheel to change lanes.\nEnabled: Nudgeless.\nSpeed limit: Normal mode: above 20mph, Enabled Lateral Controls Always On: above 35mph."),
                                  "../assets/icons/aegis_lane_change.svg",
                                  this));

  toggles.append(new ParamControl("road_edge_detection",
                                  tr("Edge Detection During Lane Changes"),
                                  tr("When the system detects obstacles at the vehicle's edge, lane change assist functionality will be temporarily suspended.\nNOTE: This will show 'Car Detected in Blindspot' warning."),
                                  "../assets/icons/aegis_edge.svg",
                                  this));

  toggles.append(new ParamControl("SmartCruiseControlVision",
                                  tr("Vision Based Turn Control"),
                                  tr("Use vision path predictions to estimate the appropriate speed to drive through turns ahead."),
                                  "../assets/icons/aegis_vision_turn.svg",
                                  this));

  toggles.append(new ParamControl("fleetmanager",
                                  tr("Enable Local File Server"),
                                  tr("This will allow you to play or download openpilot driving record files through your browser.\nUse web interface to control it: *http://&lt;device_ip&gt;:8082*.\nInternet access from mobile phone (tethering) is required."),
                                  "../assets/icons/aegis_file_server.svg",
                                  this));

  toggles.append(new ParamControl("toyota_stock_long",
                                  tr("Use Toyota Stock Longitudinal Control"),
                                  tr("Enable to use Toyota's stock longitudinal control."),
                                  "../assets/icons/aegis_toyota_stock.svg",
                                  this));

  toggles.append(new ParamControl("ToyotaDriveMode",
                                  tr("Enable Toyota Drive Mode Button"),
                                  tr("AEGIS will link the Acceleration Personality to the car's physical drive mode selector.\nReboot Required."),
                                  "../assets/icons/aegis_drive_mode.svg",
                                  this));

  toggles.append(new ParamControl("AegisToyotaScenePresets",
                                  tr("Toyota Scene Presets"),
                                  tr("AEGIS will link the car's physical drive mode selector (Power/Normal/Eco) to custom scene presets (Mountain/City/Highway). Requires enabling ToyotaDriveMode and rebooting/re-onroading.\n\nMountain: Muted automatic throttle, allowing manual accelerator overrides.\nCity: More active start-stop and following distance.\nHighway: Smoother acceleration with active following distance."),
                                  "../assets/icons/aegis_drive_mode.svg",
                                  this));

  toggles.append(new ParamControl("AleSato_AutomaticBrakeHold",
                                  tr("Automatic Brake Hold"),
                                  tr("Activates the car's brakes after 1 seconds stopped. (Only support on Toyota TSS2 Hybrid vehicles)"),
                                  "../assets/icons/aegis_brake_hold.svg",
                                  this));

  toggles.append(new ParamControl("ReverseAccChange",
                                  tr("ACC +/-: Long Press Reverse"),
                                  tr("Change the ACC +/- buttons behavior with cruise speed change in openpilot.\nDisabled (Stock): Short = 1, Long = 5.\nEnabled: Short = 2, Long = 5."),
                                  "../assets/icons/aegis_acc_reverse.svg",
                                  this));

  for (ParamControl *toggle : toggles) {
    if (main_layout->count() != 0) {
      toggle_layout->addWidget(horizontal_line());
    }
    toggle_layout->addWidget(toggle);
  }
}
