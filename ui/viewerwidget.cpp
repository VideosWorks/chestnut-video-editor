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
#include "viewerwidget.h"

#include "panels/panels.h"
#include "panels/viewer.h"
#include "panels/timeline.h"
#include "panels/project.h"
#include "project/sequence.h"
#include "project/clip.h"
#include "project/effect.h"
#include "project/transition.h"
#include "playback/playback.h"
#include "playback/audio.h"
#include "project/footage.h"
#include "io/config.h"
#include "debug.h"
#include "io/math.h"
#include "ui/collapsiblewidget.h"
#include "project/undo.h"
#include "project/media.h"
#include "ui/viewercontainer.h"
#include "io/avtogl.h"
#include "ui/timelinewidget.h"
#include "ui/renderfunctions.h"
#include "ui/renderthread.h"
#include "ui/viewerwindow.h"
#include "mainwindow.h"

#include <QPainter>
#include <QAudioOutput>
#include <QOpenGLShaderProgram>
#include <QtMath>
#include <QOpenGLFramebufferObject>
#include <QMouseEvent>
#include <QMimeData>
#include <QDrag>
#include <QMenu>
#include <QOffscreenSurface>
#include <QFileDialog>
#include <QPolygon>
#include <QDesktopWidget>
#include <QInputDialog>
#include <QApplication>
#include <QScreen>
#include <QMessageBox>

extern "C" {
#include <libavformat/avformat.h>
}


namespace {
const int RETRY_INTERVAL = 50;
const int SURFACE_DEPTH = 24;
const double DEFAULT_WAVEFORM_ZOOM = 1.0;
}

ViewerWidget::ViewerWidget(QWidget *parent) :
    QOpenGLWidget(parent),
    waveform(false),
    dragging(false),
    gizmos(nullptr),
    selected_gizmo(nullptr),
    waveform_zoom(DEFAULT_WAVEFORM_ZOOM),
    waveform_scroll(0),
    window(nullptr)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);

    QSurfaceFormat format;
    format.setDepthBufferSize(SURFACE_DEPTH);
    setFormat(format);

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(show_context_menu()));

    renderer = new RenderThread();
    renderer->start(QThread::HighPriority);
    connect(renderer, SIGNAL(ready()), this, SLOT(queue_repaint()));
    connect(renderer, SIGNAL(finished()), renderer, SLOT(deleteLater()));
}

ViewerWidget::~ViewerWidget() {
    if (window != nullptr) {
        window->close();
        delete window;
    }
    renderer->cancel();
    delete renderer;
}

void ViewerWidget::delete_function() {
    closeActiveClips(viewer->getSequence());
}

void ViewerWidget::set_waveform_scroll(int s) {
    if (waveform) {
        waveform_scroll = s;
        update();
    }
}

void ViewerWidget::show_context_menu() {
    QMenu menu(this);

    QAction* save_frame_as_image = menu.addAction(tr("Save Frame as Image..."));
    connect(save_frame_as_image, SIGNAL(triggered(bool)), this, SLOT(save_frame()));

    /*QAction* show_fullscreen_action = menu.addAction(tr("Show Fullscreen"));
    connect(show_fullscreen_action, SIGNAL(triggered()), this, SLOT(show_fullscreen()));*/
    QMenu* fullscreen_menu = menu.addMenu(tr("Show Fullscreen"));
    QList<QScreen*> screens = QGuiApplication::screens();
    if (window != nullptr && window->isVisible()) {
        fullscreen_menu->addAction(tr("Disable"));
    }
    for (int i=0;i<screens.size();i++) {
        QAction* screen_action = fullscreen_menu->addAction(tr("Screen %1: %2x%3").arg(
                                                                QString::number(i),
                                                                QString::number(screens.at(i)->size().width()),
                                                                QString::number(screens.at(i)->size().height())));
        screen_action->setData(i);
    }
    connect(fullscreen_menu, SIGNAL(triggered(QAction*)), this, SLOT(fullscreen_menu_action(QAction*)));

    QMenu zoom_menu(tr("Zoom"));
    QAction* fit_zoom = zoom_menu.addAction(tr("Fit"));
    connect(fit_zoom, SIGNAL(triggered(bool)), this, SLOT(set_fit_zoom()));
    zoom_menu.addAction("10%")->setData(0.1);
    zoom_menu.addAction("25%")->setData(0.25);
    zoom_menu.addAction("50%")->setData(0.5);
    zoom_menu.addAction("75%")->setData(0.75);
    zoom_menu.addAction("100%")->setData(1.0);
    zoom_menu.addAction("150%")->setData(1.5);
    zoom_menu.addAction("200%")->setData(2.0);
    zoom_menu.addAction("400%")->setData(4.0);
    QAction* custom_zoom = zoom_menu.addAction(tr("Custom"));
    connect(custom_zoom, SIGNAL(triggered(bool)), this, SLOT(set_custom_zoom()));
    connect(&zoom_menu, SIGNAL(triggered(QAction*)), this, SLOT(set_menu_zoom(QAction*)));
    menu.addMenu(&zoom_menu);

    if (!viewer->is_main_sequence()) {
        menu.addAction(tr("Close Media"), viewer, SLOT(close_media()));
    }

    menu.exec(QCursor::pos());
}

void ViewerWidget::save_frame() {
    QFileDialog fd(this);
    fd.setAcceptMode(QFileDialog::AcceptSave);
    fd.setFileMode(QFileDialog::AnyFile);
    fd.setWindowTitle(tr("Save Frame"));
    fd.setNameFilter("Portable Network Graphic (*.png);;JPEG (*.jpg);;Windows Bitmap (*.bmp);;Portable Pixmap (*.ppm);;X11 Bitmap (*.xbm);;X11 Pixmap (*.xpm)");

    if (fd.exec()) {
        QString fn = fd.selectedFiles().at(0);
        QString selected_ext = fd.selectedNameFilter().mid(fd.selectedNameFilter().indexOf(QRegExp("\\*.[a-z][a-z][a-z]")) + 1, 4);
        if (!fn.endsWith(selected_ext,  Qt::CaseInsensitive)) {
            fn += selected_ext;
        }

        renderer->start_render(context(), viewer->getSequence(), fn);
    }
}

void ViewerWidget::queue_repaint() {
    update();
}

void ViewerWidget::fullscreen_menu_action(QAction *action) {
    if (window == nullptr) {
        QMessageBox::critical(this, "Error", "Failed to create viewer window");
    } else {
        if (action->data().isNull()) {
            window->hide();
        } else {
            QScreen* selected_screen = QGuiApplication::screens().at(action->data().toInt());
            window->showFullScreen();
            window->setGeometry(selected_screen->geometry());

            // HACK: window seems to show with distorted texture on first showing, so we queue an update after it's shown
            QTimer::singleShot(100, window, SLOT(update()));
        }
    }
}

void ViewerWidget::set_fit_zoom() {
    container->fit = true;
    container->adjust();
}

void ViewerWidget::set_custom_zoom() {
    bool ok;
    double d = QInputDialog::getDouble(this,
                                       tr("Viewer Zoom"),
                                       tr("Set Custom Zoom Value:"),
                                       container->zoom*100, 0, 2147483647, 2, &ok);
    if (ok) {
        container->fit = false;
        container->zoom = d*0.01;
        container->adjust();
    }
}

void ViewerWidget::set_menu_zoom(QAction* action) {
    const QVariant& data = action->data();
    if (!data.isNull()) {
        container->fit = false;
        container->zoom = data.toDouble();
        container->adjust();
    }
}

void ViewerWidget::retry() {
    update();
}

void ViewerWidget::initializeGL() {
    initializeOpenGLFunctions();

    connect(context(), SIGNAL(aboutToBeDestroyed()), this, SLOT(context_destroy()), Qt::DirectConnection);

    window = new ViewerWindow(context());
}

void ViewerWidget::frame_update() {
    if (auto sqn = viewer->getSequence()) {
        const auto render_audio = (viewer->playing || audio_rendering);

        // send context to other thread for drawing
        if (waveform) {
            update();
        } else {
            doneCurrent();
            renderer->start_render(context(), sqn);
        }

        // render the audio
        compose_audio(viewer, sqn, render_audio);
    }
}

RenderThread *ViewerWidget::get_renderer() {
    return renderer;
}

//void ViewerWidget::resizeGL(int w, int h)
//{
//}

/*void ViewerWidget::paintEvent(QPaintEvent *e) {
    if (!rendering) {
        QOpenGLWidget::paintEvent(e);
    }
}*/

void ViewerWidget::seek_from_click(int x) {
    viewer->seek(getFrameFromScreenPoint(waveform_zoom, x+waveform_scroll));
}

void ViewerWidget::context_destroy() {
    makeCurrent();
    if (viewer->getSequence() != nullptr) {
        closeActiveClips(viewer->getSequence());
    }
    if (window != nullptr) {
        delete window;
    }
    //QMetaObject::invokeMethod(renderer, "delete_ctx", Qt::QueuedConnection);
    renderer->delete_ctx();
    doneCurrent();
}

EffectGizmoPtr ViewerWidget::get_gizmo_from_mouse(int x, int y) {
    if (gizmos != nullptr) {
        double multiplier = double(viewer->getSequence()->getWidth()) / double(width());
        QPoint mouse_pos(qRound(x*multiplier), qRound((height()-y)*multiplier));
        int dot_size = 2 * qRound(GIZMO_DOT_SIZE * multiplier);
        int target_size = 2 * qRound(GIZMO_TARGET_SIZE * multiplier);
        for (int i=0;i<gizmos->gizmo_count();i++) {
            EffectGizmoPtr g = gizmos->gizmo(i);

            switch (g->get_type()) {
            case GIZMO_TYPE_DOT:
                if (mouse_pos.x() > g->screen_pos[0].x() - dot_size
                        && mouse_pos.y() > g->screen_pos[0].y() - dot_size
                        && mouse_pos.x() < g->screen_pos[0].x() + dot_size
                        && mouse_pos.y() < g->screen_pos[0].y() + dot_size) {
                    return g;
                }
                break;
            case GIZMO_TYPE_POLY:
                if (QPolygon(g->screen_pos).containsPoint(mouse_pos, Qt::OddEvenFill)) {
                    return g;
                }
                break;
            case GIZMO_TYPE_TARGET:
                if (mouse_pos.x() > g->screen_pos[0].x() - target_size
                        && mouse_pos.y() > g->screen_pos[0].y() - target_size
                        && mouse_pos.x() < g->screen_pos[0].x() + target_size
                        && mouse_pos.y() < g->screen_pos[0].y() + target_size) {
                    return g;
                }
                break;
            }

        }
    }
    return nullptr;
}

void ViewerWidget::move_gizmos(QMouseEvent *event, bool done) {
    if (selected_gizmo != nullptr) {
        double multiplier = double(viewer->getSequence()->getWidth()) / double(width());

        int x_movement = qRound((event->pos().x() - drag_start_x)*multiplier);
        int y_movement = qRound((event->pos().y() - drag_start_y)*multiplier);

        gizmos->gizmo_move(selected_gizmo, x_movement, y_movement, gizmos->parent_clip->get_timecode(gizmos->parent_clip->sequence->playhead), done);

        gizmo_x_mvmt += x_movement;
        gizmo_y_mvmt += y_movement;

        drag_start_x = event->pos().x();
        drag_start_y = event->pos().y();

        gizmos->field_changed();
    }
}

void ViewerWidget::mousePressEvent(QMouseEvent* event) {
    if (waveform) {
        seek_from_click(event->x());
    } else if (event->buttons() & Qt::MiddleButton || e_panel_timeline->tool == TIMELINE_TOOL_HAND) {
        container->dragScrollPress(event->pos());
    } else {
        drag_start_x = event->pos().x();
        drag_start_y = event->pos().y();

        gizmo_x_mvmt = 0;
        gizmo_y_mvmt = 0;

        selected_gizmo = get_gizmo_from_mouse(event->pos().x(), event->pos().y());

        if (selected_gizmo != nullptr) {
            selected_gizmo->set_previous_value();
        }
    }
    dragging = true;
}

void ViewerWidget::mouseMoveEvent(QMouseEvent* event) {
    unsetCursor();
    if (e_panel_timeline->tool == TIMELINE_TOOL_HAND) {
        setCursor(Qt::OpenHandCursor);
    }
    if (dragging) {
        if (waveform) {
            seek_from_click(event->x());
        } else if (event->buttons() & Qt::MiddleButton || e_panel_timeline->tool == TIMELINE_TOOL_HAND) {
            container->dragScrollMove(event->pos());
        } else if (gizmos == nullptr) {
            QDrag* drag = new QDrag(this);
            QMimeData* mimeData = new QMimeData;
            mimeData->setText("h"); // QMimeData will fail without some kind of data
            drag->setMimeData(mimeData);
            drag->exec();
            dragging = false;
        } else {
            move_gizmos(event, false);
        }
    } else {
        EffectGizmoPtr g = get_gizmo_from_mouse(event->pos().x(), event->pos().y());
        if (g != nullptr) {
            if (g->get_cursor() > -1) {
                setCursor(static_cast<enum Qt::CursorShape>(g->get_cursor()));
            }
        }
    }
}

void ViewerWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (dragging && gizmos != nullptr && !(event->button() == Qt::MiddleButton || e_panel_timeline->tool == TIMELINE_TOOL_HAND)) {
        move_gizmos(event, true);
    }
    dragging = false;
}

void ViewerWidget::close_window() {
    if (window != nullptr) window->hide();
}

void ViewerWidget::draw_waveform_func() {
    QPainter p(this);
    if (viewer->getSequence()->using_workarea) {
        int in_x = getScreenPointFromFrame(waveform_zoom, viewer->getSequence()->workarea_in) - waveform_scroll;
        int out_x = getScreenPointFromFrame(waveform_zoom, viewer->getSequence()->workarea_out) - waveform_scroll;

        p.fillRect(QRect(in_x, 0, out_x - in_x, height()), QColor(255, 255, 255, 64));
        p.setPen(Qt::white);
        p.drawLine(in_x, 0, in_x, height());
        p.drawLine(out_x, 0, out_x, height());
    }
    QRect wr = rect();
    wr.setX(wr.x() - waveform_scroll);

    p.setPen(Qt::green);
    draw_waveform(waveform_clip, *waveform_ms, waveform_clip->timeline_info.out, p, wr, waveform_scroll, width()+waveform_scroll, waveform_zoom);
    p.setPen(Qt::red);
    int playhead_x = getScreenPointFromFrame(waveform_zoom, viewer->getSequence()->playhead) - waveform_scroll;
    p.drawLine(playhead_x, 0, playhead_x, height());
}

void ViewerWidget::draw_title_safe_area() {
    double halfWidth = 0.5;
    double halfHeight = 0.5;
    double viewportAr = (double) width() / (double) height();
    double halfAr = viewportAr*0.5;

    if (e_config.use_custom_title_safe_ratio && e_config.custom_title_safe_ratio > 0) {
        if (e_config.custom_title_safe_ratio > viewportAr) {
            halfHeight = (e_config.custom_title_safe_ratio/viewportAr)*0.5;
        } else {
            halfWidth = (viewportAr/e_config.custom_title_safe_ratio)*0.5;
        }
    }

    glLoadIdentity();
    glOrtho(-halfWidth, halfWidth, halfHeight, -halfHeight, 0, 1);

    glColor4f(0.66f, 0.66f, 0.66f, 1.0f);
    glBegin(GL_LINES);

    // action safe rectangle
    glVertex2d(-0.45, -0.45);
    glVertex2d(0.45, -0.45);
    glVertex2d(0.45, -0.45);
    glVertex2d(0.45, 0.45);
    glVertex2d(0.45, 0.45);
    glVertex2d(-0.45, 0.45);
    glVertex2d(-0.45, 0.45);
    glVertex2d(-0.45, -0.45);

    // title safe rectangle
    glVertex2d(-0.4, -0.4);
    glVertex2d(0.4, -0.4);
    glVertex2d(0.4, -0.4);
    glVertex2d(0.4, 0.4);
    glVertex2d(0.4, 0.4);
    glVertex2d(-0.4, 0.4);
    glVertex2d(-0.4, 0.4);
    glVertex2d(-0.4, -0.4);

    // horizontal centers
    glVertex2d(-0.45, 0);
    glVertex2d(-0.375, 0);
    glVertex2d(0.45, 0);
    glVertex2d(0.375, 0);

    // vertical centers
    glVertex2d(0, -0.45);
    glVertex2d(0, -0.375);
    glVertex2d(0, 0.45);
    glVertex2d(0, 0.375);

    glEnd();

    // center cross
    glLoadIdentity();
    glOrtho(-halfAr, halfAr, 0.5, -0.5, -1, 1);

    glBegin(GL_LINES);

    glVertex2d(-0.05, 0);
    glVertex2d(0.05, 0);
    glVertex2d(0, -0.05);
    glVertex2d(0, 0.05);

    glEnd();
}

void ViewerWidget::draw_gizmos() {
    float color[4];
    glGetFloatv(GL_CURRENT_COLOR, color);

    float dot_size = GIZMO_DOT_SIZE / width() * viewer->getSequence()->getWidth();
    float target_size = GIZMO_TARGET_SIZE / width() * viewer->getSequence()->getWidth();

    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, viewer->getSequence()->getWidth(), viewer->getSequence()->getHeight(), 0, -1, 10);
    float gizmo_z = 0.0f;
    for (int j=0;j<gizmos->gizmo_count();j++) {
        EffectGizmoPtr g = gizmos->gizmo(j);
        glColor4f(g->color.redF(), g->color.greenF(), g->color.blueF(), 1.0);
        switch (g->get_type()) {
        case GIZMO_TYPE_DOT: // draw dot
            glBegin(GL_QUADS);
            glVertex3f(g->screen_pos[0].x()-dot_size, g->screen_pos[0].y()-dot_size, gizmo_z);
            glVertex3f(g->screen_pos[0].x()+dot_size, g->screen_pos[0].y()-dot_size, gizmo_z);
            glVertex3f(g->screen_pos[0].x()+dot_size, g->screen_pos[0].y()+dot_size, gizmo_z);
            glVertex3f(g->screen_pos[0].x()-dot_size, g->screen_pos[0].y()+dot_size, gizmo_z);
            glEnd();
            break;
        case GIZMO_TYPE_POLY: // draw lines
            glBegin(GL_LINES);
            for (int k=1;k<g->get_point_count();k++) {
                glVertex3f(g->screen_pos[k-1].x(), g->screen_pos[k-1].y(), gizmo_z);
                glVertex3f(g->screen_pos[k].x(), g->screen_pos[k].y(), gizmo_z);
            }
            glVertex3f(g->screen_pos[g->get_point_count()-1].x(), g->screen_pos[g->get_point_count()-1].y(), gizmo_z);
            glVertex3f(g->screen_pos[0].x(), g->screen_pos[0].y(), gizmo_z);
            glEnd();
            break;
        case GIZMO_TYPE_TARGET: // draw target
            glBegin(GL_LINES);
            glVertex3f(g->screen_pos[0].x()-target_size, g->screen_pos[0].y()-target_size, gizmo_z);
            glVertex3f(g->screen_pos[0].x()+target_size, g->screen_pos[0].y()-target_size, gizmo_z);

            glVertex3f(g->screen_pos[0].x()+target_size, g->screen_pos[0].y()-target_size, gizmo_z);
            glVertex3f(g->screen_pos[0].x()+target_size, g->screen_pos[0].y()+target_size, gizmo_z);

            glVertex3f(g->screen_pos[0].x()+target_size, g->screen_pos[0].y()+target_size, gizmo_z);
            glVertex3f(g->screen_pos[0].x()-target_size, g->screen_pos[0].y()+target_size, gizmo_z);

            glVertex3f(g->screen_pos[0].x()-target_size, g->screen_pos[0].y()+target_size, gizmo_z);
            glVertex3f(g->screen_pos[0].x()-target_size, g->screen_pos[0].y()-target_size, gizmo_z);

            glVertex3f(g->screen_pos[0].x()-target_size, g->screen_pos[0].y(), gizmo_z);
            glVertex3f(g->screen_pos[0].x()+target_size, g->screen_pos[0].y(), gizmo_z);

            glVertex3f(g->screen_pos[0].x(), g->screen_pos[0].y()-target_size, gizmo_z);
            glVertex3f(g->screen_pos[0].x(), g->screen_pos[0].y()+target_size, gizmo_z);
            glEnd();
            break;
        }
    }
    glPopMatrix();

    glColor4f(color[0], color[1], color[2], color[3]);
}

void ViewerWidget::paintGL() {
    if (waveform) {
        draw_waveform_func();
    } else {
        renderer->mutex.lock();

        makeCurrent();

        // clear to solid black

        glClearColor(0.0, 0.0, 0.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT);

        // set color multipler to straight white
        glColor4f(1.0, 1.0, 1.0, 1.0);

        glEnable(GL_TEXTURE_2D);

        // set screen coords to widget size
        glLoadIdentity();
        glOrtho(0, 1, 0, 1, -1, 1);

        // draw texture from render thread

        glBindTexture(GL_TEXTURE_2D, renderer->texColorBuffer);

        glBegin(GL_QUADS);

        glVertex2f(0, 0);
        glTexCoord2f(0, 0);
        glVertex2f(0, 1);
        glTexCoord2f(1, 0);
        glVertex2f(1, 1);
        glTexCoord2f(1, 1);
        glVertex2f(1, 0);
        glTexCoord2f(0, 1);

        glEnd();

        glBindTexture(GL_TEXTURE_2D, 0);

        // draw title/action safe area
        if (e_config.show_title_safe_area) {
            draw_title_safe_area();
        }

        gizmos = renderer->gizmos;
        if (gizmos != nullptr) {
            draw_gizmos();
        }

        glDisable(GL_TEXTURE_2D);

        if (window != nullptr && window->isVisible()) {
            window->set_texture(renderer->texColorBuffer, double(viewer->getSequence()->getWidth())/double(viewer->getSequence()->getHeight()), &renderer->mutex);
        }

        renderer->mutex.unlock();

        if (renderer->did_texture_fail()) {
            doneCurrent();
            renderer->start_render(context(), viewer->getSequence());
        }
    }
}
