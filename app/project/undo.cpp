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
#include "undo.h"

#include <QTreeWidgetItem>
#include <QMessageBox>
#include <QCheckBox>
#include <utility>

#include "project/clip.h"
#include "project/sequence.h"
#include "panels/panelmanager.h"
#include "playback/playback.h"
#include "ui/sourcetable.h"
#include "project/effect.h"
#include "project/transition.h"
#include "project/footage.h"
#include "ui/labelslider.h"
#include "ui/viewerwidget.h"
#include "project/marker.h"
#include "ui/mainwindow.h"
#include "io/clipboard.h"
#include "project/media.h"
#include "debug.h"

QUndoStack e_undo_stack;

using panels::PanelManager;

//FIXME: far too much logic held in these actions

ComboAction::~ComboAction()
{
  for (auto cmd : commands){
    delete cmd;
  }
}

void ComboAction::undo()
{
  for (int i=commands.size()-1;i>=0;i--) {
    commands.at(i)->undo();
  }

  for (const auto& cmd : post_commands) {
    if (cmd != nullptr) {
      cmd->undo();
    }
  }
}

void ComboAction::redo()
{
  for (const auto& cmd : commands) {
    if (cmd != nullptr) {
      cmd->redo();
    }
  }

  for (const auto& cmd : post_commands) {
    if (cmd != nullptr) {
      cmd->redo();
    }
  }
}

void ComboAction::append(QUndoCommand* u) {
  commands.append(u);
}

void ComboAction::appendPost(QUndoCommand* u) {
  post_commands.append(u);
}

int ComboAction::size() const
{
  return commands.size();
}

MoveClipAction::MoveClipAction(const ClipPtr& c, const long iin, const long iout, const long iclip_in, const int itrack, const bool irelative)
  : clip(c),
    old_in(c->timeline_info.in),
    old_out(c->timeline_info.out),
    old_clip_in(c->timeline_info.clip_in),
    old_track(c->timeline_info.track_),
    new_in(iin),
    new_out(iout),
    new_clip_in(iclip_in),
    new_track(itrack),
    relative(irelative),
    old_project_changed(MainWindow::instance().isWindowModified())
{

}

void MoveClipAction::undo() {
  if (relative) {
    clip->timeline_info.in -= new_in;
    clip->timeline_info.out -= new_out;
    clip->timeline_info.clip_in -= new_clip_in;
    clip->timeline_info.track_ -= new_track;
  } else {
    clip->timeline_info.in = old_in;
    clip->timeline_info.out = old_out;
    clip->timeline_info.clip_in = old_clip_in;
    clip->timeline_info.track_  = old_track ;
  }

  MainWindow::instance().setWindowModified(old_project_changed);
}

void MoveClipAction::redo() {
  if (relative) {
    clip->timeline_info.in += new_in;
    clip->timeline_info.out += new_out;
    clip->timeline_info.clip_in += new_clip_in;
    clip->timeline_info.track_ += new_track;
  } else {
    clip->timeline_info.in = new_in;
    clip->timeline_info.out = new_out;
    clip->timeline_info.clip_in = new_clip_in;
    clip->timeline_info.track_ = new_track;
  }

  MainWindow::instance().setWindowModified(true);
}


DeleteClipAction::DeleteClipAction(ClipPtr del_clip) : clip_(std::move(del_clip))
{

}

void DeleteClipAction::undo()
{
  if ( (clip_ != nullptr) && (clip_->sequence != nullptr) ) {
    clip_->sequence->clips_.append(clip_);
  } else {
    qCritical() << "Null instance(s)";
  }

  // TODO: restore links to this clip

  MainWindow::instance().setWindowModified(old_project_changed);
}

void DeleteClipAction::redo()
{
  if ( (clip_ != nullptr) && (clip_->sequence != nullptr) ) {
    clip_->sequence->deleteClip(clip_->id());
  } else {
    qCritical() << "Null instance(s)";
  }

  // TODO: delete links to this clip

  MainWindow::instance().setWindowModified(true);
}

ChangeSequenceAction::ChangeSequenceAction(SequencePtr  s) :
  new_sequence(std::move(s))
{}

void ChangeSequenceAction::undo() {
  set_sequence(old_sequence);
}

void ChangeSequenceAction::redo() {
  old_sequence = global::sequence;
  set_sequence(new_sequence);
}

SetTimelineInOutCommand::SetTimelineInOutCommand(SequencePtr  s, const bool enabled, const long in, const long out) :
  seq(std::move(s)),
  new_enabled(enabled),
  new_in(in),
  new_out(out),
  old_project_changed(MainWindow::instance().isWindowModified())
{}

void SetTimelineInOutCommand::undo() {
  seq->workarea_.using_ = old_enabled;
  seq->workarea_.enabled_ = old_workarea_enabled;
  seq->workarea_.in_ = old_in;
  seq->workarea_.out_ = old_out;

  // footage viewer functions
  if (seq->wrapper_sequence_) {
    FootagePtr  m = seq->clips_.at(0)->timeline_info.media->object<Footage>();
    m->using_inout = old_enabled;
    m->in = old_in;
    m->out = old_out;
  }

  MainWindow::instance().setWindowModified(old_project_changed);
}

void SetTimelineInOutCommand::redo() {
  old_enabled = seq->workarea_.using_;
  old_workarea_enabled = seq->workarea_.enabled_;
  old_in = seq->workarea_.in_;
  old_out = seq->workarea_.out_;

  if (!seq->workarea_.using_) seq->workarea_.enabled_ = true;
  seq->workarea_.using_ = new_enabled;
  seq->workarea_.in_ = new_in;
  seq->workarea_.out_ = new_out;

  // footage viewer functions
  if (seq->wrapper_sequence_) {
    FootagePtr  m = seq->clips_.at(0)->timeline_info.media->object<Footage>();
    m->using_inout = new_enabled;
    m->in = new_in;
    m->out = new_out;
  }

  MainWindow::instance().setWindowModified(true);
}

AddEffectCommand::AddEffectCommand(ClipPtr   c, EffectPtr e, const EffectMeta& m, const int insert_pos) :
  clip(std::move(c)),
  meta(m),
  ref(std::move(e)),
  pos(insert_pos),
  done(false),
  old_project_changed(MainWindow::instance().isWindowModified())
{}


AddEffectCommand::AddEffectCommand(ClipPtr c, EffectPtr e, const int insert_pos)
  : clip(std::move(c)),
    ref(std::move(e)),
    pos(insert_pos)
{

}

AddEffectCommand::~AddEffectCommand()
{
}

void AddEffectCommand::undo() {
  clip->effects.last()->close();
  if (pos < 0) {
    clip->effects.removeLast();
  } else {
    clip->effects.removeAt(pos);
  }
  done = false;
  MainWindow::instance().setWindowModified(old_project_changed);
}

void AddEffectCommand::redo() {
  if (ref == nullptr) {
    ref = create_effect(clip, meta);
  }
  if (pos < 0) {
    clip->effects.append(ref);
  } else {
    clip->effects.insert(pos, ref);
  }
  done = true;
  MainWindow::instance().setWindowModified(true);
}


AddTransitionCommand::AddTransitionCommand(ClipPtr parent,
                                           ClipPtr secondary,
                                           const EffectMeta& meta,
                                           const ClipTransitionType type,
                                           const int length)
  : parent_(std::move(parent)),
    secondary_(std::move(secondary)),
    meta_(meta),
    type_(type),
    length_(length),
    old_project_changed(MainWindow::instance().isWindowModified())
{

}

void AddTransitionCommand::undo()
{
  if (parent_ == nullptr) {
    return;
  }
  parent_->deleteTransition(type_);
  MainWindow::instance().setWindowModified(old_project_changed);
}

void AddTransitionCommand::redo()
{
  parent_->setTransition(meta_, type_, length_);
  MainWindow::instance().setWindowModified(true);
}

ModifyTransitionCommand::ModifyTransitionCommand(ClipPtr c, const int itype, const long ilength) :
  clip(std::move(c)),
  type(itype),
  new_length(ilength),
  old_project_changed(MainWindow::instance().isWindowModified())
{}

void ModifyTransitionCommand::undo()
{
  TransitionPtr t = (type == TA_OPENING_TRANSITION) ? clip->openingTransition() : clip->closingTransition();
  if (t != nullptr) {
    t->set_length(old_length);
    MainWindow::instance().setWindowModified(old_project_changed);
  } else {
    qWarning() << "Transition command has null ptr";
  }
}

void ModifyTransitionCommand::redo()
{
  TransitionPtr t = (type == TA_OPENING_TRANSITION) ? clip->openingTransition() : clip->closingTransition();
  if (t != nullptr) {
    old_length = t->get_true_length();
    t->set_length(new_length);
    MainWindow::instance().setWindowModified(true);
  } else {
    qWarning() << "Transition command has null ptr";
  }
}

DeleteTransitionCommand::DeleteTransitionCommand(ClipPtr clp, const ClipTransitionType type)
  : clip_(std::move(clp)),
    type_(type),
    old_project_changed(MainWindow::instance().isWindowModified())
{
  if (clip_ != nullptr) {
    // Hold values required to re-create transition(s) for the clip
    switch (type_) {
    case ClipTransitionType::OPENING:
      populateOpening();
      break;
    case ClipTransitionType::CLOSING:
      populateClosing();
      break;
    case ClipTransitionType::BOTH:
      populateClosing();
      populateOpening();
      break;
    }
  }
}

void DeleteTransitionCommand::undo()
{
  if (clip_ != nullptr) {
    switch (type_) {
    case ClipTransitionType::OPENING:
      clip_->setTransition(meta_.opening_, type_, lengths_.opening_);
      break;
    case ClipTransitionType::CLOSING:
      clip_->setTransition(meta_.closing_, type_, lengths_.closing_);
      break;
    case ClipTransitionType::BOTH:
      clip_->setTransition(meta_.opening_, ClipTransitionType::OPENING, lengths_.opening_);
      clip_->setTransition(meta_.closing_, ClipTransitionType::CLOSING, lengths_.closing_);
      break;
    }
  }
  MainWindow::instance().setWindowModified(old_project_changed);
}

void DeleteTransitionCommand::redo()
{
  if (clip_ != nullptr) {
    clip_->deleteTransition(type_);
  }
  MainWindow::instance().setWindowModified(true);
}



void DeleteTransitionCommand::populateOpening()
{
  if (auto trans = clip_->getTransition(ClipTransitionType::OPENING)) {
    meta_.opening_ = trans->meta;
    lengths_.opening_ = trans->get_length();
    secondary_.opening_ = trans->secondary_clip;
  }
}

void DeleteTransitionCommand::populateClosing()
{
  if (auto trans = clip_->getTransition(ClipTransitionType::CLOSING)) {
    meta_.closing_ = trans->meta;
    lengths_.closing_ = trans->get_length();
    secondary_.closing_ = trans->secondary_clip;
  }
}

NewSequenceCommand::NewSequenceCommand(MediaPtr s, MediaPtr iparent, const bool modified) :
  seq_(std::move(s)),
  parent_(std::move(iparent)),
  done_(false),
  old_project_changed_(modified)
{
  if (parent_ == nullptr) {
    parent_ = Project::model().root();
  }
}

NewSequenceCommand::~NewSequenceCommand()
= default;

void NewSequenceCommand::undo()
{
  Project::model().removeChild(parent_, seq_);

  done_ = false;
  MainWindow::instance().setWindowModified(old_project_changed_);
}

void NewSequenceCommand::redo()
{
  Project::model().appendChild(parent_, seq_);

  done_ = true;
  MainWindow::instance().setWindowModified(true);
}

AddMediaCommand::AddMediaCommand(MediaPtr iitem, MediaPtr iparent) :
  item(std::move(iitem)),
  parent(std::move(iparent)),
  done(false),
  old_project_changed(MainWindow::instance().isWindowModified())
{}

void AddMediaCommand::undo() {
  Project::model().removeChild(parent, item);
  done = false;
  MainWindow::instance().setWindowModified(old_project_changed);
}

void AddMediaCommand::redo() {
  Project::model().appendChild(parent, item);

  done = true;
  MainWindow::instance().setWindowModified(true);
}

DeleteMediaCommand::DeleteMediaCommand(const MediaPtr& i) :
  item(i),
  parent(i->parentItem()),
  old_project_changed(MainWindow::instance().isWindowModified())
{}


void DeleteMediaCommand::undo() {
  Project::model().appendChild(parent, item);

  MainWindow::instance().setWindowModified(old_project_changed);
  done = false;
}

void DeleteMediaCommand::redo() {
  Project::model().removeChild(parent, item);

  MainWindow::instance().setWindowModified(true);
  done = true;
}

AddClipsCommand::AddClipsCommand(SequencePtr seq, const QVector<ClipPtr>& clips)
  : sequence_(std::move(seq)),
    clips_(clips),
    old_project_changed_(MainWindow::instance().isWindowModified())
{

}

void AddClipsCommand::undo()
{
  Q_ASSERT(sequence_ != nullptr);

  for (const auto& undo_clip : clips_) {
    if (undo_clip == nullptr) {
      qWarning() << "Clip instance is null";
      continue;
    }
    auto itor = std::find_if(sequence_->clips_.begin(), sequence_->clips_.end(), [&undo_clip] (const ClipPtr& clp) {
      return (clp != nullptr) && (clp->id() == undo_clip->id());
    });
    sequence_->clips_.erase(itor);
  }
}

void AddClipsCommand::redo()
{
  Q_ASSERT(sequence_ != nullptr);
  sequence_->clips_ = sequence_->clips_ + clips_;
}

LinkCommand::LinkCommand()
  : link(true),
    old_project_changed(MainWindow::instance().isWindowModified())
{

}

void LinkCommand::undo() {
  for (int i=0;i<clips.size();i++) {
    ClipPtr   c = s->clips_.at(clips.at(i));
    if (link) {
      c->clearLinks();
    } else {
      c->setLinkedClips(old_links.at(i));
    }
  }
  MainWindow::instance().setWindowModified(old_project_changed);
}

void LinkCommand::redo() {
  old_links.clear();
  for (int i=0;i<clips.size();i++) {
    ClipPtr c = s->clips_.at(clips.at(i));
    if (link) {
      for (int j=0;j<clips.size();j++) {
        if (i != j) {
          //          c->linked.append(clips.at(j)); //FIXME:
        }
      }
    } else {
      //      old_links.append(c->linked); //FIXME:
      c->clearLinks();
    }
  }
  MainWindow::instance().setWindowModified(true);
}

CheckboxCommand::CheckboxCommand(QCheckBox* b)
  : box(b),
    checked(box->isChecked()),
    done(true),
    old_project_changed(MainWindow::instance().isWindowModified())
{

}


void CheckboxCommand::undo() {
  box->setChecked(!checked);
  done = false;
  MainWindow::instance().setWindowModified(old_project_changed);
}

void CheckboxCommand::redo() {
  if (!done) {
    box->setChecked(checked);
  }
  MainWindow::instance().setWindowModified(true);
}

ReplaceMediaCommand::ReplaceMediaCommand(MediaPtr i, QString s) :
  item(std::move(i)),
  new_filename(std::move(s)),
  old_project_changed(MainWindow::instance().isWindowModified())
{
  old_filename = item->object<Footage>()->url;
}


//FIXME: on replace video and audio stream info are not retrieved and not saved to xml
void ReplaceMediaCommand::replace(QString& filename)
{
  // close any clips currently using this media
  QVector<MediaPtr> all_sequences = PanelManager::projectViewer().list_all_project_sequences();
  for (const auto & all_sequence : all_sequences) {
    SequencePtr s = all_sequence->object<Sequence>();
    for (const auto& c : s->clips_) {
      if (c != nullptr && c->timeline_info.media == item && c->is_open) {
        c->close(true);
        c->replaced = true;
      }
    }
  }

  // replace media
  QStringList files;
  files.append(filename);
  item->object<Footage>()->ready_lock.lock();
  PanelManager::projectViewer().process_file_list(files, false, item, nullptr);
}

void ReplaceMediaCommand::undo() {
  replace(old_filename);

  MainWindow::instance().setWindowModified(old_project_changed);
}

void ReplaceMediaCommand::redo() {
  replace(new_filename);

  MainWindow::instance().setWindowModified(true);
}

ReplaceClipMediaCommand::ReplaceClipMediaCommand(MediaPtr a, MediaPtr b, const bool e) :
  old_media(std::move(a)),
  new_media(std::move(b)),
  preserve_clip_ins(e),
  old_project_changed(MainWindow::instance().isWindowModified())
{}

void ReplaceClipMediaCommand::replace(bool undo) {
  if (!undo) {
    old_clip_ins.clear();
  }

  for (int i=0;i<clips.size();i++) {
    ClipPtr c = clips.at(i);
    c->close(true);

    if (undo) {
      if (!preserve_clip_ins) {
        c->timeline_info.clip_in = old_clip_ins.at(i);
      }

      c->timeline_info.media = old_media;
    } else {
      if (!preserve_clip_ins) {
        old_clip_ins.append(c->timeline_info.clip_in);
        c->timeline_info.clip_in = 0;
      }

      c->timeline_info.media = new_media;
    }

    c->replaced = true;
    c->refresh();
  }
}

void ReplaceClipMediaCommand::undo() {
  replace(true);

  MainWindow::instance().setWindowModified(old_project_changed);
}

void ReplaceClipMediaCommand::redo() {
  replace(false);

  PanelManager::refreshPanels(true);
  MainWindow::instance().setWindowModified(true);
}

EffectDeleteCommand::EffectDeleteCommand()
  : done(false),
    old_project_changed(MainWindow::instance().isWindowModified())
{

}


void EffectDeleteCommand::undo()
{
  for (int i=0;i<clips.size();i++) {
    ClipPtr   c = clips.at(i);
    c->effects.insert(fx.at(i), deleted_objects.at(i));
  }
  PanelManager::fxControls().reload_clips();
  done = false;
  MainWindow::instance().setWindowModified(old_project_changed);
}

void EffectDeleteCommand::redo() {
  deleted_objects.clear();
  for (int i=0;i<clips.size();i++) {
    ClipPtr   c = clips.at(i);
    int fx_id = fx.at(i) - i;
    EffectPtr e = c->effects.at(fx_id);
    e->close();
    deleted_objects.append(e);
    c->effects.removeAt(fx_id);
  }
  PanelManager::fxControls().reload_clips();
  done = true;
  MainWindow::instance().setWindowModified(true);
}

MediaMove::MediaMove() : old_project_changed(MainWindow::instance().isWindowModified()) {}

void MediaMove::undo() {
  for (int i=0;i<items.size();i++) {
    Project::model().moveChild(items.at(i), froms.at(i));
  }
  MainWindow::instance().setWindowModified(old_project_changed);
}

void MediaMove::redo() {
  if (to == nullptr) to = Project::model().root();
  froms.resize(items.size());
  for (int i=0;i<items.size();i++) {
    MediaWPtr parent = items.at(i)->parentItem();
    if (!parent.expired()) {
      MediaPtr parPtr = parent.lock();
      froms[i] = parPtr;
      Project::model().moveChild(items.at(i), to);
    }
  }
  MainWindow::instance().setWindowModified(true);
}

MediaRename::MediaRename(const MediaPtr& iitem, QString  ito) :
  old_project_changed(MainWindow::instance().isWindowModified()),
  item(iitem),
  from(iitem->name()),
  to(std::move(ito))
{

}

void MediaRename::undo() {
  item->setName(from);
  MainWindow::instance().setWindowModified(old_project_changed);
}

void MediaRename::redo() {
  item->setName(to);
  MainWindow::instance().setWindowModified(true);
}

KeyframeDelete::KeyframeDelete(EffectField* ifield, const int iindex) :
  field(ifield),
  index(iindex),
  old_project_changed(MainWindow::instance().isWindowModified())
{}

void KeyframeDelete::undo() {
  field->keyframes.insert(index, deleted_key);
  MainWindow::instance().setWindowModified(old_project_changed);
}

void KeyframeDelete::redo() {
  deleted_key = field->keyframes.at(index);
  field->keyframes.removeAt(index);
  MainWindow::instance().setWindowModified(true);
}

EffectFieldUndo::EffectFieldUndo(EffectField* f) :
  field(f),
  done(true),
  old_project_changed(MainWindow::instance().isWindowModified())
{
  old_val = field->get_previous_data();
  new_val = field->get_current_data();
}

void EffectFieldUndo::undo() {
  field->set_current_data(old_val);
  done = false;
  MainWindow::instance().setWindowModified(old_project_changed);
}

void EffectFieldUndo::redo() {
  if (!done) {
    field->set_current_data(new_val);
  }
  MainWindow::instance().setWindowModified(true);
}

SetAutoscaleAction::SetAutoscaleAction() :
  old_project_changed(MainWindow::instance().isWindowModified())
{}

void SetAutoscaleAction::undo()
{
  for (auto& clp : clips) {
    clp->timeline_info.autoscale = !clp->timeline_info.autoscale;
  }
  PanelManager::sequenceViewer().viewer_widget->update();
  MainWindow::instance().setWindowModified(old_project_changed);
}

void SetAutoscaleAction::redo()
{
  for (auto& clp : clips) {
    clp->timeline_info.autoscale = !clp->timeline_info.autoscale;
  }
  PanelManager::sequenceViewer().viewer_widget->update();
  MainWindow::instance().setWindowModified(true);
}

AddMarkerAction::AddMarkerAction(project::ProjectItemPtr item, const long t, QString n) :
  item_(std::move(item)),
  time(t),
  name(std::move(n)),
  old_project_changed(MainWindow::instance().isWindowModified())
{}

void AddMarkerAction::undo()
{
  if (index == -1) {
    item_->markers_.removeLast();
  } else if (item_->markers_.at(index) != nullptr) {
    item_->markers_.at(index)->name = old_name;
  }
  PanelManager::markersViewer().refresh();
  MainWindow::instance().setWindowModified(old_project_changed);
}

void AddMarkerAction::redo()
{
  index = -1;
  for (auto i = 0; i < item_->markers_.size(); ++i) {
    if (MarkerPtr mark = item_->markers_.at(i)) {
      if (mark->frame == time) {
        index = i;
        break;
      }
    }
  }

  if (index == -1) {
    auto mark = std::make_shared<Marker>();
    mark->frame = time;
    mark->name = name;
    item_->markers_.append(mark);
  } else {
    old_name = item_->markers_.at(index)->name;
    item_->markers_.at(index)->name = name;
  }

  PanelManager::markersViewer().refresh();
  MainWindow::instance().setWindowModified(true);
}

MoveMarkerAction::MoveMarkerAction(MarkerPtr m, const long o, const long n) :
  marker(m),
  old_time(o),
  new_time(n),
  old_project_changed(MainWindow::instance().isWindowModified())
{}

void MoveMarkerAction::undo() {
  marker->frame = old_time;
  MainWindow::instance().setWindowModified(old_project_changed);
}

void MoveMarkerAction::redo() {
  marker->frame = new_time;
  MainWindow::instance().setWindowModified(true);
}

DeleteMarkerAction::DeleteMarkerAction(SequencePtr s) :
  seq(std::move(s)),
  sorted(false),
  old_project_changed(MainWindow::instance().isWindowModified())
{}

void DeleteMarkerAction::undo()
{
  for (int i=markers.size()-1; i>=0; i--) {
    seq->markers_.insert(markers.at(i), copies.at(i));
  }
  MainWindow::instance().setWindowModified(old_project_changed);
}

void DeleteMarkerAction::redo()
{
  for (int i=0;i<markers.size();i++) {
    // correct future removals
    if (!sorted) {
      copies.append(seq->markers_.at(markers.at(i)));
      for (int j=i+1;j<markers.size();j++) {
        if (markers.at(j) > markers.at(i)) {
          markers[j]--;
        }
      }
    }
    seq->markers_.removeAt(markers.at(i));
  }
  sorted = true;
  MainWindow::instance().setWindowModified(true);
}

SetSpeedAction::SetSpeedAction(const ClipPtr&   c, double speed) :
  clip(c),
  old_speed(c->timeline_info.speed),
  new_speed(speed),
  old_project_changed(MainWindow::instance().isWindowModified())
{}

void SetSpeedAction::undo() {
  clip->timeline_info.speed = old_speed;
  clip->recalculateMaxLength();
  MainWindow::instance().setWindowModified(old_project_changed);
}

void SetSpeedAction::redo() {
  clip->timeline_info.speed = new_speed;
  clip->recalculateMaxLength();
  MainWindow::instance().setWindowModified(true);
}

SetBool::SetBool(bool* b, const bool setting) :
  boolean(b),
  old_setting(*b),
  new_setting(setting),
  old_project_changed(MainWindow::instance().isWindowModified())
{}

void SetBool::undo() {
  *boolean = old_setting;
  MainWindow::instance().setWindowModified(old_project_changed);
}

void SetBool::redo() {
  *boolean = new_setting;
  MainWindow::instance().setWindowModified(true);
}

SetSelectionsCommand::SetSelectionsCommand(SequencePtr s) :
  seq(std::move(s)),
  done(true),
  old_project_changed(MainWindow::instance().isWindowModified())
{}

void SetSelectionsCommand::undo() {
  seq->selections_  = old_data;
  done = false;
  MainWindow::instance().setWindowModified(old_project_changed);
}

void SetSelectionsCommand::redo() {
  if (!done) {
    seq->selections_  = new_data;
    done = true;
  }
  MainWindow::instance().setWindowModified(true);
}

SetEnableCommand::SetEnableCommand(const ClipPtr& c, const bool enable) :
  clip(c),
  old_val(c->timeline_info.enabled),
  new_val(enable),
  old_project_changed(MainWindow::instance().isWindowModified())
{}

void SetEnableCommand::undo() {
  clip->timeline_info.enabled = old_val;
  MainWindow::instance().setWindowModified(old_project_changed);
}

void SetEnableCommand::redo() {
  clip->timeline_info.enabled = new_val;
  MainWindow::instance().setWindowModified(true);
}

EditSequenceCommand::EditSequenceCommand(MediaPtr i, const SequencePtr& s) :
  item(std::move(i)),
  seq(s),
  old_project_changed(MainWindow::instance().isWindowModified()),
  old_name(s->name()),
  old_width(s->width()),
  old_height(s->height()),
  old_frame_rate(s->frameRate()),
  old_audio_frequency(s->audioFrequency()),
  old_audio_layout(s->audioLayout())
{}

void EditSequenceCommand::undo() {
  seq->setName(old_name);
  seq->setWidth(old_width);
  seq->setHeight(old_height);
  seq->setFrameRate(old_frame_rate);
  seq->setAudioFrequency(old_audio_frequency);
  seq->setAudioLayout(old_audio_layout);
  update();

  MainWindow::instance().setWindowModified(old_project_changed);
}

void EditSequenceCommand::redo() {
  seq->setName(name);
  seq->setWidth(width);
  seq->setHeight(height);
  seq->setFrameRate(frame_rate);
  seq->setAudioFrequency(audio_frequency);
  seq->setAudioLayout(audio_layout);
  update();

  MainWindow::instance().setWindowModified(true);
}

void EditSequenceCommand::update() {
  // update tooltip
  item->setSequence(seq);

  for (auto& clp : seq->clips_) {
    if (clp != nullptr) {
      clp->refresh();
    }
  }

  if (global::sequence == seq) {
    set_sequence(seq);
  }
}

void CloseAllClipsCommand::undo() {
  redo();
}

void CloseAllClipsCommand::redo() {
  if (global::sequence != nullptr) {
    global::sequence->closeActiveClips();
  }
}

UpdateFootageTooltip::UpdateFootageTooltip(MediaPtr i) :
  item(std::move(i))
{}

void UpdateFootageTooltip::undo() {
  redo();
}

void UpdateFootageTooltip::redo() {
  item->updateTooltip();
}

MoveEffectCommand::MoveEffectCommand() :
  old_project_changed(MainWindow::instance().isWindowModified())
{}

void MoveEffectCommand::undo() {
  clip->effects.move(to, from);
  MainWindow::instance().setWindowModified(old_project_changed);
}

void MoveEffectCommand::redo() {
  clip->effects.move(from, to);
  MainWindow::instance().setWindowModified(true);
}

void ResetEffectCommand::undo()
{
  doCmd(true);
}

void ResetEffectCommand::redo()
{
  doCmd(false);
}


void ResetEffectCommand::doCmd(const bool undo_cmd)
{
  for (const auto& field_entry: fields_) {
    const auto&[field, before, after] = field_entry;
        const auto val = undo_cmd ? before : after;
        if (field != nullptr) {
      field->set_current_data(val);
    }
  }
}

RemoveClipsFromClipboard::RemoveClipsFromClipboard(int index) :
  pos(index),
  old_project_changed(MainWindow::instance().isWindowModified()),
  done(false)
{

}


void RemoveClipsFromClipboard::undo() {
  e_clipboard.insert(pos, clip);
  done = false;
}

void RemoveClipsFromClipboard::redo() {
  clip = std::dynamic_pointer_cast<Clip>(e_clipboard.at(pos));
  e_clipboard.removeAt(pos);
  done = true;
}

RenameClipCommand::RenameClipCommand() :
  old_project_changed(MainWindow::instance().isWindowModified())
{}

void RenameClipCommand::undo() {
  for (int i=0;i<clips.size();i++) {
    clips.at(i)->setName(old_names.at(i));
  }
}

void RenameClipCommand::redo() {
  old_names.resize(clips.size());
  for (int i=0;i<clips.size();i++) {
    old_names[i] = clips.at(i)->name();
    clips.at(i)->setName(new_name);
  }
}

//SetPointer::SetPointer(void **pointer, void *data) :
//  old_changed(MainWindow::instance().isWindowModified()),
//  p(pointer),
//  new_data(data)
//{

//}

//void SetPointer::undo() {
//  *p = old_data;
//  MainWindow::instance().setWindowModified(old_changed);
//}

//void SetPointer::redo() {
//  old_data = *p;
//  *p = new_data;
//  MainWindow::instance().setWindowModified(true);
//}

void ReloadEffectsCommand::undo() {
  redo();
}

void ReloadEffectsCommand::redo() {
  PanelManager::fxControls().reload_clips();
}

RippleAction::RippleAction(SequencePtr is, const long ipoint, const long ilength, QVector<int> iignore) :
  s(std::move(is)),
  point(ipoint),
  length(ilength),
  ignore(std::move(iignore))
{}

void RippleAction::undo() {
  ca->undo();
  delete ca;
}

void RippleAction::redo() {
  ca = new ComboAction();
  for (int i=0;i<s->clips_.size();i++) {
    if (!ignore.contains(i)) {
      ClipPtr   c = s->clips_.at(i);
      if (c != nullptr) {
        if (c->timeline_info.in >= point) {
          move_clip(ca, c, length, length, 0, 0, true, true);
        }
      }
    }
  }
  ca->redo();
}

SetQVariant::SetQVariant(QVariant *itarget, QVariant iold, QVariant inew) :
  target(itarget),
  old_val(std::move(iold)),
  new_val(std::move(inew))
{}

void SetQVariant::undo() {
  *target = old_val;
}

void SetQVariant::redo() {
  *target = new_val;
}

KeyframeFieldSet::KeyframeFieldSet(EffectField* ifield, const int ii) :
  field(ifield),
  index(ii),
  key(ifield->keyframes.at(ii)),
  done(true),
  old_project_changed(MainWindow::instance().isWindowModified())
{}

void KeyframeFieldSet::undo() {
  field->keyframes.removeAt(index);
  MainWindow::instance().setWindowModified(old_project_changed);
  done = false;
}

void KeyframeFieldSet::redo() {
  if (!done) {
    field->keyframes.insert(index, key);
    MainWindow::instance().setWindowModified(true);
  }
  done = true;
}

SetKeyframing::SetKeyframing(EffectRow *irow, const bool ib) :
  row(irow),
  b(ib)
{}

void SetKeyframing::undo() {
  row->setKeyframing(!b);
}

void SetKeyframing::redo() {
  row->setKeyframing(b);
}

RefreshClips::RefreshClips(MediaPtr m) :
  media(std::move(m))
{}

void RefreshClips::undo() {
  redo();
}

void RefreshClips::redo()
{
  // close any clips currently using this media
  QVector<MediaPtr> all_sequences = PanelManager::projectViewer().list_all_project_sequences();
  for (const auto & proj_seq : all_sequences) {
    auto s = proj_seq->object<Sequence>();
    for (const auto& clp : s->clips_) {
      if ( (clp != nullptr) && (clp->timeline_info.media == media) ) {
        clp->replaced = true;
        clp->refresh();
      }
    }
  }
}

NudgeClipCommand::NudgeClipCommand(ClipPtr clp, const int value)
  : clip_(std::move(clp)),
    nudge_value_(value)
{

}

void NudgeClipCommand::undo()
{
  Q_ASSERT(clip_ != nullptr);
  clip_->nudge(-nudge_value_);
}

void NudgeClipCommand::redo()
{
  Q_ASSERT(clip_ != nullptr);
  clip_->nudge(nudge_value_);
}
