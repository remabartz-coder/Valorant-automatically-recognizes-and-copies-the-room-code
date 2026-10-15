#include "recognition_worker.h"
#include "code_locator.h"
#include "ocr_engine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

using namespace queuepilot;
using namespace std::chrono_literals;

namespace {
constexpr int frameCount = 157;
constexpr auto frameInterval = 250ms;
constexpr int firstVisibleKogFrame = 13;
const QRect inputBounds(648, 762, 590, 78);
const QRect dialogBounds(592, 495, 1236, 678);

struct CodeSpan {
    int first;
    int last;
    const char* code;
};

// Indices are zero-based output positions of ffmpeg's fps=4 filter, read from the source pixels.
const std::array<CodeSpan, 8> truth{{
    {12, 29, "KOG460"}, {47, 62, "ZUJ215"}, {76, 90, "TEJ065"},
    {101, 108, "ZPC960"}, {113, 117, "DJI798"}, {123, 126, "GJL089"},
    {132, 135, "EJQ559"}, {141, 144, "EHQ220"}
}};

QString expectedCode(int index)
{
    for (const auto& span : truth)
        if (index >= span.first && index <= span.last) return QString::fromLatin1(span.code);
    return {};
}

QRectF normalized(const QRect& bounds, const QSize& image)
{
    return {static_cast<double>(bounds.x()) / image.width(),
            static_cast<double>(bounds.y()) / image.height(),
            static_cast<double>(bounds.width()) / image.width(),
            static_cast<double>(bounds.height()) / image.height()};
}

QString framePath(const QString& directory, int index)
{
    return QDir(directory).filePath(QStringLiteral("frame-%1.png").arg(index + 1, 3, 10, QLatin1Char('0')));
}

struct Input {
    int index = -1;
    QString expected;
    QString phase;
};

struct Observation {
    Input input;
    Detection detection;
    Clock::time_point observedAt;
};

struct Record {
    std::mutex mutex;
    std::condition_variable changed;
    std::map<Clock::time_point, Input> inputs;
    std::vector<Observation> observations;
    std::vector<QString> statuses;
    bool ready = false;
    bool error = false;
    Clock::time_point lastObservedAt{};

    void registerInput(Clock::time_point receivedAt, Input input)
    {
        std::lock_guard lock(mutex);
        inputs.emplace(receivedAt, std::move(input));
    }

    void detect(Detection detection)
    {
        {
            std::lock_guard lock(mutex);
            const auto found = inputs.find(detection.receivedAt);
            if (found == inputs.end()) {
                error = true;
                statuses.push_back(QStringLiteral("Detection has no offered frame timestamp."));
            } else {
                lastObservedAt = detection.receivedAt;
                observations.push_back({found->second, std::move(detection), Clock::now()});
            }
        }
        changed.notify_all();
    }

    void status(QString message)
    {
        {
            std::lock_guard lock(mutex);
            ready |= message.contains(QStringLiteral("\u5df2\u5c31\u7eea"));
            error |= message.startsWith(QStringLiteral("\u8bc6\u522b\u9519\u8bef"));
            statuses.push_back(std::move(message));
        }
        changed.notify_all();
    }

    bool waitReady(Clock::time_point deadline)
    {
        std::unique_lock lock(mutex);
        return changed.wait_until(lock, deadline, [this] { return ready || error; }) && ready && !error;
    }

    void waitUntilObserved(Clock::time_point receivedAt, Clock::time_point deadline)
    {
        std::unique_lock lock(mutex);
        changed.wait_until(lock, deadline, [&] { return lastObservedAt >= receivedAt || error; });
    }
};

double percentile(std::vector<double> values, double quantile)
{
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>((values.size() - 1) * quantile);
    return values[index];
}

QJsonArray rectangle(const QRect& bounds)
{
    return {bounds.x(), bounds.y(), bounds.width(), bounds.height()};
}

QJsonObject summarize(const char* name, const QRect& roi, const Record& record, int& failures)
{
    QJsonArray rows;
    QJsonArray errors;
    QJsonArray confirmations;
    QJsonArray transitions;
    std::set<QString> confirmed;
    std::vector<double> processing;
    std::map<QString, Clock::time_point> firstVisible;
    for (const auto& [capturedAt, input] : record.inputs)
        if (input.phase == QStringLiteral("real") && !input.expected.isEmpty())
            firstVisible.try_emplace(input.expected, capturedAt);

    int falseConfirmations = 0;
    int realDetections = 0;
    std::map<QString, int> negativeDetections;
    for (const auto& observation : record.observations) {
        const auto& input = observation.input;
        const auto& detection = observation.detection;
        const QString code = detection.confirmed.value_or(QString{});
        if (input.phase == QStringLiteral("real")) {
            ++realDetections;
            processing.push_back(detection.processingMs);
        } else {
            ++negativeDetections[input.phase];
        }
        QJsonObject row{{"phase", input.phase}, {"frame_index", input.index},
            {"source_sample_seconds", input.index >= 0 ? input.index * .25 : -1},
            {"expected", input.expected}, {"candidate", detection.result.text},
            {"confidence", static_cast<double>(detection.result.confidence)},
            {"processing_ms", detection.processingMs}, {"confirmed", code}};
        rows.append(row);
        if (!code.isEmpty()) {
            confirmations.append(row);
            if (input.phase != QStringLiteral("real") || code != input.expected) {
                ++falseConfirmations;
                std::fprintf(stderr, "FAIL: %s confirmed %s in %s frame %d (expected %s).\n", name,
                    code.toUtf8().constData(), input.phase.toUtf8().constData(), input.index,
                    input.expected.toUtf8().constData());
            } else if (!confirmed.insert(code).second) {
                ++failures;
                std::fprintf(stderr, "FAIL: %s repeated confirmation of %s.\n", name, code.toUtf8().constData());
            } else {
                const double firstMs = std::chrono::duration<double, std::milli>(
                    observation.observedAt - firstVisible.at(code)).count();
                transitions.append(QJsonObject{{"code", code},
                    {"first_sample_to_confirmation_ms", firstMs}, {"confirm_frame_index", input.index}});
                std::printf("%s: %s confirmed at sampled %.2f s, first visible sample + %.1f ms, frame %.1f ms.\n",
                    name, code.toUtf8().constData(), input.index * .25, firstMs, detection.processingMs);
            }
        }
    }
    failures += falseConfirmations;
    for (const auto& span : truth) {
        if (!confirmed.contains(QString::fromLatin1(span.code))) {
            ++failures;
            std::fprintf(stderr, "FAIL: %s did not confirm visible real code %s.\n", name, span.code);
        }
    }
    for (const char* phase : {"blank", "real-dots", "two-real-codes"}) {
        if (negativeDetections[QString::fromLatin1(phase)] < 2) {
            ++failures;
            std::fprintf(stderr, "FAIL: %s did not process at least two %s frames.\n", name, phase);
        }
    }
    if (realDetections < 20 || record.error) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s worker failed or processed insufficient real frames.\n", name);
    }
    for (const auto& status : record.statuses) errors.append(status);
    const double p50 = percentile(processing, .50), p95 = percentile(processing, .95);
    std::printf("%s: %d real detections, %zu/8 codes, %d false confirmations, frame p50 %.1f ms / p95 %.1f ms.\n",
        name, realDetections, confirmed.size(), falseConfirmations, p50, p95);
    return {{"roi_pixels", rectangle(roi)}, {"real_detections", realDetections},
        {"confirmed_code_count", static_cast<int>(confirmed.size())}, {"false_confirmations", falseConfirmations},
        {"frame_processing_p50_ms", p50}, {"frame_processing_p95_ms", p95},
        {"transitions", transitions}, {"confirmations", confirmations}, {"observations", rows},
        {"statuses", errors}};
}
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    const auto arguments = application.arguments();
    if (arguments.size() < 3 || arguments.size() > 4) {
        std::fprintf(stderr, "Usage: real_video_tests <models-dir> <frames-4fps-dir> [report.json]\n");
        return 2;
    }
    const QString models = QFileInfo(arguments[1]).absoluteFilePath();
    const QString frames = QFileInfo(arguments[2]).absoluteFilePath();
    const QString reportPath = arguments.size() == 4 ? arguments[3]
        : QDir(frames).filePath(QStringLiteral("recognition-report.json"));
    const QImage first(framePath(frames, 0));
    if (first.size() != QSize(2420, 1668)) {
        std::fprintf(stderr, "Expected 2420x1668 source frames from the supplied recording.\n");
        return 2;
    }
    for (int index = 0; index < frameCount; ++index) {
        if (!QFileInfo::exists(framePath(frames, index))) {
            std::fprintf(stderr, "Missing real frame %d; extract the full recording with fps=4.\n", index + 1);
            return 2;
        }
    }

    int failures = 0;
    QJsonArray automaticLocation;
    {
        OcrEngine engine;
        CodeLocator locator;
        engine.load(QDir(models).filePath(QStringLiteral("recognizer.onnx")),
                    QDir(models).filePath(QStringLiteral("keys.txt")));
        locator.load(QDir(models).filePath(QStringLiteral("detector.onnx")));
        for (const int index : {16, 52, 84, 142}) {
            const QImage image(framePath(frames, index));
            const QString expected = expectedCode(index);
            const auto began = Clock::now();
            const auto located = locator.locate(image, engine, GateConfig{});
            const double elapsedMs = std::chrono::duration<double, std::milli>(Clock::now() - began).count();
            OcrResult verified;
            QRect pixels;
            if (located.roi) {
                const auto& roi = *located.roi;
                pixels = QRect(qRound(roi.x() * image.width()), qRound(roi.y() * image.height()),
                               qRound(roi.width() * image.width()), qRound(roi.height() * image.height()));
                verified = engine.recognizeCode(image.copy(pixels.intersected(image.rect())));
            }
            const bool passed = located.roi && located.code == expected &&
                CodeGate::normalizeCandidate(verified.text) == expected && verified.confidence >= .90f;
            if (!passed) {
                ++failures;
                std::fprintf(stderr, "FAIL: full-frame location at %d expected %s, found %s, crop %s %.4f.\n",
                    index, expected.toUtf8().constData(), located.code.toUtf8().constData(),
                    verified.text.toUtf8().constData(), verified.confidence);
            }
            automaticLocation.append(QJsonObject{{"frame_index", index}, {"expected", expected},
                {"code", located.code}, {"roi_pixels", rectangle(pixels)},
                {"location_ms", elapsedMs}, {"roi_candidate", verified.text},
                {"roi_confidence", static_cast<double>(verified.confidence)}, {"passed", passed}});
            std::printf("Full-frame location: %s -> %s, %.1f ms; ROI %s %.4f.\n",
                expected.toUtf8().constData(), located.code.toUtf8().constData(), elapsedMs,
                verified.text.toUtf8().constData(), verified.confidence);
        }
    }

    const auto deadline = Clock::now() + 60s;
    Record inputRecord, dialogRecord;
    RecognitionWorker inputWorker, dialogWorker;
    const GateConfig config{2, .90f, false, 500};
    const auto startWorker = [&](RecognitionWorker& worker, Record& record, const QRect& bounds) {
        worker.start(models, config, normalized(bounds, first.size()),
            [&record](Detection detection) { record.detect(std::move(detection)); },
            [&record](QString status) { record.status(std::move(status)); }, 30, true);
    };
    startWorker(inputWorker, inputRecord, inputBounds);
    startWorker(dialogWorker, dialogRecord, dialogBounds);
    if (!inputRecord.waitReady(Clock::now() + 5s) || !dialogRecord.waitReady(Clock::now() + 5s)) {
        std::fprintf(stderr, "Workers could not load recognition models.\n");
        return 1;
    }

    std::uint64_t sequence = 0;
    Clock::time_point lastReceived;
    const auto offer = [&](const QImage& image, Input input, double mediaSeconds) {
        const auto receivedAt = Clock::now();
        inputRecord.registerInput(receivedAt, input);
        dialogRecord.registerInput(receivedAt, std::move(input));
        const VideoFrame frame{image, ++sequence, receivedAt, mediaSeconds};
        inputWorker.offer(frame);
        dialogWorker.offer(frame);
        lastReceived = receivedAt;
    };

    const auto replayStarted = Clock::now();
    int offeredReal = 0;
    for (int index = 0; index < frameCount && Clock::now() < deadline; ++index) {
        const QImage image(framePath(frames, index));
        if (image.size() != first.size()) {
            std::fprintf(stderr, "Invalid dimensions in real frame %d.\n", index + 1);
            return 2;
        }
        std::this_thread::sleep_until(replayStarted + frameInterval * index);
        offer(image, {index, expectedCode(index), QStringLiteral("real")}, index * .25);
        ++offeredReal;
    }
    inputRecord.waitUntilObserved(lastReceived, Clock::now() + 1s);
    dialogRecord.waitUntilObserved(lastReceived, Clock::now() + 1s);

    const QImage kog(framePath(frames, firstVisibleKogFrame));
    const QImage tej(framePath(frames, 80));
    const QImage dots(framePath(frames, 69));
    QImage blank(first.size(), QImage::Format_RGB32);
    blank.fill(QColor(16, 22, 28));
    QImage multiple = kog.convertToFormat(QImage::Format_RGB32);
    {
        QPainter painter(&multiple);
        // Retain source-code pixels; put the second visible real code in the lower input field.
        painter.drawImage(QPoint(inputBounds.x(), 913), tej, inputBounds);
    }
    const std::array<std::pair<QString, QImage>, 3> negatives{{
        {QStringLiteral("blank"), blank}, {QStringLiteral("real-dots"), dots},
        {QStringLiteral("two-real-codes"), multiple}
    }};
    for (const auto& [phase, image] : negatives) {
        inputWorker.setRoi(normalized(dialogBounds, first.size()));
        dialogWorker.setRoi(normalized(dialogBounds, first.size()));
        const auto began = Clock::now();
        for (int index = 0; index < 8 && Clock::now() < deadline; ++index) {
            std::this_thread::sleep_until(began + frameInterval * index);
            offer(image, {-1, {}, phase}, -1);
        }
        inputRecord.waitUntilObserved(lastReceived, Clock::now() + 1s);
        dialogRecord.waitUntilObserved(lastReceived, Clock::now() + 1s);
    }
    inputWorker.stop();
    dialogWorker.stop();

    if (offeredReal != frameCount || Clock::now() > deadline) {
        ++failures;
        std::fprintf(stderr, "FAIL: regression exceeded its 60-second deadline or skipped source frames.\n");
    }
    QJsonObject workers;
    workers.insert(QStringLiteral("wide-input"), summarize("wide-input", inputBounds, inputRecord, failures));
    workers.insert(QStringLiteral("whole-dialog"), summarize("whole-dialog", dialogBounds, dialogRecord, failures));
    QJsonArray labels;
    for (const auto& span : truth) labels.append(QJsonObject{{"first_index", span.first},
        {"last_index", span.last}, {"code", QString::fromLatin1(span.code)}});
    const QJsonObject report{{"source", QStringLiteral("ScreenRecording_10-15-2026 09-40-44_1.mp4")},
        {"frames_directory", frames}, {"source_frame_count", frameCount}, {"replay_interval_ms", 250},
        {"confidence_threshold", .90}, {"confirmations", 2}, {"system_output", false},
        {"truth", labels}, {"automatic_location", automaticLocation}, {"workers", workers}, {"failures", failures},
        {"latency_scope", QStringLiteral("Local processing and first visible sampled frame to confirmation; excludes live transport, display delay and sampling quantization.")},
        {"negative_scope", QStringLiteral("Blank, real masked-code frame, and two codes composed from source pixels; repeated stills test rejection and are not independent recording frames.")}};
    QFile output(reportPath);
    if (!output.open(QIODevice::WriteOnly) || output.write(QJsonDocument(report).toJson()) < 0) {
        std::fprintf(stderr, "Could not save report: %s\n", reportPath.toUtf8().constData());
        return 1;
    }
    std::printf("Report: %s\n", reportPath.toUtf8().constData());
    return failures == 0 ? 0 : 1;
}
