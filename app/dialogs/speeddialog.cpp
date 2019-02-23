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
#include "speeddialog.h"

#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QDialogButtonBox>

#include "ui/labelslider.h"
#include "project/clip.h"
#include "project/sequence.h"
#include "project/footage.h"
#include "playback/playback.h"
#include "panels/panels.h"
#include "panels/timeline.h"
#include "project/undo.h"
#include "project/effect.h"
#include "project/media.h"

SpeedDialog::SpeedDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Speed/Duration"));

    QVBoxLayout* main_layout = new QVBoxLayout();
    setLayout(main_layout);

    QGridLayout* grid = new QGridLayout();
    grid->setSpacing(6);

    grid->addWidget(new QLabel(tr("Speed:")), 0, 0);
    percent = new LabelSlider();
    percent->decimal_places = 2;
    percent->set_display_type(LABELSLIDER_PERCENT);
    percent->set_default_value(1);
    grid->addWidget(percent, 0, 1);

    grid->addWidget(new QLabel(tr("Frame Rate:")), 1, 0);
    frame_rate = new LabelSlider();
    frame_rate->decimal_places = 3;
    grid->addWidget(frame_rate, 1, 1);

    grid->addWidget(new QLabel(tr("Duration:")), 2, 0);
    duration = new LabelSlider();
    duration->set_display_type(LABELSLIDER_FRAMENUMBER);
    duration->set_frame_rate(global::sequence->frameRate());
    grid->addWidget(duration, 2, 1);

    main_layout->addLayout(grid);

    reverse = new QCheckBox(tr("Reverse"));
    maintain_pitch = new QCheckBox(tr("Maintain Audio Pitch"));
    ripple = new QCheckBox(tr("Ripple Changes"));

    main_layout->addWidget(reverse);
    main_layout->addWidget(maintain_pitch);
    main_layout->addWidget(ripple);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttonBox->setCenterButtons(true);
    main_layout->addWidget(buttonBox);
    connect(buttonBox, SIGNAL(rejected()), this, SLOT(reject()));
    connect(buttonBox, SIGNAL(accepted()), this, SLOT(accept()));

    connect(percent, SIGNAL(valueChanged()), this, SLOT(percent_update()));
    connect(frame_rate, SIGNAL(valueChanged()), this, SLOT(frame_rate_update()));
    connect(duration, SIGNAL(valueChanged()), this, SLOT(duration_update()));
}

void SpeedDialog::run() {
    bool enable_frame_rate = false;
    bool multiple_audio = false;
    maintain_pitch->setEnabled(false);

    default_frame_rate = qSNaN();
    current_frame_rate = qSNaN();
    current_percent = qSNaN();
    default_length = -1;
    current_length = -1;

    for (int i=0;i<clips.size();i++) {
        auto clp = clips.at(i);

        double clip_percent;

        // get default frame rate/percentage
        clip_percent = clp->timeline_info.speed;
        if (clp->timeline_info.isVideo()) {
            bool process_video = true;
            if (clp->timeline_info.media != nullptr && clp->timeline_info.media->type() == MediaType::FOOTAGE) {
                FootagePtr ftg = clp->timeline_info.media->object<Footage>();
                if (auto stream = ftg->video_stream_from_file_index(clp->timeline_info.media_stream)) {
                    process_video = !stream->infinite_length;
                }
            }

            if (process_video) {
                double media_frame_rate = clp->mediaFrameRate();

                // get "default" frame rate"
                if (enable_frame_rate) {
                    // check if frame rate is equal to default
                    if (!qIsNaN(default_frame_rate) && !qFuzzyCompare(media_frame_rate, default_frame_rate)) {
                        default_frame_rate = qSNaN();
                    }
                    if (!qIsNaN(current_frame_rate) && !qFuzzyCompare(media_frame_rate*clp->timeline_info.speed, current_frame_rate)) {
                        current_frame_rate = qSNaN();
                    }
                } else {
                    default_frame_rate = media_frame_rate;
                    current_frame_rate = media_frame_rate*clp->timeline_info.speed;
                }

                enable_frame_rate = true;
            }
        } else {
            maintain_pitch->setEnabled(true);

            if (!multiple_audio) {
                maintain_pitch->setChecked(clp->timeline_info.maintain_audio_pitch);
                multiple_audio = true;
            } else if (!maintain_pitch->isTristate() && maintain_pitch->isChecked() != clp->timeline_info.maintain_audio_pitch) {
                maintain_pitch->setCheckState(Qt::PartiallyChecked);
                maintain_pitch->setTristate(true);
            }
        }

        if (i == 0) {
            reverse->setChecked(clp->timeline_info.reverse);
        } else if (clp->timeline_info.reverse != reverse->isChecked()) {
            reverse->setTristate(true);
            reverse->setCheckState(Qt::PartiallyChecked);
        }

        // get default length
        long clip_default_length = qRound(clp->length() * clip_percent);
        if (i == 0) {
            current_length = clp->length();
            default_length = clip_default_length;
            current_percent = clip_percent;
        } else {
            if (current_length != -1 && clp->length() != current_length) {
                current_length = -1;
            }
            if (default_length != -1 && clip_default_length != default_length) {
                default_length = -1;
            }
            if (!qIsNaN(current_percent) && !qFuzzyCompare(clip_percent, current_percent)) {
                current_percent = qSNaN();
            }
        }
    }

    frame_rate->set_minimum_value(1);
    percent->set_minimum_value(0.0001);
    duration->set_minimum_value(1);

    frame_rate->setEnabled(enable_frame_rate);
    frame_rate->set_default_value(default_frame_rate);
    frame_rate->set_value(current_frame_rate, false);
    percent->set_value(current_percent, false);
    duration->set_default_value(default_length);
    duration->set_value((current_length == -1) ? qSNaN() : current_length, false);

    exec();
}

void SpeedDialog::percent_update() {
    bool got_fr = false;
    double fr_val = qSNaN();
    long len_val = -1;

    for (int i=0;i<clips.size();i++) {
        ClipPtr c = clips.at(i);

        // get frame rate
        if (frame_rate->isEnabled() && c->timeline_info.isVideo()) {
            double clip_fr = c->mediaFrameRate() * percent->value();
            if (got_fr) {
                if (!qIsNaN(fr_val) && !qFuzzyCompare(fr_val, clip_fr)) {
                    fr_val = qSNaN();
                }
            } else {
                fr_val = clip_fr;
                got_fr = true;
            }
        }

        // get duration
        long clip_default_length = qRound(c->length() * c->timeline_info.speed);
        long new_clip_length = qRound(clip_default_length / percent->value());
        if (i == 0) {
            len_val = new_clip_length;
        } else if (len_val > -1 && len_val != new_clip_length) {
            len_val = -1;
        }
    }

    frame_rate->set_value(fr_val, false);
    duration->set_value((len_val == -1) ? qSNaN() : len_val, false);
}

void SpeedDialog::duration_update() {
    double pc_val = qSNaN();
    bool got_fr = false;
    double fr_val = qSNaN();

    for (int i=0;i<clips.size();i++) {
        ClipPtr c = clips.at(i);

        // get percent
        long clip_default_length = qRound(c->length() * c->timeline_info.speed);
        double clip_pc = clip_default_length / duration->value();
        if (i == 0) {
            pc_val = clip_pc;
        } else if (!qIsNaN(pc_val) && !qFuzzyCompare(clip_pc, pc_val)) {
            pc_val = qSNaN();
        }

        // get frame rate
        if (frame_rate->isEnabled() && c->timeline_info.isVideo()) {
            double clip_fr = c->mediaFrameRate() * clip_pc;
            if (got_fr) {
                if (!qIsNaN(fr_val) && !qFuzzyCompare(fr_val, clip_fr)) {
                    fr_val = qSNaN();
                }
            } else {
                fr_val = clip_fr;
                got_fr = true;
            }
        }
    }

    frame_rate->set_value(fr_val, false);
    percent->set_value(pc_val, false);
}

void SpeedDialog::frame_rate_update() {
    /*double fr = (frame_rate->value());
    double pc = (fr / default_frame_rate);
    percent->set_value(pc, false);
    duration->set_value(default_length / pc, false);*/

    double old_pc_val = qSNaN();
    bool got_pc_val = false;
    double pc_val = qSNaN();
    bool got_len_val = false;
    long len_val = -1;

    // analyze video clips
    for (int i=0;i<clips.size();i++) {
        ClipPtr c = clips.at(i);

        // check if all selected clips are currently the same speed
        if (i == 0) {
            old_pc_val = c->timeline_info.speed;
        } else if (!qIsNaN(old_pc_val) && !qFuzzyCompare(c->timeline_info.speed, old_pc_val)) {
            old_pc_val = qSNaN();
        }

        if (c->timeline_info.isVideo()) {
            // what would the new speed be based on this frame rate
            double new_clip_speed = frame_rate->value() / c->mediaFrameRate();
            if (!got_pc_val) {
                pc_val = new_clip_speed;
                got_pc_val = true;
            } else if (!qIsNaN(pc_val) && !qFuzzyCompare(pc_val, new_clip_speed)) {
                pc_val = qSNaN();
            }

            // what would be the new length based on this speed
            long new_clip_len = (c->length() * c->timeline_info.speed) / new_clip_speed;
            if (!got_len_val) {
                len_val = new_clip_len;
                got_len_val = true;
            } else if (len_val > -1 && new_clip_len != len_val) {
                len_val = -1;
            }
        }
    }

    // analyze audio clips
    for (int i=0;i<clips.size();i++) {
        ClipPtr c = clips.at(i);

        if (c->timeline_info.track_ >= 0) {
            long new_clip_len = (qIsNaN(old_pc_val) || qIsNaN(pc_val)) ? c->length() : ((c->length() * c->timeline_info.speed) / pc_val);
            if (len_val > -1 && new_clip_len != len_val) {
                len_val = -1;
                break;
            }
        }
    }

    percent->set_value(pc_val, false);
    duration->set_value((len_val == -1) ? qSNaN() : len_val, false);
}

void set_speed(ComboAction* ca, ClipPtr c, double speed, bool ripple, long& ep, long& lr) {
    e_panel_timeline->deselect_area(c->timeline_info.in, c->timeline_info.out, c->timeline_info.track_);

    long proposed_out = c->timeline_info.out; //FIXME: this is never used
    double multiplier = (c->timeline_info.speed / speed);
    proposed_out = c->timeline_info.in + (c->length() * multiplier);
    ca->append(new SetSpeedAction(c, speed));
    if (!ripple && proposed_out > c->timeline_info.out) {
        for (int i=0;i<c->sequence->clips_.size();i++) {
            ClipPtr compare = c->sequence->clips_.at(i);
            if (compare != nullptr
                    && compare->timeline_info.track_ == c->timeline_info.track_
                    && compare->timeline_info.in >= c->timeline_info.out && compare->timeline_info.in < proposed_out) {
                proposed_out = compare->timeline_info.in;
            }
        }
    }
    ep = qMin(ep, c->timeline_info.out);
    lr = qMax(lr, proposed_out - c->timeline_info.out);
    move_clip(ca, c, c->timeline_info.in, proposed_out, c->timeline_info.clip_in * multiplier, c->timeline_info.track_);

    c->refactorFrameRate(ca, multiplier, false);

    Selection sel;
    sel.in = c->timeline_info.in;
    sel.out = proposed_out;
    sel.track = c->timeline_info.track_;
    global::sequence->selections_.append(sel);
}

void SpeedDialog::accept() {
    ComboAction* ca = new ComboAction();

    SetSelectionsCommand* sel_command = new SetSelectionsCommand(global::sequence);
    sel_command->old_data = global::sequence->selections_;

    long earliest_point = LONG_MAX;
    long longest_ripple = LONG_MIN;

    for (int i=0;i<clips.size();i++) {
        ClipPtr c = clips.at(i);
        c->close(true);

        if (c->timeline_info.track_ >= 0
                && maintain_pitch->checkState() != Qt::PartiallyChecked
                && c->timeline_info.maintain_audio_pitch != maintain_pitch->isChecked()) {
            ca->append(new SetBool(&c->timeline_info.maintain_audio_pitch, maintain_pitch->isChecked()));
        }

        if (reverse->checkState() != Qt::PartiallyChecked && c->timeline_info.reverse != reverse->isChecked()) {
            long new_clip_in = (c->maximumLength() - (c->length() + c->timeline_info.clip_in));
            move_clip(ca, c, c->timeline_info.in, c->timeline_info.out, new_clip_in, c->timeline_info.track_);
            c->timeline_info.clip_in = new_clip_in;
            ca->append(new SetBool(&c->timeline_info.reverse, reverse->isChecked()));
        }
    }

    if (!qIsNaN(percent->value())) {
        // simply set speed
        for (int i=0;i<clips.size();i++) {
            ClipPtr c = clips.at(i);
            set_speed(ca, c, percent->value(), ripple->isChecked(), earliest_point, longest_ripple);
        }
    } else if (!qIsNaN(frame_rate->value())) {
        bool can_change_all = true;
        double cached_speed;
        double cached_fr = qSNaN();

        // see if we can use the frame rate to change all the speeds
        for (int i=0;i<clips.size();i++) {
            ClipPtr c = clips.at(i);
            if (i == 0) {
                cached_speed = c->timeline_info.speed;
            } else if (!qFuzzyCompare(cached_speed, c->timeline_info.speed)) {
                can_change_all = false;
            }
            if (c->timeline_info.isVideo()) {
                if (qIsNaN(cached_fr)) {
                    cached_fr = c->mediaFrameRate();
                } else if (!qFuzzyCompare(cached_fr, c->mediaFrameRate())) {
                    can_change_all = false;
                    break;
                }
            }
        }

        // make changes
        for (int i=0;i<clips.size();i++) {
            ClipPtr c = clips.at(i);
            if (c->timeline_info.isVideo()) {
                set_speed(ca, c, frame_rate->value() / c->mediaFrameRate(), ripple->isChecked(), earliest_point, longest_ripple);
            } else if (can_change_all) {
                set_speed(ca, c, frame_rate->value() / cached_fr, ripple->isChecked(), earliest_point, longest_ripple);
            }
        }
    } else if (!qIsNaN(duration->value())) {
        // simply set duration
        for (int i=0;i<clips.size();i++) {
            ClipPtr c = clips.at(i);
            set_speed(ca, c, (c->length() * c->timeline_info.speed) / duration->value(), ripple->isChecked(), earliest_point, longest_ripple);
        }
    }

    if (ripple->isChecked()) {
        ripple_clips(ca, clips.at(0)->sequence, earliest_point, longest_ripple);
    }

    sel_command->new_data = global::sequence->selections_;
    ca->append(sel_command);

    e_undo_stack.push(ca);

    update_ui(true);
    QDialog::accept();
}
