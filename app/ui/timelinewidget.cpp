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
#include "timelinewidget.h"

#include <QPainter>
#include <QColor>
#include <QMouseEvent>
#include <QObject>
#include <QVariant>
#include <QPoint>
#include <QMenu>
#include <QMessageBox>
#include <QtMath>
#include <QScrollBar>
#include <QMimeData>
#include <QToolTip>
#include <QInputDialog>
#include <QStatusBar>

#include "playback/audio.h"
#include "io/config.h"
#include "project/sequence.h"
#include "project/transition.h"
#include "project/clip.h"
#include "project/effect.h"
#include "project/footage.h"
#include "project/media.h"
#include "panels/panelmanager.h"
#include "ui/sourcetable.h"
#include "ui/sourceiconview.h"
#include "project/undo.h"
#include "ui/mainwindow.h"
#include "ui/viewerwidget.h"
#include "dialogs/stabilizerdialog.h"
#include "ui/resizablescrollbar.h"
#include "dialogs/newsequencedialog.h"
#include "ui/mainwindow.h"
#include "ui/rectangleselect.h"
#include "debug.h"


using panels::PanelManager;
using project::FootageStreamPtr;

constexpr int MAX_TEXT_WIDTH = 20;
constexpr int TRANSITION_BETWEEN_RANGE = 40;
constexpr int TOOLTIP_INTERVAL = 500;


namespace {
  const QColor MOUSE_RELEASE_COLOR(192, 192, 64);
  const QColor TRANSITION_COLOR(255, 0, 0, 16);
}

TimelineWidget::TimelineWidget(QWidget *parent): QWidget(parent)
{

  setMouseTracking(true);

  setAcceptDrops(true);

  setContextMenuPolicy(Qt::CustomContextMenu);
  connect(this, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(show_context_menu(const QPoint&)));

  tooltip_timer.setInterval(TOOLTIP_INTERVAL);
  connect(&tooltip_timer, SIGNAL(timeout()), this, SLOT(tooltip_timer_timeout()));
}

void TimelineWidget::right_click_ripple() {
  QVector<Selection> sels;

  Selection s{};
  s.in = rc_ripple_min;
  s.out = rc_ripple_max;
  s.track = PanelManager::timeLine().cursor_track;

  sels.append(s);

  PanelManager::timeLine().delete_selection(sels, true);
}

void TimelineWidget::show_context_menu(const QPoint& pos) {
  if (global::sequence != nullptr) {
    // hack because sometimes right clicking doesn't trigger mouse release event
    PanelManager::timeLine().rect_select_init = false;
    PanelManager::timeLine().rect_select_proc = false;

    QMenu menu(this);

    QAction* undoAction = menu.addAction("&Undo");
    QAction* redoAction = menu.addAction("&Redo");
    connect(undoAction, SIGNAL(triggered(bool)), &MainWindow::instance(), SLOT(undo()));
    connect(redoAction, SIGNAL(triggered(bool)), &MainWindow::instance(), SLOT(redo()));
    undoAction->setEnabled(e_undo_stack.canUndo());
    redoAction->setEnabled(e_undo_stack.canRedo());
    menu.addSeparator();

    // collect all the selected clips
    QVector<ClipPtr> selected_clips;
    for (const auto& clp : global::sequence->clips_) {
      if (clp != nullptr && clp->isSelected(true)) {
        selected_clips.append(clp);
      }
    }

    if (selected_clips.isEmpty()) {
      // no clips are selected
      PanelManager::timeLine().cursor_frame = PanelManager::timeLine().getTimelineFrameFromScreenPoint(pos.x());
      PanelManager::timeLine().cursor_track = getTrackFromScreenPoint(pos.y());

      bool can_ripple_delete = true;
      bool at_end_of_sequence = true;
      rc_ripple_min = 0;
      rc_ripple_max = LONG_MAX;

      for (const auto& clp : global::sequence->clips_) {
        if (clp == nullptr) {
          continue;
        }
        if (clp->timeline_info.in > PanelManager::timeLine().cursor_frame
            || clp->timeline_info.out > PanelManager::timeLine().cursor_frame) {
          at_end_of_sequence = false;
        }
        if (clp->timeline_info.track_ == PanelManager::timeLine().cursor_track) {
          if (clp->timeline_info.in <= PanelManager::timeLine().cursor_frame
              && clp->timeline_info.out >= PanelManager::timeLine().cursor_frame) {
            can_ripple_delete = false;
            break;
          }
          if (clp->timeline_info.out < PanelManager::timeLine().cursor_frame) {
            rc_ripple_min = qMax(rc_ripple_min, clp->timeline_info.out.load());
          } else if (clp->timeline_info.in > PanelManager::timeLine().cursor_frame) {
            rc_ripple_max = qMin(rc_ripple_max, clp->timeline_info.in.load());
          }
        }
      }//for

      if (can_ripple_delete && !at_end_of_sequence) {
        QAction* ripple_delete_action = menu.addAction("R&ipple Delete");
        connect(ripple_delete_action, SIGNAL(triggered(bool)), this, SLOT(right_click_ripple()));
      }

      QAction* seq_settings = menu.addAction("Sequence Settings");
      connect(seq_settings, SIGNAL(triggered(bool)), this, SLOT(open_sequence_properties()));
    } else {
      // clips are selected
      QAction* cutAction = menu.addAction("C&ut");
      connect(cutAction, SIGNAL(triggered(bool)), &MainWindow::instance(), SLOT(cut()));
      QAction* copyAction = menu.addAction("Cop&y");
      connect(copyAction, SIGNAL(triggered(bool)), &MainWindow::instance(), SLOT(copy()));
      QAction* pasteAction = menu.addAction("&Paste");
      connect(pasteAction, SIGNAL(triggered(bool)), &MainWindow::instance(), SLOT(paste()));
      menu.addSeparator();
      QAction* speedAction = menu.addAction("&Speed/Duration");
      connect(speedAction, SIGNAL(triggered(bool)), &MainWindow::instance(), SLOT(open_speed_dialog()));
      QAction* autoscaleAction = menu.addAction("Auto-s&cale");
      autoscaleAction->setCheckable(true);
      connect(autoscaleAction, SIGNAL(triggered(bool)), this, SLOT(toggle_autoscale()));

      QAction* nestAction = menu.addAction("&Nest");
      connect(nestAction, SIGNAL(triggered(bool)), &MainWindow::instance(), SLOT(nest()));

      // stabilizer option
      /*int video_clip_count = 0;
            bool all_video_is_footage = true;
            for (int i=0;i<selected_clips.size();i++) {
                if (selected_clips.at(i)->timeline_info.isVideo()) {
                    video_clip_count++;
                    if (selected_clips.at(i)>timeline_info.media == nullptr
                            || selected_clips.at(i)->timeline_info.media->type() != MediaType::FOOTAGE) {
                        all_video_is_footage = false;
                    }
                }
            }
            if (video_clip_count == 1 && all_video_is_footage) {
                QAction* stabilizerAction = menu.addAction("S&tabilizer");
                connect(stabilizerAction, SIGNAL(triggered(bool)), this, SLOT(show_stabilizer_diag()));
            }*/

      // set autoscale arbitrarily to the first selected clip
      autoscaleAction->setChecked(selected_clips.front()->timeline_info.autoscale);

      // check if all selected clips have the same media for a "Reveal In Project"
      bool same_media = true;
      rc_reveal_media = selected_clips.front()->timeline_info.media;
      for (auto& sel_clip : selected_clips) {
        if (sel_clip->timeline_info.media != rc_reveal_media) {
          same_media = false;
          break;
        }
      }

      if (same_media) {
        QAction* revealInProjectAction = menu.addAction("&Reveal in Project");
        connect(revealInProjectAction, SIGNAL(triggered(bool)), this, SLOT(reveal_media()));
      }

      QAction* rename = menu.addAction("R&ename");
      connect(rename, SIGNAL(triggered(bool)), this, SLOT(rename_clip()));
    }

    menu.exec(mapToGlobal(pos));
  }
}

void TimelineWidget::toggle_autoscale()
{
  auto action = new SetAutoscaleAction();
  for (const auto& clp : global::sequence->clips_) {
    if (clp != nullptr && clp->isSelected(true)) {
      action->clips.append(clp);
    }
  }

  if (action->clips.empty()) {
    delete action;
  } else {
    e_undo_stack.push(action);
  }
}

void TimelineWidget::tooltip_timer_timeout()
{
  if (global::sequence != nullptr) {
    if (tooltip_clip < global::sequence->clips_.size()) {
      ClipPtr c = global::sequence->clips_.at(tooltip_clip);
      if (c != nullptr) {
        QToolTip::showText(QCursor::pos(),
                           tr("%1\nStart: %2\nEnd: %3\nDuration: %4").arg(
                             c->name(),
                             frame_to_timecode(c->timeline_info.in, e_config.timecode_view, global::sequence->frameRate()),
                             frame_to_timecode(c->timeline_info.out, e_config.timecode_view, global::sequence->frameRate()),
                             frame_to_timecode(c->length(), e_config.timecode_view, global::sequence->frameRate())
                             ));
      }
    }
  }
  tooltip_timer.stop();
}

void TimelineWidget::rename_clip() {
  QVector<ClipPtr> selected_clips;
  for (int i=0;i<global::sequence->clips_.size();i++) {
    ClipPtr c = global::sequence->clips_.at(i);
    if (c != nullptr && c->isSelected(true)) {
      selected_clips.append(c);
    }
  }
  if (selected_clips.size() > 0) {
    QString s = QInputDialog::getText(this,
                                      (selected_clips.size() == 1) ? tr("Rename '%1'").arg(selected_clips.at(0)->name()) : tr("Rename multiple clips"),
                                      tr("Enter a new name for this clip:"),
                                      QLineEdit::Normal,
                                      selected_clips.at(0)->name()
                                      );
    if (!s.isEmpty()) {
      RenameClipCommand* rcc = new RenameClipCommand();
      rcc->new_name = s;
      rcc->clips = selected_clips;
      e_undo_stack.push(rcc);
      PanelManager::refreshPanels(true);
    }
  }
}

void TimelineWidget::show_stabilizer_diag() {
  StabilizerDialog sd;
  sd.exec();
}

void TimelineWidget::open_sequence_properties() {
  QVector<MediaPtr> sequence_items;
  QVector<MediaPtr> all_top_level_items;
  for (int i=0; i < Project::model().childCount(); ++i) {
    all_top_level_items.append(Project::model().child(i));
  }
  PanelManager::projectViewer().get_all_media_from_table(all_top_level_items, sequence_items, MediaType::SEQUENCE); // find all sequences in project
  for (int i=0; i<sequence_items.size(); i++) {
    if (sequence_items.at(i)->object<Sequence>() == global::sequence) {
      NewSequenceDialog nsd(this, sequence_items.at(i));
      nsd.exec();
      return;
    }
  }
  QMessageBox::critical(this, tr("Error"), tr("Couldn't locate media wrapper for sequence."));
}

bool same_sign(int a, int b) {
  return (a < 0) == (b < 0);
}

void TimelineWidget::dragEnterEvent(QDragEnterEvent *event) {
  bool import_init = false;

  QVector<MediaPtr> media_list;
  PanelManager::timeLine().importing_files = false;

  // Dragging footage from the project-view window
  if ( (event->source() == PanelManager::projectViewer().tree_view) || (event->source() == PanelManager::projectViewer().icon_view) ) {
    auto items = PanelManager::projectViewer().get_current_selected();
    media_list.resize(items.size());
    for (auto item : items) {
      media_list.append(PanelManager::projectViewer().item_to_media(item));
    }
    import_init = true;
  }

  // Dragging footage from the media-viewer window
  if (event->source() == PanelManager::footageViewer().viewer_widget) {
    auto proposed_seq = PanelManager::footageViewer().getSequence();
    if (proposed_seq != global::sequence) { // don't allow nesting the same sequence
      media_list.append(PanelManager::footageViewer().getMedia());
      import_init = true;
    }
  }

  if (e_config.enable_drag_files_to_timeline && event->mimeData()->hasUrls()) {
    QList<QUrl> urls = event->mimeData()->urls();
    if (!urls.isEmpty()) {
      QStringList file_list;
      for (auto url : urls) {
        file_list.append(url.toLocalFile());
      }

      PanelManager::projectViewer().process_file_list(file_list);

      for (int i=0; i < PanelManager::projectViewer().getMediaSize(); i++) {
        // waits for media to have a duration
        // TODO would be much nicer if this was multithreaded
        if (auto mda = PanelManager::projectViewer().getImportedMedia(i)) {
          if (auto ftg = mda->object<Footage>()) {
            ftg->ready_lock.lock();
            ftg->ready_lock.unlock();

            if (ftg->ready) {
              media_list.append(mda);
            }
          } else {
            qWarning() << "Footage Ptr is NULL";
          }
        } else {
          qWarning() << "Media Ptr is NULL";
        }
      }//for

      if (media_list.isEmpty()) {
        e_undo_stack.undo();
      } else {
        import_init = true;
        PanelManager::timeLine().importing_files = true;
      }
    }
  }

  if (import_init) {
    event->acceptProposedAction();

    long entry_point;
    auto seq = global::sequence;

    if (seq == nullptr) {
      // if no sequence, we're going to create a new one using the clips as a reference
      entry_point = 0;

      self_created_sequence = std::make_shared<Sequence>(media_list, PanelManager::projectViewer().get_next_sequence_name());
      seq = self_created_sequence;
    } else {
      entry_point = PanelManager::timeLine().getTimelineFrameFromScreenPoint(event->pos().x());
      PanelManager::timeLine().drag_frame_start = entry_point + getFrameFromScreenPoint(PanelManager::timeLine().zoom, 50);
      PanelManager::timeLine().drag_track_start = bottom_align ? -1 : 0;
    }

    PanelManager::timeLine().create_ghosts_from_media(seq, entry_point, media_list);

    PanelManager::timeLine().importing = true;
  }
}

void TimelineWidget::dragMoveEvent(QDragMoveEvent *event) {
  if (PanelManager::timeLine().importing) {
    event->acceptProposedAction();

    if (global::sequence != nullptr) {
      QPoint pos = event->pos();
      update_ghosts(pos, event->keyboardModifiers() & Qt::ShiftModifier);
      PanelManager::timeLine().move_insert = ((event->keyboardModifiers() & Qt::ControlModifier)
                                              && (PanelManager::timeLine().tool == TimelineToolType::POINTER || PanelManager::timeLine().importing));
      PanelManager::refreshPanels(false);
    }
  }
}

void TimelineWidget::wheelEvent(QWheelEvent *event) {
  bool alt = (event->modifiers() & Qt::AltModifier);
  int scroll_amount = alt ? (event->angleDelta().x()) : (event->angleDelta().y());
  if (scroll_amount != 0) {
    bool shift = (event->modifiers() & Qt::ShiftModifier);
    bool in = (scroll_amount > 0);
    if (e_config.scroll_zooms != shift) {
      PanelManager::timeLine().set_zoom(in);
    } else {
      QScrollBar* bar = alt ? scrollBar : PanelManager::timeLine().horizontalScrollBar;

      int step = bar->singleStep();
      if (in) step = -step;
      bar->setValue(bar->value() + step);
    }
  }
}

void TimelineWidget::dragLeaveEvent(QDragLeaveEvent* event) {
  event->accept();
  if (PanelManager::timeLine().importing) {
    if (PanelManager::timeLine().importing_files) {
      e_undo_stack.undo();
    }
    PanelManager::timeLine().importing_files = false;
    PanelManager::timeLine().ghosts.clear();
    PanelManager::timeLine().importing = false;
    PanelManager::refreshPanels(false);
  }
}

void delete_area_under_ghosts(ComboAction* ca)
{
  // delete areas before adding
  QVector<Selection> delete_areas;
  for (const auto& g : PanelManager::timeLine().ghosts) {
    Selection sel;
    sel.in = g.in;
    sel.out = g.out;
    sel.track = g.track;
    delete_areas.append(sel);
  }
  PanelManager::timeLine().delete_areas_and_relink(ca, delete_areas);
}

void insert_clips(ComboAction* ca) {
  bool ripple_old_point = true;

  long earliest_old_point = LONG_MAX;
  long latest_old_point = LONG_MIN;

  long earliest_new_point = LONG_MAX;
  long latest_new_point = LONG_MIN;

  QVector<int> ignore_clips;
  for (int i=0;i<PanelManager::timeLine().ghosts.size();i++) {
    const Ghost& g = PanelManager::timeLine().ghosts.at(i);

    earliest_old_point = qMin(earliest_old_point, g.old_in);
    latest_old_point = qMax(latest_old_point, g.old_out);
    earliest_new_point = qMin(earliest_new_point, g.in);
    latest_new_point = qMax(latest_new_point, g.out);

    if (g.clip >= 0) {
      ignore_clips.append(g.clip);
    } else {
      // don't try to close old gap if importing
      ripple_old_point = false;
    }
  }

  PanelManager::timeLine().split_cache.clear();

  for (int i=0;i<global::sequence->clips_.size();i++) {
    ClipPtr c = global::sequence->clips_.at(i);
    if (c != nullptr) {
      // don't split any clips that are moving
      bool found = false;
      for (int j=0;j<PanelManager::timeLine().ghosts.size();j++) {
        if (PanelManager::timeLine().ghosts.at(j).clip == i) {
          found = true;
          break;
        }
      }
      if (!found) {
        if (c->timeline_info.in < earliest_new_point && c->timeline_info.out > earliest_new_point) {
          PanelManager::timeLine().split_clip_and_relink(ca, i, earliest_new_point, true);
        }

        // determine if we should close the gap the old clips left behind
        if (ripple_old_point
            && !((c->timeline_info.in < earliest_old_point && c->timeline_info.out <= earliest_old_point)
                 || (c->timeline_info.in >= latest_old_point && c->timeline_info.out > latest_old_point))
            && !ignore_clips.contains(i)) {
          ripple_old_point = false;
        }
      }
    }
  } //for

  long ripple_length = (latest_new_point - earliest_new_point);

  ripple_clips(ca, global::sequence, earliest_new_point, ripple_length, ignore_clips);

  if (ripple_old_point) {
    // works for moving later clips earlier but not earlier to later
    long second_ripple_length = (earliest_old_point - latest_old_point);

    ripple_clips(ca, global::sequence, latest_old_point, second_ripple_length, ignore_clips);

    if (earliest_old_point < earliest_new_point) {
      for (int i=0;i<PanelManager::timeLine().ghosts.size();i++) {
        Ghost& g = PanelManager::timeLine().ghosts[i];
        g.in += second_ripple_length;
        g.out += second_ripple_length;
      }
      for (int i=0;i<global::sequence->selections_.size();i++) {
        Selection& s = global::sequence->selections_[i];
        s.in += second_ripple_length;
        s.out += second_ripple_length;
      }
    }
  }
}

void TimelineWidget::dropEvent(QDropEvent* event) {
  if (PanelManager::timeLine().importing && !PanelManager::timeLine().ghosts.empty()) {
    event->acceptProposedAction();

    auto ca = new ComboAction();

    auto working_sequence = global::sequence;

    // if we're dropping into nothing, create a new sequences based on the clip being dragged
    if (working_sequence == nullptr) {
      working_sequence = self_created_sequence;
      PanelManager::projectViewer().new_sequence(ca, self_created_sequence, true, nullptr);
      self_created_sequence = nullptr;
    } else if (event->keyboardModifiers() & Qt::ControlModifier) {
      insert_clips(ca);
    } else {
      delete_area_under_ghosts(ca);
    }

    PanelManager::timeLine().add_clips_from_ghosts(ca, working_sequence);

    e_undo_stack.push(ca);

    setFocus();

    PanelManager::refreshPanels(true);
  }
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent *event) {
  if (PanelManager::timeLine().tool == TimelineToolType::EDIT) {
    int clip_index = getClipIndexFromCoords(PanelManager::timeLine().cursor_frame, PanelManager::timeLine().cursor_track);
    if (clip_index >= 0) {
      ClipPtr clip = global::sequence->clips_.at(clip_index);
      if (!(event->modifiers() & Qt::ShiftModifier)) global::sequence->selections_.clear();
      Selection s;
      s.in = clip->timeline_info.in;
      s.out = clip->timeline_info.out;
      s.track = clip->timeline_info.track_;
      global::sequence->selections_.append(s);
      PanelManager::refreshPanels(false);
    }
  }
}

bool isLiveEditing()
{
  return (PanelManager::timeLine().tool == TimelineToolType::EDIT
          || PanelManager::timeLine().tool == TimelineToolType::RAZOR
          || PanelManager::timeLine().creating);
}

void TimelineWidget::mousePressCreatingEvent(Timeline& time_line)
{
  int comp = 0;
  switch (time_line.creating_object) {
    case AddObjectType::TITLE:
      [[fallthrough]];
    case AddObjectType::SOLID:
      [[fallthrough]];
    case AddObjectType::BARS:
      comp = -1;
      break;
    case AddObjectType::TONE:
      [[fallthrough]];
    case AddObjectType::NOISE:
      [[fallthrough]];
    case AddObjectType::AUDIO:
      comp = 1;
      break;
    default:
      qWarning() << "Unhandled object add type" << static_cast<int>(time_line.creating_object);
      break;
  }

  if ((time_line.drag_track_start < 0) == (comp < 0)) {
    Ghost g;
    g.in = g.old_in = g.out = g.old_out = time_line.drag_frame_start;
    g.track = g.old_track = time_line.drag_track_start;
    g.transition = nullptr;
    g.clip = -1;
    g.trimming = true;
    g.trim_in = false;
    time_line.ghosts.append(g);

    time_line.moving_init = true;
    time_line.moving_proc = true;
  }
}

void TimelineWidget::mousePressEvent(QMouseEvent *event)
{
  if (global::sequence == nullptr) {
    return;
  }

  TimelineToolType tool = PanelManager::timeLine().tool;
  if (event->button() == Qt::MiddleButton) {
    tool = TimelineToolType::HAND;
    PanelManager::timeLine().creating = false;
  } else if (event->button() == Qt::RightButton) {
    tool = TimelineToolType::MENU;
    PanelManager::timeLine().creating = false;
  }

  QPoint pos = event->pos();
  if (isLiveEditing()) {
    PanelManager::timeLine().drag_frame_start = PanelManager::timeLine().cursor_frame;
    PanelManager::timeLine().drag_track_start = PanelManager::timeLine().cursor_track;
  } else {
    PanelManager::timeLine().drag_frame_start = PanelManager::timeLine().getTimelineFrameFromScreenPoint(pos.x());
    PanelManager::timeLine().drag_track_start = getTrackFromScreenPoint(pos.y());
  }

//  int clip_index = PanelManager::timeLine().trim_target;
//  if (clip_index == -1) {
//    clip_index = getClipIndexFromCoords(PanelManager::timeLine().drag_frame_start, PanelManager::timeLine().drag_track_start);
//  }

  bool shift = (event->modifiers() & Qt::ShiftModifier);
  bool alt = (event->modifiers() & Qt::AltModifier);

  if (shift) {
    PanelManager::timeLine().selection_offset = global::sequence->selections_.size();
  } else {
    PanelManager::timeLine().selection_offset = 0;
  }

  if (PanelManager::timeLine().creating) {
    mousePressCreatingEvent(PanelManager::timeLine());
  } else {
    switch (tool) {
      case TimelineToolType::POINTER:
        [[fallthrough]];
      case TimelineToolType::RIPPLE:
        [[fallthrough]];
      case TimelineToolType::SLIP:
        [[fallthrough]];
      case TimelineToolType::ROLLING:
        [[fallthrough]];
      case TimelineToolType::SLIDE:
        [[fallthrough]];
      case TimelineToolType::MENU:
      {
        if (track_resizing && tool != TimelineToolType::MENU) {
          track_resize_mouse_cache = event->pos().y();
          PanelManager::timeLine().moving_init = true;
        } else {
          if (auto sel_clip = getClipFromCoords(PanelManager::timeLine().drag_frame_start,
                                                PanelManager::timeLine().drag_track_start)) {
              auto links = sel_clip->linkedClips();
              if (sel_clip->isSelected(true)) {
                if (shift) {
                  PanelManager::timeLine().deselect_area(sel_clip->timeline_info.in,
                                                         sel_clip->timeline_info.out,
                                                         sel_clip->timeline_info.track_);

                  if (!alt) {
                    for (int i=0; i<links.size(); i++) {
                      ClipPtr link = global::sequence->clip(links.at(i));
                      PanelManager::timeLine().deselect_area(link->timeline_info.in,
                                                             link->timeline_info.out,
                                                             link->timeline_info.track_);
                    }
                  }
                } else if (PanelManager::timeLine().tool == TimelineToolType::POINTER
                           && PanelManager::timeLine().transition_select != TA_NO_TRANSITION) {
                  PanelManager::timeLine().deselect_area(sel_clip->timeline_info.in,
                                                         sel_clip->timeline_info.out,
                                                         sel_clip->timeline_info.track_);

                  for (int i=0;i<links.size();i++) {
                    ClipPtr link = global::sequence->clip(links.at(i));
                    PanelManager::timeLine().deselect_area(link->timeline_info.in,
                                                           link->timeline_info.out,
                                                           link->timeline_info.track_);
                  }

                  Selection s;
                  s.track = sel_clip->timeline_info.track_;

                  if (PanelManager::timeLine().transition_select == TA_OPENING_TRANSITION
                      && sel_clip->openingTransition() != nullptr) {
                    s.in = sel_clip->timeline_info.in;
                    if (!sel_clip->openingTransition()->secondary_clip.expired()) {
                      s.in -= sel_clip->openingTransition()->get_true_length();
                    }
                    s.out = sel_clip->timeline_info.in + sel_clip->openingTransition()->get_true_length();
                  } else if (PanelManager::timeLine().transition_select == TA_CLOSING_TRANSITION
                             && sel_clip->closingTransition() != nullptr) {
                    s.in = sel_clip->timeline_info.out - sel_clip->closingTransition()->get_true_length();
                    s.out = sel_clip->timeline_info.out;
                    if (!sel_clip->closingTransition()->secondary_clip.expired()) {
                      s.out += sel_clip->closingTransition()->get_true_length();
                    }
                  }
                  global::sequence->selections_.append(s);
                }
              } else {
                // if "shift" is not down
                if (!shift) {
                  global::sequence->selections_.clear();
                }

                Selection s;

                s.in = sel_clip->timeline_info.in;
                s.out = sel_clip->timeline_info.out;

                if (PanelManager::timeLine().tool == TimelineToolType::POINTER) {
                  if (PanelManager::timeLine().transition_select == TA_OPENING_TRANSITION) {
                    s.out = sel_clip->timeline_info.in + sel_clip->openingTransition()->get_true_length();
                    if (!sel_clip->openingTransition()->secondary_clip.expired()) {
                      s.in -= sel_clip->openingTransition()->get_true_length();
                    }
                  }

                  if (PanelManager::timeLine().transition_select == TA_CLOSING_TRANSITION) {
                    s.in = sel_clip->timeline_info.out - sel_clip->closingTransition()->get_true_length();
                    if (!sel_clip->closingTransition()->secondary_clip.expired()) {
                      s.out += sel_clip->closingTransition()->get_true_length();
                    }
                  }
                }

                s.track = sel_clip->timeline_info.track_;
                global::sequence->selections_.append(s);

                if (e_config.select_also_seeks) {
                  PanelManager::sequenceViewer().seek(sel_clip->timeline_info.in);
                }

                // if alt is not down, select links
                if (!alt && PanelManager::timeLine().transition_select == TA_NO_TRANSITION) {
                  for (int i=0; i<links.size(); i++) {
                    ClipPtr link = global::sequence->clip(links.at(i));
                    if (link != nullptr && !link->isSelected(true)) {
                      Selection ss;
                      ss.in = link->timeline_info.in;
                      ss.out = link->timeline_info.out;
                      ss.track = link->timeline_info.track_;
                      global::sequence->selections_.append(ss);
                    }
                  }
                }
              }

            if (tool != TimelineToolType::MENU) PanelManager::timeLine().moving_init = true;
          } else {
            // if "shift" is not down
            if (!shift) {
              global::sequence->selections_.clear();
            }

            PanelManager::timeLine().rect_select_init = true;
          }
          PanelManager::refreshPanels(false);
        }
      }
        break;
      case TimelineToolType::HAND:
        PanelManager::timeLine().hand_moving = true;
        PanelManager::timeLine().drag_x_start = pos.x();
        PanelManager::timeLine().drag_y_start = pos.y();
        break;
      case TimelineToolType::EDIT:
        if (e_config.edit_tool_also_seeks) PanelManager::sequenceViewer().seek(PanelManager::timeLine().drag_frame_start);
        PanelManager::timeLine().selecting = true;
        break;
      case TimelineToolType::RAZOR:
      {
        PanelManager::timeLine().splitting = true;
        PanelManager::timeLine().split_tracks.insert(PanelManager::timeLine().drag_track_start);
        PanelManager::refreshPanels(false);
      }
        break;
      case TimelineToolType::TRANSITION:
      {
        if (PanelManager::timeLine().transition_tool_pre_clip > -1) {
          PanelManager::timeLine().transition_tool_init = true;
        }
      }
        break;
      default:
        qWarning() << "Unhandled timeline tool" << static_cast<int>(tool);
        break;
    }//switch
  }
}

void make_room_for_transition(ComboAction* ca, ClipPtr c, int type,
                              long transition_start, long transition_end, bool delete_old_transitions) {
  // make room for transition
  if (type == TA_OPENING_TRANSITION) {
    if (delete_old_transitions && c->openingTransition() != nullptr) {
      ca->append(new DeleteTransitionCommand(c, ClipTransitionType::OPENING));
    }
    if (c->closingTransition() != nullptr) {
      if (transition_end >= c->timeline_info.out) {
        ca->append(new DeleteTransitionCommand(c, ClipTransitionType::CLOSING));
      } else if (transition_end > c->timeline_info.out - c->closingTransition()->get_true_length()) {
        ca->append(new ModifyTransitionCommand(c, TA_CLOSING_TRANSITION, c->timeline_info.out - transition_end));
      }
    }
  } else {
    if (delete_old_transitions && c->closingTransition() != nullptr) {
      ca->append(new DeleteTransitionCommand(c, ClipTransitionType::CLOSING));
    }
    if (c->openingTransition() != nullptr) {
      if (transition_start <= c->timeline_info.in) {
        ca->append(new DeleteTransitionCommand(c, ClipTransitionType::OPENING));
      } else if (transition_start < c->timeline_info.in + c->openingTransition()->get_true_length()) {
        ca->append(new ModifyTransitionCommand(c, TA_OPENING_TRANSITION, transition_start - c->timeline_info.in));
      }
    }
  }
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent *event) {
  QToolTip::hideText();
  if (global::sequence != nullptr) {
    bool alt = (event->modifiers() & Qt::AltModifier);
    bool shift = (event->modifiers() & Qt::ShiftModifier);
    bool ctrl = (event->modifiers() & Qt::ControlModifier);

    if (event->button() == Qt::LeftButton) {
      ComboAction* ca = new ComboAction();
      bool push_undo = false;

      if (PanelManager::timeLine().creating) {
        if (!PanelManager::timeLine().ghosts.empty()) {
          const Ghost& g = PanelManager::timeLine().ghosts.front();

          if (PanelManager::timeLine().creating_object == AddObjectType::AUDIO) {
            MainWindow::instance().statusBar()->clearMessage();
            PanelManager::sequenceViewer().cue_recording(qMin(g.in, g.out), qMax(g.in, g.out), g.track);
            PanelManager::timeLine().creating = false;
          } else if (g.in != g.out) {
            ClipPtr c(std::make_shared<Clip>(global::sequence));
            c->timeline_info.media = nullptr;
            c->timeline_info.in = qMin(g.in, g.out);
            c->timeline_info.out = qMax(g.in, g.out);
            c->timeline_info.clip_in = 0;
            c->timeline_info.color = MOUSE_RELEASE_COLOR;
            c->timeline_info.track_ = g.track;

            if (ctrl) {
              insert_clips(ca);
            } else {
              Selection s;
              s.in = c->timeline_info.in;
              s.out = c->timeline_info.out;
              s.track = c->timeline_info.track_;
              QVector<Selection> areas;
              areas.append(s);
              PanelManager::timeLine().delete_areas_and_relink(ca, areas);
            }

            QVector<ClipPtr> add;
            add.append(c);
            ca->append(new AddClipCommand(global::sequence, add));

            if (c->timeline_info.isVideo()) {
              // default video effects (before custom effects)
              c->effects.append(create_effect(c, get_internal_meta(EFFECT_INTERNAL_TRANSFORM, EFFECT_TYPE_EFFECT)));
              //c->media_type = MEDIA_TYPE_SOLID;
            }

            switch (PanelManager::timeLine().creating_object) {
              case AddObjectType::TITLE:
                c->setName(tr("Title"));
                c->effects.append(create_effect(c, get_internal_meta(EFFECT_INTERNAL_TEXT, EFFECT_TYPE_EFFECT)));
                break;
              case AddObjectType::SOLID:
                c->setName(tr("Solid Color"));
                c->effects.append(create_effect(c, get_internal_meta(EFFECT_INTERNAL_SOLID, EFFECT_TYPE_EFFECT)));
                break;
              case AddObjectType::BARS:
              {
                c->setName(tr("Bars"));
                EffectPtr e = create_effect(c, get_internal_meta(EFFECT_INTERNAL_SOLID, EFFECT_TYPE_EFFECT));
                e->row(0)->field(0)->set_combo_index(1);
                c->effects.append(e);
              }
                break;
              case AddObjectType::TONE:
                c->setName(tr("Tone"));
                c->effects.append(create_effect(c, get_internal_meta(EFFECT_INTERNAL_TONE, EFFECT_TYPE_EFFECT)));
                break;
              case AddObjectType::NOISE:
                c->setName(tr("Noise"));
                c->effects.append(create_effect(c, get_internal_meta(EFFECT_INTERNAL_NOISE, EFFECT_TYPE_EFFECT)));
                break;
              default:
                qWarning() << "Unhandled object add type" << static_cast<int>(PanelManager::timeLine().creating_object);
                break;
            }

            if (c->timeline_info.track_ >= 0) {
              // default audio effects (after custom effects)
              c->effects.append(create_effect(c, get_internal_meta(EFFECT_INTERNAL_VOLUME, EFFECT_TYPE_EFFECT)));
              c->effects.append(create_effect(c, get_internal_meta(EFFECT_INTERNAL_PAN, EFFECT_TYPE_EFFECT)));
              //c->media_type = MEDIA_TYPE_TONE;
            }

            push_undo = true;

            if (!shift) {
              PanelManager::timeLine().creating = false;
            }
          }
        }
      } else if (PanelManager::timeLine().moving_proc) {
        if (PanelManager::timeLine().ghosts.size() > 0) {
          const Ghost& first_ghost = PanelManager::timeLine().ghosts.at(0);

          // if we were RIPPLING, move all the clips
          if (PanelManager::timeLine().tool == TimelineToolType::RIPPLE) {
            long ripple_length, ripple_point;

            // ripple_length becomes the length/number of frames we trimmed
            // ripple point becomes the point to ripple (i.e. the point after or before which we move every clip)
            if (PanelManager::timeLine().trim_in_point) {
              ripple_length = first_ghost.old_in - first_ghost.in;
              ripple_point = first_ghost.old_in;

              for (int i=0;i<global::sequence->selections_.size();i++) {
                global::sequence->selections_[i].in += ripple_length;
                global::sequence->selections_[i].out += ripple_length;
              }
            } else {
              // if we're trimming an out-point
              ripple_length = first_ghost.old_out - PanelManager::timeLine().ghosts.at(0).out;
              ripple_point = first_ghost.old_out;
            }
            QVector<int> ignore_clips;
            for (int i=0;i<PanelManager::timeLine().ghosts.size();i++) {
              const Ghost& g = PanelManager::timeLine().ghosts.at(i);

              // push rippled clips forward if necessary
              if (PanelManager::timeLine().trim_in_point) {
                ignore_clips.append(g.clip);
                PanelManager::timeLine().ghosts[i].in += ripple_length;
                PanelManager::timeLine().ghosts[i].out += ripple_length;
              }

              long comp_point = PanelManager::timeLine().trim_in_point ? g.old_in : g.old_out;
              ripple_point = qMin(ripple_point, comp_point);
            }
            if (!PanelManager::timeLine().trim_in_point) ripple_length = -ripple_length;

            ripple_clips(ca, global::sequence, ripple_point, ripple_length, ignore_clips);
          }

          if (PanelManager::timeLine().tool == TimelineToolType::POINTER
              && (event->modifiers() & Qt::AltModifier)
              && PanelManager::timeLine().trim_target == -1) { // if holding alt (and not trimming), duplicate rather than move
            // duplicate clips
            QVector<int> old_clips;
            QVector<ClipPtr> new_clips;
            QVector<Selection> delete_areas;
            for (int i=0;i<PanelManager::timeLine().ghosts.size();i++) {
              const Ghost& g = PanelManager::timeLine().ghosts.at(i);
              if (g.old_in != g.in || g.old_out != g.out || g.track != g.old_track || g.clip_in != g.old_clip_in) {
                // create copy of clip
                ClipPtr c = global::sequence->clips_.at(g.clip)->copy(global::sequence);

                c->timeline_info.in = g.in;
                c->timeline_info.out = g.out;
                c->timeline_info.track_ = g.track;

                Selection s;
                s.in = g.in;
                s.out = g.out;
                s.track = g.track;
                delete_areas.append(s);

                old_clips.append(g.clip);
                new_clips.append(c);
              }
            }
            if (new_clips.size() > 0) {
              PanelManager::timeLine().delete_areas_and_relink(ca, delete_areas);

              // relink duplicated clips
              PanelManager::timeLine().relink_clips_using_ids(old_clips, new_clips);

              ca->append(new AddClipCommand(global::sequence, new_clips));
            }
          } else {
            // INSERT if holding ctrl
            if (PanelManager::timeLine().tool == TimelineToolType::POINTER && ctrl) {
              insert_clips(ca);
            } else if (PanelManager::timeLine().tool == TimelineToolType::POINTER || PanelManager::timeLine().tool == TimelineToolType::SLIDE) {
              // move clips
              QVector<Selection> delete_areas;
              for (int i=0;i<PanelManager::timeLine().ghosts.size();i++) {
                // step 1 - set clips that are moving to "undeletable" (to avoid step 2 deleting any part of them)
                const Ghost& g = PanelManager::timeLine().ghosts.at(i);

                global::sequence->clips_.at(g.clip)->undeletable = true;
                if (g.transition != nullptr) {
                  g.transition->parent_clip->undeletable = true;
                  if (!g.transition->secondary_clip.expired()) {
                    g.transition->secondary_clip.lock()->undeletable = true;
                  }
                }

                Selection s;
                s.in = g.in;
                s.out = g.out;
                s.track = g.track;
                delete_areas.append(s);
              }
              PanelManager::timeLine().delete_areas_and_relink(ca, delete_areas);
              for (int i=0;i<PanelManager::timeLine().ghosts.size();i++) {
                const Ghost& g = PanelManager::timeLine().ghosts.at(i);
                global::sequence->clips_.at(g.clip)->undeletable = false;
                if (g.transition != nullptr) {
                  g.transition->parent_clip->undeletable = false;
                  if (!g.transition->secondary_clip.expired()) {
                    g.transition->secondary_clip.lock()->undeletable = false;
                  }
                }
              }
            }
            for (int i=0;i<PanelManager::timeLine().ghosts.size();i++) {
              Ghost& g = PanelManager::timeLine().ghosts[i];

              // step 3 - move clips
              ClipPtr c = global::sequence->clips_.at(g.clip);
              if (g.transition == nullptr) {
                move_clip(ca, c, (g.in - g.old_in), (g.out - g.old_out), (g.clip_in - g.old_clip_in), (g.track - g.old_track), true, true);

                // adjust transitions if we need to
                long new_clip_length = (g.out - g.in);
                if (c->openingTransition() != nullptr) {
                  long max_open_length = new_clip_length;
                  if (c->closingTransition() != nullptr && !PanelManager::timeLine().trim_in_point) {
                    max_open_length -= c->closingTransition()->get_true_length();
                  }
                  if (max_open_length <= 0) {
                    ca->append(new DeleteTransitionCommand(c, ClipTransitionType::OPENING));
                  } else if (c->openingTransition()->get_true_length() > max_open_length) {
                    ca->append(new ModifyTransitionCommand(c, TA_OPENING_TRANSITION, max_open_length));
                  }
                }
                if (c->closingTransition() != nullptr) {
                  long max_open_length = new_clip_length;
                  if (c->openingTransition() != nullptr && PanelManager::timeLine().trim_in_point) {
                    max_open_length -= c->openingTransition()->get_true_length();
                  }
                  if (max_open_length <= 0) {
                    ca->append(new DeleteTransitionCommand(c, ClipTransitionType::CLOSING));
                  } else if (c->closingTransition()->get_true_length() > max_open_length) {
                    ca->append(new ModifyTransitionCommand(c, TA_CLOSING_TRANSITION, max_open_length));
                  }
                }
              } else {
                bool is_opening_transition = (g.transition == c->openingTransition());
                long new_transition_length = g.out - g.in;
                if (!g.transition->secondary_clip.expired()) {
                  new_transition_length >>= 1;
                }
                ca->append(new ModifyTransitionCommand(c,
                                                       is_opening_transition ? TA_OPENING_TRANSITION : TA_CLOSING_TRANSITION,
                                                       new_transition_length));

                long clip_length = c->length(); //FIXME: this is never used

                if (!g.transition->secondary_clip.expired()) {
                  if (g.in != g.old_in && !g.trimming) {
                    long movement = g.in - g.old_in;
                    move_clip(ca, g.transition->parent_clip, movement, 0, movement, 0, false, true);
                    move_clip(ca, g.transition->secondary_clip.lock(), 0, movement, 0, 0, false, true);
                  }
                } else if (is_opening_transition) {
                  if (g.in != g.old_in) {
                    // if transition is going to make the clip bigger, make the clip bigger
                    move_clip(ca, c, (g.in - g.old_in), 0, (g.clip_in - g.old_clip_in), 0, true, true);
                    clip_length -= (g.in - g.old_in);
                  }

                  make_room_for_transition(ca, c, TA_OPENING_TRANSITION, g.in, g.out, false);
                } else {
                  if (g.out != g.old_out) {
                    // if transition is going to make the clip bigger, make the clip bigger
                    move_clip(ca, c, 0, (g.out - g.old_out), 0, 0, true, true);
                    clip_length += (g.out - g.old_out);
                  }

                  make_room_for_transition(ca, c, TA_CLOSING_TRANSITION, g.in, g.out, false);
                }
              }
            }
          }
          push_undo = true;
        }
      } else if (PanelManager::timeLine().selecting || PanelManager::timeLine().rect_select_proc) {
        //FIXME:
      } else if (PanelManager::timeLine().transition_tool_proc) {
        const Ghost& g = PanelManager::timeLine().ghosts.at(0);

        if (g.in != g.out) {
          long transition_start = qMin(g.in, g.out);
          long transition_end = qMax(g.in, g.out);

          ClipPtr pre = global::sequence->clips_.at(g.clip);
          ClipPtr post = pre;

          make_room_for_transition(ca, pre, PanelManager::timeLine().transition_tool_type, transition_start, transition_end, true);

          if (PanelManager::timeLine().transition_tool_post_clip > -1) {
            post = global::sequence->clips_.at(PanelManager::timeLine().transition_tool_post_clip);
            int opposite_type = (PanelManager::timeLine().transition_tool_type == TA_OPENING_TRANSITION) ? TA_CLOSING_TRANSITION : TA_OPENING_TRANSITION;
            make_room_for_transition(
                  ca,
                  post,
                  opposite_type,
                  transition_start,
                  transition_end,
                  true
                  );

            if (PanelManager::timeLine().transition_tool_type == TA_CLOSING_TRANSITION) {
              // swap
              ClipPtr temp = pre;
              pre = post;
              post = temp;
            }
          }

          if (transition_start < post->timeline_info.in || transition_end > pre->timeline_info.out) {
            // delete shit over there and extend timeline in
            QVector<Selection> areas;
            Selection s;
            s.track = post->timeline_info.track_;

            bool move_post = false;
            bool move_pre = false;

            if (transition_start < post->timeline_info.in) {
              s.in = transition_start;
              s.out = post->timeline_info.in;
              areas.append(s);
              move_post = true;
            }
            if (transition_end > pre->timeline_info.out) {
              s.in = pre->timeline_info.out;
              s.out = transition_end;
              areas.append(s);
              move_pre = true;
            }

            PanelManager::timeLine().delete_areas_and_relink(ca, areas);

            if (move_post){
              const int64_t in_point = post->timeline_info.in.load();
              move_clip(ca, post, qMin(transition_start, in_point),
                        post->timeline_info.out, post->timeline_info.clip_in - (in_point - transition_start),
                        post->timeline_info.track_);
            }
            if (move_pre) {
              move_clip(ca, pre, pre->timeline_info.in, qMax(transition_end, pre->timeline_info.out.load()),
                        pre->timeline_info.clip_in, pre->timeline_info.track_);
            }
          }

          if (PanelManager::timeLine().transition_tool_post_clip > -1) {
            ca->append(new AddTransitionCommand(pre, post, PanelManager::timeLine().transition_tool_meta,
                                                ClipTransitionType::OPENING,
                                                transition_end - pre->timeline_info.in));
          } else {
            const auto trans_type = PanelManager::timeLine().transition_tool_type == TA_OPENING_TRANSITION
                ? ClipTransitionType::OPENING  : ClipTransitionType::CLOSING;

            ca->append(new AddTransitionCommand(pre, nullptr, PanelManager::timeLine().transition_tool_meta,
                                                trans_type, transition_end - transition_start));
          }

          push_undo = true;
        }
      } else if (PanelManager::timeLine().splitting) {
        push_undo = splitClipEvent(PanelManager::timeLine().drag_frame_start, PanelManager::timeLine().split_tracks);
      }

      // remove duplicate selections
      PanelManager::timeLine().clean_up_selections(global::sequence->selections_);

      if (selection_command != nullptr) {
        selection_command->new_data = global::sequence->selections_;
        ca->append(selection_command);
        selection_command = nullptr;
        push_undo = true;
      }

      if (push_undo) {
        e_undo_stack.push(ca);
      } else {
        delete ca;
      }

      // destroy all ghosts
      PanelManager::timeLine().ghosts.clear();

      // clear split tracks
      PanelManager::timeLine().split_tracks.clear();

      PanelManager::timeLine().selecting = false;
      PanelManager::timeLine().moving_proc = false;
      PanelManager::timeLine().moving_init = false;
      PanelManager::timeLine().splitting = false;
      PanelManager::timeLine().snapped = false;
      PanelManager::timeLine().rect_select_init = false;
      PanelManager::timeLine().rect_select_proc = false;
      PanelManager::timeLine().transition_tool_init = false;
      PanelManager::timeLine().transition_tool_proc = false;
      pre_clips.clear();
      post_clips.clear();

      PanelManager::refreshPanels(true);
    }
    PanelManager::timeLine().hand_moving = false;
  }
}

void TimelineWidget::init_ghosts() {
  for (int i=0;i<PanelManager::timeLine().ghosts.size();i++) {
    Ghost& g = PanelManager::timeLine().ghosts[i];
    ClipPtr c = global::sequence->clips_.at(g.clip);

    g.track = g.old_track = c->timeline_info.track_;
    g.clip_in = g.old_clip_in = c->timeline_info.clip_in;

    if (PanelManager::timeLine().tool == TimelineToolType::SLIP) {
      g.clip_in = g.old_clip_in = c->clipInWithTransition();
      g.in = g.old_in = c->timelineInWithTransition();
      g.out = g.old_out = c->timelineOutWithTransition();
      g.ghost_length = g.old_out - g.old_in;
    } else if (g.transition == nullptr) {
      // this ghost is for a clip
      g.in = g.old_in = c->timeline_info.in;
      g.out = g.old_out = c->timeline_info.out;
      g.ghost_length = g.old_out - g.old_in;
    } else if (g.transition == c->openingTransition()) {
      g.in = g.old_in = c->timelineInWithTransition();
      g.ghost_length = c->openingTransition()->get_length();
      g.out = g.old_out = g.in + g.ghost_length;
    } else if (g.transition == c->closingTransition()) {
      g.out = g.old_out = c->timelineOutWithTransition();
      g.ghost_length = c->closingTransition()->get_length();
      g.in = g.old_in = g.out - g.ghost_length;
      g.clip_in = g.old_clip_in = c->timeline_info.clip_in + c->length() - c->closingTransition()->get_true_length();
    }

    // used for trim ops
    g.media_length = c->maximumLength();
  }
  for (int i=0;i<global::sequence->selections_.size();i++) {
    Selection& s = global::sequence->selections_[i];
    s.old_in = s.in;
    s.old_out = s.out;
    s.old_track = s.track;
  }
}

void validate_transitions(ClipPtr c, int transition_type, long& frame_diff) {
  long validator;

  if (transition_type == TA_OPENING_TRANSITION) {
    // prevent from going below 0 on the timeline
    validator = c->timeline_info.in + frame_diff;
    if (validator < 0) frame_diff -= validator;

    // prevent from going below 0 for the media
    validator = c->timeline_info.clip_in + frame_diff;
    if (validator < 0) frame_diff -= validator;

    // prevent transition from exceeding media length
    validator -= c->maximumLength();
    if (validator > 0) frame_diff -= validator;
  } else {
    // prevent from going below 0 on the timeline
    validator = c->timeline_info.out + frame_diff;
    if (validator < 0) frame_diff -= validator;

    // prevent from going below 0 for the media
    validator = c->timeline_info.clip_in + c->length() + frame_diff;
    if (validator < 0) frame_diff -= validator;

    // prevent transition from exceeding media length
    validator -= c->maximumLength();
    if (validator > 0) frame_diff -= validator;
  }
}

void TimelineWidget::update_ghosts(const QPoint& mouse_pos, bool lock_frame) {
  TimelineToolType effective_tool = PanelManager::timeLine().tool;
  if (PanelManager::timeLine().importing || PanelManager::timeLine().creating) {
    effective_tool = TimelineToolType::POINTER;
  }

  int mouse_track = getTrackFromScreenPoint(mouse_pos.y());
  long frame_diff = (lock_frame) ? 0 : PanelManager::timeLine().getTimelineFrameFromScreenPoint(mouse_pos.x()) - PanelManager::timeLine().drag_frame_start;
  int track_diff = ((effective_tool == TimelineToolType::SLIDE || PanelManager::timeLine().transition_select != TA_NO_TRANSITION)
                    && !PanelManager::timeLine().importing) ? 0 : mouse_track - PanelManager::timeLine().drag_track_start;
  long validator;
  long earliest_in_point = LONG_MAX;

  // first try to snap
  long fm;

  if (effective_tool != TimelineToolType::SLIP) {
    // slipping doesn't move the clips so we don't bother snapping for it
    for (int i=0;i<PanelManager::timeLine().ghosts.size();i++) {
      const Ghost& g = PanelManager::timeLine().ghosts.at(i);
      if (PanelManager::timeLine().trim_target == -1 || g.trim_in) {
        fm = g.old_in + frame_diff;
        if (PanelManager::timeLine().snap_to_timeline(&fm, true, true, true)) {
          frame_diff = fm - g.old_in;
          break;
        }
      }
      if (PanelManager::timeLine().trim_target == -1 || !g.trim_in) {
        fm = g.old_out + frame_diff;
        if (PanelManager::timeLine().snap_to_timeline(&fm, true, true, true)) {
          frame_diff = fm - g.old_out;
          break;
        }
      }
    }
  }

  bool clips_are_movable = (effective_tool == TimelineToolType::POINTER || effective_tool == TimelineToolType::SLIDE);

  // validate ghosts
  long temp_frame_diff = frame_diff; // cache to see if we change it (thus cancelling any snap)
  for (int i=0;i<PanelManager::timeLine().ghosts.size();i++) {
    const Ghost& g = PanelManager::timeLine().ghosts.at(i);
    ClipPtr c = nullptr;
    if (g.clip != -1) c = global::sequence->clips_.at(g.clip);

    FootageStreamPtr ms;
    bool found = false;
    if (g.clip != -1 && c->timeline_info.media != nullptr && c->timeline_info.media->type() == MediaType::FOOTAGE) {
      auto ftg = c->timeline_info.media->object<Footage>();
      if (ftg) {
        if (c->timeline_info.isVideo()) {
          ms = ftg->video_stream_from_file_index(c->timeline_info.media_stream);
        } else {
          ms = ftg->audio_stream_from_file_index(c->timeline_info.media_stream);
        }
        found = ms != nullptr;
      }
    }

    // validate ghosts for trimming
    if (PanelManager::timeLine().creating) {
      // i feel like we might need something here but we haven't so far?
    } else if (effective_tool == TimelineToolType::SLIP) {
      if ((c->timeline_info.media != nullptr && c->timeline_info.media->type() == MediaType::SEQUENCE)
          || (found && !ms->infinite_length)) {
        // prevent slip moving a clip below 0 clip_in
        validator = g.old_clip_in - frame_diff;
        if (validator < 0) {
          frame_diff += validator;
        }

        // prevent slip moving clip beyond media length
        validator += g.ghost_length;
        if (validator > g.media_length) {
          frame_diff += validator - g.media_length;
        }
      }
    } else if (g.trimming) {
      if (g.trim_in) {
        // prevent clip/transition length from being less than 1 frame long
        validator = g.ghost_length - frame_diff;
        if (validator < 1) {
          frame_diff -= (1 - validator);
        }

        // prevent timeline in from going below 0
        if (effective_tool != TimelineToolType::RIPPLE) {
          validator = g.old_in + frame_diff;
          if (validator < 0) {
            frame_diff -= validator;
          }
        }

        // prevent clip_in from going below 0
        if ((c->timeline_info.media != nullptr && c->timeline_info.media->type() == MediaType::SEQUENCE)
            || (found && !ms->infinite_length)) {
          validator = g.old_clip_in + frame_diff;
          if (validator < 0) {
            frame_diff -= validator;
          }
        }
      } else {
        // prevent clip length from being less than 1 frame long
        validator = g.ghost_length + frame_diff;
        if (validator < 1) {
          frame_diff += (1 - validator);
        }

        // prevent clip length exceeding media length
        if ((c->timeline_info.media != nullptr && c->timeline_info.media->type() == MediaType::SEQUENCE)
            || (found && !ms->infinite_length)) {
          validator = g.old_clip_in + g.ghost_length + frame_diff;
          if (validator > g.media_length) {
            frame_diff -= validator - g.media_length;
          }
        }
      }

      // prevent dual transition from going below 0 on the primary or media length on the secondary
      if (g.transition != nullptr && !g.transition->secondary_clip.expired()) {
        ClipPtr otc = g.transition->parent_clip;
        ClipPtr ctc = g.transition->secondary_clip.lock();

        if (g.trim_in) {
          frame_diff -= g.transition->get_true_length();
        } else {
          frame_diff += g.transition->get_true_length();
        }

        validate_transitions(otc, TA_OPENING_TRANSITION, frame_diff);
        validate_transitions(ctc, TA_CLOSING_TRANSITION, frame_diff);

        frame_diff = -frame_diff;
        validate_transitions(otc, TA_OPENING_TRANSITION, frame_diff);
        validate_transitions(ctc, TA_CLOSING_TRANSITION, frame_diff);
        frame_diff = -frame_diff;

        if (g.trim_in) {
          frame_diff += g.transition->get_true_length();
        } else {
          frame_diff -= g.transition->get_true_length();
        }
      }

      // ripple ops
      if (effective_tool == TimelineToolType::RIPPLE) {
        for (int j=0;j<post_clips.size();j++) {
          ClipPtr post = post_clips.at(j);

          // prevent any rippled clip from going below 0
          if (PanelManager::timeLine().trim_in_point) {
            validator = post->timeline_info.in - frame_diff;
            if (validator < 0) {
              frame_diff += validator;
            }
          }

          // prevent any post-clips colliding with pre-clips
          for (int k=0;k<pre_clips.size();k++) {
            ClipPtr pre = pre_clips.at(k);
            if (pre != post && pre->timeline_info.track_ == post->timeline_info.track_) {
              if (PanelManager::timeLine().trim_in_point) {
                validator = post->timeline_info.in - frame_diff - pre->timeline_info.out;
                if (validator < 0) {
                  frame_diff += validator;
                }
              } else {
                validator = post->timeline_info.in + frame_diff - pre->timeline_info.out;
                if (validator < 0) {
                  frame_diff -= validator;
                }
              }
            }
          }
        }
      }
    } else if (clips_are_movable) { // validate ghosts for moving
      // prevent clips from moving below 0 on the timeline
      validator = g.old_in + frame_diff;
      if (validator < 0) {
        frame_diff -= validator;
      }

      if (g.transition != nullptr) {
        if (!g.transition->secondary_clip.expired()) {
          ClipPtr secondary = g.transition->secondary_clip.lock();
          // prevent dual transitions from going below 0 on the primary or above media length on the secondary

          validator = g.transition->parent_clip->clipInWithTransition() + frame_diff;
          if (validator < 0) {
            frame_diff -= validator;
          }

          validator = secondary->timelineOutWithTransition()
                      - secondary->timelineInWithTransition() - g.transition->get_length()
                      + secondary->clipInWithTransition() + frame_diff;
          if (validator < 0) {
            frame_diff -= validator;
          }

          validator = g.transition->parent_clip->timeline_info.clip_in + frame_diff
                      - g.transition->parent_clip->maximumLength() + g.transition->get_true_length();
          if (validator > 0) {
            frame_diff -= validator;
          }

          validator = secondary->timelineOutWithTransition()
                      - secondary->timelineInWithTransition()
                      + secondary->clipInWithTransition() + frame_diff
                      - secondary->maximumLength();
          if (validator > 0) {
            frame_diff -= validator;
          }
        } else {
          // prevent clip_in from going below 0
          if (c->timeline_info.media->type() == MediaType::SEQUENCE
              || (found && !ms->infinite_length)) {
            validator = g.old_clip_in + frame_diff;
            if (validator < 0) {
              frame_diff -= validator;
            }
          }

          // prevent clip length exceeding media length
          if (c->timeline_info.media->type() == MediaType::SEQUENCE
              || (found && !ms->infinite_length)) {
            validator = g.old_clip_in + g.ghost_length + frame_diff;
            if (validator > g.media_length) {
              frame_diff -= validator - g.media_length;
            }
          }
        }
      }

      // prevent clips from crossing tracks
      if (same_sign(g.old_track, PanelManager::timeLine().drag_track_start)) {
        while (!same_sign(g.old_track, g.old_track + track_diff)) {
          if (g.old_track < 0) {
            track_diff--;
          } else {
            track_diff++;
          }
        }
      }
    } else if (effective_tool == TimelineToolType::TRANSITION) {
      if (PanelManager::timeLine().transition_tool_post_clip == -1) {
        validate_transitions(c, PanelManager::timeLine().transition_tool_type, frame_diff);
      } else {
        ClipPtr otc = c; // open transition clip
        ClipPtr ctc = global::sequence->clips_.at(PanelManager::timeLine().transition_tool_post_clip); // close transition clip

        if (PanelManager::timeLine().transition_tool_type == TA_CLOSING_TRANSITION) {
          // swap
          ClipPtr temp = otc;
          otc = ctc;
          ctc = temp;
        }

        // always gets a positive frame_diff
        validate_transitions(otc, TA_OPENING_TRANSITION, frame_diff);
        validate_transitions(ctc, TA_CLOSING_TRANSITION, frame_diff);

        // always gets a negative frame_diff
        frame_diff = -frame_diff;
        validate_transitions(otc, TA_OPENING_TRANSITION, frame_diff);
        validate_transitions(ctc, TA_CLOSING_TRANSITION, frame_diff);
        frame_diff = -frame_diff;
      }
    }
  }
  if (temp_frame_diff != frame_diff) PanelManager::timeLine().snapped = false;

  // apply changes to ghosts
  for (int i=0;i<PanelManager::timeLine().ghosts.size();i++) {
    Ghost& g = PanelManager::timeLine().ghosts[i];

    if (effective_tool == TimelineToolType::SLIP) {
      g.clip_in = g.old_clip_in - frame_diff;
    } else if (g.trimming) {
      long ghost_diff = frame_diff;

      // prevent trimming clips from overlapping each other
      for (int j=0;j<PanelManager::timeLine().ghosts.size();j++) {
        const Ghost& comp = PanelManager::timeLine().ghosts.at(j);
        if (i != j && g.track == comp.track) {
          if (g.trim_in && comp.out < g.out) {
            validator = (g.old_in + ghost_diff) - comp.out;
            if (validator < 0) {
              ghost_diff -= validator;
            }
          } else if (comp.in > g.in) {
            validator = (g.old_out + ghost_diff) - comp.in;
            if (validator > 0) {
              ghost_diff -= validator;
            }
          }
        }
      }//for

      // apply changes
      if (g.transition != nullptr && !g.transition->secondary_clip.expired()) {
        if (g.trim_in) ghost_diff = -ghost_diff;
        g.in = g.old_in - ghost_diff;
        g.out = g.old_out + ghost_diff;
      } else if (g.trim_in) {
        g.in = g.old_in + ghost_diff;
        g.clip_in = g.old_clip_in + ghost_diff;
      } else {
        g.out = g.old_out + ghost_diff;
      }
    } else if (clips_are_movable) {
      g.track = g.old_track;
      g.in = g.old_in + frame_diff;
      g.out = g.old_out + frame_diff;

      if (g.transition != nullptr && g.transition == global::sequence->clips_.at(g.clip)->openingTransition()) {
        g.clip_in = g.old_clip_in + frame_diff;
      }

      if (PanelManager::timeLine().importing) {
        if ((PanelManager::timeLine().video_ghosts && mouse_track < 0)
            || (PanelManager::timeLine().audio_ghosts && mouse_track >= 0)) {
          int abs_track_diff = abs(track_diff);
          if (g.old_track < 0) { // clip is video
            g.track -= abs_track_diff;
          } else { // clip is audio
            g.track += abs_track_diff;
          }
        }
      } else if (same_sign(g.old_track, PanelManager::timeLine().drag_track_start)) {
        g.track += track_diff;
      }
    } else if (effective_tool == TimelineToolType::TRANSITION) {
      if (PanelManager::timeLine().transition_tool_post_clip > -1) {
        g.in = g.old_in - frame_diff;
        g.out = g.old_out + frame_diff;
      } else if (PanelManager::timeLine().transition_tool_type == TA_OPENING_TRANSITION) {
        g.out = g.old_out + frame_diff;
      } else {
        g.in = g.old_in + frame_diff;
      }
    }

    earliest_in_point = qMin(earliest_in_point, g.in);
  }

  // apply changes to selections
  if (effective_tool != TimelineToolType::SLIP && !PanelManager::timeLine().importing && !PanelManager::timeLine().creating) {
    for (int i=0;i<global::sequence->selections_.size();i++) {
      Selection& s = global::sequence->selections_[i];
      if (PanelManager::timeLine().trim_target > -1) {
        if (PanelManager::timeLine().trim_in_point) {
          s.in = s.old_in + frame_diff;
        } else {
          s.out = s.old_out + frame_diff;
        }
      } else if (clips_are_movable) {
        for (auto& moveableSelection : global::sequence->selections_) {
          moveableSelection.in = moveableSelection.old_in + frame_diff;
          moveableSelection.out = moveableSelection.old_out + frame_diff;
          moveableSelection.track = moveableSelection.old_track;

          if (PanelManager::timeLine().importing) {
            int abs_track_diff = abs(track_diff);
            if (moveableSelection.old_track < 0) {
              moveableSelection.track -= abs_track_diff;
            } else {
              moveableSelection.track += abs_track_diff;
            }
          } else {
            if (same_sign(moveableSelection.track, PanelManager::timeLine().drag_track_start)) {
              moveableSelection.track += track_diff;
            }
          }
        }//for
      }
    }//for
  }

  if (PanelManager::timeLine().importing) {
    QToolTip::showText(mapToGlobal(mouse_pos), frame_to_timecode(earliest_in_point,
                                                                 e_config.timecode_view,
                                                                 global::sequence->frameRate()));
  } else {
    QString tip = ((frame_diff < 0) ? "-" : "+") + frame_to_timecode(qAbs(frame_diff),
                                                                     e_config.timecode_view,
                                                                     global::sequence->frameRate());
    if (PanelManager::timeLine().trim_target > -1) {
      // find which clip is being moved
      const Ghost* g = nullptr;
      for (int i=0;i<PanelManager::timeLine().ghosts.size();i++) {
        if (PanelManager::timeLine().ghosts.at(i).clip == PanelManager::timeLine().trim_target) {
          g = &PanelManager::timeLine().ghosts.at(i);
          break;
        }
      }

      if (g != nullptr) {
        tip += " " + tr("Duration:") + " ";
        long len = (g->old_out-g->old_in);
        if (PanelManager::timeLine().trim_in_point) {
          len -= frame_diff;
        } else {
          len += frame_diff;
        }
        tip += frame_to_timecode(len, e_config.timecode_view, global::sequence->frameRate());
      }
    }
    QToolTip::showText(mapToGlobal(mouse_pos), tip);
  }
}

void TimelineWidget::mouseMoveSplitEvent(const bool alt_pressed, Timeline& time_line) const
{
  // dragging split cursor across tracks

    const int track_start = qMin(time_line.cursor_track, time_line.drag_track_start);
    const int track_end = qMax(time_line.cursor_track, time_line.drag_track_start);

    for (int i = track_start; i < track_end; ++i) {
      time_line.split_tracks.insert(i);
    }

  if (alt_pressed) {
    return;
  }
  time_line.split_tracks.insert(time_line.drag_track_start);
  if (auto sel_clip = getClipFromCoords(time_line.drag_frame_start,
                                        time_line.drag_track_start) ) {
    auto tracks = sel_clip->getLinkedTracks();
    for (auto track : tracks) {
      time_line.split_tracks.insert(track);
    }
  }
}


//FIXME: oh god
void TimelineWidget::mouseMoveEvent(QMouseEvent *event)
{
  tooltip_timer.stop();
  if (global::sequence != nullptr) {
    bool alt = (event->modifiers() & Qt::AltModifier);

    PanelManager::timeLine().cursor_frame = PanelManager::timeLine().getTimelineFrameFromScreenPoint(event->pos().x());
    PanelManager::timeLine().cursor_track = getTrackFromScreenPoint(event->pos().y());

    PanelManager::timeLine().move_insert = ((event->modifiers() & Qt::ControlModifier)
                                            && (PanelManager::timeLine().tool == TimelineToolType::POINTER
                                                || PanelManager::timeLine().importing
                                                || PanelManager::timeLine().creating));

    if (!PanelManager::timeLine().moving_init) track_resizing = false;

    if (isLiveEditing()) {
      PanelManager::timeLine().snap_to_timeline(&PanelManager::timeLine().cursor_frame,
                                                !e_config.edit_tool_also_seeks || !PanelManager::timeLine().selecting,
                                                true,
                                                true);
    }
    if (PanelManager::timeLine().selecting) {
      int selection_count = 1 + qMax(PanelManager::timeLine().cursor_track,
                                     PanelManager::timeLine().drag_track_start) - qMin(PanelManager::timeLine().cursor_track,
                                                                                       PanelManager::timeLine().drag_track_start) + PanelManager::timeLine().selection_offset;
      if (global::sequence->selections_.size() != selection_count) {
        global::sequence->selections_.resize(selection_count);
      }
      int minimum_selection_track = qMin(PanelManager::timeLine().cursor_track, PanelManager::timeLine().drag_track_start);
      for (int i=PanelManager::timeLine().selection_offset;i<selection_count;i++) {
        Selection& s = global::sequence->selections_[i];
        s.track = minimum_selection_track + i - PanelManager::timeLine().selection_offset;
        long in = PanelManager::timeLine().drag_frame_start;
        long out = PanelManager::timeLine().cursor_frame;
        s.in = qMin(in, out);
        s.out = qMax(in, out);
      }

      // select linked clips too
      if (e_config.edit_tool_selects_links) {
        for (int j=0;j<global::sequence->clips_.size();j++) {
          ClipPtr c = global::sequence->clips_.at(j);
          for (int k=0;k<global::sequence->selections_.size();k++) {
            const Selection& s = global::sequence->selections_.at(k);
            if (!(c->timeline_info.in < s.in && c->timeline_info.out < s.in) &&  // TODO: is selected?
                !(c->timeline_info.in > s.out && c->timeline_info.out > s.out) &&
                c->timeline_info.track_ == s.track) {

              QVector<int> linked_tracks = PanelManager::timeLine().get_tracks_of_linked_clips(j);
              for (auto track : linked_tracks) {
                bool found = false;
                for (int l=0; l<global::sequence->selections_.size(); l++) {
                  const Selection& test_sel = global::sequence->selections_.at(l);
                  if (test_sel.track == track &&
                      test_sel.in == s.in &&
                      test_sel.out == s.out) {
                    found = true;
                    break;
                  }
                }
                if (!found) {
                  Selection link_sel;
                  link_sel.in = s.in;
                  link_sel.out = s.out;
                  link_sel.track = track;
                  global::sequence->selections_.append(link_sel);
                }
              }

              break;
            }
          }
        }
      }

      if (e_config.edit_tool_also_seeks) {
        PanelManager::sequenceViewer().seek(qMin(PanelManager::timeLine().drag_frame_start, PanelManager::timeLine().cursor_frame));
      } else {
        PanelManager::timeLine().repaint_timeline();
      }
    } else if (PanelManager::timeLine().hand_moving) {
      PanelManager::timeLine().block_repaints = true;
      PanelManager::timeLine().horizontalScrollBar->setValue(PanelManager::timeLine().horizontalScrollBar->value()
                                                             + PanelManager::timeLine().drag_x_start
                                                             - event->pos().x());
      scrollBar->setValue(scrollBar->value() + PanelManager::timeLine().drag_y_start - event->pos().y());
      PanelManager::timeLine().block_repaints = false;

      PanelManager::timeLine().repaint_timeline();

      PanelManager::timeLine().drag_x_start = event->pos().x();
      PanelManager::timeLine().drag_y_start = event->pos().y();
    } else if (PanelManager::timeLine().moving_init) {
      if (track_resizing) {
        int diff = track_resize_mouse_cache - event->pos().y();
        int new_height = track_resize_old_value;
        if (bottom_align) {
          new_height += diff;
        } else {
          new_height -= diff;
        }
        if (new_height < TRACK_MIN_HEIGHT) new_height = TRACK_MIN_HEIGHT;
        PanelManager::timeLine().calculate_track_height(track_target, new_height);
        update();
      } else if (PanelManager::timeLine().moving_proc) {
        update_ghosts(event->pos(), event->modifiers() & Qt::ShiftModifier);
      } else {
        // set up movement
        // create ghosts
        for (int i=0;i<global::sequence->clips_.size();i++) {
          ClipPtr c = global::sequence->clips_.at(i);
          if (c != nullptr) {
            Ghost g;
            g.transition = nullptr;

            bool add = c->isSelected(true);

            // if a whole clip is not selected, maybe just a transition is
            if (PanelManager::timeLine().tool == TimelineToolType::POINTER
                && (c->openingTransition() != nullptr || c->closingTransition() != nullptr) ) {
              // check if any selections contain the whole clip or transition
              for (int j=0;j<global::sequence->selections_.size();j++) {
                const Selection& s = global::sequence->selections_.at(j);
                if (s.track == c->timeline_info.track_) {
                  if (selection_contains_transition(s, c, TA_OPENING_TRANSITION)) {
                    g.transition = c->openingTransition();
                    add = true;
                    break;
                  } else if (selection_contains_transition(s, c, TA_CLOSING_TRANSITION)) {
                    g.transition = c->closingTransition();
                    add = true;
                    break;
                  }
                }
              }
            }

            if (add && g.transition != nullptr) {
              // check for duplicate transitions
              for (int j=0;j<PanelManager::timeLine().ghosts.size();j++) {
                if (PanelManager::timeLine().ghosts.at(j).transition == g.transition) {
                  add = false;
                  break;
                }
              }
            }

            if (add) {
              g.clip = i;
              g.trimming = (PanelManager::timeLine().trim_target > -1);
              g.trim_in = PanelManager::timeLine().trim_in_point;
              PanelManager::timeLine().ghosts.append(g);
            }
          }
        }

        int size = PanelManager::timeLine().ghosts.size();
        if (PanelManager::timeLine().tool == TimelineToolType::ROLLING) {
          for (int i=0;i<size;i++) {
            ClipPtr ghost_clip = global::sequence->clips_.at(PanelManager::timeLine().ghosts.at(i).clip);

            // see if any ghosts are touching, in which case flip them
            for (int k=0;k<size;k++) {
              ClipPtr comp_clip = global::sequence->clips_.at(PanelManager::timeLine().ghosts.at(k).clip);
              if ((PanelManager::timeLine().trim_in_point && comp_clip->timeline_info.out == ghost_clip->timeline_info.in) ||
                  (!PanelManager::timeLine().trim_in_point && comp_clip->timeline_info.in == ghost_clip->timeline_info.out)) {
                PanelManager::timeLine().ghosts[k].trim_in = !PanelManager::timeLine().trim_in_point;
              }
            }
          }

          // then look for other clips we're touching
          for (int i=0;i<size;i++) {
            const Ghost& g = PanelManager::timeLine().ghosts.at(i);
            ClipPtr ghost_clip = global::sequence->clips_.at(g.clip);
            for (int j=0;j<global::sequence->clips_.size();j++) {
              ClipPtr comp_clip = global::sequence->clips_.at(j);
              if (comp_clip->timeline_info.track_ == ghost_clip->timeline_info.track_) {
                if ((PanelManager::timeLine().trim_in_point && comp_clip->timeline_info.out == ghost_clip->timeline_info.in) ||
                    (!PanelManager::timeLine().trim_in_point && comp_clip->timeline_info.in == ghost_clip->timeline_info.out)) {
                  // see if this clip is already selected, and if so just switch the trim_in
                  bool found = false;
                  int duplicate_ghost_index;
                  for (duplicate_ghost_index=0;duplicate_ghost_index<size;duplicate_ghost_index++) {
                    if (PanelManager::timeLine().ghosts.at(duplicate_ghost_index).clip == j) {
                      found = true;
                      break;
                    }
                  }
                  if (g.trim_in == PanelManager::timeLine().trim_in_point) {
                    if (!found) {
                      // add ghost for this clip with opposite trim_in
                      Ghost gh;
                      gh.transition = nullptr;
                      gh.clip = j;
                      gh.trimming = (PanelManager::timeLine().trim_target > -1);
                      gh.trim_in = !PanelManager::timeLine().trim_in_point;
                      PanelManager::timeLine().ghosts.append(gh);
                    }
                  } else {
                    if (found) {
                      PanelManager::timeLine().ghosts.removeAt(duplicate_ghost_index);
                      size--;
                      if (duplicate_ghost_index < i) i--;
                    }
                  }
                }
              }
            }
          }
        } else if (PanelManager::timeLine().tool == TimelineToolType::SLIDE) {
          for (int i=0;i<size;i++) {
            const Ghost& g = PanelManager::timeLine().ghosts.at(i);
            ClipPtr ghost_clip = global::sequence->clips_.at(g.clip);
            PanelManager::timeLine().ghosts[i].trimming = false;
            for (int j=0;j<global::sequence->clips_.size();j++) {
              ClipPtr c = global::sequence->clips_.at(j);
              if (c != nullptr && c->timeline_info.track_ == ghost_clip->timeline_info.track_) {
                bool found = false;
                for (int k=0;k<size;k++) {
                  if (PanelManager::timeLine().ghosts.at(k).clip == j) {
                    found = true;
                    break;
                  }
                }
                if (!found) {
                  bool is_in = (c->timeline_info.in == ghost_clip->timeline_info.out);
                  if (is_in || c->timeline_info.out == ghost_clip->timeline_info.in) {
                    Ghost gh;
                    gh.transition = nullptr;
                    gh.clip = j;
                    gh.trimming = true;
                    gh.trim_in = is_in;
                    PanelManager::timeLine().ghosts.append(gh);
                  }
                }
              }
            }
          }
        }

        init_ghosts();

        // ripple edit prep
        if (PanelManager::timeLine().tool == TimelineToolType::RIPPLE) {
          int64_t axis = LONG_MAX;

          for (int i=0;i<PanelManager::timeLine().ghosts.size();i++) {
            ClipPtr c = global::sequence->clips_.at(PanelManager::timeLine().ghosts.at(i).clip);
            if (PanelManager::timeLine().trim_in_point) {
              axis = qMin(axis, c->timeline_info.in.load());
            } else {
              axis = qMin(axis, c->timeline_info.out.load());
            }
          }

          for (int i=0;i<global::sequence->clips_.size();i++) {
            ClipPtr c = global::sequence->clips_.at(i);
            if (c != nullptr && !c->isSelected(true)) {
              bool clip_is_post = (c->timeline_info.in >= axis);

              // see if this a clip on this track is already in the list, and if it's closer
              bool found = false;
              QVector<ClipPtr>& clip_list = clip_is_post ? post_clips : pre_clips;
              for (int j=0;j<clip_list.size();j++) {
                ClipPtr compare = clip_list.at(j);
                if (compare->timeline_info.track_ == c->timeline_info.track_) {
                  if ((!clip_is_post && compare->timeline_info.out < c->timeline_info.out)
                      || (clip_is_post && compare->timeline_info.in > c->timeline_info.in)) {
                    clip_list[j] = c;
                  }
                  found = true;
                  break;
                }
              }
              if (!found) {
                clip_list.append(c);
              }
            }
          }
        }

        // store selections
        selection_command = new SetSelectionsCommand(global::sequence);
        selection_command->old_data = global::sequence->selections_;

        PanelManager::timeLine().moving_proc = true;
      }
      PanelManager::refreshPanels(false);
    } else if (PanelManager::timeLine().splitting) {
      mouseMoveSplitEvent(alt, PanelManager::timeLine());
      PanelManager::refreshPanels(false);
    } else if (PanelManager::timeLine().rect_select_init) {
      if (PanelManager::timeLine().rect_select_proc) {
        PanelManager::timeLine().rect_select_w = event->pos().x() - PanelManager::timeLine().rect_select_x;
        PanelManager::timeLine().rect_select_h = event->pos().y() - PanelManager::timeLine().rect_select_y;
        if (bottom_align) PanelManager::timeLine().rect_select_h -= height();

        long frame_start = PanelManager::timeLine().getTimelineFrameFromScreenPoint(PanelManager::timeLine().rect_select_x);
        long frame_end = PanelManager::timeLine().getTimelineFrameFromScreenPoint(event->pos().x());
        long frame_min = qMin(frame_start, frame_end);
        long frame_max = qMax(frame_start, frame_end);

        int rsy = PanelManager::timeLine().rect_select_y;
        if (bottom_align) rsy += height();
        int track_start = getTrackFromScreenPoint(rsy);
        int track_end = getTrackFromScreenPoint(event->pos().y());
        int track_min = qMin(track_start, track_end);
        int track_max = qMax(track_start, track_end);

        QVector<ClipPtr> selected_clips;
        for (int i=0;i<global::sequence->clips_.size();i++) {
          ClipPtr clip = global::sequence->clips_.at(i);
          if (clip != nullptr &&
              clip->timeline_info.track_ >= track_min &&
              clip->timeline_info.track_ <= track_max &&
              !(clip->timeline_info.in < frame_min && clip->timeline_info.out < frame_min) &&
              !(clip->timeline_info.in > frame_max && clip->timeline_info.out > frame_max)) {
            QVector<ClipPtr> session_clips;
            session_clips.append(clip);

            if (!alt) {
              for (int j=0;j<clip->linkedClips().size();j++) {
                session_clips.append(global::sequence->clip(clip->linkedClips().at(j)));
              }
            }

            for (int j=0;j<session_clips.size();j++) {
              // see if clip has already been added - this can easily happen due to adding linked clips
              bool found = false;
              ClipPtr c = session_clips.at(j);
              for (int k=0;k<selected_clips.size();k++) {
                if (selected_clips.at(k) == c) {
                  found = true;
                  break;
                }
              }
              if (!found) {
                selected_clips.append(c);
              }
            }
          }
        }

        global::sequence->selections_.resize(selected_clips.size() + PanelManager::timeLine().selection_offset);
        for (int i=0;i<selected_clips.size();i++) {
          Selection& s = global::sequence->selections_[i+PanelManager::timeLine().selection_offset];
          ClipPtr clip = selected_clips.at(i);
          s.old_in = s.in = clip->timeline_info.in;
          s.old_out = s.out = clip->timeline_info.out;
          s.old_track = s.track = clip->timeline_info.track_;
        }

        PanelManager::timeLine().repaint_timeline();
      } else {
        PanelManager::timeLine().rect_select_x = event->pos().x();
        PanelManager::timeLine().rect_select_y = event->pos().y();
        if (bottom_align) PanelManager::timeLine().rect_select_y -= height();
        PanelManager::timeLine().rect_select_w = 0;
        PanelManager::timeLine().rect_select_h = 0;
        PanelManager::timeLine().rect_select_proc = true;
      }
    } else if (isLiveEditing()) {
      // redraw because we have a cursor
      PanelManager::timeLine().repaint_timeline();
    } else if (PanelManager::timeLine().tool == TimelineToolType::POINTER ||
               PanelManager::timeLine().tool == TimelineToolType::RIPPLE ||
               PanelManager::timeLine().tool == TimelineToolType::ROLLING) {
      QToolTip::hideText();

      QPoint pos = event->pos();

      int lim = 5;
      int mouse_track = getTrackFromScreenPoint(pos.y());
      long mouse_frame_lower = PanelManager::timeLine().getTimelineFrameFromScreenPoint(pos.x()-lim)-1;
      long mouse_frame_upper = PanelManager::timeLine().getTimelineFrameFromScreenPoint(pos.x()+lim)+1;
      bool found = false;
      bool cursor_contains_clip = false;
      int closeness = INT_MAX;
      int min_track = INT_MAX;
      int max_track = INT_MIN;
      PanelManager::timeLine().transition_select = TA_NO_TRANSITION;
      for (int i=0;i<global::sequence->clips_.size();i++) {
        ClipPtr c = global::sequence->clips_.at(i);
        if (c != nullptr) {
          min_track = qMin(min_track, c->timeline_info.track_.load());
          max_track = qMax(max_track, c->timeline_info.track_.load());
          if (c->timeline_info.track_ == mouse_track) {
            if (PanelManager::timeLine().cursor_frame >= c->timeline_info.in &&
                PanelManager::timeLine().cursor_frame <= c->timeline_info.out) {
              cursor_contains_clip = true;

              tooltip_timer.start();
              tooltip_clip = i;

              if (c->openingTransition() != nullptr &&
                  PanelManager::timeLine().cursor_frame <= (c->timeline_info.in + c->openingTransition()->get_true_length()) ) {
                PanelManager::timeLine().transition_select = TA_OPENING_TRANSITION;
              } else if (c->closingTransition() != nullptr
                         && PanelManager::timeLine().cursor_frame >= (c->timeline_info.out - c->closingTransition()->get_true_length()) ) {
                PanelManager::timeLine().transition_select = TA_CLOSING_TRANSITION;
              }
            }
            if (c->timeline_info.in > mouse_frame_lower && c->timeline_info.in < mouse_frame_upper) {
              int nc = qAbs(c->timeline_info.in + 1 - PanelManager::timeLine().cursor_frame);
              if (nc < closeness) {
                PanelManager::timeLine().trim_target = i;
                PanelManager::timeLine().trim_in_point = true;
                closeness = nc;
                found = true;
              }
            }
            if (c->timeline_info.out > mouse_frame_lower && c->timeline_info.out < mouse_frame_upper) {
              int nc = qAbs(c->timeline_info.out - 1 - PanelManager::timeLine().cursor_frame);
              if (nc < closeness) {
                PanelManager::timeLine().trim_target = i;
                PanelManager::timeLine().trim_in_point = false;
                closeness = nc;
                found = true;
              }
            }
            if (PanelManager::timeLine().tool == TimelineToolType::POINTER) {
              if (c->openingTransition() != nullptr) {
                long transition_point = c->timeline_info.in + c->openingTransition()->get_true_length();

                if (transition_point > mouse_frame_lower && transition_point < mouse_frame_upper) {
                  int nc = qAbs(transition_point - 1 - PanelManager::timeLine().cursor_frame);
                  if (nc < closeness) {
                    PanelManager::timeLine().trim_target = i;
                    PanelManager::timeLine().trim_in_point = false;
                    PanelManager::timeLine().transition_select = TA_OPENING_TRANSITION;
                    closeness = nc;
                    found = true;
                  }
                }
              }
              if (c->closingTransition() != nullptr) {
                long transition_point = c->timeline_info.out - c->closingTransition()->get_true_length();
                if (transition_point > mouse_frame_lower && transition_point < mouse_frame_upper) {
                  int nc = qAbs(transition_point + 1 - PanelManager::timeLine().cursor_frame);
                  if (nc < closeness) {
                    PanelManager::timeLine().trim_target = i;
                    PanelManager::timeLine().trim_in_point = true;
                    PanelManager::timeLine().transition_select = TA_CLOSING_TRANSITION;
                    closeness = nc;
                    found = true;
                  }
                }
              }
            }
          }
        }
      }
      /*if (cursor_contains_clip) {
                QToolTip::showText(mapToGlobal(event->pos()), "HOVER OVER CLIP");
            }*/
      if (found) {
        setCursor(Qt::SizeHorCursor);
      } else {
        PanelManager::timeLine().trim_target = -1;

        // look for track heights
        int track_y = 0;
        for (int i=0;i<PanelManager::timeLine().get_track_height_size(bottom_align);i++) {
          int track = (bottom_align) ? -1-i : i;
          if (track >= min_track && track <= max_track) {
            int track_height = PanelManager::timeLine().calculate_track_height(track, -1);
            track_y += track_height;
            int y_test_value = (bottom_align) ? rect().bottom() - track_y : track_y;
            int test_range = 5;
            int mouse_pos = pos.y() + scroll;
            if (mouse_pos > y_test_value-test_range && mouse_pos < y_test_value+test_range) {
              // if track lines are hidden, only resize track if a clip is already there
              if (e_config.show_track_lines || cursor_contains_clip) {
                found = true;
                track_resizing = true;
                track_target = track;
                track_resize_old_value = track_height;
              }
              break;
            }
          }
        }

        if (found) {
          setCursor(Qt::SizeVerCursor);
        } else {
          unsetCursor();
        }
      }
    } else if (PanelManager::timeLine().tool == TimelineToolType::SLIP) {
      if (getClipIndexFromCoords(PanelManager::timeLine().cursor_frame, PanelManager::timeLine().cursor_track) > -1) {
        setCursor(Qt::SizeHorCursor);
      } else {
        unsetCursor();
      }
    } else if (PanelManager::timeLine().tool == TimelineToolType::TRANSITION) {
      if (PanelManager::timeLine().transition_tool_init) {
        if (PanelManager::timeLine().transition_tool_proc) {
          update_ghosts(event->pos(), event->modifiers() & Qt::ShiftModifier);
        } else {
          ClipPtr c = global::sequence->clips_.at(PanelManager::timeLine().transition_tool_pre_clip);

          Ghost g;

          g.in = g.old_in = g.out = g.old_out = (PanelManager::timeLine().transition_tool_type == TA_OPENING_TRANSITION)
                                                ? c->timeline_info.in.load() : c->timeline_info.out.load();
          g.track = c->timeline_info.track_;
          g.clip = PanelManager::timeLine().transition_tool_pre_clip;
          g.media_stream = PanelManager::timeLine().transition_tool_type;
          g.trimming = false;

          PanelManager::timeLine().ghosts.append(g);

          PanelManager::timeLine().transition_tool_proc = true;
        }
      } else {
        int mouse_clip = getClipIndexFromCoords(PanelManager::timeLine().cursor_frame, PanelManager::timeLine().cursor_track);
        if (mouse_clip > -1) {
          ClipPtr c = global::sequence->clips_.at(mouse_clip);
          if (same_sign(c->timeline_info.track_, PanelManager::timeLine().transition_tool_side)) {
            PanelManager::timeLine().transition_tool_pre_clip = mouse_clip;
            long halfway = c->timeline_info.in + (c->length()/2);
            long between_range = getFrameFromScreenPoint(PanelManager::timeLine().zoom, TRANSITION_BETWEEN_RANGE) + 1;

            if (PanelManager::timeLine().cursor_frame > halfway) {
              PanelManager::timeLine().transition_tool_type = TA_CLOSING_TRANSITION;
            } else {
              PanelManager::timeLine().transition_tool_type = TA_OPENING_TRANSITION;
            }

            PanelManager::timeLine().transition_tool_post_clip = -1;
            if (PanelManager::timeLine().cursor_frame < c->timeline_info.in + between_range) {
              PanelManager::timeLine().transition_tool_post_clip = getClipIndexFromCoords(c->timeline_info.in - 1,
                                                                                          c->timeline_info.track_);
            } else if (PanelManager::timeLine().cursor_frame > c->timeline_info.out - between_range) {
              PanelManager::timeLine().transition_tool_post_clip = getClipIndexFromCoords(c->timeline_info.out + 1,
                                                                                          c->timeline_info.track_);
            }
          }
        } else {
          PanelManager::timeLine().transition_tool_pre_clip = -1;
          PanelManager::timeLine().transition_tool_post_clip = -1;
        }
      }

      PanelManager::timeLine().repaint_timeline();
    }
  }
}

void TimelineWidget::leaveEvent(QEvent*) {
  tooltip_timer.stop();
}

int color_brightness(const QColor& color) {
  //FIXME: magic numbers
  return qRound(0.2126*color.red() + 0.7152*color.green() + 0.0722*color.blue());
}

//TODO: logarithmic or linear option? Currently seems to be linear
void draw_waveform(ClipPtr& clip, const FootageStreamPtr& ms, const long media_length, QPainter &p, const QRect& clip_rect,
                   const int waveform_start, const int waveform_limit, const double zoom)
{
  //FIXME: magic numbers in function
  auto divider = ms->audio_channels*2;
  auto channel_height = clip_rect.height()/ms->audio_channels;

  for (auto i=waveform_start; i<waveform_limit; ++i) {
    auto waveform_index = qFloor((((clip->timeline_info.clip_in + (static_cast<double>(i)/zoom))/media_length) * ms->audio_preview.size())/divider)*divider;

    if (clip->timeline_info.reverse) {
      waveform_index = ms->audio_preview.size() - waveform_index - (ms->audio_channels * 2);
    }

    for (auto j=0; j<ms->audio_channels; ++j) {
      auto mid = (e_config.rectified_waveforms) ? clip_rect.top()+channel_height*(j+1) : clip_rect.top()+channel_height*j+(channel_height/2);
      auto offset = waveform_index+(j*2);

      if ((offset + 1) < ms->audio_preview.size()) {
        const auto min = static_cast<double>(ms->audio_preview.at(offset)) / 128.0 * (channel_height/2);
        const auto max = static_cast<double>(ms->audio_preview.at(offset+1)) / 128.0 * (channel_height/2);

        if (e_config.rectified_waveforms)  {
          p.drawLine(clip_rect.left()+i, mid, clip_rect.left()+i, mid - (max - min));
        } else {
          p.drawLine(clip_rect.left()+i, mid+min, clip_rect.left()+i, mid+max);
        }
      }/* else {
                qWarning() << "Tried to reach" << offset + 1 << ", limit:" << ms.audio_preview.size();
            }*/
    }
  }
}

void draw_transition(QPainter& p, const ClipPtr& c, const QRect& clip_rect, QRect& text_rect, int transition_type) {
  auto t = (transition_type == TA_OPENING_TRANSITION) ? c->openingTransition() : c->closingTransition();
  if (t != nullptr) {
    int transition_width = getScreenPointFromFrame(PanelManager::timeLine().zoom, t->get_true_length());
    int transition_height = clip_rect.height();
    int tr_y = clip_rect.y();
    int tr_x = 0;
    if (transition_type == TA_OPENING_TRANSITION) {
      tr_x = clip_rect.x();
      text_rect.setX(text_rect.x()+transition_width);
    } else {
      tr_x = clip_rect.right()-transition_width;
      text_rect.setWidth(text_rect.width()-transition_width);
    }
    QRect transition_rect = QRect(tr_x, tr_y, transition_width, transition_height);
    p.fillRect(transition_rect, TRANSITION_COLOR);
    QRect transition_text_rect(transition_rect.x() + CLIP_TEXT_PADDING, transition_rect.y() + CLIP_TEXT_PADDING,
                               transition_rect.width() - CLIP_TEXT_PADDING, transition_rect.height() - CLIP_TEXT_PADDING);
    if (transition_text_rect.width() > MAX_TEXT_WIDTH) {
      bool draw_text = true;

      p.setPen(QColor(0, 0, 0, 96));
      if (t->secondary_clip.expired()) {
        if (transition_type == TA_OPENING_TRANSITION) {
          p.drawLine(transition_rect.bottomLeft(), transition_rect.topRight());
        } else {
          p.drawLine(transition_rect.topLeft(), transition_rect.bottomRight());
        }
      } else {
        if (transition_type == TA_OPENING_TRANSITION) {
          p.drawLine(QPoint(transition_rect.left(), transition_rect.center().y()), transition_rect.topRight());
          p.drawLine(QPoint(transition_rect.left(), transition_rect.center().y()), transition_rect.bottomRight());
          draw_text = false;
        } else {
          p.drawLine(QPoint(transition_rect.right(), transition_rect.center().y()), transition_rect.topLeft());
          p.drawLine(QPoint(transition_rect.right(), transition_rect.center().y()), transition_rect.bottomLeft());
        }
      }

      if (draw_text) {
        p.setPen(Qt::white);
        p.drawText(transition_text_rect, 0, t->meta.name, &transition_text_rect);
      }
    }
    p.setPen(Qt::black);
    p.drawRect(transition_rect);
  }

}

void TimelineWidget::paintSplitEvent(QPainter& painter, Timeline& time_line)
{
  for (auto track : time_line.split_tracks) {
    if (is_track_visible(track)) {
      int cursor_x = PanelManager::timeLine().getTimelineScreenPointFromFrame(time_line.drag_frame_start);
      int cursor_y = getScreenPointFromTrack(track);

      painter.setPen(QColor(64, 64, 64)); //FIXME: magic number
      painter.drawLine(cursor_x,
                       cursor_y,
                       cursor_x,
                       cursor_y + time_line.calculate_track_height(track, -1));
    }
  }
}

//FIXME: oh god
void TimelineWidget::paintEvent(QPaintEvent*)
{
  // Draw clips
  if (global::sequence != nullptr) {
    QPainter painter(this);

    // get widget width and height
    int video_track_limit = 0;
    int audio_track_limit = 0;
    for (int i=0;i<global::sequence->clips_.size();i++) {
      ClipPtr clip = global::sequence->clips_.at(i);
      if (clip != nullptr) {
        video_track_limit = qMin(video_track_limit, clip->timeline_info.track_.load());
        audio_track_limit = qMax(audio_track_limit, clip->timeline_info.track_.load());
      }
    }

    int panel_height = TRACK_DEFAULT_HEIGHT;
    if (bottom_align) {
      for (int i=-1;i>=video_track_limit;i--) {
        panel_height += PanelManager::timeLine().calculate_track_height(i, -1);
      }
    } else {
      for (int i=0;i<=audio_track_limit;i++) {
        panel_height += PanelManager::timeLine().calculate_track_height(i, -1);
      }
    }
    if (bottom_align) {
      scrollBar->setMinimum(qMin(0, - panel_height + height()));
    } else {
      scrollBar->setMaximum(qMax(0, panel_height - height()));
    }

    for (int i=0;i<global::sequence->clips_.size();i++) {
      ClipPtr clip = global::sequence->clips_.at(i);
      if (clip != nullptr && is_track_visible(clip->timeline_info.track_)) {
        QRect clip_rect(PanelManager::timeLine().getTimelineScreenPointFromFrame(clip->timeline_info.in),
                        getScreenPointFromTrack(clip->timeline_info.track_),
                        getScreenPointFromFrame(PanelManager::timeLine().zoom, clip->length()),
                        PanelManager::timeLine().calculate_track_height(clip->timeline_info.track_, -1));
        QRect text_rect(clip_rect.left() + CLIP_TEXT_PADDING,
                        clip_rect.top() + CLIP_TEXT_PADDING,
                        clip_rect.width() - CLIP_TEXT_PADDING - 1,
                        clip_rect.height() - CLIP_TEXT_PADDING - 1);

        if (clip_rect.left() < width() && clip_rect.right() >= 0 && clip_rect.top() < height() && clip_rect.bottom() >= 0) {
          QRect actual_clip_rect = clip_rect;
          if (actual_clip_rect.x() < 0) {
            actual_clip_rect.setX(0);
          }
          if (actual_clip_rect.right() > width()) {
            actual_clip_rect.setRight(width());
          }
          if (actual_clip_rect.y() < 0) {
            actual_clip_rect.setY(0);
          }
          if (actual_clip_rect.bottom() > height()) {
            actual_clip_rect.setBottom(height());
          }
          painter.fillRect(actual_clip_rect, (clip->timeline_info.enabled) ? clip->timeline_info.color : QColor(96, 96, 96));

          int thumb_x = clip_rect.x() + 1;

          if (clip->timeline_info.media != nullptr &&
              clip->timeline_info.media->type() == MediaType::FOOTAGE) {
            bool draw_checkerboard = false;
            QRect checkerboard_rect(clip_rect);
            auto ftg = clip->timeline_info.media->object<Footage>();
            FootageStreamPtr ms;
            if (clip->timeline_info.isVideo()) {
              ms = ftg->video_stream_from_file_index(clip->timeline_info.media_stream);
            } else {
              ms = ftg->audio_stream_from_file_index(clip->timeline_info.media_stream);
            }

            if (ms == nullptr) {
              draw_checkerboard = true;
            } else if (ms->preview_done) {
              // draw top and tail triangles
              int triangle_size = TRACK_MIN_HEIGHT >> 2;
              if (!ms->infinite_length && clip_rect.width() > triangle_size) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(80, 80, 80));
                if (clip->timeline_info.clip_in == 0
                    && clip_rect.x() + triangle_size > 0
                    && clip_rect.y() + triangle_size > 0
                    && clip_rect.x() < width()
                    && clip_rect.y() < height()) {
                  const QPoint points[3] = {
                    QPoint(clip_rect.x(), clip_rect.y()),
                    QPoint(clip_rect.x() + triangle_size, clip_rect.y()),
                    QPoint(clip_rect.x(), clip_rect.y() + triangle_size)
                  };
                  painter.drawPolygon(points, 3);
                  text_rect.setLeft(text_rect.left() + (triangle_size >> 2));
                }
                if (clip->timeline_info.out - clip->timeline_info.in + clip->timeline_info.clip_in == clip->maximumLength()
                    && clip_rect.right() - triangle_size < width()
                    && clip_rect.y() + triangle_size > 0
                    && clip_rect.right() > 0
                    && clip_rect.y() < height()) {
                  const QPoint points[3] = {
                    QPoint(clip_rect.right(), clip_rect.y()),
                    QPoint(clip_rect.right() - triangle_size, clip_rect.y()),
                    QPoint(clip_rect.right(), clip_rect.y() + triangle_size)
                  };
                  painter.drawPolygon(points, 3);
                  text_rect.setRight(text_rect.right() - (triangle_size >> 2));
                }
              }

              painter.setBrush(Qt::NoBrush);

              // draw thumbnail/waveform
              long media_length = clip->maximumLength();

              if (clip->timeline_info.isVideo()) {
                // draw thumbnail
                int thumb_y = painter.fontMetrics().height()+CLIP_TEXT_PADDING+CLIP_TEXT_PADDING;
                if (thumb_x < width() && thumb_y < height()) {
                  int space_for_thumb = clip_rect.width()-1;
                  if (clip->openingTransition() != nullptr) {
                    int ot_width = getScreenPointFromFrame(PanelManager::timeLine().zoom, clip->openingTransition()->get_true_length());
                    thumb_x += ot_width;
                    space_for_thumb -= ot_width;
                  }
                  if (clip->closingTransition() != nullptr) {
                    space_for_thumb -= getScreenPointFromFrame(PanelManager::timeLine().zoom, clip->closingTransition()->get_true_length());
                  }
                  int thumb_height = clip_rect.height()-thumb_y;
                  int thumb_width = (thumb_height*((double)ms->video_preview.width()/(double)ms->video_preview.height()));
                  if (thumb_x + thumb_width >= 0
                      && thumb_height > thumb_y
                      && thumb_y + thumb_height >= 0
                      && space_for_thumb > MAX_TEXT_WIDTH) {
                    int thumb_clip_width = qMin(thumb_width, space_for_thumb);
                    painter.drawImage(QRect(thumb_x, clip_rect.y()+thumb_y, thumb_clip_width, thumb_height),
                                ms->video_preview,
                                QRect(0, 0, thumb_clip_width*((double)ms->video_preview.width()/(double)thumb_width), ms->video_preview.height()));
                  }
                }
                if (clip->timeline_info.out - clip->timeline_info.in + clip->timeline_info.clip_in > clip->maximumLength()) {
                  draw_checkerboard = true;
                  checkerboard_rect.setLeft(PanelManager::timeLine().getTimelineScreenPointFromFrame(clip->maximumLength() + clip->timeline_info.in - clip->timeline_info.clip_in));
                }
              } else if (clip_rect.height() > TRACK_MIN_HEIGHT) {
                // draw waveform
                painter.setPen(QColor(80, 80, 80));

                int waveform_start = -qMin(clip_rect.x(), 0);
                int waveform_limit = qMin(clip_rect.width(), getScreenPointFromFrame(PanelManager::timeLine().zoom, media_length - clip->timeline_info.clip_in));

                if ((clip_rect.x() + waveform_limit) > width()) {
                  waveform_limit -= (clip_rect.x() + waveform_limit - width());
                } else if (waveform_limit < clip_rect.width()) {
                  draw_checkerboard = true;
                  if (waveform_limit > 0) checkerboard_rect.setLeft(checkerboard_rect.left() + waveform_limit);
                }

                draw_waveform(clip, ms, media_length, painter, clip_rect, waveform_start, waveform_limit, PanelManager::timeLine().zoom);
              }
            }
            if (draw_checkerboard) {
              checkerboard_rect.setLeft(qMax(checkerboard_rect.left(), 0));
              checkerboard_rect.setRight(qMin(checkerboard_rect.right(), width()));
              checkerboard_rect.setTop(qMax(checkerboard_rect.top(), 0));
              checkerboard_rect.setBottom(qMin(checkerboard_rect.bottom(), height()));

              if (checkerboard_rect.left() < width()
                  && checkerboard_rect.right() >= 0
                  && checkerboard_rect.top() < height()
                  && checkerboard_rect.bottom() >= 0) {
                // draw "error lines" if media stream is missing
                painter.setPen(QPen(QColor(64, 64, 64), 2));
                int limit = checkerboard_rect.width();
                int clip_height = checkerboard_rect.height();
                for (int j=-clip_height;j<limit;j+=15) {
                  int lines_start_x = checkerboard_rect.left()+j;
                  int lines_start_y = checkerboard_rect.bottom();
                  int lines_end_x = lines_start_x + clip_height;
                  int lines_end_y = checkerboard_rect.top();
                  if (lines_start_x < checkerboard_rect.left()) {
                    lines_start_y -= (checkerboard_rect.left() - lines_start_x);
                    lines_start_x = checkerboard_rect.left();
                  }
                  if (lines_end_x > checkerboard_rect.right()) {
                    lines_end_y -= (checkerboard_rect.right() - lines_end_x);
                    lines_end_x = checkerboard_rect.right();
                  }
                  painter.drawLine(lines_start_x, lines_start_y, lines_end_x, lines_end_y);
                }
              }
            }
          }

          // draw clip transitions
          draw_transition(painter, clip, clip_rect, text_rect, TA_OPENING_TRANSITION);
          draw_transition(painter, clip, clip_rect, text_rect, TA_CLOSING_TRANSITION);

          // top left bevel
          painter.setPen(Qt::white);
          if (clip_rect.x() >= 0 && clip_rect.x() < width()) painter.drawLine(clip_rect.bottomLeft(), clip_rect.topLeft());
          if (clip_rect.y() >= 0 && clip_rect.y() < height()) painter.drawLine(QPoint(qMax(0, clip_rect.left()), clip_rect.top()),
                                                                         QPoint(qMin(width(), clip_rect.right()), clip_rect.top()));

          // draw text
          if (text_rect.width() > MAX_TEXT_WIDTH && text_rect.right() > 0 && text_rect.left() < width()) {
            if (!clip->timeline_info.enabled) {
              painter.setPen(Qt::gray);
            } else if (color_brightness(clip->timeline_info.color) > 160) {
              // set to black if color is bright
              painter.setPen(Qt::black);
            }
            if (!clip->linkedClips().empty()) {
              int underline_y = CLIP_TEXT_PADDING + painter.fontMetrics().height() + clip_rect.top();
              int underline_width = qMin(text_rect.width() - 1, painter.fontMetrics().width(clip->name()));
              painter.drawLine(text_rect.x(), underline_y, text_rect.x() + underline_width, underline_y);
            }
            QString name(clip->name());
            if (!qFuzzyCompare(clip->timeline_info.speed, 1.0) || clip->timeline_info.reverse) {
              name += " (";
              if (clip->timeline_info.reverse) {
                name += "-";
              }
              name += QString::number(clip->timeline_info.speed * 100) + "%)";
            }
            painter.drawText(text_rect, 0, name, &text_rect);
          }

          // bottom right gray
          painter.setPen(QColor(0, 0, 0, 128));
          if (clip_rect.right() >= 0 && clip_rect.right() < width()) painter.drawLine(clip_rect.bottomRight(), clip_rect.topRight());
          if (clip_rect.bottom() >= 0 && clip_rect.bottom() < height()) painter.drawLine(QPoint(qMax(0, clip_rect.left()), clip_rect.bottom()), QPoint(qMin(width(), clip_rect.right()), clip_rect.bottom()));

          // draw transition tool
          if (PanelManager::timeLine().tool == TimelineToolType::TRANSITION
              && (PanelManager::timeLine().transition_tool_pre_clip == i
                  || PanelManager::timeLine().transition_tool_post_clip == i)) {
            int type = PanelManager::timeLine().transition_tool_type;
            if (PanelManager::timeLine().transition_tool_post_clip == i) {
              // invert transition type
              type = (type == TA_CLOSING_TRANSITION) ? TA_OPENING_TRANSITION : TA_CLOSING_TRANSITION;
            }
            QRect transition_tool_rect = clip_rect;
            if (type == TA_CLOSING_TRANSITION) {
              if (PanelManager::timeLine().transition_tool_post_clip > -1) {
                transition_tool_rect.setLeft(transition_tool_rect.right() - TRANSITION_BETWEEN_RANGE);
              } else {
                transition_tool_rect.setLeft(transition_tool_rect.left() + (3*(transition_tool_rect.width()>>2)));
              }
            } else {
              if (PanelManager::timeLine().transition_tool_post_clip > -1) {
                transition_tool_rect.setWidth(TRANSITION_BETWEEN_RANGE);
              } else {
                transition_tool_rect.setWidth(transition_tool_rect.width()>>2);
              }
            }
            if (transition_tool_rect.left() < width() && transition_tool_rect.right() > 0) {
              if (transition_tool_rect.left() < 0) {
                transition_tool_rect.setLeft(0);
              }
              if (transition_tool_rect.right() > width()) {
                transition_tool_rect.setRight(width());
              }
              painter.fillRect(transition_tool_rect, QColor(0, 0, 0, 128));
            }
          }
        }
      }
    }

    // Draw recording clip if recording if valid
    if (PanelManager::sequenceViewer().is_recording_cued() && is_track_visible(PanelManager::sequenceViewer().recording_track)) {
      int rec_track_x = PanelManager::timeLine().getTimelineScreenPointFromFrame(PanelManager::sequenceViewer().recording_start);
      int rec_track_y = getScreenPointFromTrack(PanelManager::sequenceViewer().recording_track);
      int rec_track_height = PanelManager::timeLine().calculate_track_height(PanelManager::sequenceViewer().recording_track, -1);
      if (PanelManager::sequenceViewer().recording_start != PanelManager::sequenceViewer().recording_end) {
        QRect rec_rect(
              rec_track_x,
              rec_track_y,
              getScreenPointFromFrame(PanelManager::timeLine().zoom,
                                      PanelManager::sequenceViewer().recording_end - PanelManager::sequenceViewer().recording_start),
              rec_track_height
              );
        painter.setPen(QPen(QColor(96, 96, 96), 2));
        painter.fillRect(rec_rect, QColor(192, 192, 192));
        painter.drawRect(rec_rect);
      }
      QRect active_rec_rect(
            rec_track_x,
            rec_track_y,
            getScreenPointFromFrame(PanelManager::timeLine().zoom,
                                    PanelManager::sequenceViewer().getSequence()->playhead_ - PanelManager::sequenceViewer().recording_start),
            rec_track_height
            );
      painter.setPen(QPen(QColor(192, 0, 0), 2));
      painter.fillRect(active_rec_rect, QColor(255, 96, 96));
      painter.drawRect(active_rec_rect);

      painter.setPen(Qt::NoPen);

      if (!PanelManager::sequenceViewer().playing) {
        int rec_marker_size = 6;
        int rec_track_midY = rec_track_y + (rec_track_height >> 1);
        painter.setBrush(Qt::white);
        QPoint cue_marker[3] = {
          QPoint(rec_track_x, rec_track_midY - rec_marker_size),
          QPoint(rec_track_x + rec_marker_size, rec_track_midY),
          QPoint(rec_track_x, rec_track_midY + rec_marker_size)
        };
        painter.drawPolygon(cue_marker, 3);
      }
    }

    // Draw track lines
    if (e_config.show_track_lines) {
      painter.setPen(QColor(0, 0, 0, 96));
      audio_track_limit++;
      if (video_track_limit == 0) video_track_limit--;

      if (bottom_align) {
        // only draw lines for video tracks
        for (int i=video_track_limit;i<0;i++) {
          int line_y = getScreenPointFromTrack(i) - 1;
          painter.drawLine(0, line_y, rect().width(), line_y);
        }
      } else {
        // only draw lines for audio tracks
        for (int i=0;i<audio_track_limit;i++) {
          int line_y = getScreenPointFromTrack(i) + PanelManager::timeLine().calculate_track_height(i, -1);
          painter.drawLine(0, line_y, rect().width(), line_y);
        }
      }
    }

    // Draw selections
    for (int i=0;i<global::sequence->selections_.size();i++) {
      const Selection& s = global::sequence->selections_.at(i);
      if (is_track_visible(s.track)) {
        int selection_y = getScreenPointFromTrack(s.track);
        int selection_x = PanelManager::timeLine().getTimelineScreenPointFromFrame(s.in);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::NoBrush);
        painter.fillRect(selection_x,
                   selection_y,
                   PanelManager::timeLine().getTimelineScreenPointFromFrame(s.out) - selection_x,
                   PanelManager::timeLine().calculate_track_height(s.track, -1), QColor(0, 0, 0, 64));
      }
    }

    // draw rectangle select
    if (PanelManager::timeLine().rect_select_proc) {
      int rsy = PanelManager::timeLine().rect_select_y;
      int rsh = PanelManager::timeLine().rect_select_h;
      if (bottom_align) {
        rsy += height();
      }
      QRect rect_select(PanelManager::timeLine().rect_select_x, rsy, PanelManager::timeLine().rect_select_w, rsh);
      draw_selection_rectangle(painter, rect_select);
    }

    // Draw ghosts
    if (!PanelManager::timeLine().ghosts.isEmpty()) {
      QVector<int> insert_points;
      long first_ghost = LONG_MAX;
      for (int i=0;i<PanelManager::timeLine().ghosts.size();i++) {
        const Ghost& g = PanelManager::timeLine().ghosts.at(i);
        first_ghost = qMin(first_ghost, g.in);
        if (is_track_visible(g.track)) {
          int ghost_x = PanelManager::timeLine().getTimelineScreenPointFromFrame(g.in);
          int ghost_y = getScreenPointFromTrack(g.track);
          int ghost_width = PanelManager::timeLine().getTimelineScreenPointFromFrame(g.out) - ghost_x - 1;
          int ghost_height = PanelManager::timeLine().calculate_track_height(g.track, -1) - 1;

          insert_points.append(ghost_y + (ghost_height>>1));

          painter.setPen(QColor(255, 255, 0));
          for (int j=0;j<GHOST_THICKNESS;j++) {
            painter.drawRect(ghost_x+j, ghost_y+j, ghost_width-j-j, ghost_height-j-j);
          }
        }
      }

      // draw insert indicator
      if (PanelManager::timeLine().move_insert && !insert_points.isEmpty()) {
        painter.setBrush(Qt::white);
        painter.setPen(Qt::NoPen);
        int insert_x = PanelManager::timeLine().getTimelineScreenPointFromFrame(first_ghost);
        int tri_size = TRACK_MIN_HEIGHT>>2;

        for (int i=0;i<insert_points.size();i++) {
          QPoint points[3] = {
            QPoint(insert_x, insert_points.at(i) - tri_size),
            QPoint(insert_x + tri_size, insert_points.at(i)),
            QPoint(insert_x, insert_points.at(i) + tri_size)
          };
          painter.drawPolygon(points, 3);
        }
      }
    }


    // Draw splitting cursor
    if (PanelManager::timeLine().splitting) {
      paintSplitEvent(painter, PanelManager::timeLine());
    }

    // Draw playhead
    painter.setPen(Qt::red);
    int playhead_x = PanelManager::timeLine().getTimelineScreenPointFromFrame(global::sequence->playhead_);
    painter.drawLine(playhead_x, rect().top(), playhead_x, rect().bottom());

    // draw border
    painter.setPen(QColor(0, 0, 0, 64));
    int edge_y = (bottom_align) ? rect().height()-1 : 0;
    painter.drawLine(0, edge_y, rect().width(), edge_y);

    // draw snap point
    if (PanelManager::timeLine().snapped) {
      painter.setPen(Qt::white);
      int snap_x = PanelManager::timeLine().getTimelineScreenPointFromFrame(PanelManager::timeLine().snap_point);
      painter.drawLine(snap_x, 0, snap_x, height());
    }

    // Draw edit cursor
    if (isLiveEditing() && is_track_visible(PanelManager::timeLine().cursor_track)) {
      int cursor_x = PanelManager::timeLine().getTimelineScreenPointFromFrame(PanelManager::timeLine().cursor_frame);
      int cursor_y = getScreenPointFromTrack(PanelManager::timeLine().cursor_track);

      painter.setPen(Qt::gray);
      painter.drawLine(cursor_x, cursor_y, cursor_x, cursor_y + PanelManager::timeLine().calculate_track_height(PanelManager::timeLine().cursor_track, -1));
    }
  }
}

void TimelineWidget::resizeEvent(QResizeEvent* /*event*/) {
  scrollBar->setPageStep(height());
}

bool TimelineWidget::is_track_visible(int track) {
  return (bottom_align == (track < 0));
}

// **************************************
// screen point <-> frame/track functions
// **************************************

int TimelineWidget::getTrackFromScreenPoint(int y) {
  y += scroll;
  if (bottom_align) {
    y = -(y - height());
  }
  y--;
  int height_measure = 0;
  int counter = ((!bottom_align && y > 0) || (bottom_align && y < 0)) ? 0 : -1;
  int track_height = PanelManager::timeLine().calculate_track_height(counter, -1);
  while (qAbs(y) > height_measure+track_height) {
    if (e_config.show_track_lines && counter != -1) y--;
    height_measure += track_height;
    if ((!bottom_align && y > 0) || (bottom_align && y < 0)) {
      counter++;
    } else {
      counter--;
    }
    track_height = PanelManager::timeLine().calculate_track_height(counter, -1);
  }
  return counter;
}

int TimelineWidget::getScreenPointFromTrack(int track) {
  int y = 0;
  int counter = 0;
  while (counter != track) {
    if (bottom_align) {
      counter--;
    }
    y += PanelManager::timeLine().calculate_track_height(counter, -1);
    if (!bottom_align) {
      counter++;
    }
    if (e_config.show_track_lines && counter != -1) {
      y++;
    }
  }
  y++;
  return (bottom_align) ? height() - y - scroll : y - scroll;
}

int TimelineWidget::getClipIndexFromCoords(long frame, int track)
{
  for (int i=0;i<global::sequence->clips_.size();i++) {
    ClipPtr c = global::sequence->clips_.at(i);
    if (c != nullptr && c->timeline_info.track_ == track && frame >= c->timeline_info.in && frame < c->timeline_info.out) {
      return i;
    }
  }
  return -1;
}

ClipPtr TimelineWidget::getClipFromCoords(const long frame, const int track) const
{
  for (const auto& seq_clip : global::sequence->clips_) {
    if (seq_clip != nullptr && seq_clip->timeline_info.track_ == track && seq_clip->timeline_info.in <= frame
        && seq_clip->timeline_info.out > frame) {
      return seq_clip;
    }
  }
  return nullptr;
}


bool TimelineWidget::splitClipEvent(const long frame, const QSet<int>& tracks)
{
  bool split = false;
  QVector<ClipPtr> new_clips;
  QSet<int> all_tracks = tracks;
  if (tracks.size() == 1) {
    // add the linked tracks
    if (auto clp = getClipFromCoords(frame, *tracks.begin())) {
      auto linked_tracks = clp->getLinkedTracks();
      all_tracks = all_tracks.unite(linked_tracks);
    }
  }
  for (auto track : all_tracks) {
    auto pre = getClipFromCoords(frame, track);
    if (pre != nullptr) {
      // Create copy
      auto post = pre->copy(global::sequence);
      // Adjust the in/out points
      pre->timeline_info.out = frame;
      post->timeline_info.in = frame;
      // Adjust transitions
      //FIXME:
//      post->closing_transition = pre->closing_transition;
//      pre->closing_transition = -1;
//      post->opening_transition = -1;
      if (pre->openingTransition() != nullptr) {
        pre->openingTransition()->set_length(qMin(pre->openingTransition()->get_length(), pre->length()));
      }
      if (post->closingTransition()) {
        post->closingTransition()->set_length(qMin(post->closingTransition()->get_length(), post->length()));
      }

      // Put new clip in sequence
      global::sequence->clips_.append(post);
      split = true;
      new_clips.append(post);
    }
  }
  //FIXME: on a drag-split, all splits will become linked
  // Sort out links to each other
  for (const auto& new_clip : new_clips) {
    for (const auto& other_clip : new_clips) {
      if (new_clip == other_clip) {
        // ignore, don't link itself
        continue;
      }
      new_clip->addLinkedClip(other_clip);
    }
  }

  return split;
}

void TimelineWidget::setScroll(int s) {
  scroll = s;
  update();
}

void TimelineWidget::reveal_media() {
  PanelManager::projectViewer().reveal_media(rc_reveal_media);
}
