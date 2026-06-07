#include "selfdrive/ui/qt/onroad/model.h"
#include "selfdrive/ui/qt/tesla_theme.h"
#include "selfdrive/ui/qt/util.h"
#include "system/hardware/hw.h" 

#include <algorithm>
#include <cmath>

constexpr int CLIP_MARGIN = 500;
constexpr float MIN_DRAW_DISTANCE = 10.0;
constexpr float MAX_DRAW_DISTANCE = 100.0;
float ModelRenderer::global_a_rel = 0.0f;
float ModelRenderer::global_a_rel_col = 0.0f;

ModelRenderer::ModelRenderer() {
  for (int i = 0; i < LeadcarLockon_MAX; i++) {
    leadcar_lockon[i] = {0};
  }
  vc_speed = 0.0f;
}

static int get_path_length_idx(const cereal::XYZTData::Reader &line, const float path_height) {
  const auto &line_x = line.getX();
  int max_idx = 0;
  for (int i = 1; i < line_x.size() && line_x[i] <= path_height; ++i) {
    max_idx = i;
  }
  return max_idx;
}

void ModelRenderer::draw(QPainter &painter, const QRect &rect_param) {
  surface_rect = rect_param;
  auto *s = uiState();
  auto &sm = *(s->sm);

  vc_speed = sm["carState"].getCarState().getVEgo();
  // Check if data is up-to-date
  if (sm.rcv_frame("liveCalibration") < s->scene.started_frame ||
      sm.rcv_frame("modelV2") < s->scene.started_frame) {
    return;
  }

  clip_region = surface_rect.adjusted(-CLIP_MARGIN, -CLIP_MARGIN, CLIP_MARGIN, CLIP_MARGIN);
  longitudinal_control = sm["carParams"].getCarParams().getOpenpilotLongitudinalControl();
  path_offset_z = sm["liveCalibration"].getLiveCalibration().getHeight()[0];

  painter.save();

  const auto &model = sm["modelV2"].getModelV2();
  const auto &radar_state = sm["radarState"].getRadarState();
  const auto &lead_one = radar_state.getLeadOne();

  update_model(model, lead_one);
  drawLaneLines(painter);
  drawPath(painter, surface_rect.height());

  if (longitudinal_control && sm.alive("radarState")) {
    update_leads(radar_state, model.getPosition());
    const auto leads = model.getLeadsV3();
    size_t leads_num = leads.size();
    for (size_t i=0; i<leads_num && i < LeadcarLockon_MAX; i++){
      if (leads[i].getProb() > .2){
        drawLockon(painter, leads[i], lead_vertices[i], i);
      }
    }
    const auto &lead_two = radar_state.getLeadTwo();
    if (lead_one.getStatus()) {
      drawLead(painter, lead_one, lead_vertices[0], 0);
    }
    if (lead_two.getStatus() && (std::abs(lead_one.getDRel() - lead_two.getDRel()) > 3.0)) {
      drawLead(painter, lead_two, lead_vertices[1], 1);
    }
  }

  painter.restore();
}

void ModelRenderer::update_leads(const cereal::RadarState::Reader &radar_state, const cereal::XYZTData::Reader &line) {
  for (int i = 0; i < 2; ++i) {
    const auto &lead_data = (i == 0) ? radar_state.getLeadOne() : radar_state.getLeadTwo();
    if (lead_data.getStatus()) {
      float z = line.getZ()[get_path_length_idx(line, lead_data.getDRel())];
      mapToScreen(lead_data.getDRel(), -lead_data.getYRel(), z + path_offset_z, &lead_vertices[i]);
    }
  }
}

void ModelRenderer::update_model(const cereal::ModelDataV2::Reader &model, const cereal::RadarState::LeadData::Reader &lead) {
  const auto &model_position = model.getPosition();
  float max_distance = std::clamp(*(model_position.getX().end() - 1), MIN_DRAW_DISTANCE, MAX_DRAW_DISTANCE);

  // update lane lines
  const auto &lane_lines = model.getLaneLines();
  const auto &line_probs = model.getLaneLineProbs();
  int max_idx = get_path_length_idx(lane_lines[0], max_distance);
  for (int i = 0; i < std::size(lane_line_vertices); i++) {
    lane_line_probs[i] = line_probs[i];
    mapLineToPolygon(lane_lines[i], 0.025 * lane_line_probs[i], 0, &lane_line_vertices[i], max_idx);
  }

  // update road edges
  const auto &road_edges = model.getRoadEdges();
  const auto &edge_stds = model.getRoadEdgeStds();
  for (int i = 0; i < std::size(road_edge_vertices); i++) {
    road_edge_stds[i] = edge_stds[i];
    mapLineToPolygon(road_edges[i], 0.025, 0, &road_edge_vertices[i], max_idx);
  }

  // update path
  if (lead.getStatus()) {
    const float lead_d = lead.getDRel() * 2.;
    max_distance = std::clamp((float)(lead_d - fmin(lead_d * 0.35, 10.)), 0.0f, max_distance);
  }
  max_idx = get_path_length_idx(model_position, max_distance);
  mapLineToPolygon(model_position, 0.9, path_offset_z, &track_vertices, max_idx, false);
}

void ModelRenderer::drawLaneLines(QPainter &painter) {
  // lanelines
  for (int i = 0; i < std::size(lane_line_vertices); ++i) {
    QColor lane_color(80, 255, 180);
    lane_color.setAlphaF(std::clamp<float>(lane_line_probs[i], 0.0f, 0.82f));
    painter.setBrush(lane_color);
    painter.drawPolygon(lane_line_vertices[i]);
  }

  // road edges
  for (int i = 0; i < std::size(road_edge_vertices); ++i) {
    QColor edge_color(255, 190, 90);
    edge_color.setAlphaF(std::clamp<float>(1.0f - road_edge_stds[i], 0.0f, 0.62f));
    painter.setBrush(edge_color);
    painter.drawPolygon(road_edge_vertices[i]);
  }
}

void ModelRenderer::drawPath(QPainter &painter, int height) {
  QLinearGradient bg(0, height, 0, 0);
  updateRainbowPathGradient(bg);

  painter.setBrush(bg);
  painter.drawPolygon(track_vertices);
}

void ModelRenderer::updateRainbowPathGradient(QLinearGradient &bg) {
  static float hue_offset = 0.0f;
  if (vc_speed > 0.0f) {
    hue_offset = std::fmod(hue_offset + std::sqrt(vc_speed) / std::sqrt(145.0f / 3.6f), 360.0f);
  }

  constexpr int stops = 12;
  for (int i = 0; i <= stops; ++i) {
    const float position = static_cast<float>(i) / stops;
    const float hue = std::fmod((position * 360.0f) + hue_offset, 360.0f);
    const float alpha = util::map_val(position, 0.0f, 1.0f, 0.52f, 0.08f);
    bg.setColorAt(position, QColor::fromHslF(hue / 360.0f, 1.0f, 0.50f, alpha));
  }

  bg.setSpread(QGradient::RepeatSpread);
}

void ModelRenderer::drawLead(QPainter &painter, const cereal::RadarState::LeadData::Reader &lead_data,
                             const QPointF &vd, int num) {
  painter.save();
  const float speedBuff = 10.;
  const float leadBuff = 40.;
  const float d_rel = lead_data.getDRel();
  const float v_rel = lead_data.getVRel();

  float fillAlpha = 0;
  if (d_rel < leadBuff) {
    fillAlpha = 255 * (1.0 - (d_rel / leadBuff));
    if (v_rel < 0) {
      fillAlpha += 255 * (-1 * (v_rel / speedBuff));
    }
    fillAlpha = (int)(fmin(fillAlpha, 255));
  }

  float sz = std::clamp((25 * 30) / (d_rel / 3 + 30), 15.0f, 30.0f) * 2.35;
  float x = std::clamp<float>(vd.x(), 0.f, this->surface_rect.width() - sz / 2);
  float y = std::min<float>(vd.y(), this->surface_rect.height() - sz * 0.6);

  float g_xo = sz / 5;
  float g_yo = sz / 10;

  //QPointF glow[] = {{x + (sz * 1.35) + g_xo, y + sz + g_yo}, {x, y - g_yo}, {x - (sz * 1.35) - g_xo, y + sz + g_yo}};
  float homebase_h = 12;
  QPointF glow[] = {{x + (sz * 1.35) + g_xo, y + sz + g_yo + homebase_h}, {x + (sz * 1.35) + g_xo, y + sz + g_yo}, {x, y - g_yo}, {x - (sz * 1.35) - g_xo, y + sz + g_yo}, {x - (sz * 1.35) - g_xo, y + sz + g_yo + homebase_h}, {x, y + sz + homebase_h + g_yo + 10}};
  painter.setBrush(tesla_theme::blue(210));
  painter.drawPolygon(glow, std::size(glow));

  // chevron
  //QPointF chevron[] = {{x + (sz * 1.25), y + sz}, {x, y}, {x - (sz * 1.25), y + sz}};
  QPointF chevron[] = {{x + (sz * 1.25), y + sz + homebase_h}, {x + (sz * 1.25), y + sz}, {x, y}, {x - (sz * 1.25), y + sz}, {x - (sz * 1.25), y + sz + homebase_h}, {x, y + sz + homebase_h - 7}};
  painter.setBrush(tesla_theme::blue(fillAlpha));
  painter.drawPolygon(chevron, std::size(chevron));

  if (num == 0){ //顯示到第 0 輛前車的距離
    //float dist = d_rel; //lead_data.getT()[0];
    QString dist = QString::number(d_rel, 'f', 0) + "m";
    int str_w = 200;
    // float vc_speed = uiState()->scene.car_state.getVEgo();
    QString kmph = QString::number((v_rel + vc_speed)*3.6, 'f', 0) + "k";
    int str_w2 = 200;
//    dist += "<" + QString::number(rect().height()) + ">"; str_w += 500;c2 和 c3 的屏幕高度均為 1020。
//    dist += "<" + QString::number(leads_num) + ">";
//   int str_w = 600; //200;
//    dist += QString::number(v_rel,'f',1) + "v";
//    dist += QString::number(t_rel,'f',1) + "t";
//    dist += QString::number(y_rel,'f',1) + "y";
//    dist += QString::number(a_rel,'f',1) + "a";
    painter.setFont(InterFont(44, QFont::DemiBold));
    painter.setPen(QColor(0x0, 0x0, 0x0 , 200)); //影
    float lock_indicator_dx = 2; //避免向下的十字準星。
    painter.drawText(QRect(x+2+lock_indicator_dx, y-50+2, str_w, 50), Qt::AlignBottom | Qt::AlignLeft, dist);
    painter.drawText(QRect(x+2-lock_indicator_dx-str_w2-2, y-50+2, str_w2, 50), Qt::AlignBottom | Qt::AlignRight, kmph);
    painter.setPen(QColor(0xff, 0xff, 0xff));
    painter.drawText(QRect(x+lock_indicator_dx, y-50, str_w, 50), Qt::AlignBottom | Qt::AlignLeft, dist);
    if (global_a_rel >= global_a_rel_col){
      global_a_rel_col = -0.1; //減少混亂的緩衝區。
      painter.setPen(QColor(0.09*255, 0.945*255, 0.26*255, 255));
    } else {
      global_a_rel_col = 0;
      painter.setPen(QColor(245, 0, 0, 255));
    }
    painter.drawText(QRect(x-lock_indicator_dx-str_w2-2, y-50, str_w2, 50), Qt::AlignBottom | Qt::AlignRight, kmph);
    painter.setPen(Qt::NoPen);
  }
  painter.restore();
}

// Ichirio Stuff
void ModelRenderer::drawLockon(QPainter &painter, const cereal::ModelDataV2::LeadDataV3::Reader &lead_data, const QPointF &vd, int num) {
  painter.save();
  const float d_rel = lead_data.getX()[0];
  float a_rel = lead_data.getA()[0];
  global_a_rel = a_rel;

  float sz = std::clamp((25 * 30) / (d_rel / 3 + 30), 15.0f, 30.0f) * 2.35;
  float x = std::clamp((float)vd.x(), 0.f, this->surface_rect.width() - sz / 2);
  float y = (float)vd.y();

  painter.setCompositionMode(QPainter::CompositionMode_Plus);

  float prob_alpha = lead_data.getProb();
  if (prob_alpha < 0){
    prob_alpha = 0;
  } else if (prob_alpha > 1.0){
    prob_alpha = 1.0;
  }
  prob_alpha *= 245;

  painter.setPen(QPen(QColor(0.09*255, 0.945*255, 0.26*255, prob_alpha), 2));
  painter.setBrush(QColor(0, 0, 0, 0));
  float ww = 300, hh = 300;
  if (Hardware::TICI()){
    ww *= 1.25; hh *= 1.25;
  }
  float d = d_rel;
  if (d < 1){
    d = 1;
  }

  leadcar_lockon[num].x = leadcar_lockon[num].x + (x - leadcar_lockon[num].x) / 6;
  leadcar_lockon[num].y = leadcar_lockon[num].y + (y - leadcar_lockon[num].y) / 6;
  leadcar_lockon[num].d = leadcar_lockon[num].d + (d - leadcar_lockon[num].d) / 6;
  x = leadcar_lockon[num].x;
  y = leadcar_lockon[num].y;
  d = leadcar_lockon[num].d;
  if (d < 1){
    d = 1;
  }

  leadcar_lockon[num].a = leadcar_lockon[num].a + (a_rel - leadcar_lockon[num].a) / 10;
  a_rel = leadcar_lockon[num].a;

  float dh = 50;
  if (!uiState()->scene.wide_cam) {
    float dd = d;
    dd -= 25;
    dd /= (75.0/2);
    dd += 1;
    if (dd < 1) dd = 1;
    dh /= dd;
  } else {
    ww *= 0.5; hh *= 0.5;
    dh = 100;
    float dd = d;
    dd -= 5;
    dd /= (95.0/10);
    dd += 1;
    if (dd < 1) dd = 1;
    dh /= dd*dd;
  }

  ww = ww * 2 * 5 / d;
  hh = hh * 2 * 5 / d;
  y = std::fmin(surface_rect.height(), y - dh) + dh;
  QRect r = QRect(x - ww/2, y - hh - dh, ww, hh);

  float y0 = leadcar_lockon[0].x * leadcar_lockon[0].d;
  float y1 = leadcar_lockon[1].x * leadcar_lockon[1].d;

  painter.setFont(InterFont(38, QFont::DemiBold));
  if (num == 0) {
    painter.setPen(QPen(QColor(0.09*255, 0.945*255, 0.26*255, prob_alpha), 2));
    painter.drawRect(r);

    if (leadcar_lockon[0].x > leadcar_lockon[1].x - 20) {
      leadcar_lockon[num].lxt = leadcar_lockon[num].lxt + (r.right() - leadcar_lockon[num].lxt) / 20;
      leadcar_lockon[num].lxf = leadcar_lockon[num].lxf + (surface_rect.width() - leadcar_lockon[num].lxf) / 20;
    } else {
      leadcar_lockon[num].lxt = leadcar_lockon[num].lxt + (r.left() - leadcar_lockon[num].lxt) / 20;
      leadcar_lockon[num].lxf = leadcar_lockon[num].lxf + (0 - leadcar_lockon[num].lxf) / 20;
    }
    painter.drawText(r, Qt::AlignTop | Qt::AlignLeft, " " + QString::number(num+1));

    float lxt = leadcar_lockon[num].lxt;
    if (lxt < r.left()) {
      lxt = r.left();
    } else if (lxt > r.right()) {
      lxt = r.right();
    }
    painter.drawLine(lxt, r.top(), leadcar_lockon[num].lxf, 0);

    if (ww >= 40) {
      painter.setPen(Qt::NoPen);
      float wwa = ww * 0.15;
      if (wwa > 40) {
        wwa = 40;
      } else if (wwa < 10) {
        wwa = 10;
      }
      if (wwa > ww) {
        wwa = ww;
      }

      float hha = 0;
      if (a_rel > 0) {
        hha = 1 - 0.1 / a_rel;
        painter.setBrush(QColor(0.09*255, 0.945*255, 0.26*255, prob_alpha*0.9));

        if (hha < 0) {
          hha = 0;
        }
        hha = hha * hh;

        QPointF meter[] = {{(float)x + ww/2 - wwa/2 - wwa/2 * hha / hh , (float)y - hh - dh + (hh-hha)},{(float)x + ww/2 , (float)y - hh - dh + (hh-hha)}, {(float)x + ww/2 , (float)y - hh - dh + hh}, {(float)x + ww/2 - wwa/2 , (float)y - hh - dh + hh}};
        painter.drawPolygon(meter, std::size(meter));
      }
      if (a_rel < 0) {
        hha = 1 + 0.1 / a_rel;
        painter.setBrush(QColor(245, 0, 0, prob_alpha));
        if (hha < 0) {
          hha = 0;
        }
        hha = hha * hh;

        QPointF meter[] = {{(float)x + ww/2 - wwa/2 , (float)y - hh - dh},{(float)x + ww/2 , (float)y - hh - dh}, {(float)x + ww/2 , (float)y - hh - dh + hha}, {(float)x + ww/2 - wwa/2 - wwa/2 * hha / hh, (float)y - hh - dh + hha}};
        painter.drawPolygon(meter, std::size(meter));
      }
    }

    if (std::abs(y0 - y1) <= 300){
      leadcar_lockon[num].lockOK = leadcar_lockon[num].lockOK + (40 - leadcar_lockon[num].lockOK) / 5;
    } else {
      leadcar_lockon[num].lockOK = leadcar_lockon[num].lockOK + (0 - leadcar_lockon[num].lockOK) / 5;
    }
    float td = leadcar_lockon[num].lockOK;
    
    if (td >= 3) {
      float dd = leadcar_lockon[num].d;
      if (dd < 10) {
        dd = 10;
      }
      dd -= 10;
      dd /= (90.0/2);
      dd += 1;
      td /= dd;

      float tlw = 8;
      float tlw_2 = tlw / 2;
      painter.setPen(QPen(QColor(0.09*255, 0.945*255, 0.26*255, prob_alpha), tlw));
      painter.drawLine(r.center().x(), r.top()-tlw_2, r.center().x(), r.top() - td);
      painter.drawLine(r.left()-tlw_2, r.center().y(), r.left() - td, r.center().y());
      painter.drawLine(r.right()+tlw_2, r.center().y(), r.right() + td, r.center().y());
      painter.drawLine(r.center().x(), r.bottom()+tlw_2, r.center().x(), r.bottom() + td);
    }

  } else if (true){
    if (num == 1) {
      if (std::abs(y0 - y1) > 300) {
        painter.setPen(QPen(QColor(245, 0, 0, prob_alpha), 2));
      } else {
        painter.setPen(QPen(QColor(0.09*255, 0.945*255, 0.26*255, prob_alpha), 2));
      }

      if (leadcar_lockon[0].x > leadcar_lockon[1].x - 20) {
        leadcar_lockon[num].lxt = leadcar_lockon[num].lxt + (r.left() - leadcar_lockon[num].lxt) / 20;
        leadcar_lockon[num].lxf = leadcar_lockon[num].lxf + (0 - leadcar_lockon[num].lxf) / 20;
      } else {
        leadcar_lockon[num].lxt = leadcar_lockon[num].lxt + (r.right() - leadcar_lockon[num].lxt) / 20;
        leadcar_lockon[num].lxf = leadcar_lockon[num].lxf + (surface_rect.width() - leadcar_lockon[num].lxf) / 20;
      }

      float lxt = leadcar_lockon[num].lxt;
      if (lxt < r.left()) {
        lxt = r.left();
      } else if (lxt > r.right()) {
        lxt = r.right();
      }
      painter.drawLine(lxt, r.top(), leadcar_lockon[num].lxf, 0);
    } else if (num == 2) {
      painter.setPen(QPen(QColor(0.09*255, 0.945*255, 0.26*255, prob_alpha), 2));
    } else {
      painter.setPen(QPen(QColor(0.09*255, 0.945*255, 0.26*255, prob_alpha), 2));
    }

    painter.drawRect(r);

    if (ww >= 80) {
      float d_lim = 12;
      if (!uiState()->scene.wide_cam) {
        d_lim = 32;
      }
      if (num == 0 || (num==1 && (d_rel < d_lim || std::abs(y0 - y1) > 300))) {
        painter.drawText(r, Qt::AlignBottom | Qt::AlignLeft, " " + QString::number(num+1));
      }
    }
  }
  
  painter.setPen(Qt::NoPen);
  painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
  painter.restore();
}

// Projects a point in car to space to the corresponding point in full frame image space.
bool ModelRenderer::mapToScreen(float in_x, float in_y, float in_z, QPointF *out) {
  Eigen::Vector3f input(in_x, in_y, in_z);
  auto pt = car_space_transform * input;
  *out = QPointF(pt.x() / pt.z(), pt.y() / pt.z());
  return clip_region.contains(*out);
}

void ModelRenderer::mapLineToPolygon(const cereal::XYZTData::Reader &line, float y_off, float z_off,
                                     QPolygonF *pvd, int max_idx, bool allow_invert) {
  const auto line_x = line.getX(), line_y = line.getY(), line_z = line.getZ();
  QPointF left, right;
  pvd->clear();
  for (int i = 0; i <= max_idx; i++) {
    // highly negative x positions  are drawn above the frame and cause flickering, clip to zy plane of camera
    if (line_x[i] < 0) continue;

    bool l = mapToScreen(line_x[i], line_y[i] - y_off, line_z[i] + z_off, &left);
    bool r = mapToScreen(line_x[i], line_y[i] + y_off, line_z[i] + z_off, &right);
    if (l && r) {
      // For wider lines the drawn polygon will "invert" when going over a hill and cause artifacts
      if (!allow_invert && pvd->size() && left.y() > pvd->back().y()) {
        continue;
      }
      pvd->push_back(left);
      pvd->push_front(right);
    }
  }
}
