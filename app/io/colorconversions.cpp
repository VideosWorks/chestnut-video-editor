/*
 * Chestnut. Chestnut is a free non-linear video editor for Linux.
 * Copyright (C) 2019
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "colorconversions.h"

#include <cmath>


int32_t io::color_conversion::rgbToLuma(const QRgb clr)
{
  return static_cast<int32_t>(lround(qRed(clr) * LUMA_RED_COEFF)
                              + lround(qGreen(clr) * LUMA_GREEN_COEFF)
                              + lround(qBlue(clr) * LUMA_BLUE_COEFF));
}
