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
#ifndef TIMELINEHEADER_H
#define TIMELINEHEADER_H

#include <QWidget>
#include <QFontMetrics>
class Viewer;
class QScrollBar;

bool center_scroll_to_playhead(QScrollBar* bar, double zoom, long playhead);

class TimelineHeader : public QWidget
{
    Q_OBJECT
  public:
    explicit TimelineHeader(QWidget *parent = nullptr);

    TimelineHeader(const TimelineHeader& ) = delete;
    TimelineHeader& operator=(const TimelineHeader&) = delete;

    void set_in_point(long p);
    void set_out_point(long p);

    void show_text(bool enable);
    double get_zoom();
    void delete_markers();
    void set_scrollbar_max(QScrollBar* bar, long sequence_end_frame, int offset);

    Viewer* viewer{nullptr};
    bool snapping;

  public slots:
    void update_zoom(double z);
    void set_scroll(int);
    void set_visible_in(long i);
    void show_context_menu(const QPoint &pos);
    void resized_scroll_listener(double d);

  protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void focusOutEvent(QFocusEvent*) override;

  private:
    void update_parents();

    bool dragging;

    bool resizing_workarea;
    bool resizing_workarea_in;
    long temp_workarea_in;
    long temp_workarea_out;
    long sequence_end;

    double zoom;

    long in_visible;

    void set_playhead(int mouse_x);

    QFontMetrics fm;

    int drag_start;
    bool dragging_markers;
    QVector<int> selected_markers;
    QVector<long> selected_marker_original_times;

    long getHeaderFrameFromScreenPoint(int x);
    int getHeaderScreenPointFromFrame(long frame);

    int scroll;

    int height_actual;
    bool text_enabled;

  signals:
};

#endif // TIMELINEHEADER_H
