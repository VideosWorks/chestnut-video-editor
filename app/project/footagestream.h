/*
 * Chestnut. Chestnut is a free non-linear video editor for Linux.
 * Copyright (C) 2019 Jonathan Noble
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef FOOTAGESTREAM_H
#define FOOTAGESTREAM_H

#include <memory>
#include <QImage>
#include <QIcon>
#include <mediahandling/imediastream.h>
#include <QPixmap>
#include <mutex>

#include "project/ixmlstreamer.h"

class Footage;

namespace project {

  class FootageStream : public project::IXMLStreamer
  {
    public:
      int file_index {-1};
      int video_width {-1};
      int video_height {-1};
      double video_frame_rate {0.0};
      int audio_channels {-1};
      int audio_layout {-1};
      int audio_frequency {-1};
      media_handling::Rational timescale_ {0, 1};
      QImage video_preview;
      QIcon video_preview_square;
      QVector<char> audio_preview;
      media_handling::StreamType type_ {media_handling::StreamType::UNKNOWN};
      bool enabled_ {true};
      bool infinite_length {false};
      bool preview_done_ {false};

      FootageStream() = delete;
      explicit FootageStream(std::weak_ptr<Footage> parent);
      FootageStream(media_handling::MediaStreamPtr stream_info, const bool is_audio, std::weak_ptr<Footage> parent);

      FootageStream(const FootageStream& cpy) = delete;
      FootageStream(const FootageStream&& cpy) = delete;
      FootageStream& operator=(const FootageStream& rhs) = delete;
      FootageStream& operator=(const FootageStream&& rhs) = delete;

      /**
       * @brief   Generate the preview or load if already exists
       * @return  true==preview available
       */
      bool generatePreview();

      std::tuple<QPixmap, media_handling::Rational> frame();

      QByteArray audioFrame(const int64_t out=-1);

      void seek(const int64_t position);

      void setStreamInfo(media_handling::MediaStreamPtr stream_info);
      std::optional<media_handling::FieldOrder> fieldOrder() const;

      /* IXMLStreamer overrides */
      virtual bool load(QXmlStreamReader& stream) override;
      virtual bool save(QXmlStreamWriter& stream) const override;


    private:
      struct WaveformInfo
      {
          uint32_t version_ {};
          uint32_t flags_ {};
          uint32_t rate_ {};
          uint32_t samples_per_pixel_ {};
          uint32_t length_ {};
          uint32_t channels_ {};
      } waveform_info_;
      std::weak_ptr<Footage> parent_;
      media_handling::MediaStreamPtr stream_info_{nullptr};
      QString data_path;
      std::optional<int64_t> seek_position_;
      std::once_flag frame_retrieved_;
      bool audio_ {false};

      void initialise(const media_handling::IMediaStream& stream);
      /**
       * @brief   Generate preview of video/image stream
       * @note    Only generates if doesn't already exist. Loads if it does exist
       * @return  true==preview available
       */
      bool generateVisualPreview();
      /**
       * @brief   Generate preview of an audio stream
       * @note    Only generates if doesn't already exist. Loads if it does exist
       * @return  true==preview available
       */
      bool generateAudioPreview();
      /**
       * @brief   The hash of the source file path
       * @return
       */
      QString previewHash() const;
      /**
       * @brief  Filepath where a thumbnail of this stream should be located
       * @return filepath
       */
      QString thumbnailPath() const;
      /**
       * @brief  Filepath where waveform data of this stream should be located
       * @return filepath
       */
      QString waveformPath() const;
      /**
       * @brief           Load a waveform file formatted using bbc/audiowaveform structure
       * @param data_path Location of waveform file to load
       * @return          true==success
       */
      bool loadWaveformFile(const QString& data_path);
      /**
       * @brief Make an thumbnail (icon)
       * @note Thumbnail is used in tree-view
       */
      void makeSquareThumb();
  };

  using FootageStreamPtr = std::shared_ptr<FootageStream>;
  using FootageStreamWPtr = std::weak_ptr<FootageStream>;
}

#endif // FOOTAGESTREAM_H
