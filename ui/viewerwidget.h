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
#ifndef VIEWERWIDGET_H
#define VIEWERWIDGET_H

#include <QOpenGLWidget>
#include <QMatrix4x4>
#include <QOpenGLTexture>
#include <QTimer>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>

#include "project/effect.h"
#include "project/clip.h"

class Viewer;
class Clip;
struct FootageStream;
class EffectGizmo;
class ViewerContainer;
struct GLTextureCoords;

class ViewerWidget : public QOpenGLWidget, QOpenGLFunctions
{
	Q_OBJECT
public:
    explicit ViewerWidget(QWidget *parent = 0);

    void paintGL();
    void initializeGL();
	Viewer* viewer;
    ViewerContainer* container;

    QOpenGLFramebufferObject* default_fbo;

	bool waveform;
    ClipPtr waveform_clip;
    const FootageStream* waveform_ms;
    double waveform_zoom;
    int waveform_scroll;

    bool force_quit = false;
public slots:
    void delete_function();
    void set_waveform_scroll(int s);
protected:
    virtual void paintEvent(QPaintEvent *e);
//    void resizeGL(int w, int h);
    virtual void mousePressEvent(QMouseEvent *event);
    virtual void mouseMoveEvent(QMouseEvent *event);
    virtual void mouseReleaseEvent(QMouseEvent *event);
private:
	QTimer retry_timer;
    void drawTitleSafeArea();
	bool dragging;
	void seek_from_click(int x);
    GLuint compose_sequence(QVector<ClipPtr > &nests, bool render_audio);
    GLuint draw_clip(QOpenGLFramebufferObject *clip, GLuint texture, bool clear);
    void process_effect(ClipPtr c, EffectPtr e, double timecode,
                        GLTextureCoords& coords, GLuint& composite_texture,
                        bool& fbo_switcher, int data);
    EffectPtr gizmos;
    int drag_start_x;
    int drag_start_y;
    int gizmo_x_mvmt;
    int gizmo_y_mvmt;
    EffectGizmoPtr selected_gizmo;
    EffectGizmoPtr get_gizmo_from_mouse(const int x_coord, const int y_coord);
    bool drawn_gizmos;
    void move_gizmos(QMouseEvent *event, bool done);
private slots:
	void retry();
	void show_context_menu();
	void save_frame();
    void show_fullscreen();

    void set_fit_zoom();
    void set_custom_zoom();
    void set_menu_zoom(QAction *action);
};

#endif // VIEWERWIDGET_H
