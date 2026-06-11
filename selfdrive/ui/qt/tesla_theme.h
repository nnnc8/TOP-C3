#pragma once

#include <QColor>

namespace tesla_theme {

inline QColor background(int alpha = 255) {
  return QColor(3, 6, 14, alpha);
}

inline QColor blue(int alpha = 255) {
  return QColor(122, 174, 206, alpha);
}

inline QColor blue_dark(int alpha = 255) {
  return QColor(73, 124, 156, alpha);
}

inline QColor blue_soft(int alpha = 255) {
  return QColor(159, 209, 236, alpha);
}

inline QColor panel(int alpha = 190) {
  return QColor(4, 8, 18, alpha);
}

inline QColor panel_solid(int alpha = 255) {
  return QColor(13, 17, 27, alpha);
}

inline QColor panel_hover(int alpha = 255) {
  return QColor(22, 29, 44, alpha);
}

inline QColor panel_border(int alpha = 150) {
  return QColor(122, 174, 206, alpha);
}

inline QColor divider(int alpha = 70) {
  return QColor(255, 255, 255, alpha);
}

inline QColor text(int alpha = 255) {
  return QColor(244, 246, 250, alpha);
}

inline QColor text_muted(int alpha = 255) {
  return QColor(166, 176, 190, alpha);
}

inline QColor standby(int alpha = 255) {
  return QColor(108, 116, 130, alpha);
}

inline QColor warning_yellow(int alpha = 255) {
  return QColor(218, 202, 37, alpha);
}

inline QColor warning_red(int alpha = 255) {
  return QColor(201, 34, 49, alpha);
}

}  // namespace tesla_theme
