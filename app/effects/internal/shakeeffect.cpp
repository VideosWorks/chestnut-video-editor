/* 
 * Olive. Olive is a free non-linear video editor for Windows, macOS, and Linux.
 * Copyright (C) 2018  {{ organization }}
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 *along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include "shakeeffect.h"

#include <QGridLayout>
#include <QLabel>
#include <QtMath>
#include <QOpenGLFunctions>

#include "ui/labelslider.h"
#include "ui/collapsiblewidget.h"
#include "project/clip.h"
#include "project/sequence.h"
#include "panels/timeline.h"

#include "debug.h"

ShakeEffect::ShakeEffect(ClipPtr c, const EffectMeta *em) : Effect(c, em)
{
  setCapability(Capability::COORDS);
  EffectRowPtr intensity_row = add_row(tr("Intensity"));
  intensity_val = intensity_row->add_field(EffectFieldType::DOUBLE, "intensity");
  intensity_val->set_double_minimum_value(0);

  EffectRowPtr rotation_row = add_row(tr("Rotation"));
  rotation_val = rotation_row->add_field(EffectFieldType::DOUBLE, "rotation");
  rotation_val->set_double_minimum_value(0);

  EffectRowPtr frequency_row = add_row(tr("Frequency"));
  frequency_val = frequency_row->add_field(EffectFieldType::DOUBLE, "frequency");
  frequency_val->set_double_minimum_value(0);

  // set defaults
  intensity_val->set_double_default_value(25);
  rotation_val->set_double_default_value(10);
  frequency_val->set_double_default_value(5);

  const auto limit = std::numeric_limits<int32_t>::max();
  for (int i=0;i<RANDOM_VAL_SIZE;i++) {
    random_vals[i] = static_cast<double>(this->randomNumber<int32_t>()) / limit;
  }
}

void ShakeEffect::process_coords(double timecode, GLTextureCoords& coords, int)
{
  int lim = RANDOM_VAL_SIZE/6;

  double multiplier = intensity_val->get_double_value(timecode)/lim;
  double rotmult = rotation_val->get_double_value(timecode)/lim/10;
  double x = timecode * frequency_val->get_double_value(timecode);

  double xoff = 0;
  double yoff = 0;
  double rotoff = 0;

  for (int i=0;i<lim;i++) {
    int offset = 6*i;
    xoff += qSin((x+random_vals[offset])*random_vals[offset+1]);
    yoff += qSin((x+random_vals[offset+2])*random_vals[offset+3]);
    rotoff += qSin((x+random_vals[offset+4])*random_vals[offset+5]);
  }

  xoff *= multiplier;
  yoff *= multiplier;
  rotoff *= rotmult;

  coords.vertices_[0].x_ += xoff;
  coords.vertices_[1].x_ += xoff;
  coords.vertices_[3].x_ += xoff;
  coords.vertices_[2].x_ += xoff;

  coords.vertices_[0].y_ += yoff;
  coords.vertices_[1].y_ += yoff;
  coords.vertices_[3].y_ += yoff;
  coords.vertices_[2].y_ += yoff;

  glRotatef(rotoff, 0, 0, 1);
}
