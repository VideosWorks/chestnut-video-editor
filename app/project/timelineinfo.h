#ifndef TIMELINEINFO_H
#define TIMELINEINFO_H

#include <QString>
#include <QColor>
#include <atomic>
#include <memory>

#include "project/media.h"


namespace project {
  /**
   * @brief The TimelineInfo class of whose members may be used in multiple threads.
   */
  class TimelineInfo
  {
    public:
      TimelineInfo();
      bool isVideo() const;

      bool enabled = true;
      std::atomic_int64_t clip_in{0};
      std::atomic_int64_t in{0};
      std::atomic_int64_t out{0};
      std::atomic_int32_t track_{-1};
      QString name = "";
      QColor color = {0,0,0};
      MediaPtr media = nullptr; //TODO: assess Media members in lieu of c++20 atomic_shared_ptr
      std::atomic_int32_t media_stream{-1};
      std::atomic<double> speed{1.0};
      double cached_fr = 0.0;
      bool reverse = false; //TODO: revisit
      bool maintain_audio_pitch = true;
      bool autoscale = true;
  };
  using TimelineInfoPtr = std::shared_ptr<TimelineInfo>;
}



#endif // TIMELINEINFO_H
