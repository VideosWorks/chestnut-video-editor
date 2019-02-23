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
#ifndef KEYFRAMEVIEW_H
#define KEYFRAMEVIEW_H

#include <QWidget>
#include <QPainter>

#include "project/effectrow.h"

class Clip;
class Effect;
class EffectField;
class TimelineHeader;

class KeyframeView : public QWidget {
    Q_OBJECT
  public:
    explicit KeyframeView(QWidget* parent = nullptr);

    KeyframeView(const KeyframeView& ) = delete;
    KeyframeView& operator=(const KeyframeView&) = delete;

    void delete_selected_keyframes();

    TimelineHeader* header;

    int64_t visible_in;
    int64_t visible_out;
  public slots:
    void set_x_scroll(int);
    void set_y_scroll(int);
    void resize_move(double d);
  private:
    long adjust_row_keyframe(EffectRowPtr row, long time);
    QVector<EffectField*> selected_fields;
    QVector<int> selected_keyframes;
    QVector<int> rowY;
    QVector<EffectRowPtr> rows;
    QVector<long> old_key_vals;
    void mousePressEvent(QMouseEvent* event);
    void mouseMoveEvent(QMouseEvent* event);
    void mouseReleaseEvent(QMouseEvent *event);
    void paintEvent(QPaintEvent *event);
    bool mousedown;
    bool dragging;
    bool keys_selected;
    bool select_rect;
    bool scroll_drag;

    bool keyframeIsSelected(EffectField* field, int keyframe);

    long drag_frame_start;
    long last_frame_diff;
    int rect_select_x;
    int rect_select_y;
    int rect_select_w;
    int rect_select_h;
    int rect_select_offset;

    int x_scroll;
    int y_scroll;

    void update_keys();
  private slots:
    void show_context_menu(const QPoint& pos);
    void menu_set_key_type(QAction*);
};

#endif // KEYFRAMEVIEW_H
