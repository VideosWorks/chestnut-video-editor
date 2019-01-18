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

#include "project/clip.h"
#include "project/sequence.h"
#include "panels/panels.h"
#include "panels/project.h"
#include "panels/effectcontrols.h"
#include "panels/viewer.h"
#include "panels/timeline.h"
#include "playback/playback.h"
#include "ui/sourcetable.h"
#include "project/effect.h"
#include "project/transition.h"
#include "project/footage.h"
#include "ui/labelslider.h"
#include "ui/viewerwidget.h"
#include "project/marker.h"
#include "mainwindow.h"
#include "io/clipboard.h"
#include "project/media.h"
#include "debug.h"

QUndoStack e_undo_stack;

ComboAction::ComboAction() {}

ComboAction::~ComboAction() {
    for (int i=0;i<commands.size();i++) {
        delete commands.at(i);
    }
}

void ComboAction::undo() {
    for (int i=commands.size()-1;i>=0;i--) {
        commands.at(i)->undo();
    }
    for (int i=0;i<post_commands.size();i++) {
        post_commands.at(i)->undo();
    }
}

void ComboAction::redo() {
    for (int i=0;i<commands.size();i++) {
        commands.at(i)->redo();
    }
    for (int i=0;i<post_commands.size();i++) {
        post_commands.at(i)->redo();
    }
}

void ComboAction::append(QUndoCommand* u) {
    commands.append(u);
}

void ComboAction::appendPost(QUndoCommand* u) {
    post_commands.append(u);
}

MoveClipAction::MoveClipAction(ClipPtr c, const long iin, const long iout, const long iclip_in, const int itrack, const bool irelative)
    : clip(c),
      old_in(c->timeline_info.in),
      old_out(c->timeline_info.out),
      old_clip_in(c->timeline_info.clip_in),
      old_track(c->timeline_info.track),
      new_in(iin),
      new_out(iout),
      new_clip_in(iclip_in),
      new_track(itrack),
      relative(irelative),
      old_project_changed(mainWindow->isWindowModified())
{}

void MoveClipAction::undo() {
    if (relative) {
        clip->timeline_info.in -= new_in;
        clip->timeline_info.out -= new_out;
        clip->timeline_info.clip_in -= new_clip_in;
        clip->timeline_info.track -= new_track;
    } else {
        clip->timeline_info.in = old_in;
        clip->timeline_info.out = old_out;
        clip->timeline_info.clip_in = old_clip_in;
        clip->timeline_info.track = old_track;
    }

    mainWindow->setWindowModified(old_project_changed);
}

void MoveClipAction::redo() {
    if (relative) {
        clip->timeline_info.in += new_in;
        clip->timeline_info.out += new_out;
        clip->timeline_info.clip_in += new_clip_in;
        clip->timeline_info.track += new_track;
    } else {
        clip->timeline_info.in = new_in;
        clip->timeline_info.out = new_out;
        clip->timeline_info.clip_in = new_clip_in;
        clip->timeline_info.track = new_track;
    }

    mainWindow->setWindowModified(true);
}

DeleteClipAction::DeleteClipAction(SequencePtr s, const int clip) :
    seq(s),
    index(clip),
    opening_transition(-1),
    closing_transition(-1),
    old_project_changed(mainWindow->isWindowModified())
{}

DeleteClipAction::~DeleteClipAction() {

}

void DeleteClipAction::undo() {
    // restore ref to clip
    seq->clips[index] = ref;

    // restore shared transitions
    if (opening_transition > -1) {
        seq->transitions.at(opening_transition)->secondary_clip = seq->transitions.at(opening_transition)->parent_clip;
        seq->transitions.at(opening_transition)->parent_clip = ref;
        ref->opening_transition = opening_transition;
        opening_transition = -1;
    }
    if (closing_transition > -1) {
        seq->transitions.at(closing_transition)->secondary_clip = ref;
        ref->closing_transition = closing_transition;
        closing_transition = -1;
    }

    // restore links to this clip
    for (int i=linkClipIndex.size()-1;i>=0;i--) {
        seq->clips.at(linkClipIndex.at(i))->linked.insert(linkLinkIndex.at(i), index);
    }

    ref = nullptr;

    mainWindow->setWindowModified(old_project_changed);
}

void DeleteClipAction::redo() {
	// remove ref to clip
	ref = seq->clips.at(index);
    ref->close(true);
    
	seq->clips[index] = nullptr;

	// save shared transitions
    if (ref->opening_transition > -1 && !ref->get_opening_transition()->secondary_clip.expired()) {
		opening_transition = ref->opening_transition;
        ref->get_opening_transition()->parent_clip = ref->get_opening_transition()->secondary_clip.lock();
        ref->get_opening_transition()->secondary_clip.reset();
		ref->opening_transition = -1;
	}
    if (ref->closing_transition > -1 && !ref->get_closing_transition()->secondary_clip.expired()) {
		closing_transition = ref->closing_transition;
        ref->get_closing_transition()->secondary_clip.reset();
		ref->closing_transition = -1;
	}

	// delete link to this clip
	linkClipIndex.clear();
	linkLinkIndex.clear();
	for (int i=0;i<seq->clips.size();i++) {
        ClipPtr   c = seq->clips.at(i);
		if (c != nullptr) {
			for (int j=0;j<c->linked.size();j++) {
				if (c->linked.at(j) == index) {
					linkClipIndex.append(i);
					linkLinkIndex.append(j);
					c->linked.removeAt(j);
				}
			}
		}
	}

	mainWindow->setWindowModified(true);
}

ChangeSequenceAction::ChangeSequenceAction(SequencePtr s) :
	new_sequence(s)
{}

void ChangeSequenceAction::undo() {
    set_sequence(old_sequence);
}

void ChangeSequenceAction::redo() {
    old_sequence = e_sequence;
    set_sequence(new_sequence);
}

SetTimelineInOutCommand::SetTimelineInOutCommand(SequencePtr s, const bool enabled, const long in, const long out) :
    seq(s),
    new_enabled(enabled),
    new_in(in),
    new_out(out),
    old_project_changed(mainWindow->isWindowModified())
{}

void SetTimelineInOutCommand::undo() {
    seq->using_workarea = old_enabled;
    seq->enable_workarea = old_workarea_enabled;
    seq->workarea_in = old_in;
    seq->workarea_out = old_out;

    // footage viewer functions
    if (seq->wrapper_sequence) {
        FootagePtr  m = seq->clips.at(0)->timeline_info.media->get_object<Footage>();
        m->using_inout = old_enabled;
        m->in = old_in;
        m->out = old_out;
    }

    mainWindow->setWindowModified(old_project_changed);
}

void SetTimelineInOutCommand::redo() {
    old_enabled = seq->using_workarea;
    old_workarea_enabled = seq->enable_workarea;
    old_in = seq->workarea_in;
    old_out = seq->workarea_out;

    if (!seq->using_workarea) seq->enable_workarea = true;
    seq->using_workarea = new_enabled;
    seq->workarea_in = new_in;
    seq->workarea_out = new_out;

    // footage viewer functions
    if (seq->wrapper_sequence) {
        FootagePtr  m = seq->clips.at(0)->timeline_info.media->get_object<Footage>();
        m->using_inout = new_enabled;
        m->in = new_in;
        m->out = new_out;
    }

    mainWindow->setWindowModified(true);
}

AddEffectCommand::AddEffectCommand(ClipPtr   c, EffectPtr e, const EffectMeta *m, const int insert_pos) :
    clip(c),
    meta(m),
    ref(e),
    pos(insert_pos),
    done(false),
    old_project_changed(mainWindow->isWindowModified())
{}

AddEffectCommand::~AddEffectCommand() {

}

void AddEffectCommand::undo() {
    clip->effects.last()->close();
    if (pos < 0) {
        clip->effects.removeLast();
    } else {
        clip->effects.removeAt(pos);
    }
    done = false;
    mainWindow->setWindowModified(old_project_changed);
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
    mainWindow->setWindowModified(true);
}

AddTransitionCommand::AddTransitionCommand(ClipPtr   c, ClipPtr s, Transition* copy, const EffectMeta *itransition, const int itype, const int ilength) :
    clip(c),
    secondary(s),
    transition_to_copy(copy),
    transition(itransition),
    type(itype),
    length(ilength),
    old_project_changed(mainWindow->isWindowModified())
{}

void AddTransitionCommand::undo() {
    clip->sequence->hard_delete_transition(clip, type);
    if (secondary != nullptr) secondary->sequence->hard_delete_transition(secondary, (type == TA_OPENING_TRANSITION) ? TA_CLOSING_TRANSITION : TA_OPENING_TRANSITION);

    if (type == TA_OPENING_TRANSITION) {
        clip->opening_transition = old_ptransition;
        if (secondary != nullptr) secondary->closing_transition = old_stransition;
    } else {
        clip->closing_transition = old_ptransition;
        if (secondary != nullptr) secondary->opening_transition = old_stransition;
    }

    mainWindow->setWindowModified(old_project_changed);
}

void AddTransitionCommand::redo() {
	if (type == TA_OPENING_TRANSITION) {
		old_ptransition = clip->opening_transition;
		clip->opening_transition = (transition_to_copy == nullptr) ? create_transition(clip, secondary, transition) : transition_to_copy->copy(clip, nullptr);
		if (secondary != nullptr) {
			old_stransition = secondary->closing_transition;
			secondary->closing_transition = clip->opening_transition;
		}
		if (length > 0) {
			clip->get_opening_transition()->set_length(length);
		}
	} else {
		old_ptransition = clip->closing_transition;
		clip->closing_transition = (transition_to_copy == nullptr) ? create_transition(clip, secondary, transition) : transition_to_copy->copy(clip, nullptr);
		if (secondary != nullptr) {
			old_stransition = secondary->opening_transition;
			secondary->opening_transition = clip->closing_transition;
		}
		if (length > 0) {
			clip->get_closing_transition()->set_length(length);
		}
	}
	mainWindow->setWindowModified(true);
}

ModifyTransitionCommand::ModifyTransitionCommand(ClipPtr c, const int itype, const long ilength) :
	clip(c),
	type(itype),
	new_length(ilength),
	old_project_changed(mainWindow->isWindowModified())
{}

void ModifyTransitionCommand::undo() {
    Transition* t = (type == TA_OPENING_TRANSITION) ? clip->get_opening_transition() : clip->get_closing_transition();
    t->set_length(old_length);
    mainWindow->setWindowModified(old_project_changed);
}

void ModifyTransitionCommand::redo() {
	Transition* t = (type == TA_OPENING_TRANSITION) ? clip->get_opening_transition() : clip->get_closing_transition();
	old_length = t->get_true_length();
	t->set_length(new_length);
	mainWindow->setWindowModified(true);
}

DeleteTransitionCommand::DeleteTransitionCommand(SequencePtr s, const int transition_index) :
	seq(s),
	index(transition_index),
	transition(nullptr),
	otc(nullptr),
	ctc(nullptr),
	old_project_changed(mainWindow->isWindowModified())
{}

DeleteTransitionCommand::~DeleteTransitionCommand() {
	if (transition != nullptr) delete transition;
}

void DeleteTransitionCommand::undo() {
    seq->transitions[index] = transition;

    if (otc != nullptr) otc->opening_transition = index;
    if (ctc != nullptr) ctc->closing_transition = index;

    transition = nullptr;
    mainWindow->setWindowModified(old_project_changed);
}

void DeleteTransitionCommand::redo() {
    for (int i=0;i<seq->clips.size();i++) {
        ClipPtr   c = seq->clips.at(i);
        if (c != nullptr) {
            if (c->opening_transition == index) {
                otc = c;
                c->opening_transition = -1;
            }
            if (c->closing_transition == index) {
                ctc = c;
                c->closing_transition = -1;
            }
        }
    }

    transition = seq->transitions.at(index);
    seq->transitions[index] = nullptr;

    mainWindow->setWindowModified(true);
}

NewSequenceCommand::NewSequenceCommand(MediaPtr s, MediaPtr iparent) :
    seq(s),
    parent(iparent),
    done(false),
    old_project_changed(mainWindow->isWindowModified())
{
    if (parent == nullptr) parent = project_model.get_root();
}

NewSequenceCommand::~NewSequenceCommand() {

}

void NewSequenceCommand::undo() {
    project_model.removeChild(parent, seq);

    done = false;
    mainWindow->setWindowModified(old_project_changed);
}

void NewSequenceCommand::redo() {
    project_model.appendChild(parent, seq);

    done = true;
    mainWindow->setWindowModified(true);
}

AddMediaCommand::AddMediaCommand(MediaPtr iitem, MediaPtr iparent) :
    item(iitem),
    parent(iparent),
    done(false),
    old_project_changed(mainWindow->isWindowModified())
{}

AddMediaCommand::~AddMediaCommand() {

}

void AddMediaCommand::undo() {
    project_model.removeChild(parent, item);
    done = false;
    mainWindow->setWindowModified(old_project_changed);
}

void AddMediaCommand::redo() {
    project_model.appendChild(parent, item);

    done = true;
    mainWindow->setWindowModified(true);
}

DeleteMediaCommand::DeleteMediaCommand(MediaPtr i) :
    item(i),
    parent(i->parentItem()),
    old_project_changed(mainWindow->isWindowModified())
{}

DeleteMediaCommand::~DeleteMediaCommand() {

}

void DeleteMediaCommand::undo() {
    project_model.appendChild(parent, item);

    mainWindow->setWindowModified(old_project_changed);
    done = false;
}

void DeleteMediaCommand::redo() {
    project_model.removeChild(parent, item);

    mainWindow->setWindowModified(true);
    done = true;
}

AddClipCommand::AddClipCommand(SequencePtr s, QVector<ClipPtr  >& add) :
    seq(s),
    clips(add),
    old_project_changed(mainWindow->isWindowModified())
{}

AddClipCommand::~AddClipCommand() {

}

void AddClipCommand::undo() {
    e_panel_effect_controls->clear_effects(true);
    for (int i=0;i<clips.size();i++) {
        ClipPtr   c = seq->clips.last();
        e_panel_timeline->deselect_area(c->timeline_info.in, c->timeline_info.out, c->timeline_info.track);
        undone_clips.prepend(c);
        c->close(true);
        seq->clips.removeLast();
    }
    mainWindow->setWindowModified(old_project_changed);
}

void AddClipCommand::redo() {
    if (undone_clips.size() > 0) {
        for (int i=0;i<undone_clips.size();i++) {
            seq->clips.append(undone_clips.at(i));
        }
        undone_clips.clear();
    } else {
        int linkOffset = seq->clips.size();
        for (int i=0;i<clips.size();i++) {
            ClipPtr   original = clips.at(i);
            ClipPtr   copy = original->copy(seq);
            copy->linked.resize(original->linked.size());
            for (int j=0;j<original->linked.size();j++) {
                copy->linked[j] = original->linked.at(j) + linkOffset;
            }
            if (original->opening_transition > -1) copy->opening_transition = original->get_opening_transition()->copy(copy, nullptr);
            if (original->closing_transition > -1) copy->closing_transition = original->get_closing_transition()->copy(copy, nullptr);
            seq->clips.append(copy);
        }
    }
    mainWindow->setWindowModified(true);
}

LinkCommand::LinkCommand() : link(true), old_project_changed(mainWindow->isWindowModified()) {}

void LinkCommand::undo() {
    for (int i=0;i<clips.size();i++) {
        ClipPtr   c = s->clips.at(clips.at(i));
        if (link) {
            c->linked.clear();
        } else {
            c->linked = old_links.at(i);
        }
    }
    mainWindow->setWindowModified(old_project_changed);
}

void LinkCommand::redo() {
    old_links.clear();
    for (int i=0;i<clips.size();i++) {
        dout << clips.at(i);
        ClipPtr   c = s->clips.at(clips.at(i));
        if (link) {
            for (int j=0;j<clips.size();j++) {
                if (i != j) {
                    c->linked.append(clips.at(j));
                }
            }
        } else {
            old_links.append(c->linked);
            c->linked.clear();
        }
    }
    mainWindow->setWindowModified(true);
}

CheckboxCommand::CheckboxCommand(QCheckBox* b) : box(b), checked(box->isChecked()), done(true), old_project_changed(mainWindow->isWindowModified()) {}

CheckboxCommand::~CheckboxCommand() {}

void CheckboxCommand::undo() {
    box->setChecked(!checked);
    done = false;
    mainWindow->setWindowModified(old_project_changed);
}

void CheckboxCommand::redo() {
    if (!done) {
        box->setChecked(checked);
    }
    mainWindow->setWindowModified(true);
}

ReplaceMediaCommand::ReplaceMediaCommand(MediaPtr i, QString s) :
    item(i),
    new_filename(s),
    old_project_changed(mainWindow->isWindowModified())
{
    old_filename = item->get_object<Footage>()->url;
}

void ReplaceMediaCommand::replace(QString& filename) {
    // close any clips currently using this media
    QVector<MediaPtr> all_sequences = e_panel_project->list_all_project_sequences();
    for (int i=0;i<all_sequences.size();i++) {
        SequencePtr s = all_sequences.at(i)->get_object<Sequence>();
        for (int j=0;j<s->clips.size();j++) {
            ClipPtr   c = s->clips.at(j);
            if (c != nullptr && c->timeline_info.media == item && c->is_open) {
                c->close(true);
                c->replaced = true;
            }
        }
    }

    // replace media
    QStringList files;
    files.append(filename);
    item->get_object<Footage>()->ready_lock.lock();
    e_panel_project->process_file_list(files, false, item, nullptr);
}

void ReplaceMediaCommand::undo() {
    replace(old_filename);

    mainWindow->setWindowModified(old_project_changed);
}

void ReplaceMediaCommand::redo() {
    replace(new_filename);

    mainWindow->setWindowModified(true);
}

ReplaceClipMediaCommand::ReplaceClipMediaCommand(MediaPtr a, MediaPtr b, const bool e) :
    old_media(a),
    new_media(b),
    preserve_clip_ins(e),
    old_project_changed(mainWindow->isWindowModified())
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

    mainWindow->setWindowModified(old_project_changed);
}

void ReplaceClipMediaCommand::redo() {
    replace(false);

    update_ui(true);
    mainWindow->setWindowModified(true);
}

EffectDeleteCommand::EffectDeleteCommand()
    : done(false),
      old_project_changed(mainWindow->isWindowModified())
{

}

EffectDeleteCommand::~EffectDeleteCommand() {

}

void EffectDeleteCommand::undo() {
    for (int i=0;i<clips.size();i++) {
        ClipPtr   c = clips.at(i);
        c->effects.insert(fx.at(i), deleted_objects.at(i));
    }
    e_panel_effect_controls->reload_clips();
    done = false;
    mainWindow->setWindowModified(old_project_changed);
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
    e_panel_effect_controls->reload_clips();
    done = true;
    mainWindow->setWindowModified(true);
}

MediaMove::MediaMove() : old_project_changed(mainWindow->isWindowModified()) {}

void MediaMove::undo() {
    for (int i=0;i<items.size();i++) {
        project_model.moveChild(items.at(i), froms.at(i));
    }
    mainWindow->setWindowModified(old_project_changed);
}

void MediaMove::redo() {
    if (to == nullptr) to = project_model.get_root();
    froms.resize(items.size());
    for (int i=0;i<items.size();i++) {
        MediaWPtr parent = items.at(i)->parentItem();
        if (!parent.expired()) {
            MediaPtr parPtr = parent.lock();
            froms[i] = parPtr;
            project_model.moveChild(items.at(i), to);
        }
    }
    mainWindow->setWindowModified(true);
}

MediaRename::MediaRename(MediaPtr iitem, QString ito) :
    item(iitem),
    from(iitem->get_name()),
    to(ito),
    old_project_changed(mainWindow->isWindowModified())
{}

void MediaRename::undo() {
    item->set_name(from);
    mainWindow->setWindowModified(old_project_changed);
}

void MediaRename::redo() {
    item->set_name(to);
    mainWindow->setWindowModified(true);
}

KeyframeDelete::KeyframeDelete(EffectFieldPtr ifield, const int iindex) :
    field(ifield),
    index(iindex),
    old_project_changed(mainWindow->isWindowModified())
{}

void KeyframeDelete::undo() {
    field->keyframes.insert(index, deleted_key);
    mainWindow->setWindowModified(old_project_changed);
}

void KeyframeDelete::redo() {
    deleted_key = field->keyframes.at(index);
    field->keyframes.removeAt(index);
    mainWindow->setWindowModified(true);
}

EffectFieldUndo::EffectFieldUndo(EffectFieldPtr f) :
    field(f),
    done(true),
    old_project_changed(mainWindow->isWindowModified())
{
    old_val = field->get_previous_data();
    new_val = field->get_current_data();
}

void EffectFieldUndo::undo() {
    field->set_current_data(old_val);
    done = false;
    mainWindow->setWindowModified(old_project_changed);
}

void EffectFieldUndo::redo() {
    if (!done) {
        field->set_current_data(new_val);
    }
    mainWindow->setWindowModified(true);
}

SetAutoscaleAction::SetAutoscaleAction() :
    old_project_changed(mainWindow->isWindowModified())
{}

void SetAutoscaleAction::undo() {
    for (int i=0;i<clips.size();i++) {
        clips.at(i)->timeline_info.autoscale = !clips.at(i)->timeline_info.autoscale;
    }
    e_panel_sequence_viewer->viewer_widget->update();
    mainWindow->setWindowModified(old_project_changed);
}

void SetAutoscaleAction::redo() {
    for (int i=0;i<clips.size();i++) {
        clips.at(i)->timeline_info.autoscale = !clips.at(i)->timeline_info.autoscale;
    }
    e_panel_sequence_viewer->viewer_widget->update();
    mainWindow->setWindowModified(true);
}

AddMarkerAction::AddMarkerAction(SequencePtr s, const long t, QString n) :
    seq(s),
    time(t),
    name(n),
    old_project_changed(mainWindow->isWindowModified())
{}

void AddMarkerAction::undo() {
    if (index == -1) {
        seq->markers.removeLast();
    } else {
        seq->markers[index].name = old_name;
    }

    mainWindow->setWindowModified(old_project_changed);
}

void AddMarkerAction::redo() {
    index = -1;
    for (int i=0;i<seq->markers.size();i++) {
        if (seq->markers.at(i).frame == time) {
            index = i;
            break;
        }
    }

    if (index == -1) {
        Marker m;
        m.frame = time;
        seq->markers.append(m);
    } else {
        old_name = seq->markers.at(index).name;
        seq->markers[index].name = name;
    }

    mainWindow->setWindowModified(true);
}

MoveMarkerAction::MoveMarkerAction(Marker* m, const long o, const long n) :
    marker(m),
    old_time(o),
    new_time(n),
    old_project_changed(mainWindow->isWindowModified())
{}

void MoveMarkerAction::undo() {
    marker->frame = old_time;
    mainWindow->setWindowModified(old_project_changed);
}

void MoveMarkerAction::redo() {
    marker->frame = new_time;
    mainWindow->setWindowModified(true);
}

DeleteMarkerAction::DeleteMarkerAction(SequencePtr s) :
    seq(s),
    sorted(false),
    old_project_changed(mainWindow->isWindowModified())
{}

void DeleteMarkerAction::undo() {
    for (int i=markers.size()-1;i>=0;i--) {
        seq->markers.insert(markers.at(i), copies.at(i));
    }
    mainWindow->setWindowModified(old_project_changed);
}

void DeleteMarkerAction::redo() {
    for (int i=0;i<markers.size();i++) {
        // correct future removals
        if (!sorted) {
            copies.append(seq->markers.at(markers.at(i)));
            for (int j=i+1;j<markers.size();j++) {
                if (markers.at(j) > markers.at(i)) {
                    markers[j]--;
                }
            }
        }
        seq->markers.removeAt(markers.at(i));
    }
    sorted = true;
    mainWindow->setWindowModified(true);
}

SetSpeedAction::SetSpeedAction(ClipPtr   c, double speed) :
    clip(c),
    old_speed(c->timeline_info.speed),
    new_speed(speed),
    old_project_changed(mainWindow->isWindowModified())
{}

void SetSpeedAction::undo() {
    clip->timeline_info.speed = old_speed;
    clip->recalculateMaxLength();
    mainWindow->setWindowModified(old_project_changed);
}

void SetSpeedAction::redo() {
    clip->timeline_info.speed = new_speed;
    clip->recalculateMaxLength();
    mainWindow->setWindowModified(true);
}

SetBool::SetBool(bool* b, const bool setting) :
    boolean(b),
    old_setting(*b),
    new_setting(setting),
    old_project_changed(mainWindow->isWindowModified())
{}

void SetBool::undo() {
    *boolean = old_setting;
    mainWindow->setWindowModified(old_project_changed);
}

void SetBool::redo() {
    *boolean = new_setting;
    mainWindow->setWindowModified(true);
}

SetSelectionsCommand::SetSelectionsCommand(SequencePtr s) :
    seq(s),
    done(true),
    old_project_changed(mainWindow->isWindowModified())
{}

void SetSelectionsCommand::undo() {
    seq->selections = old_data;
    done = false;
    mainWindow->setWindowModified(old_project_changed);
}

void SetSelectionsCommand::redo() {
    if (!done) {
        seq->selections = new_data;
        done = true;
    }
    mainWindow->setWindowModified(true);
}

SetEnableCommand::SetEnableCommand(ClipPtr c, const bool enable) :
    clip(c),
    old_val(c->timeline_info.enabled),
    new_val(enable),
    old_project_changed(mainWindow->isWindowModified())
{}

void SetEnableCommand::undo() {
    clip->timeline_info.enabled = old_val;
    mainWindow->setWindowModified(old_project_changed);
}

void SetEnableCommand::redo() {
    clip->timeline_info.enabled = new_val;
    mainWindow->setWindowModified(true);
}

EditSequenceCommand::EditSequenceCommand(MediaPtr i, SequencePtr s) :
    item(i),
    seq(s),
    old_project_changed(mainWindow->isWindowModified()),
    old_name(s->getName()),
    old_width(s->getWidth()),
    old_height(s->getHeight()),
    old_frame_rate(s->getFrameRate()),
    old_audio_frequency(s->getAudioFrequency()),
    old_audio_layout(s->getAudioLayout())
{}

void EditSequenceCommand::undo() {
    seq->setName(old_name);
    seq->setWidth(old_width);
    seq->setHeight(old_height);
    seq->setFrameRate(old_frame_rate);
    seq->setAudioFrequency(old_audio_frequency);
    seq->setAudioLayout(old_audio_layout);
    update();

    mainWindow->setWindowModified(old_project_changed);
}

void EditSequenceCommand::redo() {
    seq->setName(name);
    seq->setWidth(width);
    seq->setHeight(height);
    seq->setFrameRate(frame_rate);
    seq->setAudioFrequency(audio_frequency);
    seq->setAudioLayout(audio_layout);
    update();

    mainWindow->setWindowModified(true);
}

void EditSequenceCommand::update() {
    // update tooltip
    item->set_sequence(seq);

    for (int i=0;i<seq->clips.size();i++) {
        if (seq->clips.at(i) != nullptr) seq->clips.at(i)->refresh();
    }

    if (e_sequence == seq) {
        set_sequence(seq);
    }
}

SetInt::SetInt(int* pointer, const int new_value) :
    p(pointer),
    oldval(*pointer),
    newval(new_value),
    old_project_changed(mainWindow->isWindowModified())
{}

void SetInt::undo() {
    *p = oldval;
    mainWindow->setWindowModified(old_project_changed);
}

void SetInt::redo() {
    *p = newval;
    mainWindow->setWindowModified(true);
}

SetString::SetString(QString* pointer, QString new_value) :
    p(pointer),
    oldval(*pointer),
    newval(new_value),
    old_project_changed(mainWindow->isWindowModified())
{}

void SetString::undo() {
    *p = oldval;
    mainWindow->setWindowModified(old_project_changed);
}

void SetString::redo() {
    *p = newval;
    mainWindow->setWindowModified(true);
}

void CloseAllClipsCommand::undo() {
    redo();
}

void CloseAllClipsCommand::redo() {
    closeActiveClips(e_sequence);
}

UpdateFootageTooltip::UpdateFootageTooltip(MediaPtr i) :
    item(i)
{}

void UpdateFootageTooltip::undo() {
    redo();
}

void UpdateFootageTooltip::redo() {
    item->update_tooltip();
}

MoveEffectCommand::MoveEffectCommand() :
    old_project_changed(mainWindow->isWindowModified())
{}

void MoveEffectCommand::undo() {
    clip->effects.move(to, from);
    mainWindow->setWindowModified(old_project_changed);
}

void MoveEffectCommand::redo() {
    clip->effects.move(from, to);
    mainWindow->setWindowModified(true);
}

RemoveClipsFromClipboard::RemoveClipsFromClipboard(int index) :
    pos(index),
    old_project_changed(mainWindow->isWindowModified()),
    done(false)
{}

RemoveClipsFromClipboard::~RemoveClipsFromClipboard() {

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
    old_project_changed(mainWindow->isWindowModified())
{}

void RenameClipCommand::undo() {
    for (int i=0;i<clips.size();i++) {
        clips.at(i)->setName(old_names.at(i));
    }
}

void RenameClipCommand::redo() {
    old_names.resize(clips.size());
    for (int i=0;i<clips.size();i++) {
        old_names[i] = clips.at(i)->getName();
        clips.at(i)->setName(new_name);
    }
}

SetPointer::SetPointer(void **pointer, void *data) :
    p(pointer),
    new_data(data),
    old_changed(mainWindow->isWindowModified())
{}

void SetPointer::undo() {
    *p = old_data;
    mainWindow->setWindowModified(old_changed);
}

void SetPointer::redo() {
    old_data = *p;
    *p = new_data;
    mainWindow->setWindowModified(true);
}

void ReloadEffectsCommand::undo() {
    redo();
}

void ReloadEffectsCommand::redo() {
    e_panel_effect_controls->reload_clips();
}

RippleAction::RippleAction(SequencePtr is, const long ipoint, const long ilength, const QVector<int> &iignore) :
    s(is),
    point(ipoint),
    length(ilength),
    ignore(iignore)
{}

void RippleAction::undo() {
    ca->undo();
    delete ca;
}

void RippleAction::redo() {
    ca = new ComboAction();
    for (int i=0;i<s->clips.size();i++) {
        if (!ignore.contains(i)) {
            ClipPtr   c = s->clips.at(i);
            if (c != nullptr) {
                if (c->timeline_info.in >= point) {
                    move_clip(ca, c, length, length, 0, 0, true, true);
                }
            }
        }
    }
    ca->redo();
}

SetDouble::SetDouble(double* pointer, double old_value, double new_value) :
    p(pointer),
    oldval(old_value),
    newval(new_value),
    old_project_changed(mainWindow->isWindowModified())
{}

void SetDouble::undo() {
    *p = oldval;
    mainWindow->setWindowModified(old_project_changed);
}

void SetDouble::redo() {
    *p = newval;
    mainWindow->setWindowModified(true);
}

SetQVariant::SetQVariant(QVariant *itarget, const QVariant &iold, const QVariant &inew) :
    target(itarget),
    old_val(iold),
    new_val(inew)
{}

void SetQVariant::undo() {
    *target = old_val;
}

void SetQVariant::redo() {
    *target = new_val;
}

SetLong::SetLong(long *pointer, const long old_value, const long new_value) :
    p(pointer),
    oldval(old_value),
    newval(new_value),
    old_project_changed(mainWindow->isWindowModified())
{}

void SetLong::undo() {
    *p = oldval;
    mainWindow->setWindowModified(old_project_changed);
}

void SetLong::redo() {
    *p = newval;
    mainWindow->setWindowModified(true);
}

KeyframeFieldSet::KeyframeFieldSet(EffectFieldPtr ifield, const int ii) :
    field(ifield),
    index(ii),
    key(ifield->keyframes.at(ii)),
    done(true),
    old_project_changed(mainWindow->isWindowModified())
{}

void KeyframeFieldSet::undo() {
    field->keyframes.removeAt(index);
    mainWindow->setWindowModified(old_project_changed);
    done = false;
}

void KeyframeFieldSet::redo() {
    if (!done) {
        field->keyframes.insert(index, key);
        mainWindow->setWindowModified(true);
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
    media(m)
{}

void RefreshClips::undo() {
    redo();
}

void RefreshClips::redo() {
    // close any clips currently using this media
    QVector<MediaPtr> all_sequences = e_panel_project->list_all_project_sequences();
    for (int i=0;i<all_sequences.size();i++) {
        SequencePtr s = all_sequences.at(i)->get_object<Sequence>();
        for (int j=0;j<s->clips.size();j++) {
            ClipPtr   c = s->clips.at(j);
            if (c != nullptr && c->timeline_info.media == media) {
                c->replaced = true;
                c->refresh();
            }
        }
    }
}
