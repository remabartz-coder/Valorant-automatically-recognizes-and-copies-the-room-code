#include "code_gate.h"

#include <cmath>
#include <stdexcept>

namespace queuepilot {

CodeGate::CodeGate(GateConfig config) : config_(config)
{
    if (config_.confirmations < 1 || config_.maxAgeMs < 1 ||
        !std::isfinite(config_.minConfidence) ||
        config_.minConfidence < 0 || config_.minConfidence > 1) {
        throw std::invalid_argument("Invalid room-code confirmation settings");
    }
}

QString CodeGate::normalizeCandidate(QString text)
{
    QString normalized;
    normalized.reserve(text.size());
    for (const QChar character : text) {
        if (!character.isSpace()) {
            normalized.append(character);
        }
    }
    return normalized.toUpper();
}

bool CodeGate::validCode(const QString& text, bool lettersFirst)
{
    if (text.size() != 6) {
        return false;
    }
    int letters = 0;
    int digits = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const auto value = text[i].unicode();
        const bool letter = value >= 'A' && value <= 'Z';
        const bool digit = value >= '0' && value <= '9';
        if (!letter && !digit) {
            return false;
        }
        if (lettersFirst && ((i < 3 && !letter) || (i >= 3 && !digit))) {
            return false;
        }
        letters += letter;
        digits += digit;
    }
    return letters == 3 && digits == 3;
}

std::optional<QString> CodeGate::observe(const OcrResult& result, std::uint64_t frameSequence,
                                        Clock::time_point capturedAt, Clock::time_point now)
{
    if (pendingCapturedAt_ && (now < *pendingCapturedAt_ ||
        now - *pendingCapturedAt_ > std::chrono::milliseconds(config_.maxAgeMs))) {
        clearPending();
    }
    // Repeated or reordered frames cannot provide an independent confirmation.
    if (lastSequence_ && frameSequence <= *lastSequence_) {
        return std::nullopt;
    }
    lastSequence_ = frameSequence;
    const QString candidate = normalizeCandidate(result.text);
    if (capturedAt > now || now - capturedAt > std::chrono::milliseconds(config_.maxAgeMs) ||
        !std::isfinite(result.confidence) || result.confidence < config_.minConfidence ||
        result.confidence > 1 || !validCode(candidate, config_.lettersFirst) ||
        emitted_.contains(candidate)) {
        clearPending();
        return std::nullopt;
    }
    if (pending_ != candidate) {
        pending_ = candidate;
        confirmations_ = 1;
    } else {
        ++confirmations_;
    }
    pendingCapturedAt_ = capturedAt;
    if (confirmations_ < config_.confirmations) {
        return std::nullopt;
    }
    emitted_.insert(candidate);
    clearPending();
    return candidate;
}

void CodeGate::clearPending()
{
    pending_.clear();
    confirmations_ = 0;
    pendingCapturedAt_.reset();
}

void CodeGate::reset()
{
    clearPending();
    lastSequence_.reset();
    emitted_.clear();
}

}
