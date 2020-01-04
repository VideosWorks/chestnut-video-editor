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

#ifndef AUDIOWORKER_H
#define AUDIOWORKER_H

#include <QObject>
#include <QQueue>
#include <QMutex>
#include <QWaitCondition>
#include <QAudioFormat>
#include <QAudioDeviceInfo>
#include <QAudioOutput>
#include <memory>

namespace chestnut::system
{

  class AudioWorker : public QObject
  {
      Q_OBJECT
    public:
      explicit AudioWorker(QObject *parent = nullptr);
      ~AudioWorker() override;
      void clear();
      void queueData(QByteArray data);
      void setup(const int channels);
    signals:
      void finished();

    public slots:
      void process();
    private:
      QQueue<QByteArray> queue_;
      QMutex mutex_;
      QMutex queue_mutex_;
      QWaitCondition wait_cond_;
      std::unique_ptr<QAudioOutput> audio_output_;
      QIODevice* audio_io_ {nullptr};
      std::atomic_bool running_ {true};
  };
}

#endif // AUDIOWORKER_H
