#include "recognition_worker.h"
#include "code_locator.h"
#include "ocr_engine.h"
#include <QDir>
#include <algorithm>
#include <cmath>
#include <deque>
#include <utility>

namespace queuepilot {
namespace {
bool accepted(const OcrResult& result, const GateConfig& config)
{
    return std::isfinite(result.confidence) && result.confidence >= config.minConfidence &&
           result.confidence <= 1 && CodeGate::validCode(CodeGate::normalizeCandidate(result.text),
                                                        config.lettersFirst);
}

struct TextLineCache {
    std::vector<DetectedTextLine> lines;
    QSize dimensions;
    Clock::time_point scannedAt{};
    bool scanned = false;
    bool complete = false;
    int misses = 0;
    QImage layout;
    std::vector<unsigned char> textMask;
    std::vector<QImage> rejectedLines;

    bool layoutChanged(const QImage& crop) const
    {
        if (layout.isNull()) return true;
        const auto image = crop.convertToFormat(QImage::Format_RGB32);
        int changes = 0;
        for (int y = 0; y < image.height(); y += 2) {
            const auto* previous = reinterpret_cast<const QRgb*>(layout.constScanLine(y));
            const auto* current = reinterpret_cast<const QRgb*>(image.constScanLine(y));
            for (int x = 0; x < image.width(); x += 2) {
                if (textMask[static_cast<std::size_t>(y) * image.width() + x]) continue;
                if (std::abs(qGray(previous[x]) - qGray(current[x])) >= 48 && ++changes >= 12)
                    return true;
            }
        }
        return false;
    }

    void rememberLayout(const QImage& crop)
    {
        layout = crop.convertToFormat(QImage::Format_RGB32);
        textMask.assign(static_cast<std::size_t>(crop.width()) * crop.height(), 0);
        for (const auto& line : lines) {
            const auto bounds = line.bounds.intersected(crop.rect());
            for (int y = bounds.top(); y <= bounds.bottom(); ++y)
                std::fill_n(textMask.begin() + static_cast<std::size_t>(y) * crop.width() + bounds.left(),
                            bounds.width(), static_cast<unsigned char>(1));
        }
    }

    OcrResult recognize(const QImage& source, const QRect& selection,
                        OcrEngine& engine, CodeLocator& locator,
                        const QString& detectorPath, const GateConfig& config,
                        const std::function<bool()>& cancelled)
    {
        const auto began = Clock::now();
        const QImage crop = source.copy(selection);
        if (dimensions != crop.size()) { *this = {}; dimensions = crop.size(); }
        OcrResult raw = engine.recognizeCode(crop);

        const auto now = Clock::now();
        const auto age = now - scannedAt;
        // Cache geometry only; each confirmation still reads pixels from a new video frame.
        if (!scanned || age >= std::chrono::seconds(1) || layoutChanged(crop) ||
            (misses >= 2 && age >= std::chrono::milliseconds(250))) {
            if (cancelled()) return {};
            if (!locator.loaded()) locator.load(detectorPath);
            lines = locator.detectTextLines(crop, cancelled);
            rejectedLines.assign(lines.size(), {});
            scannedAt = Clock::now();
            scanned = true;
            complete = !cancelled() && lines.size() <= 256;
            misses = 0;
            rememberLayout(crop);
        }
        OcrResult result = raw;
        result.confidence = 0;
        QString chosenCode;
        if (complete) {
            for (std::size_t index = 0; index < lines.size(); ++index) {
                const auto& line = lines[index];
                if (cancelled()) return {};
                const QRect core = line.coreBounds.translated(selection.topLeft());
                const int padding = std::max(4, qRound(core.height() * 1.20 + 2));
                const QImage pixels = source.copy(core.adjusted(-padding, -padding, padding, padding)
                                                     .intersected(source.rect()));
                // Reuse only rejection of identical pixels. Valid codes are always read afresh.
                if (!rejectedLines[index].isNull() && rejectedLines[index] == pixels) continue;
                auto candidate = engine.recognizeDetectedCode(source, core);
                if (!accepted(candidate, config)) {
                    rejectedLines[index] = pixels;
                    continue;
                }
                rejectedLines[index] = {};
                const auto code = CodeGate::normalizeCandidate(candidate.text);
                if (!chosenCode.isEmpty() && code != chosenCode) {
                    result = raw;
                    result.confidence = 0;
                    chosenCode.clear();
                    break;
                }
                if (chosenCode.isEmpty() || candidate.confidence > result.confidence) {
                    result = std::move(candidate);
                    result.rawText = raw.rawText.isEmpty() ? raw.text : raw.rawText;
                    result.rawConfidence = raw.rawText.isEmpty() ? raw.confidence : raw.rawConfidence;
                    result.lineNormalized = true;
                }
                chosenCode = code;
            }
        }
        if (chosenCode.isEmpty()) ++misses;
        else misses = 0;
        result.inferenceMs = std::chrono::duration<double, std::milli>(Clock::now() - began).count();
        return result;
    }
};
}

RecognitionWorker::~RecognitionWorker() { stop(); }
void RecognitionWorker::start(QString modelDir, GateConfig config, QRectF roi,
                              std::function<void(Detection)> detected,
                              std::function<void(QString)> status, int sampleFps, bool enabled)
{
    stop();
    {
        std::lock_guard lock(mutex_);
        stopping_ = false; pending_ = false; latest_ = {}; roi_ = roi; revision_ = 0;
        sampleFps_ = std::clamp(sampleFps, 5, 60); enabled_ = enabled;
        config_ = config;
        locating_ = false; locationCallback_ = {};
    }
    thread_ = std::thread([this, modelDir, config, detected, status] {
        const auto report = [this, &status](const QString& message) {
            std::lock_guard callbackLock(callbacks_);
            { std::lock_guard lock(mutex_); if (stopping_) return; }
            if (status) status(message);
        };
        try {
            OcrEngine engine;
            engine.load(QDir(modelDir).filePath("recognizer.onnx"), QDir(modelDir).filePath("keys.txt"));
            report(QStringLiteral("\u8bc6\u522b\u6a21\u578b\u5df2\u5c31\u7eea"));
            CodeLocator locator;
            TextLineCache textLines;
            QSize activeFrameSize;
            CodeGate gate(config);
            std::uint64_t activeRevision = 0, processedFrames = 0;
            auto nextSample = Clock::now();
            std::deque<Clock::time_point> processedAt;
            while (true) {
                VideoFrame frame;
                QRectF roi;
                GateConfig taskConfig;
                std::uint64_t revision = 0;
                std::function<void(Location)> locateCallback;
                {
                    std::unique_lock lock(mutex_);
                    changed_.wait(lock, [this] {
                        return stopping_ || locationCallback_ || (enabled_ && pending_);
                    });
                    if (stopping_) break;
                    if (locationCallback_) {
                        locateCallback = std::exchange(locationCallback_, {});
                        revision = locationRevision_;
                    } else {
                        if (revision_ != activeRevision) nextSample = Clock::now();
                        if (Clock::now() < nextSample) {
                            const auto waitingRevision = revision_;
                            changed_.wait_until(lock, nextSample, [this, waitingRevision] {
                                return stopping_ || locationCallback_ || !enabled_ || revision_ != waitingRevision;
                            });
                            continue;
                        }
                        revision = revision_; roi = roi_; pending_ = false;
                        nextSample = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                            std::chrono::duration<double>(1.0 / sampleFps_));
                    }
                    frame = latest_;
                    taskConfig = config_;
                }
                if (locateCallback) {
                    Location location;
                    location.revision = revision;
                    const auto cancelled = [this, revision] {
                        std::lock_guard lock(mutex_);
                        return stopping_ || revision != revision_;
                    };
                    try {
                        if (!cancelled()) {
                            if (!locator.loaded()) locator.load(QDir(modelDir).filePath("detector.onnx"));
                            const auto result = locator.locate(frame.image, engine, taskConfig, cancelled);
                            location.roi = result.roi; location.message = result.message;
                        }
                    } catch (const std::exception& error) {
                        location.message = QStringLiteral("\u5b9a\u4f4d\u5931\u8d25: %1")
                                               .arg(QString::fromUtf8(error.what()));
                    }
                    std::lock_guard callbackLock(callbacks_);
                    {
                        std::lock_guard lock(mutex_);
                        locating_ = false;
                        if (stopping_) break;
                        if (revision != revision_) continue;
                    }
                    locateCallback(std::move(location));
                    continue;
                }
                if (revision != activeRevision) {
                    gate = CodeGate(taskConfig); processedAt.clear(); activeRevision = revision;
                    textLines = {};
                }
                if (frame.image.isNull()) continue;
                const QSize dimensions = frame.image.size();
                if (dimensions != activeFrameSize) {
                    gate = CodeGate(taskConfig);
                    textLines = {};
                    activeFrameSize = dimensions;
                }
                const QRect cropRect(qRound(roi.x() * dimensions.width()), qRound(roi.y() * dimensions.height()),
                                     qRound(roi.width() * dimensions.width()), qRound(roi.height() * dimensions.height()));
                const QRect selection = cropRect.intersected(frame.image.rect());
                Detection detection;
                detection.receivedAt = frame.receivedAt; detection.revision = revision;
                const auto cancelled = [this, revision] {
                    std::lock_guard lock(mutex_);
                    return stopping_ || !enabled_ || revision != revision_;
                };
                detection.result = textLines.recognize(frame.image, selection, engine, locator,
                    QDir(modelDir).filePath("detector.onnx"), taskConfig, cancelled);
                const auto now = Clock::now();
                detection.processingMs = std::chrono::duration<double, std::milli>(now - frame.receivedAt).count();
                detection.confirmed = gate.observe(detection.result, frame.sequence, frame.receivedAt, now);
                detection.processedFrames = ++processedFrames;
                processedAt.push_back(now);
                while (processedAt.size() > 1 && now - processedAt.front() > std::chrono::seconds(1))
                    processedAt.pop_front();
                if (processedAt.size() > 1) {
                    detection.actualFps = (processedAt.size() - 1) /
                        std::chrono::duration<double>(now - processedAt.front()).count();
                }
                // Serialize callback admission with revision changes and stop, without holding the frame mutex.
                std::lock_guard callbackLock(callbacks_);
                {
                    std::lock_guard lock(mutex_);
                    if (stopping_) break;
                    if (!enabled_ || revision != revision_) continue;
                }
                if (detected) detected(std::move(detection));
            }
        } catch (const std::exception& error) {
            report(QStringLiteral("\u8bc6\u522b\u9519\u8bef: %1").arg(QString::fromUtf8(error.what())));
        }
    });
}

void RecognitionWorker::offer(VideoFrame frame)
{
    { std::lock_guard lock(mutex_); if (stopping_) return; latest_ = std::move(frame); pending_ = true; }
    changed_.notify_one();
}
std::uint64_t RecognitionWorker::setRoi(QRectF roi)
{
    std::lock_guard callbackLock(callbacks_);
    std::uint64_t revision;
    {
        std::lock_guard lock(mutex_);
        roi_ = roi; pending_ = false;
        if (locationCallback_) { locationCallback_ = {}; locating_ = false; }
        revision = ++revision_;
    }
    changed_.notify_all();
    return revision;
}
std::uint64_t RecognitionWorker::setEnabled(bool enabled)
{
    std::lock_guard callbackLock(callbacks_);
    std::uint64_t revision;
    {
        std::lock_guard lock(mutex_);
        enabled_ = enabled; pending_ = false;
        if (locationCallback_) { locationCallback_ = {}; locating_ = false; }
        revision = ++revision_;
    }
    changed_.notify_all();
    return revision;
}
std::uint64_t RecognitionWorker::setSamplingFps(int sampleFps)
{
    std::lock_guard callbackLock(callbacks_);
    std::uint64_t revision;
    {
        std::lock_guard lock(mutex_);
        sampleFps_ = std::clamp(sampleFps, 5, 60); pending_ = false;
        if (locationCallback_) { locationCallback_ = {}; locating_ = false; }
        revision = ++revision_;
    }
    changed_.notify_all();
    return revision;
}
std::uint64_t RecognitionWorker::setGateConfig(GateConfig config)
{
    (void)CodeGate(config);
    std::lock_guard callbackLock(callbacks_);
    std::uint64_t revision;
    {
        std::lock_guard lock(mutex_);
        config_ = config; pending_ = false;
        if (locationCallback_) { locationCallback_ = {}; locating_ = false; }
        revision = ++revision_;
    }
    changed_.notify_all();
    return revision;
}
bool RecognitionWorker::requestLocation(std::function<void(Location)> callback)
{
    {
        std::lock_guard lock(mutex_);
        if (stopping_ || locating_ || latest_.image.isNull() || !callback) return false;
        locationCallback_ = std::move(callback); locationRevision_ = revision_; locating_ = true;
    }
    changed_.notify_one();
    return true;
}
VideoFrame RecognitionWorker::latest() const { std::lock_guard lock(mutex_); return latest_; }
void RecognitionWorker::stop()
{
    {
        std::lock_guard callbackLock(callbacks_);
        std::lock_guard lock(mutex_);
        stopping_ = true; pending_ = false; locationCallback_ = {};
    }
    changed_.notify_all();
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) thread_.join();
}
}
