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
#include "timeline.h"

#include <QTime>
#include <QScrollBar>
#include <QtMath>
#include <QGuiApplication>
#include <QScreen>
#include <QPainter>
#include <QMenu>
#include <QInputDialog>
#include <QMessageBox>
#include <QCheckBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QSplitter>
#include <QStatusBar>

#include "panels/panelmanager.h"
#include "ui/timelinewidget.h"
#include "project/sequence.h"
#include "project/clip.h"
#include "ui/viewerwidget.h"
#include "playback/audio.h"
#include "playback/playback.h"
#include "project/undo.h"
#include "project/media.h"
#include "io/config.h"
#include "project/effect.h"
#include "project/transition.h"
#include "project/footage.h"
#include "io/clipboard.h"
#include "ui/timelineheader.h"
#include "ui/resizablescrollbar.h"
#include "ui/audiomonitor.h"
#include "ui/mainwindow.h"
#include "debug.h"


constexpr auto STILL_IMAGE_FRAMES = 100;
constexpr int RECORD_MSG_TIMEOUT = 10000;

namespace
{
  const QColor AUDIO_ONLY_COLOR(128, 192, 128);
  const QColor VIDEO_ONLY_COLOR(192, 160, 128);
  const QColor AUDIO_VIDEO_COLOR(128, 128, 192);
  const QColor SEQUENCE_COLOR(192, 128, 128);
}

using panels::PanelManager;


Timeline::Timeline(QWidget *parent) :
  QDockWidget(parent),
  cursor_frame(0),
  cursor_track(0),
  zoom(1.0),
  zoom_just_changed(false),
  showing_all(false),
  snapping(true),
  snapped(false),
  snap_point(0),
  selecting(false),
  rect_select_init(false),
  rect_select_proc(false),
  moving_init(false),
  moving_proc(false),
  move_insert(false),
  trim_target(-1),
  trim_in_point(false),
  splitting(false),
  importing(false),
  importing_files(false),
  creating(false),
  transition_tool_init(false),
  transition_tool_proc(false),
  transition_tool_pre_clip(-1),
  transition_tool_post_clip(-1),
  hand_moving(false),
  block_repaints(false),
  last_frame(0),
  scroll(0)
{
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

  setup_ui();

  default_track_height = qRound((QGuiApplication::primaryScreen()->logicalDotsPerInch() / 96) * TRACK_DEFAULT_HEIGHT);

  headers->viewer = &PanelManager::sequenceViewer();

  video_area->bottom_align = true;
  video_area->scrollBar = videoScrollbar;
  audio_area->scrollBar = audioScrollbar;

  tool_buttons.append(toolArrowButton);
  tool_buttons.append(toolEditButton);
  tool_buttons.append(toolRippleButton);
  tool_buttons.append(toolRazorButton);
  tool_buttons.append(toolSlipButton);
  tool_buttons.append(toolSlideButton);
  tool_buttons.append(toolTransitionButton);
  tool_buttons.append(toolHandButton);

  toolArrowButton->click();

  connect(horizontalScrollBar, SIGNAL(valueChanged(int)), this, SLOT(setScroll(int)));
  connect(videoScrollbar, SIGNAL(valueChanged(int)), video_area, SLOT(setScroll(int)));
  connect(audioScrollbar, SIGNAL(valueChanged(int)), audio_area, SLOT(setScroll(int)));
  connect(horizontalScrollBar, SIGNAL(resize_move(double)), this, SLOT(resize_move(double)));

  update_sequence();
}


void Timeline::previous_cut() {
  if (global::sequence->playhead_ > 0) {
    long p_cut = 0;
    for (int i=0;i<global::sequence->clips_.size();i++) {
      ClipPtr c = global::sequence->clips_.at(i);
      if (c != nullptr) {
        if (c->timeline_info.out > p_cut && c->timeline_info.out < global::sequence->playhead_) {
          p_cut = c->timeline_info.out;
        } else if (c->timeline_info.in > p_cut && c->timeline_info.in < global::sequence->playhead_) {
          p_cut = c->timeline_info.in;
        }
      }
    }
    PanelManager::sequenceViewer().seek(p_cut);
  }
}

void Timeline::next_cut()
{
  bool seek_enabled = false;
  long n_cut = LONG_MAX;
  for (int i=0;i<global::sequence->clips_.size();i++) {
    ClipPtr c = global::sequence->clips_.at(i);
    if (c != nullptr) {
      if (c->timeline_info.in < n_cut && c->timeline_info.in > global::sequence->playhead_) {
        n_cut = c->timeline_info.in;
        seek_enabled = true;
      } else if (c->timeline_info.out < n_cut && c->timeline_info.out > global::sequence->playhead_) {
        n_cut = c->timeline_info.out;
        seek_enabled = true;
      }
    }
  }
  if (seek_enabled) {
    PanelManager::sequenceViewer().seek(n_cut);
  }
}

/**
 * @brief Nudge selected clips by a specified amount + direction.
 * @param pos
 */
void Timeline::nudgeClips(const int pos)
{
  // TODO: make this a QUndoCommand
  for (const auto& clp : selectedClips()) {
    if (clp != nullptr) {
      clp->nudge(pos);
    }
  }

  for (auto& sel : global::sequence->selections_) {
    //TODO:check limits
    sel.in += pos;
    sel.out += pos;
  }

  repaint_timeline();
  PanelManager::sequenceViewer().update_viewer();
}

void ripple_clips(ComboAction* ca, SequencePtr s, long point, long length, const QVector<int>& ignore) {
  ca->append(new RippleAction(s, point, length, ignore));
}

void Timeline::toggle_show_all() {
  showing_all = !showing_all;
  if (showing_all) {
    old_zoom = zoom;
    set_zoom_value(double(timeline_area->width() - 200) / double(global::sequence->endFrame()));
  } else {
    set_zoom_value(old_zoom);
  }
}

void Timeline::create_ghosts_from_media(SequencePtr &seq, const long entry_point, QVector<MediaPtr>& media_list)
{
  video_ghosts = false;
  audio_ghosts = false;
  auto entry = entry_point;

  for (auto mda : media_list) {
    if (!mda) {
      qWarning() << "Null Media ptr";
      continue;
    }
    bool can_import = true;
    FootagePtr ftg;
    SequencePtr lcl_seq;
    auto sequence_length = 0L;
    auto default_clip_in = 0L;
    auto default_clip_out = 0L;

    switch (mda->type()) {
      case MediaType::FOOTAGE:
        if (ftg = mda->object<Footage>()) {
          can_import = ftg->ready;
          if (ftg->using_inout) {
            auto source_fr = 30.0;
            if ( (!ftg->video_tracks.empty()) && !qIsNull(ftg->video_tracks.front()->video_frame_rate)) {
              source_fr = ftg->video_tracks.front()->video_frame_rate * ftg->speed;
            }
            default_clip_in = refactor_frame_number(ftg->in, source_fr, seq->frameRate());
            default_clip_out = refactor_frame_number(ftg->out, source_fr, seq->frameRate());
          }
        }
        break;
      case MediaType::SEQUENCE:
        lcl_seq = mda->object<Sequence>();
        sequence_length = lcl_seq->endFrame();
        if (lcl_seq != nullptr) {
          sequence_length = refactor_frame_number(sequence_length, lcl_seq->frameRate(), seq->frameRate());
        }
        can_import = ( (lcl_seq != seq) && (sequence_length != 0) );
        if (lcl_seq->workarea_.using_) {
          default_clip_in = refactor_frame_number(lcl_seq->workarea_.in_, lcl_seq->frameRate(), seq->frameRate());
          default_clip_out = refactor_frame_number(lcl_seq->workarea_.out_, lcl_seq->frameRate(), seq->frameRate());
        }
        break;
      default:
        can_import = false;
        break;
    }//switch

    if (!can_import) {
      continue;
    }

    Ghost g;
    g.clip = -1;
    g.trimming = false;
    g.old_clip_in = g.clip_in = default_clip_in;
    g.media = mda;
    g.in = entry;
    g.transition = nullptr;

    switch (mda->type()) {
      case MediaType::FOOTAGE:
        if (ftg->isImage()) {
          g.out = g.in + STILL_IMAGE_FRAMES; //TODO: shouldn't this be calculated to a duration?
        } else {
          long length = ftg->get_length_in_frames(seq->frameRate());
          g.out = entry + length - default_clip_in;
          if (ftg->using_inout) {
            g.out -= (length - default_clip_out);
          }
        }

        for (int j=0;j<ftg->audio_tracks.size();j++) {
          if (ftg->audio_tracks.at(j)->enabled) {
            g.track = j;
            g.media_stream = ftg->audio_tracks.at(j)->file_index;
            ghosts.append(g);
            audio_ghosts = true;
          }
        }
        for (int j=0;j<ftg->video_tracks.size();j++) {
          if (ftg->video_tracks.at(j)->enabled) {
            g.track = -1-j;
            g.media_stream = ftg->video_tracks.at(j)->file_index;
            ghosts.append(g);
            video_ghosts = true;
          }
        }
        break;
      case MediaType::SEQUENCE:
        g.out = entry + sequence_length - default_clip_in;

        if (lcl_seq->workarea_.using_ && lcl_seq->workarea_.enabled_) {
          g.out -= (sequence_length - default_clip_out);
        }

        g.track = -1;
        ghosts.append(g);
        g.track = 0;
        ghosts.append(g);

        video_ghosts = true;
        audio_ghosts = true;
        break;
      default:
        qWarning() << "Unhandled Media Type" << static_cast<int>(mda->type());
        break;
    }//switch

    entry = g.out;
  } //for

  for (auto& g : ghosts) {
    g.old_in = g.in;
    g.old_out = g.out;
    g.old_track = g.track;
  }
}

void Timeline::add_clips_from_ghosts(ComboAction* ca, SequencePtr s) {
  // add clips
  long earliest_point = LONG_MAX;
  QVector<ClipPtr> added_clips;
  for (int i=0;i<ghosts.size();i++) {
    const Ghost& g = ghosts.at(i);

    earliest_point = qMin(earliest_point, g.in);

    auto clp = std::make_shared<Clip>(s);
    clp->timeline_info.media = g.media;
    clp->timeline_info.media_stream = g.media_stream;
    clp->timeline_info.in = g.in;
    clp->timeline_info.out = g.out;
    clp->timeline_info.clip_in = g.clip_in;
    clp->timeline_info.track_ = g.track;
    if (clp->timeline_info.media->type() == MediaType::FOOTAGE) {
      auto ftg = clp->timeline_info.media->object<Footage>();
      if (ftg->video_tracks.empty()) {
        // audio only (greenish)
        clp->timeline_info.color = AUDIO_ONLY_COLOR;
      } else if (ftg->audio_tracks.empty()) {
        // video only (orangeish)
        clp->timeline_info.color = VIDEO_ONLY_COLOR;
      } else {
        // video and audio (blueish)
        clp->timeline_info.color = AUDIO_VIDEO_COLOR;
      }
      clp->timeline_info.name = ftg->name();
    } else if (clp->timeline_info.media->type() == MediaType::SEQUENCE) {
      // sequence (red?ish?)
      clp->timeline_info.color = SEQUENCE_COLOR;

      SequencePtr media = clp->timeline_info.media->object<Sequence>();
      clp->timeline_info.name = media->name();
    }
    clp->recalculateMaxLength();
    added_clips.append(clp);
  }
  ca->append(new AddClipCommand(s, added_clips));

  // link clips from the same media
  for (int i=0;i<added_clips.size();i++) {
    const ClipPtr& c = added_clips.at(i);
    for (int j=0;j<added_clips.size();j++) {
      const ClipPtr& cc = added_clips.at(j);
      if (c != cc && c->timeline_info.media == cc->timeline_info.media) {
        c->linked.append(j);
      }
    }

    if (c->timeline_info.isVideo()) {
      // add default video effects
      c->effects.append(create_effect(c, get_internal_meta(EFFECT_INTERNAL_TRANSFORM, EFFECT_TYPE_EFFECT)));
    } else {
      // add default audio effects
      c->effects.append(create_effect(c, get_internal_meta(EFFECT_INTERNAL_VOLUME, EFFECT_TYPE_EFFECT)));
      c->effects.append(create_effect(c, get_internal_meta(EFFECT_INTERNAL_PAN, EFFECT_TYPE_EFFECT)));
    }
  }
  if (e_config.enable_seek_to_import) {
    PanelManager::sequenceViewer().seek(earliest_point);
  }
  PanelManager::timeLine().ghosts.clear();
  PanelManager::timeLine().importing = false;
  PanelManager::timeLine().snapped = false;
}

int Timeline::get_track_height_size(bool video) {
  if (video) {
    return video_track_heights.size();
  }
  return audio_track_heights.size();
}

void Timeline::add_transition()
{
  auto ca = new ComboAction();
  bool adding = false;

  for (int i=0;i<global::sequence->clips_.size();i++) {
    ClipPtr c = global::sequence->clips_.at(i);
    if (c != nullptr && c->isSelected(true)) {
      if (c->openingTransition() == nullptr) {
        ca->append(new AddTransitionCommand(c, nullptr, nullptr,
                                            get_internal_meta(TRANSITION_INTERNAL_LINEARFADE, EFFECT_TYPE_TRANSITION),
                                            TA_OPENING_TRANSITION, 30));
        adding = true;
      }
      if (c->closingTransition() == nullptr) {
        ca->append(new AddTransitionCommand(c, nullptr, nullptr,
                                            get_internal_meta(TRANSITION_INTERNAL_LINEARFADE, EFFECT_TYPE_TRANSITION),
                                            TA_OPENING_TRANSITION, 30));
        adding = true;
      }
    }
  }

  if (adding) {
    e_undo_stack.push(ca);
  } else {
    delete ca;
  }

  PanelManager::refreshPanels(true);
}

int Timeline::calculate_track_height(int track, int value) {
  int index = (track < 0) ? qAbs(track + 1) : track;
  QVector<int>& vector = (track < 0) ? video_track_heights : audio_track_heights;
  while (vector.size() < index+1) {
    vector.append(default_track_height); //FIXME: infinite loop experienced here
  }
  if (value > -1) {
    vector[index] = value;
  }
  return vector.at(index);
}

void Timeline::update_sequence() {
  bool null_sequence = (global::sequence == nullptr);

  for (int i=0;i<tool_buttons.count();i++) {
    tool_buttons[i]->setEnabled(!null_sequence);
  }
  snappingButton->setEnabled(!null_sequence);
  zoomInButton->setEnabled(!null_sequence);
  zoomOutButton->setEnabled(!null_sequence);
  recordButton->setEnabled(!null_sequence);
  addButton->setEnabled(!null_sequence);
  headers->setEnabled(!null_sequence);

  QString title = tr("Timeline: ");
  if (null_sequence) {
    setWindowTitle(title + tr("<none>"));
  } else {
    setWindowTitle(title + global::sequence->name());
    PanelManager::refreshPanels(false);
  }
}

int Timeline::get_snap_range() {
  return getFrameFromScreenPoint(zoom, 10);
}

bool Timeline::focused() {
  return (global::sequence != nullptr && (headers->hasFocus() || video_area->hasFocus() || audio_area->hasFocus()));
}

void Timeline::repaint_timeline() {
  if (!block_repaints) {
    bool draw = true;

    if (global::sequence != nullptr
        && !horizontalScrollBar->isSliderDown()
        && !horizontalScrollBar->is_resizing()
        && PanelManager::sequenceViewer().playing
        && !zoom_just_changed) {
      // auto scroll
      if (e_config.autoscroll == AUTOSCROLL_PAGE_SCROLL) {
        const int playhead_x = PanelManager::timeLine().getTimelineScreenPointFromFrame(global::sequence->playhead_);
        if (playhead_x < 0 || playhead_x > (editAreas->width() - videoScrollbar->width())) {
          horizontalScrollBar->setValue(getScreenPointFromFrame(zoom, global::sequence->playhead_));
          draw = false;
        }
      } else if (e_config.autoscroll == AUTOSCROLL_SMOOTH_SCROLL) {
        if (center_scroll_to_playhead(horizontalScrollBar, zoom, global::sequence->playhead_)) {
          draw = false;
        }
      }
    }

    zoom_just_changed = false;

    if (draw) {
      headers->update();
      video_area->update();
      audio_area->update();

      if (global::sequence != nullptr) {
        set_sb_max();

        if (last_frame != global::sequence->playhead_) {
          audio_monitor->update();
          last_frame = global::sequence->playhead_;
        }
      }
    }
  }
}

void Timeline::select_all() {
  if (global::sequence != nullptr) {
    global::sequence->selections_.clear();
    for (int i=0;i<global::sequence->clips_.size();i++) {
      ClipPtr c = global::sequence->clips_.at(i);
      if (c != nullptr) {
        Selection s;
        s.in = c->timeline_info.in;
        s.out = c->timeline_info.out;
        s.track = c->timeline_info.track_;
        global::sequence->selections_.append(s);
      }
    }
    repaint_timeline();
  }
}

void Timeline::scroll_to_frame(long frame) {
  scroll_to_frame_internal(horizontalScrollBar, frame, zoom, timeline_area->width());
}

void Timeline::select_from_playhead() {
  global::sequence->selections_.clear();
  for (int i=0;i<global::sequence->clips_.size();i++) {
    ClipPtr c = global::sequence->clips_.at(i);
    if (c != nullptr
        && c->timeline_info.in <= global::sequence->playhead_
        && c->timeline_info.out > global::sequence->playhead_) {
      Selection s;
      s.in = c->timeline_info.in;
      s.out = c->timeline_info.out;
      s.track = c->timeline_info.track_;
      global::sequence->selections_.append(s);
    }
  }
}

void Timeline::resizeEvent(QResizeEvent* /*event*/) {
  if (global::sequence != nullptr) {
    set_sb_max();
  }
}

void Timeline::delete_in_out(bool ripple) {
  if (global::sequence != nullptr && global::sequence->workarea_.using_) {
    QVector<Selection> areas;
    auto[video_tracks, audio_tracks] = global::sequence->trackLimits();
        for (auto i=video_tracks;i<=audio_tracks;i++) {
      Selection s;
      s.in = global::sequence->workarea_.in_;
      s.out = global::sequence->workarea_.out_;
      s.track = static_cast<int>(i);
      areas.append(s);
    }
    ComboAction* ca = new ComboAction();
    delete_areas_and_relink(ca, areas);
    if (ripple) ripple_clips(ca, global::sequence, global::sequence->workarea_.in_, global::sequence->workarea_.in_ - global::sequence->workarea_.out_);
    ca->append(new SetTimelineInOutCommand(global::sequence, false, 0, 0));
    e_undo_stack.push(ca);
    PanelManager::refreshPanels(true);
  }
}

void Timeline::delete_selection(QVector<Selection>& selections, bool ripple_delete) {
  if (selections.size() > 0) {
    PanelManager::fxControls().clear_effects(true);

    auto ca = new ComboAction();

    delete_areas_and_relink(ca, selections);

    if (ripple_delete) {
      long ripple_point = selections.at(0).in;
      long ripple_length = selections.at(0).out - selections.at(0).in;

      // retrieve ripple_point and ripple_length from current selection
      for (int i=1;i<selections.size();i++) {
        const Selection& s = selections.at(i);
        ripple_point = qMin(ripple_point, s.in);
        ripple_length = qMin(ripple_length, s.out - s.in);
      }

      // it feels a little more intuitive with this here
      ripple_point++;

      bool can_ripple = true;
      for (int i=0;i<global::sequence->clips_.size();i++) {
        ClipPtr c = global::sequence->clips_.at(i);
        if (c != nullptr && c->timeline_info.in < ripple_point && c->timeline_info.out > ripple_point) {
          // conflict detected, but this clip may be getting deleted so let's check
          bool deleted = false;
          for (int j=0;j<selections.size();j++) {
            const Selection& s = selections.at(j);
            if (s.track == c->timeline_info.track_
                && !(c->timeline_info.in < s.in && c->timeline_info.out < s.in)
                && !(c->timeline_info.in > s.out && c->timeline_info.out > s.out)) {
              deleted = true;
              break;
            }
          }
          if (!deleted) {
            for (int j=0;j<global::sequence->clips_.size();j++) {
              ClipPtr cc = global::sequence->clips_.at(j);
              if (cc != nullptr
                  && cc->timeline_info.track_ == c->timeline_info.track_
                  && cc->timeline_info.in > c->timeline_info.out
                  && cc->timeline_info.in < c->timeline_info.out + ripple_length) {
                ripple_length = cc->timeline_info.in - c->timeline_info.out;
              }
            }
          }
        }
      }

      if (can_ripple) {
        ripple_clips(ca, global::sequence, ripple_point, -ripple_length);
        PanelManager::sequenceViewer().seek(ripple_point-1);
      }
    }

    selections.clear();

    e_undo_stack.push(ca);

    PanelManager::refreshPanels(true);
  }
}

void Timeline::set_zoom_value(double v) {
  zoom = v;
  zoom_just_changed = true;
  headers->update_zoom(zoom);

  repaint_timeline();

  // TODO find a way to gradually move towards target_scroll instead of just centering it?
  if (!horizontalScrollBar->is_resizing())
    center_scroll_to_playhead(horizontalScrollBar, zoom, global::sequence->playhead_);
}

void Timeline::set_zoom(bool in) {
  showing_all = false;
  set_zoom_value(zoom * ((in) ? 2 : 0.5));
}

void Timeline::decheck_tool_buttons(QObject* sender) {
  for (int i=0;i<tool_buttons.count();i++) {
    tool_buttons[i]->setChecked(tool_buttons.at(i) == sender);
  }
}

QVector<int> Timeline::get_tracks_of_linked_clips(int i) {
  QVector<int> tracks;
  ClipPtr clip = global::sequence->clips_.at(i);
  for (int j=0;j<clip->linked.size();j++) {
    tracks.append(global::sequence->clips_.at(clip->linked.at(j))->timeline_info.track_);
  }
  return tracks;
}

void Timeline::zoom_in() {
  set_zoom(true);
}

void Timeline::zoom_out() {
  set_zoom(false);
}



void Timeline::snapping_clicked(bool checked) {
  snapping = checked;
}

ClipPtr Timeline::split_clip(ComboAction* ca, int p, long frame) {
  return split_clip(ca, p, frame, frame);
}

ClipPtr Timeline::split_clip(ComboAction* ca, int p, long frame, long post_in)
{
  ClipPtr pre = global::sequence->clips_.at(p); // before the split
  if ( (pre != nullptr) && (pre->timeline_info.in < frame) && (pre->timeline_info.out > frame) ) { // guard against attempts to split at in/out points
    // copy clip but not transitions
    ClipPtr post = pre->copy(global::sequence); // after the split

    long new_clip_length = frame - pre->timeline_info.in;

    post->timeline_info.in = post_in;
    post->timeline_info.clip_in = pre->timeline_info.clip_in + (post->timeline_info.in - pre->timeline_info.in);

    move_clip(ca, pre, pre->timeline_info.in, frame, pre->timeline_info.clip_in, pre->timeline_info.track_);

    if (pre->openingTransition() != nullptr) {
      if (pre->openingTransition()->get_true_length() > new_clip_length) {
        ca->append(new ModifyTransitionCommand(pre, TA_OPENING_TRANSITION, new_clip_length));
      }

      post->sequence->hardDeleteTransition(post, TA_OPENING_TRANSITION);
    }
    if (pre->closingTransition() != nullptr) {
      // move closing transition
      post->closing_transition = pre->closing_transition;
      // remove closing transition from pre-split clip
      ca->append(new SetValCommand<int>(pre->closing_transition, pre->closing_transition, -1));

      // update references
      ca->append(new SetValCommand<ClipWPtr>(post->closingTransition()->secondary_clip,
                                             post->closingTransition()->secondary_clip,
                                             post));
      ca->append(new SetValCommand<ClipPtr>(post->closingTransition()->parent_clip,
                                            post->closingTransition()->parent_clip,
                                            post));
      // ensure transition length is sane
      if (post->closingTransition()->get_length() > post->length()) {
        ca->append(new ModifyTransitionCommand(post,
                                               TA_CLOSING_TRANSITION,
                                               qMin(post->closingTransition()->get_length(), post->length())));
      }
    }
    return post;
  }
  return nullptr;
}

bool Timeline::has_clip_been_split(int c) {
  for (int i=0;i<split_cache.size();i++) {
    if (split_cache.at(i) == c) {
      return true;
    }
  }
  return false;
}

bool Timeline::split_clip_and_relink(ComboAction *ca, int clip, long frame, bool relink) {
  // see if we split this clip before
  if (has_clip_been_split(clip)) {
    return false;
  }

  split_cache.append(clip);

  ClipPtr c = global::sequence->clips_.at(clip);
  if (c != nullptr) {
    QVector<int> pre_clips;
    QVector<ClipPtr> post_clips;

    ClipPtr post = split_clip(ca, clip, frame);

    // if alt is not down, split clips links too
    if (post == nullptr) {
      return false;
    } else {
      post_clips.append(post);
      if (relink) {
        pre_clips.append(clip);

        bool original_clip_is_selected = c->isSelected(true);

        // find linked clips of old clip
        for (int i=0;i<c->linked.size();i++) {
          int l = c->linked.at(i);
          if (!has_clip_been_split(l)) {
            ClipPtr link = global::sequence->clips_.at(l);
            if ((original_clip_is_selected && link->isSelected(true)) || !original_clip_is_selected) {
              split_cache.append(l);
              ClipPtr s = split_clip(ca, l, frame);
              if (s != nullptr) {
                pre_clips.append(l);
                post_clips.append(s);
              }
            }
          }
        }

        relink_clips_using_ids(pre_clips, post_clips);
      }
      ca->append(new AddClipCommand(global::sequence, post_clips));
      return true;
    }
  }
  return false;
}

void Timeline::clean_up_selections(QVector<Selection>& areas) {
  for (int i=0;i<areas.size();i++) {
    Selection& s = areas[i];
    for (int j=0;j<areas.size();j++) {
      if (i != j) {
        Selection& ss = areas[j];
        if (s.track == ss.track) {
          bool remove = false;
          if (s.in < ss.in && s.out > ss.out) {
            // do nothing
          } else if (s.in >= ss.in && s.out <= ss.out) {
            remove = true;
          } else if (s.in <= ss.out && s.out > ss.out) {
            ss.out = s.out;
            remove = true;
          } else if (s.out >= ss.in && s.in < ss.in) {
            ss.in = s.in;
            remove = true;
          }
          if (remove) {
            areas.removeAt(i);
            i--;
            break;
          }
        }
      }
    }
  }
}

bool selection_contains_transition(const Selection& s, ClipPtr c, int type) {
  //FIXME: christ almighty
  if (type == TA_OPENING_TRANSITION) {
    return c->openingTransition() != nullptr
                                     && s.out == c->timeline_info.in + c->openingTransition()->get_true_length()
                                     && ((c->openingTransition()->secondary_clip.expired() && s.in == c->timeline_info.in)
                                         || (!c->openingTransition()->secondary_clip.expired() && s.in == c->timeline_info.in - c->openingTransition()->get_true_length()));
  } else {
    return c->closingTransition() != nullptr
                                     && s.in == c->timeline_info.out - c->closingTransition()->get_true_length()
                                     && ((c->closingTransition()->secondary_clip.expired() && s.out == c->timeline_info.out)
                                         || (!c->closingTransition()->secondary_clip.expired() && s.out == c->timeline_info.out + c->closingTransition()->get_true_length()));
  }
}

void Timeline::delete_areas_and_relink(ComboAction* ca, QVector<Selection>& areas) {
  clean_up_selections(areas);

  QVector<int> pre_clips;
  QVector<ClipPtr> post_clips;

  for (int i=0;i<areas.size();i++) {
    const Selection& s = areas.at(i);
    for (int j=0;j<global::sequence->clips_.size();j++) {
      ClipPtr c = global::sequence->clips_.at(j);
      if (c != nullptr && c->timeline_info.track_ == s.track && !c->undeletable) {
        if (selection_contains_transition(s, c, TA_OPENING_TRANSITION)) {
          // delete opening transition
          ca->append(new DeleteTransitionCommand(c->sequence, c->opening_transition));
        } else if (selection_contains_transition(s, c, TA_CLOSING_TRANSITION)) {
          // delete closing transition
          ca->append(new DeleteTransitionCommand(c->sequence, c->closing_transition));
        } else if (c->timeline_info.in >= s.in && c->timeline_info.out <= s.out) {
          // clips falls entirely within deletion area
          ca->append(new DeleteClipAction(global::sequence, j));
        } else if (c->timeline_info.in < s.in && c->timeline_info.out > s.out) {
          // middle of clip is within deletion area

          // duplicate clip
          ClipPtr post = split_clip(ca, j, s.in, s.out);

          pre_clips.append(j);
          post_clips.append(post);
        } else if (c->timeline_info.in < s.in && c->timeline_info.out > s.in) {
          // only out point is in deletion area
          move_clip(ca, c, c->timeline_info.in, s.in, c->timeline_info.clip_in, c->timeline_info.track_);

          if (c->closingTransition() != nullptr) {
            if (s.in < c->timeline_info.out - c->closingTransition()->get_true_length()) {
              ca->append(new DeleteTransitionCommand(c->sequence, c->closing_transition));
            } else {
              ca->append(new ModifyTransitionCommand(c, TA_CLOSING_TRANSITION, c->closingTransition()->get_true_length() - (c->timeline_info.out - s.in)));
            }
          }
        } else if (c->timeline_info.in < s.out && c->timeline_info.out > s.out) {
          // only in point is in deletion area
          move_clip(ca, c, s.out, c->timeline_info.out, c->timeline_info.clip_in + (s.out - c->timeline_info.in), c->timeline_info.track_);

          if (c->openingTransition() != nullptr) {
            if (s.out > c->timeline_info.in + c->openingTransition()->get_true_length()) {
              ca->append(new DeleteTransitionCommand(c->sequence, c->opening_transition));
            } else {
              ca->append(new ModifyTransitionCommand(c, TA_OPENING_TRANSITION, c->openingTransition()->get_true_length() - (s.out - c->timeline_info.in)));
            }
          }
        }
      }
    }
  }
  relink_clips_using_ids(pre_clips, post_clips);
  ca->append(new AddClipCommand(global::sequence, post_clips));
}

void Timeline::copy(bool del) {
  bool cleared = false;
  bool copied = false;

  long min_in = 0;

  for (int i=0;i<global::sequence->clips_.size();i++) {
    ClipPtr c = global::sequence->clips_.at(i);
    if (c != nullptr) {
      for (int j=0;j<global::sequence->selections_.size();j++) {
        const Selection& s = global::sequence->selections_.at(j);
        if (s.track == c->timeline_info.track_ &&
            !( ( (c->timeline_info.in <= s.in) && (c->timeline_info.out <= s.in) )
               || ( (c->timeline_info.in >= s.out) && (c->timeline_info.out >= s.out) ) )) {
          if (!cleared) {
            clear_clipboard();
            cleared = true;
            e_clipboard_type = CLIPBOARD_TYPE_CLIP;
          }

          std::shared_ptr<Clip> copied_clip(c->copy(nullptr));

          // copy linked IDs (we correct these later in paste())
          copied_clip->linked = c->linked;

          if (copied_clip->timeline_info.in < s.in) {
            copied_clip->timeline_info.clip_in += (s.in - copied_clip->timeline_info.in);
            copied_clip->timeline_info.in = s.in;
          }

          if (copied_clip->timeline_info.out > s.out) {
            copied_clip->timeline_info.out = s.out;
          }

          if (copied) {
            min_in = qMin(min_in, s.in);
          } else {
            min_in = s.in;
            copied = true;
          }

          copied_clip->load_id = i;

          e_clipboard.append(copied_clip);
        }
      }//for
    }
  }//for

  for (int i=0;i<e_clipboard.size();i++) {
    // initialize all timeline_ins to 0 or offsets of
    std::dynamic_pointer_cast<Clip>(e_clipboard.at(i))->timeline_info.in -= min_in;
    std::dynamic_pointer_cast<Clip>(e_clipboard.at(i))->timeline_info.out -= min_in;
  }

  if (del && copied) {
    delete_selection(global::sequence->selections_, false);
  }
}

void Timeline::relink_clips_using_ids(QVector<int>& old_clips, QVector<ClipPtr>& new_clips) {
  // relink pasted clips
  for (int i=0;i<old_clips.size();i++) {
    // these indices should correspond
    ClipPtr oc = global::sequence->clips_.at(old_clips.at(i));
    for (int j=0;j<oc->linked.size();j++) {
      for (int k=0;k<old_clips.size();k++) { // find clip with that ID
        if (oc->linked.at(j) == old_clips.at(k)) {
          new_clips.at(i)->linked.append(k);
        }
      }
    }
  }
}

void Timeline::paste(bool insert) {
  if (e_clipboard.size() > 0) {
    if (e_clipboard_type == CLIPBOARD_TYPE_CLIP) {
      ComboAction* ca = new ComboAction();

      // create copies and delete areas that we'll be pasting to
      QVector<Selection> delete_areas;
      QVector<ClipPtr> pasted_clips;
      int64_t paste_start = LONG_MAX;
      int64_t paste_end = LONG_MIN;
      for (int i=0;i<e_clipboard.size();i++) {
        ClipPtr c = std::dynamic_pointer_cast<Clip>(e_clipboard.at(i));

        // create copy of clip and offset by playhead
        ClipPtr cc = c->copy(global::sequence);

        // convert frame rates
        cc->timeline_info.in = refactor_frame_number(cc->timeline_info.in, c->timeline_info.cached_fr,
                                                     global::sequence->frameRate());
        cc->timeline_info.out = refactor_frame_number(cc->timeline_info.out, c->timeline_info.cached_fr,
                                                      global::sequence->frameRate());
        cc->timeline_info.clip_in = refactor_frame_number(cc->timeline_info.clip_in, c->timeline_info.cached_fr,
                                                          global::sequence->frameRate());

        cc->timeline_info.in += global::sequence->playhead_;
        cc->timeline_info.out += global::sequence->playhead_;
        cc->timeline_info.track_ = c->timeline_info.track_.load();

        paste_start = qMin(paste_start, cc->timeline_info.in.load());
        paste_end = qMax(paste_end, cc->timeline_info.out.load());

        pasted_clips.append(cc);

        if (!insert) {
          Selection s;
          s.in = cc->timeline_info.in;
          s.out = cc->timeline_info.out;
          s.track = c->timeline_info.track_;
          delete_areas.append(s);
        }
      }
      if (insert) {
        split_cache.clear();
        split_all_clips_at_point(ca, global::sequence->playhead_);
        ripple_clips(ca, global::sequence, paste_start, paste_end - paste_start);
      } else {
        delete_areas_and_relink(ca, delete_areas);
      }

      // correct linked clips
      for (int i=0;i<e_clipboard.size();i++) {
        // these indices should correspond
        ClipPtr oc = std::dynamic_pointer_cast<Clip>(e_clipboard.at(i));

        for (int j=0;j<oc->linked.size();j++) {
          for (int k=0;k<e_clipboard.size();k++) { // find clip with that ID
            ClipPtr comp = std::dynamic_pointer_cast<Clip>(e_clipboard.at(k));
            if (comp->load_id == oc->linked.at(j)) {
              pasted_clips.at(i)->linked.append(k);
            }
          }
        }
      }

      ca->append(new AddClipCommand(global::sequence, pasted_clips));

      e_undo_stack.push(ca);

      PanelManager::refreshPanels(true);

      if (e_config.paste_seeks) {
        PanelManager::sequenceViewer().seek(paste_end);
      }
    } else if (e_clipboard_type == CLIPBOARD_TYPE_EFFECT) {
      ComboAction* ca = new ComboAction();
      bool push = false;

      bool replace = false;
      bool skip = false;
      bool ask_conflict = true;

      for (int i=0;i<global::sequence->clips_.size();i++) {
        ClipPtr c = global::sequence->clips_.at(i);
        if (c != nullptr && c->isSelected(true)) {
          for (int j=0;j<e_clipboard.size();j++) {
            EffectPtr e = std::dynamic_pointer_cast<Effect>(e_clipboard.at(j));
            if ((c->timeline_info.isVideo()) == (e->meta->subtype == EFFECT_TYPE_VIDEO)) {
              int found = -1;
              if (ask_conflict) {
                replace = false;
                skip = false;
              }
              for (int k=0;k<c->effects.size();k++) {
                if (c->effects.at(k)->meta == e->meta) {
                  found = k;
                  break;
                }
              }
              if (found >= 0 && ask_conflict) {
                QMessageBox box(this);
                box.setWindowTitle(tr("Effect already exists"));
                box.setText(tr("Clip '%1' already contains a '%2' effect. Would you like to replace it with the pasted one or add it as a separate effect?").arg(c->name(), e->meta->name));
                box.setIcon(QMessageBox::Icon::Question);

                box.addButton(tr("Add"), QMessageBox::YesRole);
                QPushButton* replace_button = box.addButton(tr("Replace"), QMessageBox::NoRole);
                QPushButton* skip_button = box.addButton(tr("Skip"), QMessageBox::RejectRole);

                QCheckBox* future_box = new QCheckBox(tr("Do this for all conflicts found"));
                box.setCheckBox(future_box);

                box.exec();

                if (box.clickedButton() == replace_button) {
                  replace = true;
                } else if (box.clickedButton() == skip_button) {
                  skip = true;
                }
                ask_conflict = !future_box->isChecked();
              }

              if (found >= 0 && skip) {
                // do nothing
              } else if (found >= 0 && replace) {
                EffectDeleteCommand* delcom = new EffectDeleteCommand();
                delcom->clips.append(c);
                delcom->fx.append(found);
                ca->append(delcom);

                ca->append(new AddEffectCommand(c, e->copy(c), nullptr, found));
                push = true;
              } else {
                ca->append(new AddEffectCommand(c, e->copy(c), nullptr));
                push = true;
              }
            }
          }
        }
      }
      if (push) {
        ca->appendPost(new ReloadEffectsCommand());
        e_undo_stack.push(ca);
      } else {
        delete ca;
      }
      PanelManager::refreshPanels(true);
    }
  }
}

void Timeline::ripple_to_in_point(bool in, bool ripple) {
  if ( (global::sequence != nullptr) && !global::sequence->clips_.empty()) {
    // get track count
    int track_min = INT_MAX;
    int track_max = INT_MIN;
    int64_t sequence_end = 0;

    bool playhead_falls_on_in = false;
    bool playhead_falls_on_out = false;
    int64_t next_cut = LONG_MAX;
    int64_t prev_cut = 0;

    // find closest in point to playhead
    for (int i=0;i<global::sequence->clips_.size();i++) {
      ClipPtr c = global::sequence->clips_.at(i);
      if (c != nullptr) {
        track_min = qMin(track_min, c->timeline_info.track_.load());
        track_max = qMax(track_max, c->timeline_info.track_.load());

        sequence_end = qMax(c->timeline_info.out.load(), sequence_end);
        // TODO: check this. Looks repetitive at first glance
        if (c->timeline_info.in == global::sequence->playhead_)
          playhead_falls_on_in = true;
        if (c->timeline_info.out == global::sequence->playhead_)
          playhead_falls_on_out = true;
        if (c->timeline_info.in > global::sequence->playhead_)
          next_cut = qMin(c->timeline_info.in.load(), next_cut);
        if (c->timeline_info.out > global::sequence->playhead_)
          next_cut = qMin(c->timeline_info.out.load(), next_cut);
        if (c->timeline_info.in < global::sequence->playhead_)
          prev_cut = qMax(c->timeline_info.in.load(), prev_cut);
        if (c->timeline_info.out < global::sequence->playhead_)
          prev_cut = qMax(c->timeline_info.out.load(), prev_cut);
      }
    }

    next_cut = qMin(sequence_end, next_cut);

    QVector<Selection> areas;
    ComboAction* ca = new ComboAction();
    bool push_undo = true;
    long seek = global::sequence->playhead_;

    if ((in && (playhead_falls_on_out || (playhead_falls_on_in && global::sequence->playhead_ == 0)))
        || (!in && (playhead_falls_on_in || (playhead_falls_on_out && global::sequence->playhead_ == sequence_end)))) { // one frame mode
      if (ripple) {
        // set up deletion areas based on track count
        long in_point = global::sequence->playhead_;
        if (!in) {
          in_point--;
          seek--;
        }

        if (in_point >= 0) {
          Selection s;
          s.in = in_point;
          s.out = in_point + 1;
          for (int i=track_min;i<=track_max;i++) {
            s.track = i;
            areas.append(s);
          }

          // trim and move clips around the in point
          delete_areas_and_relink(ca, areas);

          if (ripple) ripple_clips(ca, global::sequence, in_point, -1);
        } else {
          push_undo = false;
        }
      } else {
        push_undo = false;
      }
    } else {
      // set up deletion areas based on track count
      Selection s;
      if (in) seek = prev_cut;
      s.in = in ? prev_cut : global::sequence->playhead_;
      s.out = in ? global::sequence->playhead_ : next_cut;

      if (s.in == s.out) {
        push_undo = false;
      } else {
        for (int i=track_min;i<=track_max;i++) {
          s.track = i;
          areas.append(s);
        }

        // trim and move clips around the in point
        delete_areas_and_relink(ca, areas);
        if (ripple) ripple_clips(ca, global::sequence, s.in, s.in - s.out);
      }
    }

    if (push_undo) {
      e_undo_stack.push(ca);

      PanelManager::refreshPanels(true);

      if ( (seek != global::sequence->playhead_) && ripple) {
        PanelManager::sequenceViewer().seek(seek);
      }
    } else {
      delete ca;
    }
  } else {
    PanelManager::sequenceViewer().seek(0);
  }
}

bool Timeline::split_selection(ComboAction* ca) {
  bool split = false;

  // temporary relinking vectors
  QVector<int> pre_splits;
  QVector<ClipPtr> post_splits;
  QVector<ClipPtr> secondary_post_splits;

  // find clips within selection and split
  for (int j=0;j<global::sequence->clips_.size();j++) {
    ClipPtr clip = global::sequence->clips_.at(j);
    if (clip != nullptr) {
      for (int i=0;i<global::sequence->selections_.size();i++) {
        const Selection& s = global::sequence->selections_.at(i);
        if (s.track == clip->timeline_info.track_) {
          if (clip->timeline_info.in < s.in && clip->timeline_info.out > s.out) {
            ClipPtr split_A = clip->copy(global::sequence);
            split_A->timeline_info.clip_in += (s.in - clip->timeline_info.in);
            split_A->timeline_info.in = s.in;
            split_A->timeline_info.out = s.out;
            pre_splits.append(j);
            post_splits.append(split_A);

            ClipPtr split_B = clip->copy(global::sequence);
            split_B->timeline_info.clip_in += (s.out - clip->timeline_info.in);
            split_B->timeline_info.in = s.out;
            secondary_post_splits.append(split_B);

            if (clip->openingTransition() != nullptr) {
              split_B->sequence->hardDeleteTransition(split_B, TA_OPENING_TRANSITION);
              split_A->sequence->hardDeleteTransition(split_A, TA_OPENING_TRANSITION);
            }

            if (clip->closingTransition() != nullptr) {
              ca->append(new DeleteTransitionCommand(clip->sequence, clip->closing_transition));

              split_A->sequence->hardDeleteTransition(split_A, TA_CLOSING_TRANSITION);
            }

            move_clip(ca, clip, clip->timeline_info.in, s.in, clip->timeline_info.clip_in, clip->timeline_info.track_);
            split = true;
          } else {
            ClipPtr post_a = split_clip(ca, j, s.in);
            ClipPtr post_b = split_clip(ca, j, s.out);
            if (post_a != nullptr) {
              pre_splits.append(j);
              post_splits.append(post_a);
              split = true;
            }
            if (post_b != nullptr) {
              if (post_a != nullptr) {
                pre_splits.append(j);
                post_splits.append(post_b);
              } else {
                secondary_post_splits.append(post_b);
              }
              split = true;
            }
          }
        }
      }
    }
  }

  if (split) {
    // relink after splitting
    relink_clips_using_ids(pre_splits, post_splits);
    relink_clips_using_ids(pre_splits, secondary_post_splits);
    ca->append(new AddClipCommand(global::sequence, post_splits));
    ca->append(new AddClipCommand(global::sequence, secondary_post_splits));

    return true;
  }
  return false;
}

bool Timeline::split_all_clips_at_point(ComboAction* ca, long point) {
  bool split = false;
  for (int j=0;j<global::sequence->clips_.size();j++) {
    ClipPtr c = global::sequence->clips_.at(j);
    if (c != nullptr) {
      // always relinks
      if (split_clip_and_relink(ca, j, point, true)) {
        split = true;
      }
    }
  }
  return split;
}

void Timeline::split_at_playhead()
{
  ComboAction* ca = new ComboAction();
  bool split_selected = false;
  split_cache.clear();

  if (global::sequence->selections_.size() > 0) {
    // see if whole clips are selected
    QVector<int> pre_clips;
    QVector<ClipPtr> post_clips;
    for (int j=0;j<global::sequence->clips_.size();j++) {
      ClipPtr clip = global::sequence->clips_.at(j);
      if (clip != nullptr && clip->isSelected(true)) {
        ClipPtr s = split_clip(ca, j, global::sequence->playhead_);
        if (s != nullptr) {
          pre_clips.append(j);
          post_clips.append(s);
          split_selected = true;
        }
      }
    }

    if (split_selected) {
      // relink clips if we split
      relink_clips_using_ids(pre_clips, post_clips);
      ca->append(new AddClipCommand(global::sequence, post_clips));
    } else {
      // split a selection if not
      split_selected = split_selection(ca);
    }
  }

  // if nothing was selected or no selections fell within playhead, simply split at playhead
  if (!split_selected) {
    split_selected = split_all_clips_at_point(ca, global::sequence->playhead_);
  }

  if (split_selected) {
    e_undo_stack.push(ca);
    PanelManager::refreshPanels(true);
  } else {
    delete ca;
  }
}

void Timeline::deselect_area(long in, long out, int track) {
  int len = global::sequence->selections_.size();
  for (int i=0;i<len;i++) {
    Selection& s = global::sequence->selections_[i];
    if (s.track == track) {
      if (s.in >= in && s.out <= out) {
        // whole selection is in deselect area
        global::sequence->selections_.removeAt(i);
        i--;
        len--;
      } else if (s.in < in && s.out > out) {
        // middle of selection is in deselect area
        Selection new_sel;
        new_sel.in = out;
        new_sel.out = s.out;
        new_sel.track = s.track;
        global::sequence->selections_.append(new_sel);

        s.out = in;
      } else if (s.in < in && s.out > in) {
        // only out point is in deselect area
        s.out = in;
      } else if (s.in < out && s.out > out) {
        // only in point is in deselect area
        s.in = out;
      }
    }
  }
}

bool Timeline::snap_to_point(long point, long* l) {
  int limit = get_snap_range();
  if (*l > point-limit-1 && *l < point+limit+1) {
    snap_point = point;
    *l = point;
    snapped = true;
    return true;
  }
  return false;
}

bool Timeline::snap_to_timeline(long* l, bool use_playhead, bool use_markers, bool use_workarea) {
  snapped = false;
  if (snapping) {
    if (use_playhead && !PanelManager::sequenceViewer().playing) {
      // snap to playhead
      if (snap_to_point(global::sequence->playhead_, l)) return true;
    }

    // snap to marker
    if (use_markers) {
      for (int i=0;i<global::sequence->markers_.size();i++) {
        if (snap_to_point(global::sequence->markers_.at(i).frame, l)) return true;
      }
    }

    // snap to in/out
    if (use_workarea && global::sequence->workarea_.using_) {
      if (snap_to_point(global::sequence->workarea_.in_, l)) return true;
      if (snap_to_point(global::sequence->workarea_.out_, l)) return true;
    }

    // snap to clip/transition
    for (int i=0;i<global::sequence->clips_.size();i++) {
      ClipPtr c = global::sequence->clips_.at(i);
      if (c != nullptr) {
        if (snap_to_point(c->timeline_info.in, l)) {
          return true;
        } else if (snap_to_point(c->timeline_info.out, l)) {
          return true;
        } else if (c->openingTransition() != nullptr
                   && snap_to_point(c->timeline_info.in + c->openingTransition()->get_true_length(), l)) {
          return true;
        } else if (c->closingTransition() != nullptr
                   && snap_to_point(c->timeline_info.out - c->closingTransition()->get_true_length(), l)) {
          return true;
        }
      }
    }
  }
  return false;
}

void Timeline::set_marker() {
  bool add_marker = !e_config.set_name_with_marker;
  QString marker_name;

  if (!add_marker) {
    QInputDialog d(this);
    d.setWindowTitle(tr("Set Marker"));
    d.setLabelText(tr("Set marker name:"));
    d.setInputMode(QInputDialog::TextInput);
    add_marker = (d.exec() == QDialog::Accepted);
    marker_name = d.textValue();
  }


  if (add_marker) {
    e_undo_stack.push(new AddMarkerAction(global::sequence, global::sequence->playhead_, marker_name));
  }
}

void Timeline::toggle_links()
{
  auto command = new LinkCommand();
  command->s = global::sequence;
  for (int i=0;i<global::sequence->clips_.size();i++) {
    ClipPtr c = global::sequence->clips_.at(i);
    if (c != nullptr && c->isSelected(true)) {
      if (!command->clips.contains(i)) {
        command->clips.append(i);
      }

      if (c->linked.size() > 0) {
        command->link = false; // prioritize unlinking

        for (int j=0;j<c->linked.size();j++) { // add links to the command
          if (!command->clips.contains(c->linked.at(j))) {
            command->clips.append(c->linked.at(j));
          }
        }
      }
    }
  }
  if (!command->clips.empty()) {
    e_undo_stack.push(command);
    repaint_timeline();
  } else {
    delete command;
  }
}

void Timeline::increase_track_height() {
  for (int i=0;i<video_track_heights.size();i++) {
    video_track_heights[i] += TRACK_HEIGHT_INCREMENT;
  }
  for (int i=0;i<audio_track_heights.size();i++) {
    audio_track_heights[i] += TRACK_HEIGHT_INCREMENT;
  }
  repaint_timeline();
}

void Timeline::decrease_track_height() {
  for (int i=0;i<video_track_heights.size();i++) {
    video_track_heights[i] -= TRACK_HEIGHT_INCREMENT;
    if (video_track_heights[i] < TRACK_MIN_HEIGHT) video_track_heights[i] = TRACK_MIN_HEIGHT;
  }
  for (int i=0;i<audio_track_heights.size();i++) {
    audio_track_heights[i] -= TRACK_HEIGHT_INCREMENT;
    if (audio_track_heights[i] < TRACK_MIN_HEIGHT) audio_track_heights[i] = TRACK_MIN_HEIGHT;
  }
  repaint_timeline();
}

void Timeline::deselect() {
  global::sequence->selections_.clear();
  repaint_timeline();
}

long getFrameFromScreenPoint(double zoom, int x) {
  long f = qCeil(double(x) / zoom);
  if (f < 0) {
    return 0;
  }
  return f;
}

int getScreenPointFromFrame(double zoom, long frame) {
  return qFloor(double(frame)*zoom);
}

long Timeline::getTimelineFrameFromScreenPoint(int x) {
  return getFrameFromScreenPoint(zoom, x + scroll);
}

int Timeline::getTimelineScreenPointFromFrame(long frame) {
  return getScreenPointFromFrame(zoom, frame) - scroll;
}

void Timeline::add_btn_click() {
  QMenu add_menu(this);

  QAction* titleMenuItem = new QAction(&add_menu);
  titleMenuItem->setText(tr("Title..."));
  titleMenuItem->setData(static_cast<int>(AddObjectType::TITLE));
  add_menu.addAction(titleMenuItem);

  QAction* solidMenuItem = new QAction(&add_menu);
  solidMenuItem->setText(tr("Solid Color..."));
  solidMenuItem->setData(static_cast<int>(AddObjectType::SOLID));
  add_menu.addAction(solidMenuItem);

  QAction* barsMenuItem = new QAction(&add_menu);
  barsMenuItem->setText(tr("Bars..."));
  barsMenuItem->setData(static_cast<int>(AddObjectType::BARS));
  add_menu.addAction(barsMenuItem);

  add_menu.addSeparator();

  QAction* toneMenuItem = new QAction(&add_menu);
  toneMenuItem->setText(tr("Tone..."));
  toneMenuItem->setData(static_cast<int>(AddObjectType::TONE));
  add_menu.addAction(toneMenuItem);

  QAction* noiseMenuItem = new QAction(&add_menu);
  noiseMenuItem->setText(tr("Noise..."));
  noiseMenuItem->setData(static_cast<int>(AddObjectType::NOISE));
  add_menu.addAction(noiseMenuItem);

  connect(&add_menu, SIGNAL(triggered(QAction*)), this, SLOT(add_menu_item(QAction*)));

  add_menu.exec(QCursor::pos());
}

void Timeline::add_menu_item(QAction* action) {
  creating = true;
  creating_object = static_cast<AddObjectType>(action->data().toInt());
}

void Timeline::setScroll(int s) {
  scroll = s;
  headers->set_scroll(s);
  repaint_timeline();
}

void Timeline::record_btn_click()
{
  if (project_url.isEmpty()) {
    QMessageBox::critical(this,
                          tr("Unsaved Project"),
                          tr("You must save this project before you can record audio in it."),
                          QMessageBox::Ok);
  } else {
    creating = true;
    creating_object = AddObjectType::AUDIO;
    MainWindow::instance().statusBar()->showMessage(
          tr("Click on the timeline where you want to start recording (drag to limit the recording to a certain timeframe)"),
          RECORD_MSG_TIMEOUT);
  }
}

void Timeline::transition_tool_click() {
  creating = false;

  QMenu transition_menu(this);

  for (int i=0;i<effects.size();i++) {
    const EffectMeta& em = effects.at(i);
    if (em.type == EFFECT_TYPE_TRANSITION && em.subtype == EFFECT_TYPE_VIDEO) {
      QAction* a = transition_menu.addAction(em.name);
      a->setObjectName("v");
      a->setData(reinterpret_cast<quintptr>(&em));
    }
  }

  transition_menu.addSeparator();

  for (int i=0;i<effects.size();i++) {
    const EffectMeta& em = effects.at(i);
    if (em.type == EFFECT_TYPE_TRANSITION && em.subtype == EFFECT_TYPE_AUDIO) {
      QAction* a = transition_menu.addAction(em.name);
      a->setObjectName("a");
      a->setData(reinterpret_cast<quintptr>(&em));
    }
  }

  connect(&transition_menu, SIGNAL(triggered(QAction*)), this, SLOT(transition_menu_select(QAction*)));

  toolTransitionButton->setChecked(false);

  transition_menu.exec(QCursor::pos());
}

void Timeline::transition_menu_select(QAction* a) {
  transition_tool_meta = reinterpret_cast<const EffectMeta*>(a->data().value<quintptr>());

  if (a->objectName() == "v") {
    transition_tool_side = -1;
  } else {
    transition_tool_side = 1;
  }

  decheck_tool_buttons(sender());
  timeline_area->setCursor(Qt::CrossCursor);
  tool = TimelineToolType::TRANSITION;
  toolTransitionButton->setChecked(true);
}

void Timeline::resize_move(double z) {
  set_zoom_value(zoom * z);
}

void Timeline::set_sb_max() {
  headers->set_scrollbar_max(horizontalScrollBar, global::sequence->endFrame(), editAreas->width() - getScreenPointFromFrame(zoom, 200));
}

void Timeline::setup_ui() {
  QWidget* dockWidgetContents = new QWidget();

  QHBoxLayout* horizontalLayout = new QHBoxLayout(dockWidgetContents);
  horizontalLayout->setSpacing(0);
  horizontalLayout->setContentsMargins(0, 0, 0, 0);

  QWidget* tool_buttons_widget = new QWidget(dockWidgetContents);
  tool_buttons_widget->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Minimum);

  QVBoxLayout* tool_buttons_layout = new QVBoxLayout(tool_buttons_widget);
  tool_buttons_layout->setSpacing(4);
  tool_buttons_layout->setContentsMargins(0, 0, 0, 0);

  toolArrowButton = new QPushButton(tool_buttons_widget);
  QIcon arrow_icon;
  arrow_icon.addFile(QStringLiteral(":/icons/arrow.png"), QSize(), QIcon::Normal, QIcon::Off);
  arrow_icon.addFile(QStringLiteral(":/icons/arrow-disabled.png"), QSize(), QIcon::Disabled, QIcon::Off);
  toolArrowButton->setIcon(arrow_icon);
  toolArrowButton->setCheckable(true);
  toolArrowButton->setToolTip(tr("Pointer Tool") + " (V)");
  toolArrowButton->setProperty("tool", static_cast<int>(TimelineToolType::POINTER));
  connect(toolArrowButton, SIGNAL(clicked(bool)), this, SLOT(set_tool()));
  tool_buttons_layout->addWidget(toolArrowButton);

  toolEditButton = new QPushButton(tool_buttons_widget);
  QIcon icon1;
  icon1.addFile(QStringLiteral(":/icons/beam.png"), QSize(), QIcon::Normal, QIcon::Off);
  icon1.addFile(QStringLiteral(":/icons/beam-disabled.png"), QSize(), QIcon::Disabled, QIcon::Off);
  toolEditButton->setIcon(icon1);
  toolEditButton->setCheckable(true);
  toolEditButton->setToolTip(tr("Edit Tool") + " (X)");
  toolEditButton->setProperty("tool", static_cast<int>(TimelineToolType::EDIT));
  connect(toolEditButton, SIGNAL(clicked(bool)), this, SLOT(set_tool()));
  tool_buttons_layout->addWidget(toolEditButton);

  toolRippleButton = new QPushButton(tool_buttons_widget);
  QIcon icon2;
  icon2.addFile(QStringLiteral(":/icons/ripple.png"), QSize(), QIcon::Normal, QIcon::Off);
  icon2.addFile(QStringLiteral(":/icons/ripple-disabled.png"), QSize(), QIcon::Disabled, QIcon::Off);
  toolRippleButton->setIcon(icon2);
  toolRippleButton->setCheckable(true);
  toolRippleButton->setToolTip(tr("Ripple Tool") + " (B)");
  toolRippleButton->setProperty("tool", static_cast<int>(TimelineToolType::RIPPLE));
  connect(toolRippleButton, SIGNAL(clicked(bool)), this, SLOT(set_tool()));
  tool_buttons_layout->addWidget(toolRippleButton);

  toolRazorButton = new QPushButton(tool_buttons_widget);
  QIcon icon4;
  icon4.addFile(QStringLiteral(":/icons/razor.png"), QSize(), QIcon::Normal, QIcon::Off);
  icon4.addFile(QStringLiteral(":/icons/razor-disabled.png"), QSize(), QIcon::Disabled, QIcon::Off);
  toolRazorButton->setIcon(icon4);
  toolRazorButton->setCheckable(true);
  toolRazorButton->setToolTip(tr("Razor Tool") + " (C)");
  toolRazorButton->setProperty("tool", static_cast<int>(TimelineToolType::RAZOR));
  connect(toolRazorButton, SIGNAL(clicked(bool)), this, SLOT(set_tool()));
  tool_buttons_layout->addWidget(toolRazorButton);

  toolSlipButton = new QPushButton(tool_buttons_widget);
  QIcon icon5;
  icon5.addFile(QStringLiteral(":/icons/slip.png"), QSize(), QIcon::Normal, QIcon::On);
  icon5.addFile(QStringLiteral(":/icons/slip-disabled.png"), QSize(), QIcon::Disabled, QIcon::On);
  toolSlipButton->setIcon(icon5);
  toolSlipButton->setCheckable(true);
  toolSlipButton->setToolTip(tr("Slip Tool") + " (Y)");
  toolSlipButton->setProperty("tool", static_cast<int>(TimelineToolType::SLIP));
  connect(toolSlipButton, SIGNAL(clicked(bool)), this, SLOT(set_tool()));
  tool_buttons_layout->addWidget(toolSlipButton);

  toolSlideButton = new QPushButton(tool_buttons_widget);
  QIcon icon6;
  icon6.addFile(QStringLiteral(":/icons/slide.png"), QSize(), QIcon::Normal, QIcon::On);
  icon6.addFile(QStringLiteral(":/icons/slide-disabled.png"), QSize(), QIcon::Disabled, QIcon::On);
  toolSlideButton->setIcon(icon6);
  toolSlideButton->setCheckable(true);
  toolSlideButton->setToolTip(tr("Slide Tool") + " (U)");
  toolSlideButton->setProperty("tool", static_cast<int>(TimelineToolType::SLIDE));
  connect(toolSlideButton, SIGNAL(clicked(bool)), this, SLOT(set_tool()));
  tool_buttons_layout->addWidget(toolSlideButton);

  toolHandButton = new QPushButton(tool_buttons_widget);
  QIcon icon7;
  icon7.addFile(QStringLiteral(":/icons/hand.png"), QSize(), QIcon::Normal, QIcon::On);
  icon7.addFile(QStringLiteral(":/icons/hand-disabled.png"), QSize(), QIcon::Disabled, QIcon::On);
  toolHandButton->setIcon(icon7);
  toolHandButton->setCheckable(true);
  toolHandButton->setToolTip(tr("Hand Tool") + " (H)");
  toolHandButton->setProperty("tool", static_cast<int>(TimelineToolType::HAND));
  connect(toolHandButton, SIGNAL(clicked(bool)), this, SLOT(set_tool()));
  tool_buttons_layout->addWidget(toolHandButton);

  toolTransitionButton = new QPushButton(tool_buttons_widget);
  QIcon icon8;
  icon8.addFile(QStringLiteral(":/icons/transition-tool.png"), QSize(), QIcon::Normal, QIcon::On);
  icon8.addFile(QStringLiteral(":/icons/transition-tool-disabled.png"), QSize(), QIcon::Disabled, QIcon::On);
  toolTransitionButton->setIcon(icon8);
  toolTransitionButton->setCheckable(true);
  toolTransitionButton->setToolTip(tr("Transition Tool") + " (T)");
  connect(toolTransitionButton, SIGNAL(clicked(bool)), this, SLOT(transition_tool_click()));
  tool_buttons_layout->addWidget(toolTransitionButton);

  snappingButton = new QPushButton(tool_buttons_widget);
  QIcon icon9;
  icon9.addFile(QStringLiteral(":/icons/magnet.png"), QSize(), QIcon::Normal, QIcon::On);
  icon9.addFile(QStringLiteral(":/icons/magnet-disabled.png"), QSize(), QIcon::Disabled, QIcon::On);
  snappingButton->setIcon(icon9);
  snappingButton->setCheckable(true);
  snappingButton->setChecked(true);
  snappingButton->setToolTip(tr("Snapping") + " (S)");
  connect(snappingButton, SIGNAL(toggled(bool)), this, SLOT(snapping_clicked(bool)));
  tool_buttons_layout->addWidget(snappingButton);

  zoomInButton = new QPushButton(tool_buttons_widget);
  QIcon icon10;
  icon10.addFile(QStringLiteral(":/icons/zoomin.png"), QSize(), QIcon::Normal, QIcon::On);
  icon10.addFile(QStringLiteral(":/icons/zoomin-disabled.png"), QSize(), QIcon::Disabled, QIcon::On);
  zoomInButton->setIcon(icon10);
  zoomInButton->setToolTip(tr("Zoom In") + " (=)");
  connect(zoomInButton, SIGNAL(clicked(bool)), this, SLOT(zoom_in()));
  tool_buttons_layout->addWidget(zoomInButton);

  zoomOutButton = new QPushButton(tool_buttons_widget);
  QIcon icon11;
  icon11.addFile(QStringLiteral(":/icons/zoomout.png"), QSize(), QIcon::Normal, QIcon::On);
  icon11.addFile(QStringLiteral(":/icons/zoomout-disabled.png"), QSize(), QIcon::Disabled, QIcon::On);
  zoomOutButton->setIcon(icon11);
  zoomOutButton->setToolTip(tr("Zoom Out") + " (-)");
  connect(zoomOutButton, SIGNAL(clicked(bool)), this, SLOT(zoom_out()));
  tool_buttons_layout->addWidget(zoomOutButton);

  recordButton = new QPushButton(tool_buttons_widget);
  QIcon icon12;
  icon12.addFile(QStringLiteral(":/icons/record.png"), QSize(), QIcon::Normal, QIcon::On);
  icon12.addFile(QStringLiteral(":/icons/record-disabled.png"), QSize(), QIcon::Disabled, QIcon::On);
  recordButton->setIcon(icon12);
  recordButton->setToolTip(tr("Record audio"));
  connect(recordButton, SIGNAL(clicked(bool)), this, SLOT(record_btn_click()));

  tool_buttons_layout->addWidget(recordButton);

  addButton = new QPushButton(tool_buttons_widget);
  QIcon icon13;
  icon13.addFile(QStringLiteral(":/icons/add-button.png"), QSize(), QIcon::Normal, QIcon::On);
  icon13.addFile(QStringLiteral(":/icons/add-button-disabled.png"), QSize(), QIcon::Disabled, QIcon::On);
  addButton->setIcon(icon13);
  addButton->setToolTip(tr("Add title, solid, bars, etc."));
  connect(addButton, SIGNAL(clicked()), this, SLOT(add_btn_click()));
  tool_buttons_layout->addWidget(addButton);

  tool_buttons_layout->addStretch();

  horizontalLayout->addWidget(tool_buttons_widget);

  timeline_area = new QWidget(dockWidgetContents);
  QSizePolicy sizePolicy2(QSizePolicy::Minimum, QSizePolicy::Minimum);
  sizePolicy2.setHorizontalStretch(1);
  sizePolicy2.setVerticalStretch(0);
  sizePolicy2.setHeightForWidth(timeline_area->sizePolicy().hasHeightForWidth());
  timeline_area->setSizePolicy(sizePolicy2);
  QVBoxLayout* timeline_area_layout = new QVBoxLayout(timeline_area);
  timeline_area_layout->setSpacing(0);
  timeline_area_layout->setContentsMargins(0, 0, 0, 0);
  headers = new TimelineHeader(timeline_area);

  timeline_area_layout->addWidget(headers);

  editAreas = new QWidget(timeline_area);
  QHBoxLayout* editAreaLayout = new QHBoxLayout(editAreas);
  editAreaLayout->setSpacing(0);
  editAreaLayout->setContentsMargins(0, 0, 0, 0);
  QSplitter* splitter = new QSplitter(editAreas);
  splitter->setOrientation(Qt::Vertical);
  QWidget* videoContainer = new QWidget(splitter);
  QHBoxLayout* videoContainerLayout = new QHBoxLayout(videoContainer);
  videoContainerLayout->setSpacing(0);
  videoContainerLayout->setContentsMargins(0, 0, 0, 0);
  video_area = new TimelineWidget(videoContainer);
  video_area->setFocusPolicy(Qt::ClickFocus);

  videoContainerLayout->addWidget(video_area);

  videoScrollbar = new QScrollBar(videoContainer);
  videoScrollbar->setMaximum(0);
  videoScrollbar->setSingleStep(20);
  videoScrollbar->setPageStep(1826);
  videoScrollbar->setOrientation(Qt::Vertical);

  videoContainerLayout->addWidget(videoScrollbar);

  splitter->addWidget(videoContainer);

  QWidget* audioContainer = new QWidget(splitter);
  QHBoxLayout* audioContainerLayout = new QHBoxLayout(audioContainer);
  audioContainerLayout->setSpacing(0);
  audioContainerLayout->setContentsMargins(0, 0, 0, 0);
  audio_area = new TimelineWidget(audioContainer);
  audio_area->setFocusPolicy(Qt::ClickFocus);

  audioContainerLayout->addWidget(audio_area);

  audioScrollbar = new QScrollBar(audioContainer);
  audioScrollbar->setMaximum(0);
  audioScrollbar->setOrientation(Qt::Vertical);

  audioContainerLayout->addWidget(audioScrollbar);

  splitter->addWidget(audioContainer);

  editAreaLayout->addWidget(splitter);

  timeline_area_layout->addWidget(editAreas);

  horizontalScrollBar = new ResizableScrollBar(timeline_area);
  horizontalScrollBar->setMaximum(0);
  horizontalScrollBar->setSingleStep(20);
  horizontalScrollBar->setPageStep(1826);
  horizontalScrollBar->setOrientation(Qt::Horizontal);

  timeline_area_layout->addWidget(horizontalScrollBar);

  horizontalLayout->addWidget(timeline_area);

  audio_monitor = new AudioMonitor(dockWidgetContents);
  audio_monitor->setMinimumSize(QSize(50, 0));

  horizontalLayout->addWidget(audio_monitor);

  setWidget(dockWidgetContents);
}


std::vector<ClipPtr> Timeline::selectedClips()
{
  std::vector<ClipPtr> clips;
  auto seqClips = global::sequence->clips_;
  for (const auto& clp : seqClips) {
    if (clp->isSelected(true)) {
      clips.emplace_back(clp);
    }
  }
  return clips;
}


void move_clip(ComboAction* ca, ClipPtr c, long iin, long iout, long iclip_in, int itrack, bool verify_transitions, bool relative) {
  ca->append(new MoveClipAction(c, iin, iout, iclip_in, itrack, relative));

  if (verify_transitions) {
    if (c->openingTransition() != nullptr && !c->openingTransition()->secondary_clip.expired()
        && c->openingTransition()->secondary_clip.lock()->timeline_info.out != iin) {
      // separate transition
      //            ca->append(new SetPointer(reinterpret_cast<void**>(&c->openingTransition()->secondary_clip), nullptr)); //FIXME: casting
      ca->append(new AddTransitionCommand(c->openingTransition()->secondary_clip.lock(), nullptr,
                                          c->openingTransition(), nullptr, TA_CLOSING_TRANSITION, 0));
    }

    if (c->closingTransition() != nullptr && !c->closingTransition()->secondary_clip.expired()
        && c->closingTransition()->parent_clip->timeline_info.in != iout) {
      // separate transition
      //            ca->append(new SetPointer(reinterpret_cast<void**>(&c->closingTransition()->secondary_clip), nullptr)); //FIXME: casting
      ca->append(new AddTransitionCommand(c, nullptr, c->closingTransition(), nullptr, TA_CLOSING_TRANSITION, 0));
    }
  }
}

void Timeline::set_tool() {
  QPushButton* button = static_cast<QPushButton*>(sender());
  decheck_tool_buttons(button);
  tool = static_cast<TimelineToolType>(button->property("tool").toInt());
  creating = false;
  switch (tool) {
    case TimelineToolType::EDIT:
    case TimelineToolType::RAZOR:
      timeline_area->setCursor(Qt::IBeamCursor);
      break;
    case TimelineToolType::HAND:
      timeline_area->setCursor(Qt::OpenHandCursor);
      break;
    default:
      timeline_area->setCursor(Qt::ArrowCursor);
  }
}
