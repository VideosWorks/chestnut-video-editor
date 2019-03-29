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
#ifndef GRAPHEDITOR_H
#define GRAPHEDITOR_H

#include <QDockWidget>

#include "project/keyframe.h"

class GraphView;
class TimelineHeader;
class QPushButton;
class EffectRow;
class QHBoxLayout;
class LabelSlider;
class QLabel;
class KeyframeNavigator;

class GraphEditor : public QDockWidget {
    Q_OBJECT
  public:
    explicit GraphEditor(QWidget* parent = nullptr);

    GraphEditor(const GraphEditor& ) = delete;
    GraphEditor& operator=(const GraphEditor&) = delete;

    void update_panel();
    void set_row(EffectRow* r);
    bool view_is_focused();
    bool view_is_under_mouse();
    void delete_selected_keys();
    void select_all();
  private:
    GraphView* view;
    TimelineHeader* header;
    QHBoxLayout* value_layout;
    QVector<LabelSlider*> slider_proxies;
    QVector<QPushButton*> slider_proxy_buttons;
    QVector<LabelSlider*> slider_proxy_sources;
    QLabel* current_row_desc;
    EffectRow* row;
    KeyframeNavigator* keyframe_nav;
    QPushButton* linear_button;
    QPushButton* bezier_button;
    QPushButton* hold_button;
  private slots:
    void set_key_button_enabled(bool e, const KeyframeType type);
    void passthrough_slider_value();
    void set_keyframe_type();
    void set_field_visibility(bool b);
};

#endif // GRAPHEDITOR_H
