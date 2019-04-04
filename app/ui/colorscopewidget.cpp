#include "colorscopewidget.h"

#include <QPainter>
#include <QPen>
#include <cmath>

using ui::ColorScopeWidget;

constexpr int HORIZONTAL_STEP = 8;
constexpr int PEN_ALPHA = 24;
constexpr int MINOR_GRID_STEP = 8;
constexpr int MAJOR_GRID_STEP = MINOR_GRID_STEP / 2;

constexpr double LUMA_RED_COEFF = 0.2126;
constexpr double LUMA_GREEN_COEFF = 0.7152;
constexpr double LUMA_BLUE_COEFF = 0.0722;

namespace {
  const QPen r_pen(QColor(255,0,0,PEN_ALPHA));
  const QPen g_pen(QColor(0,255,0,PEN_ALPHA));
  const QPen b_pen(QColor(0,0,255,PEN_ALPHA));
  const QPen bk_pen(Qt::black);
  const QPen bka_pen(QColor(0,0,0, 128));
  const QPen luma_pen(QColor(160,160,160, PEN_ALPHA));
}

ColorScopeWidget::ColorScopeWidget(QWidget *parent) : QWidget(parent)
{

}

/**
 * @brief Update the image used to draw scope
 * @param img Image to be drawn from
 */
void ColorScopeWidget::updateImage(QImage img)
{
  img_ = std::move(img);
}


void ColorScopeWidget::paintEvent(QPaintEvent*/*event*/)
{
  QPainter painter(this);
  // clear last paint
  painter.eraseRect(0, 0, width(), img_.height());

  const auto w_step = img_.width() / width();
  int pos;
  QRgb val;

  // FIXME: far too slow to be done for every pixel
  // with QImage::setPixel the blending of values is lost
  if (mode_ == 1) {
    painter.setPen(luma_pen);
  }
  for (auto w = 0; w < width(); ++w) {
    for (auto h = 0; h < img_.height(); h+=HORIZONTAL_STEP) {
      // draw pixel value (per channel) on y-axis at x-position
      val = img_.pixel(w * w_step, h);
      if (mode_ == 0) {
        pos = qRed(val);
        painter.setPen(r_pen);
        painter.drawPoint(w, height() - pos);
        pos = qGreen(val);
        painter.setPen(g_pen);
        painter.drawPoint(w, height() - pos);
        pos = qBlue(val);
        painter.setPen(b_pen);
        painter.drawPoint(w, height() - pos);
      } else if (mode_ == 1) {
        pos = lround(qRed(val) * LUMA_RED_COEFF) + lround(qGreen(val) * LUMA_GREEN_COEFF) + lround(qBlue(val) * LUMA_BLUE_COEFF);
        painter.drawPoint(w, height() - pos);
      }
    }
  }

  // paint surrounding box
  painter.setPen(bk_pen);
  painter.drawRect(0, 0, width() -1, height()-1);

  // paint grid-lines
  QVector<qreal> dashes;
  dashes << 3 << 3;
  QPen minor_pen(bka_pen);
  minor_pen.setDashPattern(dashes);
  const int major_step = height() / MAJOR_GRID_STEP;
  const int minor_step = height() / MINOR_GRID_STEP;

  for (auto h = minor_step; h < height(); h+=minor_step) {
    if (h % major_step == 0) {
      painter.setPen(bka_pen);
    } else {
      painter.setPen(minor_pen);
    }
    painter.drawLine(1, height() - h, width() - 1, height() -  h);
  }
}
