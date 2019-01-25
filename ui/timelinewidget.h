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
#ifndef TIMELINEWIDGET_H
#define TIMELINEWIDGET_H

#include <QTimer>
#include <QWidget>
#include <QScrollBar>
#include <QPainter>

#include "timelinetools.h"
#include "project/sequence.h"
#include "project/media.h"

#define GHOST_THICKNESS 2 // thiccccc
#define CLIP_TEXT_PADDING 3

#define TRACK_MIN_HEIGHT 30
#define TRACK_HEIGHT_INCREMENT 10


class Clip;
struct FootageStream;
class Timeline;
class TimelineAction;
class SetSelectionsCommand;

bool same_sign(int a, int b);
void draw_waveform(ClipPtr& clip, const FootageStream& ms, const long media_length, QPainter& p, const QRect& clip_rect,
                   const int waveform_start, const int waveform_limit, const double zoom);

class TimelineWidget : public QWidget {
	Q_OBJECT
public:
	explicit TimelineWidget(QWidget *parent = 0);
	QScrollBar* scrollBar;
	bool bottom_align;
protected:
	void paintEvent(QPaintEvent*);

	void resizeEvent(QResizeEvent *event);

	void mouseDoubleClickEvent(QMouseEvent *event);
	void mousePressEvent(QMouseEvent *event);
	void mouseReleaseEvent(QMouseEvent *event);
	void mouseMoveEvent(QMouseEvent *event);
	void leaveEvent(QEvent *event);

	void dragEnterEvent(QDragEnterEvent *event);
	void dragLeaveEvent(QDragLeaveEvent *event);
	void dropEvent(QDropEvent* event);
	void dragMoveEvent(QDragMoveEvent *event);

	void wheelEvent(QWheelEvent *event);
private:
	void init_ghosts();
	void update_ghosts(const QPoint& mouse_pos, bool lock_frame);
	bool is_track_visible(int track);
	int getTrackFromScreenPoint(int y);
	int getScreenPointFromTrack(int track);
	int getClipIndexFromCoords(long frame, int track);

	int track_resize_mouse_cache;
	int track_resize_old_value;
	bool track_resizing;
	int track_target;

    QVector<ClipPtr> pre_clips;
    QVector<ClipPtr> post_clips;

    SequencePtr self_created_sequence;

	// used for "right click ripple"
	long rc_ripple_min;
	long rc_ripple_max;
    MediaPtr rc_reveal_media;

	QTimer tooltip_timer;
	int tooltip_clip;

	int scroll;

	SetSelectionsCommand* selection_command;
signals:

public slots:
	void setScroll(int);

private slots:
	void reveal_media();
	void right_click_ripple();
	void show_context_menu(const QPoint& pos);
	void toggle_autoscale();
	void tooltip_timer_timeout();
	void rename_clip();
	void show_stabilizer_diag();
	void open_sequence_properties();
};

#endif // TIMELINEWIDGET_H
