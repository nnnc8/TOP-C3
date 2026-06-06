#pragma once

#include <QColor>

namespace tesla_theme {

inline QColor blue(int alpha = 255) {
  return QColor(0, 72, 255, alpha);
}

inline QColor blue_dark(int alpha = 255) {
  return QColor(0, 57, 204, alpha);
}

inline QColor panel(int alpha = 190) {
  return QColor(4, 8, 18, alpha);
}

inline QColor panel_border(int alpha = 150) {
  return QColor(0, 72, 255, alpha);
}

inline QColor warning_red(int alpha = 255) {
  return QColor(201, 34, 49, alpha);
}

}  // namespace tesla_theme
