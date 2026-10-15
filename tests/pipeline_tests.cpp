#include "recognition_worker.h"

#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QPainter>
#include <QRawFont>

#include <chrono>
#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <exception>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace queuepilot;

namespace {
int failures = 0;
Clock::time_point overallDeadline;

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

QImage codeImage(const QString& code = QStringLiteral("TYH663"), int pixels = 64)
{
    QFont font(QStringLiteral("Segoe UI"));
    font.setPixelSize(pixels);
    font.setWeight(QFont::DemiBold);
    const QRawFont rawFont = QRawFont::fromFont(font);
    if (!rawFont.isValid()) {
        throw std::runtime_error("Test font could not be loaded by the Qt platform");
    }
    for (const QChar character : code) {
        if (!rawFont.supportsCharacter(character)) {
            throw std::runtime_error("Test font is missing a room-code glyph");
        }
    }
    const QRect bounds = QFontMetrics(font).tightBoundingRect(code);
    constexpr int margin = 10;
    QImage image(bounds.width() + 2 * margin, bounds.height() + 2 * margin,
                 QImage::Format_RGB888);
    image.fill(QColor(25, 25, 25));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setFont(font);
    painter.setPen(QColor(250, 250, 250));
    painter.drawText(QPoint(margin - bounds.left(), margin - bounds.top()), code);
    return image;
}

QImage denseImage(bool secondCode = false)
{
    QImage image(960, 720, QImage::Format_RGB888);
    image.fill(QColor(25, 25, 25));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setPen(QColor(250, 250, 250));
    QFont font(QStringLiteral("Segoe UI"));
    font.setPixelSize(30);
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    for (int row = 0; row < 6; ++row)
        for (int column = 0; column < 5; ++column)
            painter.drawText(QPoint(30 + column * 180, 45 + row * 90), QStringLiteral("PLAYERS"));
    font.setPixelSize(40);
    painter.setFont(font);
    painter.drawText(QPoint(100, 660), QStringLiteral("TYH663"));
    if (secondCode) painter.drawText(QPoint(600, 660), QStringLiteral("ABC123"));
    return image;
}

struct Record {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<Detection> detections;
    std::vector<Location> locations;
    bool ready = false;
    bool error = false;

    void detect(Detection detection)
    {
        {
            std::lock_guard lock(mutex);
            detections.push_back(std::move(detection));
        }
        changed.notify_all();
    }

    void status(const QString& message)
    {
        {
            std::lock_guard lock(mutex);
            ready = ready || message.contains(QStringLiteral("\u5df2\u5c31\u7eea"));
            error = error || message.startsWith(QStringLiteral("\u8bc6\u522b\u9519\u8bef"));
            if (error) {
                std::fprintf(stderr, "Worker error: %s\n", message.toUtf8().constData());
            }
        }
        changed.notify_all();
    }

    void located(Location location)
    {
        { std::lock_guard lock(mutex); locations.push_back(std::move(location)); }
        changed.notify_all();
    }

    bool wait(const std::function<bool(const Record&)>& predicate)
    {
        std::unique_lock lock(mutex);
        const auto deadline = std::min(overallDeadline, Clock::now() + std::chrono::seconds(4));
        const bool arrived = changed.wait_until(lock, deadline, [&] {
            return error || predicate(*this);
        });
        return arrived && !error;
    }

    std::size_t count()
    {
        std::lock_guard lock(mutex);
        return detections.size();
    }

    std::size_t confirmedCountLocked() const
    {
        std::size_t count = 0;
        for (const auto& detection : detections) {
            count += detection.confirmed.has_value();
        }
        return count;
    }

    std::size_t confirmedCount()
    {
        std::lock_guard lock(mutex);
        return confirmedCountLocked();
    }
};

bool start(RecognitionWorker& worker, Record& record, const QString& modelsDir,
           int sampleFps = 30, bool enabled = true)
{
    worker.start(modelsDir, {}, QRectF(0, 0, 1, 1),
                 [&record](Detection detection) { record.detect(std::move(detection)); },
                 [&record](QString status) { record.status(status); }, sampleFps, enabled);
    const bool ready = record.wait([](const Record& value) { return value.ready; });
    expect(ready, "recognition model becomes ready within the bounded test budget");
    return ready;
}

bool offerAndWait(RecognitionWorker& worker, Record& record, const QImage& image,
                  std::uint64_t sequence, Clock::time_point capturedAt)
{
    const auto before = record.count();
    worker.offer({image, sequence, capturedAt, 0});
    const bool completed = record.wait([before, capturedAt](const Record& value) {
        for (std::size_t index = before; index < value.detections.size(); ++index) {
            if (value.detections[index].receivedAt == capturedAt) return true;
        }
        return false;
    });
    expect(completed, "offered frame completes recognition within the bounded test budget");
    return completed;
}

void testConfirmation(const QString& modelsDir, const QImage& image)
{
    Record record;
    RecognitionWorker worker;
    if (!start(worker, record, modelsDir)) return;
    if (!offerAndWait(worker, record, image, 1, Clock::now())) return;
    expect(record.confirmedCount() == 0, "one video frame does not confirm a room code");
    if (!offerAndWait(worker, record, image, 2, Clock::now())) return;
    expect(record.confirmedCount() == 1, "two independent frames confirm exactly once");
    {
        std::lock_guard lock(record.mutex);
        if (!record.detections.empty()) {
            const auto& last = record.detections.back();
            expect(last.confirmed && *last.confirmed == QStringLiteral("TYH663"),
                   "worker confirms the expected code");
            expect(last.revision == 0, "initial ROI revision is attached to detection");
            expect(last.processingMs >= last.result.inferenceMs, "processing time includes inference");
            std::printf("Pipeline code=%s confidence=%.4f inference=%.2fms processing=%.2fms\n",
                        last.result.text.toUtf8().constData(), last.result.confidence,
                        last.result.inferenceMs, last.processingMs);
        }
    }
    if (!offerAndWait(worker, record, image, 2, Clock::now())) return;
    expect(record.confirmedCount() == 1, "duplicate frame cannot publish again");
    const auto latest = worker.latest();
    expect(latest.sequence == 2 && latest.image.size() == image.size(), "latest frame slot tracks offered frame");
    worker.stop();
}

void testStaleBurst(const QString& modelsDir, const QImage& image)
{
    Record record;
    RecognitionWorker worker;
    if (!start(worker, record, modelsDir)) return;
    const auto stale = Clock::now() - std::chrono::seconds(2);
    for (std::uint64_t sequence = 1; sequence <= 150; ++sequence) {
        worker.offer({image, sequence, stale, 0});
    }
    const bool received = record.wait([](const Record& value) { return !value.detections.empty(); });
    expect(received, "stale burst reaches recognition");
    expect(record.confirmedCount() == 0, "more than one hundred stale frames never confirm");
    expect(worker.latest().sequence == 150, "burst keeps the newest offered frame");
    if (!offerAndWait(worker, record, image, 200, Clock::now())) return;
    expect(record.confirmedCount() == 0, "stale burst cannot prime a fresh confirmation");
    if (!offerAndWait(worker, record, image, 201, Clock::now())) return;
    expect(record.confirmedCount() == 1, "fresh frames can confirm after stale burst");
    worker.stop();
}

void testRoiRevision(const QString& modelsDir, const QImage& image)
{
    Record record;
    RecognitionWorker worker;
    if (!start(worker, record, modelsDir)) return;
    if (!offerAndWait(worker, record, image, 1, Clock::now())) return;
    expect(record.confirmedCount() == 0, "ROI test starts with one unconfirmed frame");
    const auto revision = worker.setRoi(QRectF(0, 0, 1, 1));
    expect(revision == 1, "changing ROI increments revision");
    if (!offerAndWait(worker, record, image, 2, Clock::now())) return;
    expect(record.confirmedCount() == 0, "ROI change invalidates previous confirmation");
    if (!offerAndWait(worker, record, image, 3, Clock::now())) return;
    expect(record.confirmedCount() == 1, "new ROI requires two fresh frames");
    {
        std::lock_guard lock(record.mutex);
        if (!record.detections.empty()) {
            expect(record.detections.back().revision == revision, "new ROI revision accompanies result");
        }
    }
    worker.stop();
}

void testIsolatedLineNormalization(const QString& modelsDir)
{
    const QImage text = codeImage(QStringLiteral("AZI973"), 42);
    QImage image(text.width() + 100, text.height() + 80, QImage::Format_RGB32);
    image.fill(text.pixelColor(0, 0));
    {
        QPainter painter(&image);
        painter.drawImage(50, 40, text);
        painter.setPen(QPen(QColor(30, 220, 160), 1));
        painter.drawRect(image.rect().adjusted(1, 1, -2, -2));
    }
    Record record;
    RecognitionWorker worker;
    if (!start(worker, record, modelsDir)) return;
    if (!offerAndWait(worker, record, image, 1, Clock::now())) return;
    expect(record.confirmedCount() == 0, "normalization does not replace independent frame confirmation");
    if (!offerAndWait(worker, record, image, 1, Clock::now())) return;
    expect(record.confirmedCount() == 0, "normalized duplicate frame cannot confirm");
    if (!offerAndWait(worker, record, image, 2, Clock::now())) return;
    expect(record.confirmedCount() == 1, "two independent normalized frames can confirm");
    {
        std::lock_guard lock(record.mutex);
        const auto& detection = record.detections.back();
        expect(detection.confirmed == QStringLiteral("AZI973"), "normalized code matches the rendered code");
        expect(detection.result.lineNormalized && !detection.result.rawText.isEmpty(),
               "normalization retains raw OCR evidence in the worker callback");
        expect(detection.result.confidence >= .90f && detection.result.rawConfidence < .90f,
               "normalization recovers the loose region without lowering the confidence gate");
    }
    worker.setRoi(QRectF(0, 0, 1, 1));
    const auto stale = Clock::now() - std::chrono::seconds(2);
    if (!offerAndWait(worker, record, image, 3, stale)) return;
    if (!offerAndWait(worker, record, image, 4, stale)) return;
    expect(record.confirmedCount() == 1, "stale normalized frames cannot confirm");
    if (!offerAndWait(worker, record, image, 5, Clock::now())) return;
    expect(record.confirmedCount() == 1, "stale normalization cannot prime a fresh confirmation");
    if (!offerAndWait(worker, record, image, 6, Clock::now())) return;
    expect(record.confirmedCount() == 2, "fresh normalized frames confirm after rejecting stale work");
    worker.stop();
}

void testResolutionChange(const QString& modelsDir, const QImage& image)
{
    Record record;
    RecognitionWorker worker;
    if (!start(worker, record, modelsDir)) return;
    if (!offerAndWait(worker, record, image, 1, Clock::now())) return;
    QImage resized(image.width() + 4, image.height() + 4, QImage::Format_RGB32);
    resized.fill(image.pixelColor(0, 0));
    {
        QPainter painter(&resized);
        painter.drawImage(2, 2, image);
    }
    if (!offerAndWait(worker, record, resized, 2, Clock::now())) return;
    expect(record.confirmedCount() == 0, "resolution change invalidates old geometry and partial confirmation");
    if (!offerAndWait(worker, record, resized, 3, Clock::now())) return;
    expect(record.confirmedCount() == 1, "new resolution confirms using independent new frames");
    worker.stop();
}

void testStop(const QString& modelsDir, const QImage& image)
{
    Record record;
    RecognitionWorker worker;
    if (!start(worker, record, modelsDir)) return;
    for (std::uint64_t sequence = 1; sequence <= 150; ++sequence) {
        worker.offer({image, sequence, Clock::now(), 0});
    }
    worker.stop();
    const auto afterStop = record.count();
    worker.offer({image, 151, Clock::now(), 0});
    {
        std::unique_lock lock(record.mutex);
        const bool callbackAfterStop = record.changed.wait_for(lock, std::chrono::milliseconds(150), [&] {
            return record.detections.size() != afterStop;
        });
        expect(!callbackAfterStop, "stop joins worker and rejects future frames without callbacks");
    }
    worker.stop();
}

void testPauseAndLocation(const QString& modelsDir, const QImage& image)
{
    Record record;
    RecognitionWorker worker;
    if (!start(worker, record, modelsDir, 30, false)) return;
    expect(!worker.requestLocation([&record](Location value) { record.located(std::move(value)); }),
           "location requires an available frame");
    for (std::uint64_t sequence = 1; sequence <= 50; ++sequence)
        worker.offer({image, sequence, Clock::now(), 0});
    {
        std::unique_lock lock(record.mutex);
        expect(!record.changed.wait_for(lock, std::chrono::milliseconds(150), [&] {
            return !record.detections.empty();
        }), "paused recognition receives frames without OCR callbacks");
    }
    expect(worker.latest().sequence == 50, "preview keeps updating while recognition is paused");
    expect(worker.requestLocation([&record](Location value) { record.located(std::move(value)); }),
           "manual location runs while recognition is paused");
    expect(!worker.requestLocation([&record](Location value) { record.located(std::move(value)); }),
           "only one location task can run at a time");
    expect(record.wait([](const Record& value) { return !value.locations.empty(); }),
           "paused location returns within the bounded test budget");
    {
        std::lock_guard lock(record.mutex);
        expect(record.detections.empty(), "location cannot emit a recognition or confirmation callback");
        if (!record.locations.empty()) {
            expect(record.locations.back().roi.has_value(), "paused location finds the rendered code");
            expect(record.locations.back().revision == 0, "location carries the current revision");
        }
    }
    const auto resumed = worker.setEnabled(true);
    expect(resumed == 1, "resume increments revision");
    if (!offerAndWait(worker, record, image, 60, Clock::now())) return;
    expect(record.confirmedCount() == 0, "resuming cannot use a paused frame to confirm");
    if (!offerAndWait(worker, record, image, 61, Clock::now())) return;
    expect(record.confirmedCount() == 1, "two fresh frames confirm after resume");
    const auto paused = worker.setEnabled(false);
    const auto count = record.count();
    worker.offer({image, 62, Clock::now(), 0});
    {
        std::unique_lock lock(record.mutex);
        expect(!record.changed.wait_for(lock, std::chrono::milliseconds(100), [&] {
            return record.detections.size() != count;
        }), "pause cancels OCR callback admission immediately");
    }
    expect(paused == 2, "pause increments revision");
    std::size_t locationCount;
    { std::lock_guard lock(record.mutex); locationCount = record.locations.size(); }
    expect(worker.requestLocation([&record](Location value) { record.located(std::move(value)); }),
           "a new location can run after the first completes");
    expect(worker.setRoi(QRectF(0, 0, 1, 1)) == 3, "ROI revision cancels an outstanding location");
    {
        std::unique_lock lock(record.mutex);
        expect(!record.changed.wait_for(lock, std::chrono::milliseconds(150), [&] {
            return record.locations.size() != locationCount;
        }), "cancelled location cannot publish an outdated region");
    }
    worker.stop();
    expect(!worker.requestLocation([&record](Location value) { record.located(std::move(value)); }),
           "stopped worker cannot accept a location request");
}

void testSampling(const QString& modelsDir, const QImage& image)
{
    Record record;
    RecognitionWorker worker;
    if (!start(worker, record, modelsDir, 0)) return;
    const auto began = Clock::now();
    std::jthread producer([&worker, &image](std::stop_token stop) {
        std::uint64_t sequence = 0;
        while (!stop.stop_requested()) {
            worker.offer({image, ++sequence, Clock::now(), 0});
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    expect(record.wait([](const Record& value) { return value.detections.size() >= 4; }),
           "sampling processes a continuously arriving stream");
    {
        std::lock_guard lock(record.mutex);
        const double elapsed = std::chrono::duration<double>(Clock::now() - began).count();
        expect(record.detections.size() <= static_cast<std::size_t>(elapsed * 5 + 3),
               "sample rate clamps to five instead of processing every offered frame");
        if (record.detections.size() >= 4) {
            const auto& last = record.detections.back();
            expect(last.processedFrames >= 4, "processed-frame counter reports completed work");
            expect(last.actualFps > 3 && last.actualFps < 8, "actual FPS measures sampled processing");
        }
    }
    const auto revision = worker.setSamplingFps(999);
    expect(revision == 1, "changing sample rate invalidates old callbacks");
    expect(record.wait([revision](const Record& value) {
        int fresh = 0;
        for (const auto& detection : value.detections) fresh += detection.revision == revision;
        return fresh >= 3;
    }), "new sampling revision processes fresh frames");
    producer.request_stop();
    producer.join();
    worker.stop();
}

void testRuntimeGateConfig(const QString& modelsDir, const QImage& image)
{
    Record record;
    RecognitionWorker worker;
    if (!start(worker, record, modelsDir)) return;
    if (!offerAndWait(worker, record, image, 1, Clock::now())) return;
    GateConfig config;
    config.confirmations = 3;
    const auto revision = worker.setGateConfig(config);
    expect(revision == 1, "runtime gate settings increment revision");
    if (!offerAndWait(worker, record, image, 2, Clock::now())) return;
    if (!offerAndWait(worker, record, image, 3, Clock::now())) return;
    expect(record.confirmedCount() == 0, "new settings cannot use earlier confirmation frames");
    if (!offerAndWait(worker, record, image, 4, Clock::now())) return;
    expect(record.confirmedCount() == 1, "updated confirmation count takes effect without restarting capture");
    {
        std::lock_guard lock(record.mutex);
        expect(record.detections.back().revision == revision, "runtime configuration revision labels the result");
    }
    config.minConfidence = 1.0f;
    config.confirmations = 2;
    expect(worker.setGateConfig(config) == 2, "runtime confidence change increments revision");
    const auto beforeThresholdDetections = record.count();
    const auto beforeThresholdConfirmations = record.confirmedCount();
    if (!offerAndWait(worker, record, image, 5, Clock::now())) return;
    if (!offerAndWait(worker, record, image, 6, Clock::now())) return;
    {
        std::lock_guard lock(record.mutex);
        bool everyFrameAtThreshold = true;
        for (std::size_t index = beforeThresholdDetections; index < record.detections.size(); ++index) {
            const auto& value = record.detections[index];
            everyFrameAtThreshold = everyFrameAtThreshold && value.result.confidence >= config.minConfidence;
            if (value.result.confidence < config.minConfidence)
                expect(!value.confirmed, "confidence below the updated threshold cannot confirm");
        }
        expect(record.confirmedCountLocked() == beforeThresholdConfirmations + (everyFrameAtThreshold ? 1 : 0),
               "updated confidence threshold follows the measured model output, including saturated probabilities");
    }
    const auto afterThresholdConfirmations = record.confirmedCount();
    config.minConfidence = 0.90f;
    expect(worker.setGateConfig(config) == 3, "restoring confidence uses a fresh revision");
    if (!offerAndWait(worker, record, image, 7, Clock::now())) return;
    expect(record.confirmedCount() == afterThresholdConfirmations, "restored threshold still needs two fresh frames");
    if (!offerAndWait(worker, record, image, 8, Clock::now())) return;
    expect(record.confirmedCount() == afterThresholdConfirmations + 1, "restored threshold can confirm in the same worker session");
    const auto beforeFormatConfirmations = record.confirmedCount();
    const QImage unbalanced = codeImage(QStringLiteral("AB1234"));
    expect(worker.setGateConfig(config) == 4, "refreshing format rules starts a new revision");
    if (!offerAndWait(worker, record, unbalanced, 9, Clock::now())) return;
    if (!offerAndWait(worker, record, unbalanced, 10, Clock::now())) return;
    expect(record.confirmedCount() == beforeFormatConfirmations, "strict format rejects two letters and four digits");
    if (!offerAndWait(worker, record, unbalanced, 11, Clock::now())) return;
    if (!offerAndWait(worker, record, unbalanced, 12, Clock::now())) return;
    expect(record.confirmedCount() == beforeFormatConfirmations, "repeated unbalanced text never confirms");
    const auto beforeOrderedConfirmations = record.confirmedCount();
    const QImage mixedOrder = codeImage(QStringLiteral("H6T3Y6"));
    config.lettersFirst = true;
    expect(worker.setGateConfig(config) == 5, "ordered format can be enabled at runtime");
    if (!offerAndWait(worker, record, mixedOrder, 13, Clock::now())) return;
    if (!offerAndWait(worker, record, mixedOrder, 14, Clock::now())) return;
    expect(record.confirmedCount() == beforeOrderedConfirmations, "ordered format rejects a mixed-order code independently of confidence");
    {
        std::lock_guard lock(record.mutex);
        expect(record.detections.back().result.text == QStringLiteral("H6T3Y6"),
               "filtered worker detection retains its raw OCR text");
    }
    config.lettersFirst = false;
    expect(worker.setGateConfig(config) == 6, "balanced format can be restored after ordered rejection");
    if (!offerAndWait(worker, record, mixedOrder, 15, Clock::now())) return;
    if (!offerAndWait(worker, record, mixedOrder, 16, Clock::now())) return;
    expect(record.confirmedCount() == beforeOrderedConfirmations + 1,
           "restored general format confirms the previously filtered code");
    worker.stop();
}

void testDenseStream(const QString& modelsDir)
{
    const QImage image = denseImage();
    Record record;
    RecognitionWorker worker;
    if (!start(worker, record, modelsDir)) return;
    const auto began = Clock::now();
    std::jthread producer([&worker, &image](std::stop_token stop) {
        std::uint64_t sequence = 0;
        while (!stop.stop_requested()) {
            worker.offer({image, ++sequence, Clock::now(), 0});
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
    });
    bool confirmed = false;
    {
        std::unique_lock lock(record.mutex);
        confirmed = record.changed.wait_until(lock, std::min(overallDeadline, began + std::chrono::seconds(2)), [&] {
            return record.error || record.confirmedCountLocked() > 0;
        }) && !record.error && record.confirmedCountLocked() == 1;
        expect(confirmed, "a continuous 30 FPS dense stream confirms within two seconds");
        if (confirmed) {
            const auto found = std::find_if(record.detections.begin(), record.detections.end(), [](const auto& value) {
                return value.confirmed.has_value();
            });
            expect(found->confirmed == QStringLiteral("TYH663"), "dense stream confirms the complete room code");
            std::printf("Dense stream confirmed after %.0fms; cold=%.0fms confirmed-frame=%.0fms\n",
                        std::chrono::duration<double, std::milli>(Clock::now() - began).count(),
                        record.detections.front().processingMs, found->processingMs);
        }
    }
    producer.request_stop();
    producer.join();
    worker.stop();
}

void testNewCodeInvalidatesGeometry(const QString& modelsDir)
{
    Record record;
    RecognitionWorker worker;
    if (!start(worker, record, modelsDir)) return;
    if (!offerAndWait(worker, record, denseImage(), 1, Clock::now())) return;
    expect(record.confirmedCount() == 0, "one dense frame supplies only an unconfirmed candidate");
    const auto before = record.count();
    const QImage ambiguous = denseImage(true);
    std::jthread producer([&worker, &ambiguous](std::stop_token stop) {
        std::uint64_t sequence = 1;
        while (!stop.stop_requested()) {
            worker.offer({ambiguous, ++sequence, Clock::now(), 0});
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
    });
    const bool processed = record.wait([before](const Record& value) { return value.detections.size() >= before + 3; });
    expect(processed, "the stream processes new frames after a second code appears");
    producer.request_stop();
    producer.join();
    worker.stop();
    {
        std::lock_guard lock(record.mutex);
        expect(record.confirmedCountLocked() == 0, "new text invalidates cached geometry before a single code confirms");
        for (std::size_t index = before; index < record.detections.size(); ++index)
            expect(record.detections[index].result.confidence == 0,
                   "two current codes remain ambiguous on every processed frame");
    }
}
}

int main(int argc, char** argv)
{
#ifndef Q_OS_WIN
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
#endif
    QGuiApplication application(argc, argv);
    overallDeadline = Clock::now() + std::chrono::seconds(24);
    const auto arguments = application.arguments();
    if (arguments.size() != 2) {
        std::fprintf(stderr, "Usage: pipeline_tests MODELS_DIRECTORY\n");
        return 2;
    }
    try {
        const QImage image = codeImage();
        testConfirmation(arguments[1], image);
        testStaleBurst(arguments[1], image);
        testRoiRevision(arguments[1], image);
        testIsolatedLineNormalization(arguments[1]);
        testResolutionChange(arguments[1], image);
        testStop(arguments[1], image);
        testPauseAndLocation(arguments[1], image);
        testSampling(arguments[1], image);
        testRuntimeGateConfig(arguments[1], image);
        testDenseStream(arguments[1]);
        testNewCodeInvalidatesGeometry(arguments[1]);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Pipeline test error: %s\n", error.what());
        return 1;
    }
    if (failures == 0) {
        std::printf("All recognition pipeline checks passed.\n");
    }
    return failures == 0 ? 0 : 1;
}
