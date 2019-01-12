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
#include "clip.h"

#include "project/effect.h"
#include "project/transition.h"
#include "project/footage.h"
#include "io/config.h"
#include "playback/playback.h"
#include "playback/cacher.h"
#include "panels/project.h"
#include "project/sequence.h"
#include "panels/timeline.h"
#include "project/media.h"
#include "io/clipboard.h"
#include "undo.h"
#include "debug.h"

extern "C" {
#include <libavformat/avformat.h>
}

Clip::Clip(SequencePtr s) :
    sequence(s),
    enabled(true),
    clip_in(0),
    timeline_in(0),
    timeline_out(0),
    track(0),
    media(NULL),
    speed(1.0),
    reverse(false),
    maintain_audio_pitch(false),
    autoscale(config.autoscale_by_default),
    opening_transition(-1),
    closing_transition(-1),
    undeletable(false),
    replaced(false),
    ignore_reverse(false),
    use_existing_frame(false),
    filter_graph(NULL),
    fbo(NULL),
    media_handling()
{
    media_handling.pkt = av_packet_alloc();
    reset();
}


Clip::~Clip() {
    if (open) {
        // FIXME: ..... all that over there should be here
        //                close_clip(this, true);
    }

    //FIXME:
    //    if (opening_transition != -1) this->sequence->hard_delete_transition(this, TA_OPENING_TRANSITION);
    //    if (closing_transition != -1) this->sequence->hard_delete_transition(this, TA_CLOSING_TRANSITION);

    effects.clear();
    av_packet_free(&media_handling.pkt);
}

/**
 * @brief Copy constructor
 * @param cpy
 */
Clip::Clip(const Clip& cpy)
{

}

ClipPtr Clip::copy(SequencePtr s) {
    ClipPtr copyClip(new Clip(s));

    copyClip->enabled = enabled;
    copyClip->name = QString(name);
    copyClip->clip_in = clip_in;
    copyClip->timeline_in = timeline_in;
    copyClip->timeline_out = timeline_out;
    copyClip->track = track;
    copyClip->color_r = color_r;
    copyClip->color_g = color_g;
    copyClip->color_b = color_b;
    copyClip->media = media;
    copyClip->media_stream = media_stream;
    copyClip->autoscale = autoscale;
    copyClip->speed = speed;
    copyClip->maintain_audio_pitch = maintain_audio_pitch;
    copyClip->reverse = reverse;

    for (int i=0; i<effects.size(); i++) {
        //        copy->effects.append(effects.at(i)->copy(copy)); //TODO:hmm
    }

    copyClip->cached_fr = (this->sequence == NULL) ? cached_fr : this->sequence->getFrameRate();

    if (get_opening_transition() != NULL && get_opening_transition()->secondary_clip == NULL) {
        copyClip->opening_transition = get_opening_transition()->copy(copyClip, NULL);
    }
    if (get_closing_transition() != NULL && get_closing_transition()->secondary_clip == NULL) {
        copyClip->closing_transition = get_closing_transition()->copy(copyClip, NULL);
    }
    copyClip->recalculateMaxLength();

    return copyClip;
}


project::SequenceItemType_E Clip::getType() const {
    return project::SEQUENCE_ITEM_CLIP;
}


bool Clip::isActive(const long playhead) {
    if (enabled) {
        if (sequence != NULL) {
            if ( get_timeline_in_with_transition() < (playhead + (ceil(sequence->getFrameRate()*2)) ) )  {                      //TODO:what are we checking?
                if (get_timeline_out_with_transition() > playhead) {                                                            //TODO:what are we checking?
                    if ( (playhead - get_timeline_in_with_transition() + get_clip_in_with_transition()) < getMaximumLength() ){ //TODO:what are we checking?
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

void Clip::reset() {
    audio_playback.just_reset = false;
    open = false;
    finished_opening = false;
    pkt_written = false;
    audio_playback.reset = false;
    audio_playback.frame_sample_index = -1;
    audio_playback.buffer_write = false;
    texture_frame = -1;
    last_invalid_ts = -1;
    //TODO: check memory has been freed correctly or needs to be done here
    media_handling.formatCtx = NULL;
    media_handling.stream = NULL;
    media_handling.codec = NULL;
    media_handling.codecCtx = NULL;
    texture = NULL;
}

void Clip::reset_audio() {
    if (media == NULL || media->get_type() == MEDIA_TYPE_FOOTAGE) {
        audio_playback.reset = true;
        audio_playback.frame_sample_index = -1;
        audio_playback.buffer_write = 0;
    } else if (media->get_type() == MEDIA_TYPE_SEQUENCE) {
        SequencePtr nested_sequence = media->get_object<Sequence>();
        for (int i=0;i<nested_sequence->clips.size();i++) {
            ClipPtr c(nested_sequence->clips.at(i));
            if (c != NULL) c->reset_audio();
        }
    }
}

void Clip::refresh() {
    // validates media if it was replaced
    if (replaced && media != NULL && media->get_type() == MEDIA_TYPE_FOOTAGE) {
        FootagePtr m = media->get_object<Footage>();

        if (track < 0 && m->video_tracks.size() > 0)  {
            media_stream = m->video_tracks.at(0).file_index;
        } else if (track >= 0 && m->audio_tracks.size() > 0) {
            media_stream = m->audio_tracks.at(0).file_index;
        }
    }
    replaced = false;

    // reinitializes all effects... just in case
    for (int i=0;i<effects.size();i++) {
        effects.at(i)->refresh();
    }

    recalculateMaxLength();
}

void Clip::queue_clear() {
    while (queue.size() > 0) {
        av_frame_free(&queue.first());
        queue.removeFirst();
    }
}

void Clip::queue_remove_earliest() {
    int earliest_frame = 0;
    for (int i=1;i<queue.size();i++) {
        // TODO/NOTE: this will not work on sped up AND reversed video
        if (queue.at(i)->pts < queue.at(earliest_frame)->pts) {
            earliest_frame = i;
        }
    }
    av_frame_free(&queue[earliest_frame]);
    queue.removeAt(earliest_frame);
}

Transition* Clip::get_opening_transition() {
    if (opening_transition > -1) {
        if (this->sequence == NULL) {
            return e_clipboard_transitions.at(opening_transition);
        } else {
            return this->sequence->transitions.at(opening_transition);
        }
    }
    return NULL;
}

Transition* Clip::get_closing_transition() {
    if (closing_transition > -1) {
        if (this->sequence == NULL) {
            return e_clipboard_transitions.at(closing_transition);
        } else {
            return this->sequence->transitions.at(closing_transition);
        }
    }
    return NULL;
}


long Clip::get_clip_in_with_transition() {
    if (get_opening_transition() != NULL && get_opening_transition()->secondary_clip != NULL) {
        // we must be the secondary clip, so return (timeline in - length)
        return clip_in - get_opening_transition()->get_true_length();
    }
    return clip_in;
}

long Clip::get_timeline_in_with_transition() {
    if (get_opening_transition() != NULL && get_opening_transition()->secondary_clip != NULL) {
        // we must be the secondary clip, so return (timeline in - length)
        return timeline_in - get_opening_transition()->get_true_length();
    }
    return timeline_in;
}

long Clip::get_timeline_out_with_transition() {
    if (get_closing_transition() != NULL && get_closing_transition()->secondary_clip != NULL) {
        // we must be the primary clip, so return (timeline out + length2)
        return timeline_out + get_closing_transition()->get_true_length();
    } else {
        return timeline_out;
    }
}

// timeline functions
long Clip::getLength() {
    return timeline_out - timeline_in;
}

double Clip::getMediaFrameRate() {
    Q_ASSERT(track < 0);
    if (media != NULL) {
        double rate = media->get_frame_rate(media_stream);
        if (!qIsNaN(rate)) return rate;
    }
    if (sequence != NULL) return sequence->getFrameRate();
    return qSNaN();
}

void Clip::recalculateMaxLength() {
    // TODO: calculated_length on failures
    if (sequence != NULL) {
        double fr = this->sequence->getFrameRate();

        fr /= speed;

        media_handling.calculated_length = LONG_MAX;

        if (media != NULL) {
            switch (media->get_type()) {
            case MEDIA_TYPE_FOOTAGE:
            {
                FootagePtr m = media->get_object<Footage>();
                if (m != NULL) {
                    const FootageStream* ms = m->get_stream_from_file_index(track < 0, media_stream);
                    if (ms != NULL && ms->infinite_length) {
                        media_handling.calculated_length = LONG_MAX;
                    } else {
                        media_handling.calculated_length = m->get_length_in_frames(fr);
                    }
                }
            }
                break;
            case MEDIA_TYPE_SEQUENCE:
            {
                SequencePtr s = media->get_object<Sequence>();
                if (s != NULL) {
                    media_handling.calculated_length = refactor_frame_number(s->getEndFrame(), s->getFrameRate(), fr);
                }
            }
                break;
            default:
                //TODO: log/something
                break;
            }
        }
    }
}

long Clip::getMaximumLength() {
    return media_handling.calculated_length;
}

int Clip::getWidth() {
    if (media == NULL && sequence != NULL) {
        return sequence->getWidth();
    }

    switch (media->get_type()) {
    case MEDIA_TYPE_FOOTAGE:
    {
        const FootageStream* ms = media->get_object<Footage>()->get_stream_from_file_index(track < 0, media_stream);
        if (ms != NULL) {
            return ms->video_width;
        }
        if (sequence != NULL) {
            return sequence->getWidth();
        }
        break;
    }
    case MEDIA_TYPE_SEQUENCE:
    {
        SequencePtr sequenceNow = media->get_object<Sequence>();
        if (sequenceNow != NULL) {
            return sequenceNow->getWidth();
        }
        break;
    }
    default:
        //TODO: log/something
        break;
    }
    return 0;
}

int Clip::getHeight() {
    if ( (media == NULL) && (sequence != NULL) ) {
        return sequence->getHeight();
    }

    switch (media->get_type()) {
    case MEDIA_TYPE_FOOTAGE:
    {
        const FootageStream* ms = media->get_object<Footage>()->get_stream_from_file_index(track < 0, media_stream);
        if (ms != NULL) {
            return ms->video_height;
        }
        if (sequence != NULL) {
            return sequence->getHeight();
        }
        break;
    }
    case MEDIA_TYPE_SEQUENCE:
    {
        SequencePtr s = media->get_object<Sequence>();
        return s->getHeight();
        break;
    }
    default:
        //TODO: log/something
        break;
    }//switch
    return 0;
}

void Clip::refactor_frame_rate(ComboAction* ca, double multiplier, bool change_timeline_points) {
    if (change_timeline_points) {
        if (ca != NULL) {
            move(*ca,
                 qRound(static_cast<double>(timeline_in) * multiplier),
                 qRound(static_cast<double>(timeline_out) * multiplier),
                 qRound(static_cast<double>(clip_in) * multiplier),
                 track);
        }
    }

    // move keyframes
    for (int i=0;i<effects.size();i++) {
        EffectPtr effectNow = effects.at(i);
        for (int j=0; j<effectNow->row_count();j++) {
            EffectRowPtr effectRowNow = effectNow->row(j);
            for (int l=0; l<effectRowNow->fieldCount(); l++) {
                EffectField* f = effectRowNow->field(l);
                for (int k=0; k<f->keyframes.size(); k++) {
                    ca->append(new SetLong(&f->keyframes[k].time, f->keyframes[k].time, f->keyframes[k].time * multiplier));
                }
            }
        }
    }
}

//TODO: use of pointers is causing holdup
void Clip::move(ComboAction &ca, const long iin, const long iout,
                const long iclip_in, const int itrack, const bool verify_transitions,
                const bool relative)
{
    //TODO: check the ClipPtr(this) does what intended
    ca.append(new MoveClipAction(ClipPtr(this), iin, iout, iclip_in, itrack, relative));

    if (verify_transitions) {
        if ( (get_opening_transition() != NULL) &&
             (get_opening_transition()->secondary_clip != NULL) &&
             (get_opening_transition()->secondary_clip->timeline_out != iin) ) {
            // separate transition
            //            ca.append(new SetPointer((void**) &c->get_opening_transition()->secondary_clip, NULL));
            ca.append(new AddTransitionCommand(get_opening_transition()->secondary_clip, NULL, get_opening_transition(), NULL, TA_CLOSING_TRANSITION, 0));
        }

        if ( (get_closing_transition() != NULL) &&
             (get_closing_transition()->secondary_clip != NULL) &&
             (get_closing_transition()->parent_clip->timeline_in != iout) ) {
            // separate transition
            //            ca.append(new SetPointer((void**) &c->get_closing_transition()->secondary_clip, NULL));
            ca.append(new AddTransitionCommand(ClipPtr(this), NULL, get_closing_transition(), NULL, TA_CLOSING_TRANSITION, 0));
        }
    }
}
