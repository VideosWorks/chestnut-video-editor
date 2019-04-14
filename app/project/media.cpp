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
#include "media.h"

#include "footage.h"
#include "sequence.h"
#include "undo.h"
#include "io/config.h"
#include "panels/viewer.h"
#include "panels/project.h"
#include "projectmodel.h"

#include <QPainter>
#include <QCoreApplication>

#include "debug.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

using project::ScanMethod;
using project::FootageStreamPtr;

namespace
{
  const auto COLUMN_COUNT = 3;
  const auto FOLDER_ICON = ":/icons/folder.png";
  const auto SEQUENCE_ICON = ":/icons/sequence.png";
  const auto FRAME_RATE_DECIMAL_POINTS = 2;
  const auto FRAME_RATE_ARG_FORMAT = 'f';
}

QString get_interlacing_name(const ScanMethod interlacing)
{
  switch (interlacing) {
    case ScanMethod::PROGRESSIVE: return QCoreApplication::translate("InterlacingName", "None (Progressive)");
    case ScanMethod::TOP_FIRST: return QCoreApplication::translate("InterlacingName", "Top Field First");
    case ScanMethod::BOTTOM_FIRST: return QCoreApplication::translate("InterlacingName", "Bottom Field First");
    default: return QCoreApplication::translate("InterlacingName", "Invalid");
  }
}

QString get_channel_layout_name(const int32_t channels, const uint64_t layout) 
{
  switch (channels) {
    case 0: return QCoreApplication::translate("ChannelLayoutName", "Invalid");
    case 1: return QCoreApplication::translate("ChannelLayoutName", "Mono");
    case 2: return QCoreApplication::translate("ChannelLayoutName", "Stereo");
    default: {
      char buf[50];
      av_get_channel_layout_string(static_cast<char*>(buf), sizeof(buf), channels, layout);
      return QString(static_cast<char*>(buf));
    }
  }
}

int32_t Media::nextID = 0;


Media::Media() : Media(nullptr)
{
  // If no parent, this instance is the root
  root_ = true;
}

Media::Media(const MediaPtr& iparent) :
  throbber(nullptr),
  root_(false),
  parent_(iparent),
  folder_name_(),
  tool_tip_(),
  id_(nextID++)
{

}

/**
 * @brief Obtain this instance unique-id
 * @return id
 */
int32_t Media::id() const 
{
  return id_;
}

void Media::clearObject() 
{
  type_ = MediaType::NONE;
  object_ = nullptr;
  root_ = true;
}

bool Media::setFootage(const FootagePtr& ftg)
{
  if ((ftg != nullptr) && (ftg != object_)) {
    type_ = MediaType::FOOTAGE;
    object_ = ftg;
    root_ = false;
    return true;
  }
  return false;
}

bool Media::setSequence(const SequencePtr& sqn)
{
  if ( (sqn != nullptr) && (sqn != object_) ) {
    setIcon(QIcon(SEQUENCE_ICON));
    type_ = MediaType::SEQUENCE;
    object_ = sqn;
    root_ = false;
    updateTooltip();
    return true;
  }
  return false;
}

void Media::setFolder() 
{
  if (folder_name_.isEmpty()) {
    folder_name_ = QCoreApplication::translate("Media", "New Folder");
  }
  setIcon(QIcon(FOLDER_ICON));
  type_ = MediaType::FOLDER;
  object_ = nullptr;
  root_ = false;
}

void Media::setIcon(const QIcon &ico) 
{
  icon_ = ico;
}

void Media::setParent(const MediaWPtr& p)
{
  parent_ = p;
  root_ = false;
}

void Media::updateTooltip(const QString& error) 
{
  switch (type_) {
    case MediaType::FOOTAGE:
    {
      auto ftg = object<Footage>();
      tool_tip_ = QCoreApplication::translate("Media", "Name:") + " " + ftg->name() + "\n"
                 + QCoreApplication::translate("Media", "Filename:") + " " + ftg->url + "\n";

      if (error.isEmpty()) {
        if (!ftg->video_tracks.empty()) {
          tool_tip_ += QCoreApplication::translate("Media", "Video Dimensions:") + " ";
          for (int32_t i=0;i<ftg->video_tracks.size();i++) {
            if (i > 0) {
              tool_tip_ += ", ";
            }
            tool_tip_ += QString::number(ftg->video_tracks.at(i)->video_width) + "x"
                        + QString::number(ftg->video_tracks.at(i)->video_height);
          }
          tool_tip_ += "\n";

          if (!ftg->video_tracks.front()->infinite_length) {
            tool_tip_ += QCoreApplication::translate("Media", "Frame Rate:") + " ";
            for (int32_t i=0;i<ftg->video_tracks.size();i++) {
              if (i > 0) {
                tool_tip_ += ", ";
              }
              if (ftg->video_tracks.at(i)->video_interlacing == ScanMethod::PROGRESSIVE) {
                tool_tip_ += QString::number(ftg->video_tracks.at(i)->video_frame_rate * ftg->speed);
              } else {
                tool_tip_ += QCoreApplication::translate("Media", "%1 fields (%2 frames)").arg(
                              QString::number(ftg->video_tracks.at(i)->video_frame_rate * ftg->speed * 2),
                              QString::number(ftg->video_tracks.at(i)->video_frame_rate * ftg->speed)
                              );
              }
            }
            tool_tip_ += "\n";
          }

          tool_tip_ += QCoreApplication::translate("Media", "Interlacing:") + " ";
          for (int32_t i=0;i<ftg->video_tracks.size();i++) {
            if (i > 0) {
              tool_tip_ += ", ";
            }
            tool_tip_ += get_interlacing_name(ftg->video_tracks.at(i)->video_interlacing);
          }
        }

        if (!ftg->audio_tracks.empty()) {
          tool_tip_ += "\n";

          tool_tip_ += QCoreApplication::translate("Media", "Audio Frequency:") + " ";
          for (int32_t i=0;i<ftg->audio_tracks.size();i++) {
            if (i > 0) {
              tool_tip_ += ", ";
            }
            tool_tip_ += QString::number(ftg->audio_tracks.at(i)->audio_frequency * ftg->speed);
          }
          tool_tip_ += "\n";

          tool_tip_ += QCoreApplication::translate("Media", "Audio Channels:") + " ";
          for (int32_t i=0;i<ftg->audio_tracks.size();i++) {
            if (i > 0) {
              tool_tip_ += ", ";
            }
            tool_tip_ += get_channel_layout_name(ftg->audio_tracks.at(i)->audio_channels,
                                                ftg->audio_tracks.at(i)->audio_layout);
          }
          // tooltip += "\n";
        }
      } else {
        tool_tip_ += error;
      }
    }
      break;
    case MediaType::SEQUENCE:
    {
      auto sqn = object<Sequence>();

      tool_tip_ = QCoreApplication::translate("Media", "Name: %1"
                                                      "\nVideo Dimensions: %2x%3"
                                                      "\nFrame Rate: %4"
                                                      "\nAudio Frequency: %5"
                                                      "\nAudio Layout: %6").arg(
                   sqn->name(),
                   QString::number(sqn->width()),
                   QString::number(sqn->height()),
                   QString::number(sqn->frameRate()),
                   QString::number(sqn->audioFrequency()),
                   get_channel_layout_name(av_get_channel_layout_nb_channels(sqn->audioLayout()), sqn->audioLayout())
                   );
    }
      break;
    default:
      throw UnhandledMediaTypeException();
      break;
  }//switch

}

MediaType Media::type() const 
{
  return type_;
}

const QString &Media::name() 
{
  switch (type_) {
    case MediaType::FOOTAGE:
      [[fallthrough]];
    case MediaType::SEQUENCE: 
      return object_->name();
    default: return folder_name_;
  }
}

void Media::setName(const QString &name) 
{
  switch (type_) {
    case MediaType::FOOTAGE:
      object<Footage>()->setName(name);
      break;
    case MediaType::SEQUENCE:
      object<Sequence>()->setName(name);
      break;
    case MediaType::FOLDER:
      folder_name_ = name;
      break;
    default:
      throw UnhandledMediaTypeException();
      break;
  }//switch
}

double Media::frameRate(const int32_t stream) 
{
  switch (type()) {
    case MediaType::FOOTAGE:
    {
      if (auto ftg = object<Footage>()) {
        if ( (stream < 0) && !ftg->video_tracks.empty()) {
          return ftg->video_tracks.front()->video_frame_rate * ftg->speed;
        }
        if (FootageStreamPtr ms = ftg->video_stream_from_file_index(stream)) {
          return ms->video_frame_rate * ftg->speed;
        }
      }
      break;
    }
    case MediaType::SEQUENCE:
      if (auto sqn = object<Sequence>()) {
        return sqn->frameRate();
      }
      break;
    default:
      throw UnhandledMediaTypeException();
      break;
  }//switch

  return 0.0;
}

int32_t Media::samplingRate(const int32_t stream) 
{
  switch (type()) {
    case MediaType::FOOTAGE:
    {
      if (auto ftg = object<Footage>()) {
        if (stream < 0) {
          return qRound(ftg->audio_tracks.front()->audio_frequency * ftg->speed);
        }
        if (FootageStreamPtr ms = ftg->audio_stream_from_file_index(stream)) {
          return qRound(ms->audio_frequency * ftg->speed);
        }
      }
      break;
    }
    case MediaType::SEQUENCE:
      if (auto sqn = object<Sequence>()) {
        return sqn->audioFrequency();
      }
      break;
    default:
      throw UnhandledMediaTypeException();
      break;
  }//switch
  return 0;
}

void Media::appendChild(const MediaPtr& child)
{
  child->setParent(shared_from_this());
  children_.append(child);
}

bool Media::setData(const int32_t column, const QVariant &value) 
{
  if (column == 0) {
    const auto n = value.toString();
    if (!n.isEmpty() && name() != n) {
      e_undo_stack.push(new MediaRename(shared_from_this(), n));
      return true;
    }
  }
  return false;
}

MediaPtr Media::child(const int32_t row) 
{
  return children_.value(row);
}

int32_t Media::childCount() const 
{
  return children_.count();
}

int32_t Media::columnCount() const 
{
  return COLUMN_COUNT;
}

QVariant Media::data(const int32_t column, const int32_t role) 
{
  switch (role) {
    case Qt::DecorationRole:
      if (column == 0) {
        if (type() == MediaType::FOOTAGE) {
          FootagePtr f = object<Footage>();
          if ( (!f->video_tracks.empty())
              && f->video_tracks.front()->preview_done) {
            return f->video_tracks.front()->video_preview_square;
          }
        }

        return icon_;
      }
      break;
    case Qt::DisplayRole:
      switch (column) {
        case 0: return (root_) ? QCoreApplication::translate("Media", "Name") : name();
        case 1:
          if (root_) return QCoreApplication::translate("Media", "Duration");
          if (type() == MediaType::SEQUENCE) {
            SequencePtr s = object<Sequence>();
            return frame_to_timecode(s->endFrame(), e_config.timecode_view, s->frameRate());
          }
          if (type() == MediaType::FOOTAGE) {
            FootagePtr f = object<Footage>();
            double r = 30;

            if ( (!f->video_tracks.empty()) && !qIsNull(f->video_tracks.front()->video_frame_rate)) {
              r = f->video_tracks.front()->video_frame_rate * f->speed;
            }

            long len = f->get_length_in_frames(r);
            if (len > 0) return frame_to_timecode(len, e_config.timecode_view, r);
          }
          break;
        case 2:
          if (root_) return QCoreApplication::translate("Media", "Rate");
          if (type() == MediaType::SEQUENCE) return QString::number(frameRate(),
                                                                    FRAME_RATE_ARG_FORMAT,
                                                                    FRAME_RATE_DECIMAL_POINTS) + " FPS";
          if (type() == MediaType::FOOTAGE) {
            auto ftg = object<Footage>();
            const double rate = frameRate();
            QString ret_str;
            if ( (!ftg->video_tracks.empty()) && !qIsNull(rate)) {
              ret_str = QString::number(rate, FRAME_RATE_ARG_FORMAT, FRAME_RATE_DECIMAL_POINTS) + " FPS";
            } else if (!ftg->audio_tracks.empty()) {
              ret_str = QString::number(samplingRate()) + " Hz";
            }
            return ret_str;
          }
          break;
        default:
          // There's only 3 columns
          break;
      }//switch
      break;
    case Qt::ToolTipRole:
      return tool_tip_;
    default:
      // Unhandled role
      break;
  }//switch
  return QVariant();
}

int32_t Media::row()
{
  if (auto parPtr = parent_.lock()) {
    return parPtr->children_.indexOf(shared_from_this());
  }
  return 0;
}

MediaPtr Media::parentItem() 
{
  return parent_.lock();
}

void Media::removeChild(const int32_t index) 
{
  children_.removeAt(index);
}

