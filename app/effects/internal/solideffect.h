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
#ifndef SOLIDEFFECT_H
#define SOLIDEFFECT_H

#include <QImage>

#include "project/effect.h"

class QOpenGLTexture;

class SolidEffect : public Effect {
    Q_OBJECT
  public:
    SolidEffect() = delete;
    SolidEffect(ClipPtr c, const EffectMeta& em);
  protected:
    void redraw(double timecode) override;
  private slots:
    void ui_update(int index);
  private:
    EffectField* solid_type = nullptr;
    EffectField* solid_color_field = nullptr;
    EffectField* opacity_field = nullptr;
    EffectField* checkerboard_size_field = nullptr;
};

#endif // SOLIDEFFECT_H
