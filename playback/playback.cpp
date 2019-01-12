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
#include "playback.h"

#include "project/clip.h"
#include "project/sequence.h"
#include "project/footage.h"
#include "playback/audio.h"
#include "playback/cacher.h"
#include "panels/panels.h"
#include "panels/timeline.h"
#include "panels/viewer.h"
#include "project/effect.h"
#include "panels/effectcontrols.h"
#include "project/media.h"
#include "io/config.h"
#include "io/avtogl.h"
#include "debug.h"

extern "C" {
	#include <libavformat/avformat.h>
	#include <libavutil/pixdesc.h>
	#include <libavcodec/avcodec.h>
	#include <libswscale/swscale.h>
	#include <libswresample/swresample.h>
}

#include <QtMath>
#include <QObject>
#include <QOpenGLTexture>
#include <QOpenGLPixelTransferOptions>
#include <QOpenGLFramebufferObject>

#ifdef QT_DEBUG
//#define GCF_DEBUG
#endif

bool e_texture_failed = false;
bool e_rendering = false;



double get_timecode(ClipPtr c, long playhead) {
    return ((double)(playhead-c->get_timeline_in_with_transition()+c->get_clip_in_with_transition())/(double)c->sequence->getFrameRate());
}

void get_clip_frame(ClipPtr c, long playhead) {
	if (c->finished_opening) {
        const FootageStream* ms = c->timeline_info.media->get_object<Footage>()->get_stream_from_file_index(c->timeline_info.track < 0, c->timeline_info.media_stream);

		int64_t target_pts = qMax(static_cast<int64_t>(0), playhead_to_timestamp(c, playhead));
        int64_t second_pts = qRound64(av_q2d(av_inv_q(c->media_handling.stream->time_base)));
		if (ms->video_interlacing != VIDEO_PROGRESSIVE) {
			target_pts *= 2;
			second_pts *= 2;
		}

        AVFrame* target_frame = NULL;

		bool reset = false;
        bool cache = true;

		c->queue_lock.lock();
		if (c->queue.size() > 0) {
			if (ms->infinite_length) {
				target_frame = c->queue.at(0);
#ifdef GCF_DEBUG
				dout << "GCF ==> USE PRECISE (INFINITE)";
#endif
			} else {
				// correct frame may be somewhere else in the queue
				int closest_frame = 0;

				for (int i=1;i<c->queue.size();i++) {
					//dout << "results for" << i << qAbs(c->queue.at(i)->pts - target_pts) << qAbs(c->queue.at(closest_frame)->pts - target_pts) << c->queue.at(i)->pts << target_pts;

					if (c->queue.at(i)->pts == target_pts) {
#ifdef GCF_DEBUG
						dout << "GCF ==> USE PRECISE";
#endif
						closest_frame = i;
						break;
					} else if (c->queue.at(i)->pts > c->queue.at(closest_frame)->pts && c->queue.at(i)->pts < target_pts) {
						closest_frame = i;
					}
				}

				// remove all frames earlier than this one from the queue
				target_frame = c->queue.at(closest_frame);
				int64_t next_pts = INT64_MAX;
				int64_t minimum_ts = target_frame->pts;

				int previous_frame_count = 0;
                if (e_config.previous_queue_type == FRAME_QUEUE_TYPE_SECONDS) {
                    minimum_ts -= (second_pts*e_config.previous_queue_size);
				}

				//dout << "closest frame was" << closest_frame << "with" << target_frame->pts << "/" << target_pts;
				for (int i=0;i<c->queue.size();i++) {
					if (c->queue.at(i)->pts > target_frame->pts && c->queue.at(i)->pts < next_pts) {
						next_pts = c->queue.at(i)->pts;
					}
                    if (c->queue.at(i) != target_frame && ((c->queue.at(i)->pts > minimum_ts) == c->timeline_info.reverse)) {
                        if (e_config.previous_queue_type == FRAME_QUEUE_TYPE_SECONDS) {
							//dout << "removed frame at" << i << "because its pts was" << c->queue.at(i)->pts << "compared to" << target_frame->pts;
							av_frame_free(&c->queue[i]); // may be a little heavy for the main thread?
							c->queue.removeAt(i);
							i--;
						} else {
							// TODO sort from largest to smallest
							previous_frame_count++;
						}
					}
				}

                if (e_config.previous_queue_type == FRAME_QUEUE_TYPE_FRAMES) {
                    while (previous_frame_count > qCeil(e_config.previous_queue_size)) {
						int smallest = 0;
						for (int i=1;i<c->queue.size();i++) {
							if (c->queue.at(i)->pts < c->queue.at(smallest)->pts) {
								smallest = i;
							}
						}
						av_frame_free(&c->queue[smallest]);
						c->queue.removeAt(smallest);
						previous_frame_count--;
					}
				}

                if (next_pts == INT64_MAX) next_pts = target_frame->pts + target_frame->pkt_duration;

				// we didn't get the exact timestamp
				if (target_frame->pts != target_pts) {
					if (target_pts > target_frame->pts && target_pts <= next_pts) {
#ifdef GCF_DEBUG
						dout << "GCF ==> USE IMPRECISE";
#endif
					} else {
						int64_t pts_diff = qAbs(target_pts - target_frame->pts);
						if (c->reached_end && target_pts > target_frame->pts) {
#ifdef GCF_DEBUG
							dout << "GCF ==> EOF TOLERANT";
#endif
							c->reached_end = false;
							cache = false;
						} else if (target_pts != c->last_invalid_ts && (target_pts < target_frame->pts || pts_diff > second_pts)) {

#ifdef GCF_DEBUG
                            dout << "GCF ==> RESET" << target_pts << "(" << target_frame->pts << "-" << target_frame->pts+target_frame->pkt_duration << ")";
#endif
                            if (!e_config.fast_seeking) target_frame = NULL;
							reset = true;
							c->last_invalid_ts = target_pts;
						} else {
#ifdef GCF_DEBUG
							dout << "GCF ==> WAIT - target pts:" << target_pts << "closest frame:" << target_frame->pts;
#endif
							if (c->queue.size() >= c->max_queue_size) c->queue_remove_earliest();
							c->ignore_reverse = true;
							target_frame = NULL;
						}
					}
				}
			}
		} else {
			reset = true;
		}

		if (target_frame == NULL || reset) {
			// reset cache
            e_texture_failed = true;
			dout << "[INFO] Frame queue couldn't keep up - either the user seeked or the system is overloaded (queue size:" << c->queue.size() << ")";
		}

		if (target_frame != NULL) {
			int nb_components = av_pix_fmt_desc_get(static_cast<enum AVPixelFormat>(c->pix_fmt))->nb_components;
			glPixelStorei(GL_UNPACK_ROW_LENGTH, target_frame->linesize[0]/nb_components);

			bool copied = false;
			uint8_t* data = target_frame->data[0];
			int frame_size;

			for (int i=0;i<c->effects.size();i++) {
                EffectPtr e = c->effects.at(i);
				if (e->enable_image) {
					if (!copied) {
						frame_size = target_frame->linesize[0]*target_frame->height;
						data = new uint8_t[frame_size];
						memcpy(data, target_frame->data[0], frame_size);
						copied = true;
					}
					e->process_image(get_timecode(c, playhead), data, frame_size);
				}
			}

			c->texture->setData(0, get_gl_pix_fmt_from_av(c->pix_fmt), QOpenGLTexture::UInt8, data);

			if (copied) delete [] data;

			glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
		}

		c->queue_lock.unlock();

		// get more frames
        QVector<ClipPtr> empty;
        if (cache) {
            c->cache(playhead, reset, false, empty);
        }
	}
}

long playhead_to_clip_frame(ClipPtr c, long playhead) {
	return (qMax(0L, playhead - c->get_timeline_in_with_transition()) + c->get_clip_in_with_transition());
}

double playhead_to_clip_seconds(ClipPtr c, long playhead) {
	// returns time in seconds
	long clip_frame = playhead_to_clip_frame(c, playhead);
    if (c->timeline_info.reverse) clip_frame = c->getMaximumLength() - clip_frame - 1;
    double secs = ((double) clip_frame/c->sequence->getFrameRate())*c->timeline_info.speed;
    if (c->timeline_info.media != NULL && c->timeline_info.media->get_type() == MEDIA_TYPE_FOOTAGE) secs *= c->timeline_info.media->get_object<Footage>()->speed;
	return secs;
}

int64_t seconds_to_timestamp(ClipPtr c, double seconds) {
    return qRound64(seconds * av_q2d(av_inv_q(c->media_handling.stream->time_base))) + qMax((int64_t) 0, c->media_handling.stream->start_time);
}

int64_t playhead_to_timestamp(ClipPtr c, long playhead) {
	return seconds_to_timestamp(c, playhead_to_clip_seconds(c, playhead));
}

//int retrieve_next_frame(ClipPtr c, AVFrame* f) {
//	int result = 0;
//	int receive_ret;

//	// do we need to retrieve a new packet for a new frame?
//	av_frame_unref(f);
//    while ((receive_ret = avcodec_receive_frame(c->media_handling.codecCtx, f)) == AVERROR(EAGAIN)) {
//		int read_ret = 0;
//		do {
//            if (c->pkt_written) {
//                av_packet_unref(c->media_handling.pkt);
//                c->pkt_written = false;
//			}
//            read_ret = av_read_frame(c->media_handling.formatCtx, c->media_handling.pkt);
//			if (read_ret >= 0) {
//                c->pkt_written = true;
//			}
//        } while (read_ret >= 0 && c->media_handling.pkt->stream_index != c->timeline_info.media_stream);

//		if (read_ret >= 0) {
//            int send_ret = avcodec_send_packet(c->media_handling.codecCtx, c->media_handling.pkt);
//			if (send_ret < 0) {
//				dout << "[ERROR] Failed to send packet to decoder." << send_ret;
//				return send_ret;
//			}
//		} else {
//			if (read_ret == AVERROR_EOF) {
//                int send_ret = avcodec_send_packet(c->media_handling.codecCtx, NULL);
//				if (send_ret < 0) {
//					dout << "[ERROR] Failed to send packet to decoder." << send_ret;
//					return send_ret;
//				}
//			} else {
//				dout << "[ERROR] Could not read frame." << read_ret;
//				return read_ret; // skips trying to find a frame at all
//			}
//		}
//	}
//	if (receive_ret < 0) {
//		if (receive_ret != AVERROR_EOF) dout << "[ERROR] Failed to receive packet from decoder." << receive_ret;
//		result = receive_ret;
//	}

//	return result;
//}

void set_sequence(SequencePtr s) {
    e_panel_effect_controls->clear_effects(true);
	e_sequence = s;
    e_panel_sequence_viewer->set_main_sequence();
    e_panel_timeline->update_sequence();
    e_panel_timeline->setFocus();
}

void closeActiveClips(SequencePtr s) {
	if (s != NULL) {
		for (int i=0;i<s->clips.size();i++) {
            ClipPtr c = s->clips.at(i);
			if (c != NULL) {
                if (c->timeline_info.media != NULL && c->timeline_info.media->get_type() == MEDIA_TYPE_SEQUENCE) {
                    closeActiveClips(c->timeline_info.media->get_object<Sequence>());
                }
                c->close(true);
			}
		}
	}
}
