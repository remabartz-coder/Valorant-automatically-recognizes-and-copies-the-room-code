#pragma once

#include "types.h"

#include <QSet>
#include <optional>

namespace queuepilot {

struct GateConfig {
    int confirmations = 2;
    float minConfidence = 0.90f;
    bool lettersFirst = false;
    int maxAgeMs = 500;
};

class CodeGate {
public:
    explicit CodeGate(GateConfig config = {});

    std::optional<QString> observe(const OcrResult& result, std::uint64_t frameSequence,
                                   Clock::time_point capturedAt, Clock::time_point now);
    void reset();

    static QString normalizeCandidate(QString text);
    static bool validCode(const QString& text, bool lettersFirst = false);

private:
    void clearPending();

    GateConfig config_;
    QString pending_;
    int confirmations_ = 0;
    std::optional<Clock::time_point> pendingCapturedAt_;
    std::optional<std::uint64_t> lastSequence_;
    QSet<QString> emitted_;
};

}
