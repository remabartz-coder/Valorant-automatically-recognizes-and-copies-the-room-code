#pragma once

#include <QImage>
#include <QString>
#include <QByteArray>
#include <chrono>
#include <cstdint>

namespace queuepilot {
using Clock = std::chrono::steady_clock;

struct VideoFrame {
    QImage image;
    std::uint64_t sequence = 0;
    Clock::time_point receivedAt;
    double mediaSeconds = 0;
};

struct DecoderOptions {
    QString source;
    QString referer;
    QByteArray cookies;
    bool lowLatency = true;
    bool loopFile = false;
};

struct OcrResult {
    QString text;
    float confidence = 0;
    double inferenceMs = 0;
    QString rawText;
    float rawConfidence = 0;
    bool lineNormalized = false;
};
}
