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
#ifndef SEQUENCE_H
#define SEQUENCE_H

#include <QVector>
#include <memory>

#include "project/marker.h"
#include "project/selection.h"
#include "project/projectitem.h"
//FIXME: this is used EVERYWHERE. This has to be water-tight and heavily tested.

class Clip;
class Transition;
class Media;
typedef std::shared_ptr<Clip> ClipPtr;
typedef std::shared_ptr<Transition> TransitionPtr;

using SequencePtr = std::shared_ptr<Sequence>;
using SequenceUPtr = std::unique_ptr<Sequence>;
using SequenceWPtr = std::weak_ptr<Sequence>;

class Sequence : public project::ProjectItem {
public:

    Sequence() = default;
    Sequence(QVector<std::shared_ptr<Media>>& media_list, const QString& sequenceName);
    virtual ~Sequence() override;
    Sequence(const Sequence&& cpy) = delete;
    Sequence& operator=(const Sequence&) = delete;
    Sequence& operator=(const Sequence&&) = delete;

    Sequence(const Sequence& cpy);
    std::shared_ptr<Sequence> copy();
    void getTrackLimits(int& video_limit, int& audio_limit) const;
    long getEndFrame() const;
    void hard_delete_transition(ClipPtr& c, const int type);

    bool setWidth(const int val);
    int getWidth() const;
    bool setHeight(const int val);
    int getHeight() const;
    double getFrameRate() const;
    bool setFrameRate(const double frameRate);
    int getAudioFrequency() const;
    bool setAudioFrequency(const int frequency);
    /**
     * @brief getAudioLayout from ffmpeg libresample
     * @return AV_CH_LAYOUT_*
     */
    int getAudioLayout() const;
    /**
     * @brief setAudioLayout using ffmpeg libresample
     * @param layout AV_CH_LAYOUT_* value from libavutil/channel_layout.h
     */
    void setAudioLayout(const int layout);

    void closeActiveClips(const int depth=0);

    QVector<Selection> selections;
    QVector<ClipPtr> clips;
    int save_id = 0;
    bool using_workarea = false;
    bool enable_workarea = true;
    long workarea_in = 0;
    long workarea_out = 0;
    QVector<TransitionPtr> transitions;
    QVector<Marker> markers;
    long playhead = 0;
    bool wrapper_sequence = false;

private:
    int width = -1;
    int height = -1;
    double frame_rate = -0.0;
    int audio_frequency = -1;
    int audio_layout = -1;
};

namespace global {
    // static variable for the currently active sequence
    extern SequencePtr sequence;
}

#endif // SEQUENCE_H
