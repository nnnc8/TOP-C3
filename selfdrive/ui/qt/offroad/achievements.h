#pragma once

#include <map>
#include <string>

#include "common/aegis_achievements.h"
#include "common/params.h"
#include "selfdrive/ui/qt/widgets/controls.h"

class AchievementsPanel : public ListWidget {
public:
  explicit AchievementsPanel(QWidget *parent = nullptr);
  void showEvent(QShowEvent *event) override;

private:
  void refresh();
  void resetAchievements();

  Params params;
  LabelControl *level_label;
  LabelControl *xp_label;
  LabelControl *daily_xp_label;
  LabelControl *time_label;
  LabelControl *distance_label;
  LabelControl *routes_label;
  LabelControl *badges_label;
  std::map<std::string, LabelControl*> badge_rows;
};
