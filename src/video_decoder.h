#pragma once

#include "types.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace queuepilot {

class VideoDecoder final {
public:
    using FrameCallback = std::function<void(VideoFrame)>;
    using StatusCallback = std::function<void(QString)>;

    VideoDecoder() = default;
    ~VideoDecoder();
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    // Lifecycle methods belong to the owner thread; callbacks run on the decoder thread.
    // The frame callback should replace a latest-frame slot rather than enqueue frames.
    void start(const DecoderOptions& options, FrameCallback frame, StatusCallback status);
    void stop();

private:
    static int interrupt(void* opaque);
    void run(DecoderOptions options, FrameCallback frame, StatusCallback status);
    bool waitUntil(Clock::time_point target);
    void setIoDeadline(std::chrono::seconds timeout);

    std::thread worker_;
    std::atomic_bool stopRequested_{true};
    std::atomic<std::int64_t> ioDeadlineNanoseconds_{0};
    std::mutex wakeMutex_;
    std::condition_variable wake_;
};

}
