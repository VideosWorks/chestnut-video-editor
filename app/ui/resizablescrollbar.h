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
#ifndef RESIZABLESCROLLBAR_H
#define RESIZABLESCROLLBAR_H

#include <QScrollBar>

class ResizableScrollBar : public QScrollBar
{
    Q_OBJECT
public:
    explicit ResizableScrollBar(QWidget * parent = nullptr);
    bool is_resizing() const noexcept;
signals:
    void resize_move(double i);
protected:
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
private:
    bool resize_init {false};
    bool resize_proc {false};
    bool resize_top {false};
    int resize_start {0};

    int resize_start_max {0};
    int resize_start_width {0};
};

#endif // RESIZABLESCROLLBAR_H
