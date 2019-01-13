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
#ifndef UNDO_H
#define UNDO_H

#include <QUndoStack>
#include <QUndoCommand>
#include <QVector>
#include <QVariant>
#include <QModelIndex>
#include <QCheckBox>

#include "project/marker.h"
#include "project/selection.h"
#include "project/effectfield.h"
#include "project/sequence.h"
#include "project/effect.h"
#include "project/clip.h"

class Media;
class LabelSlider;
class SourceTable;
class EffectRow;
class EffectField;
class Transition;
class EffectGizmo;

class Footage;
struct EffectMeta;


extern QUndoStack e_undo_stack;

class ComboAction : public QUndoCommand {
public:
	ComboAction();
    ~ComboAction();
    void undo();
    void redo();
	void append(QUndoCommand* u);
	void appendPost(QUndoCommand* u);
private:
	QVector<QUndoCommand*> commands;
	QVector<QUndoCommand*> post_commands;
};

class MoveClipAction : public QUndoCommand {
public:
    MoveClipAction(ClipPtr c, long iin, long iout, long iclip_in, int itrack, bool irelative);
    ~MoveClipAction();
    void undo();
    void redo();
private:
    ClipPtr clip;

	long old_in;
	long old_out;
	long old_clip_in;
	int old_track;

	long new_in;
	long new_out;
	long new_clip_in;
	int new_track;

	bool relative;

	bool old_project_changed;
};

class RippleAction : public QUndoCommand {
public:
    RippleAction(SequencePtr is, long ipoint, long ilength, const QVector<int>& iignore);
	void undo();
	void redo();
private:
    SequencePtr s;
	long point;
	long length;
	QVector<int> ignore;
	ComboAction* ca;
};

class DeleteClipAction : public QUndoCommand {
public:
    DeleteClipAction(SequencePtr s, int clip);
	~DeleteClipAction();
	void undo();
	void redo();
private:
    SequencePtr seq;
    ClipPtr ref;
	int index;

	int opening_transition;
	int closing_transition;

	QVector<int> linkClipIndex;
	QVector<int> linkLinkIndex;

	bool old_project_changed;
};

class ChangeSequenceAction : public QUndoCommand {
public:
    ChangeSequenceAction(SequencePtr s);
	void undo();
	void redo();
private:
    SequencePtr old_sequence;
    SequencePtr new_sequence;
};

class AddEffectCommand : public QUndoCommand {
public:
    AddEffectCommand(ClipPtr c, EffectPtr e, const EffectMeta* m, int insert_pos = -1);
	~AddEffectCommand();
	void undo();
	void redo();
private:
    ClipPtr clip;
	const EffectMeta* meta;
    EffectPtr ref;
	int pos;
	bool done;
	bool old_project_changed;
};

class AddTransitionCommand : public QUndoCommand {
public:
    AddTransitionCommand(ClipPtr c, ClipPtr s, Transition *copy, const EffectMeta* itransition, int itype, int ilength);
	void undo();
	void redo();
private:
    ClipPtr clip;
    ClipPtr secondary;
	Transition* transition_to_copy;
	const EffectMeta* transition;
	int type;
	int length;
	bool old_project_changed;
	int old_ptransition;
	int old_stransition;
};

class ModifyTransitionCommand : public QUndoCommand {
public:
    ModifyTransitionCommand(ClipPtr c, int itype, long ilength);
	void undo();
	void redo();
private:
    ClipPtr clip;
	int type;
	long new_length;
	long old_length;
	bool old_project_changed;
};

class DeleteTransitionCommand : public QUndoCommand {
public:
    DeleteTransitionCommand(SequencePtr s, int transition_index);
	~DeleteTransitionCommand();
	void undo();
	void redo();
private:
    SequencePtr seq;
	int index;
	Transition* transition;
    ClipPtr otc;
    ClipPtr ctc;
	bool old_project_changed;
};

class SetTimelineInOutCommand : public QUndoCommand {
public:
    SetTimelineInOutCommand(SequencePtr s, bool enabled, long in, long out);
	void undo();
	void redo();
private:
    SequencePtr seq;

    bool old_workarea_enabled;

	bool old_enabled;
	long old_in;
	long old_out;

	bool new_enabled;
	long new_in;
	long new_out;

	bool old_project_changed;
};

class NewSequenceCommand : public QUndoCommand {
public:
	NewSequenceCommand(Media *s, Media* iparent);
	~NewSequenceCommand();
	void undo();
	void redo();
private:
	Media* seq;
	Media* parent;
	bool done;
	bool old_project_changed;
};

class AddMediaCommand : public QUndoCommand {
public:
	AddMediaCommand(Media* iitem, Media* iparent);
	~AddMediaCommand();
	void undo();
	void redo();
private:
	Media* item;
	Media* parent;
	bool done;
	bool old_project_changed;
};

class DeleteMediaCommand : public QUndoCommand {
public:
	DeleteMediaCommand(Media *i);
	~DeleteMediaCommand();
	void undo();
	void redo();
private:
	Media* item;
	Media* parent;
	bool old_project_changed;
	bool done;
};

class AddClipCommand : public QUndoCommand {
public:
    AddClipCommand(SequencePtr s, QVector<ClipPtr>& add);
	~AddClipCommand();
	void undo();
	void redo();
private:
    SequencePtr seq;
    QVector<ClipPtr> clips;
    QVector<ClipPtr> undone_clips;
	bool old_project_changed;
};

class LinkCommand : public QUndoCommand {
public:
	LinkCommand();
	void undo();
	void redo();
    SequencePtr s;
	QVector<int> clips;
	bool link;
private:
	QVector< QVector<int> > old_links;
	bool old_project_changed;
};

class CheckboxCommand : public QUndoCommand {
public:
	CheckboxCommand(QCheckBox* b);
	~CheckboxCommand();
	void undo();
	void redo();
private:
	QCheckBox* box;
	bool checked;
	bool done;
	bool old_project_changed;
};

class ReplaceMediaCommand : public QUndoCommand {
public:
	ReplaceMediaCommand(Media*, QString);
	void undo();
	void redo();
private:
	Media *item;
	QString old_filename;
	QString new_filename;
	bool old_project_changed;
	void replace(QString& filename);
};

class ReplaceClipMediaCommand : public QUndoCommand {
public:
	ReplaceClipMediaCommand(Media *, Media *, bool);
	void undo();
	void redo();
    QVector<ClipPtr> clips;
private:
	Media* old_media;
	Media* new_media;
	bool preserve_clip_ins;
	bool old_project_changed;
	QVector<int> old_clip_ins;
	void replace(bool undo);
};

class EffectDeleteCommand : public QUndoCommand {
public:
	EffectDeleteCommand();
	~EffectDeleteCommand();
	void undo();
	void redo();
    QVector<ClipPtr> clips;
	QVector<int> fx;
private:
	bool done;
	bool old_project_changed;
    QVector<EffectPtr> deleted_objects;
};

class MediaMove : public QUndoCommand {
public:
	MediaMove();
	QVector<Media*> items;
	Media* to;
	void undo();
	void redo();
private:
	QVector<Media*> froms;
	bool old_project_changed;
};

class MediaRename : public QUndoCommand {
public:
	MediaRename(Media* iitem, QString to);
	void undo();
	void redo();
private:
	bool old_project_changed;
	Media* item;
	QString from;
	QString to;
};

class KeyframeDelete : public QUndoCommand {
public:
    KeyframeDelete(EffectFieldPtr ifield, int iindex);
	void undo();
	void redo();
private:
    EffectFieldPtr field;
	int index;
	bool done;
	EffectKeyframe deleted_key;
	bool old_project_changed;
};

// a more modern version of the above, could probably replace it
// assumes the keyframe already exists
class KeyframeFieldSet : public QUndoCommand {
public:
    KeyframeFieldSet(EffectFieldPtr ifield, int ii);
	void undo();
	void redo();
private:
    EffectFieldPtr field;
	int index;
	EffectKeyframe key;
	bool done;
	bool old_project_changed;
};

class EffectFieldUndo : public QUndoCommand {
public:
    EffectFieldUndo(EffectFieldPtr field);
	void undo();
	void redo();
private:
    EffectFieldPtr field;
	QVariant old_val;
	QVariant new_val;
	bool done;
	bool old_project_changed;
};

class SetAutoscaleAction : public QUndoCommand {
public:
	SetAutoscaleAction();
	void undo();
	void redo();
    QVector<ClipPtr> clips;
private:
	bool old_project_changed;
};

class AddMarkerAction : public QUndoCommand {
public:
    AddMarkerAction(SequencePtr s, long t, QString n);
	void undo();
	void redo();
private:
    SequencePtr seq;
	long time;
	QString name;
	QString old_name;
	bool old_project_changed;
	int index;
};

class MoveMarkerAction : public QUndoCommand {
public:
	MoveMarkerAction(Marker* m, long o, long n);
	void undo();
	void redo();
private:
	Marker* marker;
	long old_time;
	long new_time;
	bool old_project_changed;
};

class DeleteMarkerAction : public QUndoCommand {
public:
    DeleteMarkerAction(SequencePtr s);
	void undo();
	void redo();
	QVector<int> markers;
private:
    SequencePtr seq;
	QVector<Marker> copies;
	bool sorted;
	bool old_project_changed;
};

class SetSpeedAction : public QUndoCommand {
public:
    SetSpeedAction(ClipPtr c, double speed);
	void undo();
	void redo();
private:
    ClipPtr clip;
	double old_speed;
	double new_speed;
	bool old_project_changed;
};

class SetBool : public QUndoCommand {
public:
	SetBool(bool* b, bool setting);
	void undo();
	void redo();
private:
	bool* boolean;
	bool old_setting;
	bool new_setting;
	bool old_project_changed;
};

class SetSelectionsCommand : public QUndoCommand {
public:
    SetSelectionsCommand(SequencePtr s);
	void undo();
	void redo();
	QVector<Selection> old_data;
	QVector<Selection> new_data;
private:
    SequencePtr seq;
	bool done;
	bool old_project_changed;
};

class SetEnableCommand : public QUndoCommand {
public:
    SetEnableCommand(ClipPtr c, bool enable);
	void undo();
	void redo();
private:
    ClipPtr clip;
	bool old_val;
	bool new_val;
	bool old_project_changed;
};

class EditSequenceCommand : public QUndoCommand {
    //FIXME: nononononono
public:
    EditSequenceCommand(Media *i, SequencePtr s);
	void undo();
	void redo();
	void update();

	QString name;
	int width;
	int height;
	double frame_rate;
	int audio_frequency;
	int audio_layout;
private:
	Media* item;
    SequencePtr seq;
	bool old_project_changed;

	QString old_name;
	int old_width;
	int old_height;
	double old_frame_rate;
	int old_audio_frequency;
	int old_audio_layout;
};

class SetInt : public QUndoCommand {
public:
	SetInt(int* pointer, int new_value);
	void undo();
	void redo();
private:
	int* p;
	int oldval;
	int newval;
	bool old_project_changed;
};

class SetLong : public QUndoCommand {
public:
	SetLong(long* pointer, long old_value, long new_value);
	void undo();
	void redo();
private:
	long* p;
	long oldval;
	long newval;
	bool old_project_changed;
};

class SetDouble : public QUndoCommand {
public:
	SetDouble(double* pointer, double old_value, double new_value);
	void undo();
	void redo();
private:
	double* p;
	double oldval;
	double newval;
	bool old_project_changed;
};

class SetString : public QUndoCommand {
public:
	SetString(QString* pointer, QString new_value);
	void undo();
	void redo();
private:
	QString* p;
	QString oldval;
	QString newval;
	bool old_project_changed;
};

class CloseAllClipsCommand : public QUndoCommand {
public:
	void undo();
	void redo();
};

class UpdateFootageTooltip : public QUndoCommand {
public:
	UpdateFootageTooltip(Media* i);
	void undo();
	void redo();
private:
	Media* item;
};

class MoveEffectCommand : public QUndoCommand {
public:
	MoveEffectCommand();
	void undo();
	void redo();
    ClipPtr clip;
	int from;
	int to;
private:
	bool old_project_changed;
};

class RemoveClipsFromClipboard : public QUndoCommand {
public:
	RemoveClipsFromClipboard(int index);
	~RemoveClipsFromClipboard();
	void undo();
	void redo();
private:
	int pos;
    ClipPtr clip;
	bool old_project_changed;
	bool done;
};

class RenameClipCommand : public QUndoCommand {
public:
	RenameClipCommand();
    QVector<ClipPtr> clips;
	QString new_name;
	void undo();
	void redo();
private:
	QVector<QString> old_names;
	bool old_project_changed;
};

class SetPointer : public QUndoCommand {
public:
	SetPointer(void** pointer, void* data);
	void undo();
	void redo();
private:
	bool old_changed;
	void** p;
	void* new_data;
	void* old_data;
};

class ReloadEffectsCommand : public QUndoCommand {
public:
	void undo();
	void redo();
};

class SetQVariant : public QUndoCommand {
public:
	SetQVariant(QVariant* itarget, const QVariant& iold, const QVariant& inew);
	void undo();
	void redo();
private:
	QVariant* target;
	QVariant old_val;
	QVariant new_val;
};

class SetKeyframing : public QUndoCommand {
public:
	SetKeyframing(EffectRow* irow, bool ib);
	void undo();
	void redo();
private:
	EffectRow* row;
	bool b;
};

class RefreshClips : public QUndoCommand {
public:
	RefreshClips(Media* m);
	void undo();
	void redo();
private:
	Media* media;
};

#endif // UNDO_H
