#pragma once
#include "types.h"
#include "code_gate.h"
#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <optional>
#include <QRectF>

namespace queuepilot {
struct Detection {
    OcrResult result;
    std::optional<QString> confirmed;
    Clock::time_point receivedAt;
    double processingMs = 0;
    std::uint64_t revision = 0;
    std::uint64_t processedFrames = 0;
    double actualFps = 0;
};
struct Location {
    std::optional<QRectF> roi;
    QString message;
    std::uint64_t revision = 0;
};
class RecognitionWorker final {
public:
    ~RecognitionWorker();
    void start(QString modelDir, GateConfig config, QRectF roi,
               std::function<void(Detection)> detected, std::function<void(QString)> status,
               int sampleFps = 30, bool enabled = true);
    void offer(VideoFrame frame);
    std::uint64_t setRoi(QRectF roi);
    std::uint64_t setEnabled(bool enabled);
    std::uint64_t setSamplingFps(int sampleFps);
    std::uint64_t setGateConfig(GateConfig config);
    bool requestLocation(std::function<void(Location)> callback);
    void stop();
    VideoFrame latest() const;
private:
    mutable std::mutex mutex_;
    std::recursive_mutex callbacks_;
    std::condition_variable changed_;
    std::thread thread_;
    VideoFrame latest_;
    QRectF roi_;
    std::uint64_t revision_ = 0;
    bool stopping_ = true;
    bool pending_ = false;
    bool enabled_ = true;
    int sampleFps_ = 30;
    GateConfig config_;
    bool locating_ = false;
    std::function<void(Location)> locationCallback_;
    std::uint64_t locationRevision_ = 0;
};
}
