#include "stream_resolver.h"
#include "video_decoder.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QTimer>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>

namespace {
struct Observations {
    std::mutex mutex;
    std::condition_variable changed;
    std::uint64_t lastSequence = 0;
    std::uint64_t frames = 0;
    double firstMedia = 0;
    double lastMedia = 0;
    queuepilot::Clock::time_point firstReceived;
    queuepilot::Clock::time_point lastReceived;
    bool nonblank = false;
    bool invalidFrame = false;
    bool nonmonotonic = false;
    QString lastStatus;

    bool complete() const {
        return invalidFrame || nonmonotonic || (frames >= 8 && lastMedia - firstMedia >= 0.2);
    }
};

bool isNonblank(const QImage& image) {
    int minimum = 255;
    int maximum = 0;
    std::uint64_t sum = 0;
    for (int y = 0; y < image.height(); y += qMax(1, image.height() / 32)) {
        const auto* line = image.constScanLine(y);
        for (int x = 0; x < image.width(); x += qMax(1, image.width() / 32)) {
            const int offset = x * 3;
            for (int channel = 0; channel < 3; ++channel) {
                const int value = line[offset + channel];
                minimum = qMin(minimum, value);
                maximum = qMax(maximum, value);
                sum += static_cast<unsigned int>(value);
            }
        }
    }
    return sum > 0 && maximum - minimum > 4;
}
}

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    const bool resolveRoom = arguments.size() == 3 && arguments[1] == QStringLiteral("--resolve");
    if (!resolveRoom && (arguments.size() != 2 || !QFileInfo(arguments[1]).isFile())) {
        std::cerr << "Supply an existing local test video, or --resolve with a Douyin room URL.\n";
        return 2;
    }
    {
        queuepilot::VideoDecoder guardDecoder;
        queuepilot::DecoderOptions unsafe;
        unsafe.source = QStringLiteral("https://127.0.0.1:1/test.flv");
        unsafe.cookies = QByteArrayLiteral("queuepilot_audit_dummy=1");
        QString status;
        bool producedFrame = false;
        guardDecoder.start(unsafe, [&](queuepilot::VideoFrame) { producedFrame = true; },
                           [&](QString message) { status = std::move(message); });
        guardDecoder.stop();
        if (producedFrame || !status.contains(QStringLiteral("do not accept Cookie"))) {
            std::cerr << "FAIL: decoder Cookie guard did not reject before network access\n";
            return 1;
        }
    }

    Observations observations;
    queuepilot::VideoDecoder decoder;
    const auto begin = [&](const QString& source, const QString& referer, const QByteArray& cookies) {
        queuepilot::DecoderOptions options;
        options.source = source;
        options.referer = referer;
        options.cookies = cookies;
        decoder.start(options, [&](queuepilot::VideoFrame frame) {
            std::lock_guard lock(observations.mutex);
            if (frame.image.isNull() || frame.image.width() <= 0 || frame.image.height() <= 0 ||
                frame.image.format() != QImage::Format_RGB888) {
                observations.invalidFrame = true;
            } else {
                observations.nonblank |= isNonblank(frame.image);
            }
            if (observations.frames > 0 && (frame.sequence <= observations.lastSequence ||
                frame.mediaSeconds + 0.000001 < observations.lastMedia)) {
                observations.nonmonotonic = true;
            }
            if (observations.frames == 0) {
                observations.firstMedia = frame.mediaSeconds;
                observations.firstReceived = frame.receivedAt;
            }
            observations.lastMedia = frame.mediaSeconds;
            observations.lastReceived = frame.receivedAt;
            observations.lastSequence = frame.sequence;
            ++observations.frames;
            observations.changed.notify_all();
        }, [&](QString status) {
            std::lock_guard lock(observations.mutex);
            observations.lastStatus = std::move(status);
            observations.changed.notify_all();
        });
    };

    bool sourceStarted = !resolveRoom;
    QString resolutionError;
    if (resolveRoom) {
        queuepilot::StreamResolver resolver;
        QObject::connect(&resolver, &queuepilot::StreamResolver::resolved, &application,
            [&](const QString& source, const QString& referer, const QByteArray& cookies) {
                sourceStarted = true;
                begin(source, referer, cookies);
                application.quit();
            });
        QObject::connect(&resolver, &queuepilot::StreamResolver::failed, &application,
            [&](const QString& message) {
                resolutionError = message;
                application.quit();
            });
        QTimer::singleShot(20000, &application, [&] {
            resolutionError = QStringLiteral("Room resolution timed out.");
            resolver.cancel();
            application.quit();
        });
        resolver.resolve(arguments[2]);
        application.exec();
    } else {
        begin(QFileInfo(arguments[1]).absoluteFilePath(), {}, {});
    }
    if (!sourceStarted) {
        std::cerr << "FAIL: " << resolutionError.toStdString() << '\n';
        return 1;
    }

    bool completed = false;
    {
        std::unique_lock lock(observations.mutex);
        completed = observations.changed.wait_for(lock, std::chrono::seconds(resolveRoom ? 25 : 10),
            [&] { return observations.complete(); });
    }
    const auto stopStart = queuepilot::Clock::now();
    decoder.stop();
    const auto stopMs = std::chrono::duration_cast<std::chrono::milliseconds>(queuepilot::Clock::now() - stopStart).count();
    bool passed = true;
    const auto fail = [&](const char* reason) {
        std::cerr << "FAIL: " << reason << '\n';
        passed = false;
    };
    if (!completed || observations.frames < 8) {
        fail("not enough decoded frames before timeout");
    }
    if (observations.invalidFrame) {
        fail("invalid frame dimensions or RGB format");
    }
    if (observations.nonmonotonic) {
        fail("frame sequence or media timestamp decreased");
    }
    if (!observations.nonblank) {
        fail("decoded frames are blank or uniform");
    }
    if (stopMs > 2000) {
        fail("stop took longer than two seconds");
    }
    const double mediaSpan = observations.lastMedia - observations.firstMedia;
    const double receivedSpan = std::chrono::duration<double>(observations.lastReceived - observations.firstReceived).count();
    if (!resolveRoom && (receivedSpan + 0.06 < mediaSpan || receivedSpan > mediaSpan + 1.0)) {
        fail("local playback was not paced by media timestamps");
    }
    if (!passed && !observations.lastStatus.isEmpty()) {
        std::cerr << "Decoder status: " << observations.lastStatus.toStdString() << '\n';
    }
    if (passed) {
        std::cout << "Decoder smoke passed: " << observations.frames << " RGB frames, media span "
            << mediaSpan << " s, playback span " << receivedSpan << " s, stop " << stopMs << " ms.\n";
    }
    return passed ? 0 : 1;
}
