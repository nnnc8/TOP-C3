#pragma once

#include <map>
#include <string>

#include <QFrame>
#include <QLabel>
#include <QProgressBar>

#include "common/aegis_achievements.h"
#include "common/params.h"
#include "selfdrive/ui/qt/widgets/controls.h"

class AchievementsPanel : public QWidget {
  Q_OBJECT

public:
  explicit AchievementsPanel(QWidget *parent = nullptr);
  void showEvent(QShowEvent *event) override;

private:
  QString formatAssistedTime(double seconds) const;
  QString formatDistance(double meters) const;
  QString badgeTitle(const AegisBadge &badge) const;
  QString badgeDescription(const AegisBadge &badge) const;
  void refresh();
  void resetAchievements();

  Params params;
  QLabel *level_value;
  QLabel *xp_progress_label;
  QProgressBar *xp_bar;
  QLabel *daily_xp_value;
  QProgressBar *daily_xp_bar;
  QLabel *time_value;
  QLabel *distance_value;
  QLabel *routes_value;
  QLabel *accolade_summary;
  std::map<std::string, QFrame*> badge_cards;
  std::map<std::string, QLabel*> badge_state_labels;
};
