#include "code_gate.h"

#include <QCoreApplication>

#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

using namespace queuepilot;

namespace {
int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

OcrResult reading(const QString& text = QStringLiteral("TYH663"), float confidence = 0.99f)
{
    return {text, confidence, 2.0};
}

void testFormat()
{
    expect(CodeGate::validCode(QStringLiteral("TYH663")), "real code format is accepted");
    expect(CodeGate::validCode(QStringLiteral("6T6Y3H")), "three letters/digits in any order is accepted");
    expect(!CodeGate::validCode(QStringLiteral("AZ1973")), "two letters and four digits is rejected");
    expect(!CodeGate::validCode(QStringLiteral("D0B453")), "interleaved two letters and four digits is rejected");
    expect(!CodeGate::validCode(QStringLiteral("6T6Y3H"), true), "letters-first mode rejects permutations");
    expect(CodeGate::validCode(QStringLiteral("TYH663"), true), "letters-first mode accepts canonical format");
    expect(!CodeGate::validCode(QStringLiteral("TYH66")), "partial code is rejected");
    expect(!CodeGate::validCode(QStringLiteral("TYH6633")), "long code is rejected");
    expect(!CodeGate::validCode(QStringLiteral("123456")), "all digits is rejected");
    expect(!CodeGate::validCode(QStringLiteral("ABCDEF")), "all letters is rejected");
    expect(!CodeGate::validCode(QStringLiteral("TYHI63")), "four letters and two digits is rejected");
    expect(!CodeGate::validCode(QStringLiteral("AB1234")), "two letters and four digits is rejected regardless of order");
    expect(CodeGate::validCode(QStringLiteral("KOG460")), "letter O is preserved in a valid code");
    expect(!CodeGate::validCode(QStringLiteral("K0G460")), "digit zero cannot replace a required third letter");
    expect(!CodeGate::validCode(QStringLiteral("AZ1973"), true), "letters-first mode rejects two letters and four digits");
    expect(!CodeGate::validCode(QStringLiteral("ABCDEF"), true), "letters-first mode rejects all letters");
    expect(!CodeGate::validCode(QStringLiteral("tyh663")), "raw lowercase requires normalization");
    expect(!CodeGate::validCode(QStringLiteral("AB-123")), "six-character punctuation is rejected");
    expect(!CodeGate::validCode(QStringLiteral("AB\u4E2D123")), "non-ASCII letters are rejected");
    expect(!CodeGate::validCode(QStringLiteral("ABC\uFF11\uFF12\uFF13")), "non-ASCII digits are rejected");
    expect(!CodeGate::validCode(QStringLiteral("TYH-663")), "punctuation is not silently corrected");
    expect(!CodeGate::validCode(QStringLiteral("ROOM TYH663")), "mixed label is not extracted as a code");
    expect(CodeGate::normalizeCandidate(QStringLiteral(" \tt y h\n6 6 3 ")) == QStringLiteral("TYH663"),
           "only whitespace and case are normalized");
    expect(CodeGate::normalizeCandidate(QStringLiteral("OIH663")) == QStringLiteral("OIH663"),
           "ambiguous letters are not changed into digits");
    expect(CodeGate::normalizeCandidate(QStringLiteral("D0B453")) == QStringLiteral("D0B453"),
           "ambiguous digits are not changed into letters");
}

void testThreeLettersThreeDigitsGate()
{
    const auto now = Clock::now();
    std::uint64_t sequence = 0;
    CodeGate gate;
    for (const auto& invalid : {QStringLiteral("AZ1973"), QStringLiteral("D0B453"),
                                QStringLiteral("AB1234"), QStringLiteral("ABCD12"),
                                QStringLiteral("ABCDEF"), QStringLiteral("123456"),
                                QStringLiteral("K0G460")}) {
        expect(!gate.observe(reading(invalid), ++sequence, now, now), "unbalanced frame is rejected");
        expect(!gate.observe(reading(invalid), ++sequence, now, now), "repeated unbalanced text cannot confirm");
    }
    expect(!gate.observe(reading(QStringLiteral("6T6Y3H")), ++sequence, now, now), "valid permutation starts confirmation");
    expect(gate.observe(reading(QStringLiteral("6T6Y3H")), ++sequence, now, now).has_value(), "three-letter three-digit permutation confirms");
    expect(!gate.observe(reading(QStringLiteral("KOG460")), ++sequence, now, now), "real letter O starts confirmation without substitution");
    const auto confirmed = gate.observe(reading(QStringLiteral("KOG460")), ++sequence, now, now);
    expect(confirmed && *confirmed == QStringLiteral("KOG460"), "valid code confirms without replacing the letter O");
}

void testConfirmationAndDedup()
{
    CodeGate gate;
    const auto now = Clock::now();
    expect(!gate.observe(reading(), 1, now, now), "first frame does not publish");
    expect(!gate.observe(reading(), 1, now, now), "same frame does not confirm");
    expect(!gate.observe(reading(), 0, now, now), "reordered frame does not confirm");
    const auto code = gate.observe(reading(), 2, now, now);
    expect(code && *code == QStringLiteral("TYH663"), "second distinct frame confirms");
    expect(!gate.observe(reading(), 3, now, now), "published code is not repeated");
    expect(!gate.observe(reading(QStringLiteral("")), 4, now, now), "disappearance publishes nothing");
    expect(!gate.observe(reading(), 5, now, now), "old code is not reaccepted after disappearance");
    expect(!gate.observe(reading(), 6, now, now), "old code remains blocked on repeated frames");
    expect(!gate.observe(reading(QStringLiteral("ABC123")), 7, now, now), "new code starts new confirmation");
    expect(gate.observe(reading(QStringLiteral("ABC123")), 8, now, now).has_value(), "new code can publish");
    expect(!gate.observe(reading(), 9, now, now), "earlier published code remains blocked after another code");
    gate.reset();
    expect(!gate.observe(reading(), 1, now, now), "session reset starts fresh confirmation");
    expect(gate.observe(reading(), 2, now, now).has_value(), "session reset allows explicitly rearmed code");
}

void testInvalidAndAge()
{
    CodeGate gate;
    const auto now = Clock::now();
    expect(!gate.observe(reading(), 1, now, now), "initial candidate starts");
    expect(!gate.observe(reading(QStringLiteral("TYH663"), 0.89f), 2, now, now), "low confidence resets");
    expect(!gate.observe(reading(), 3, now, now), "valid frame after low confidence must restart");
    expect(!gate.observe(reading(QStringLiteral("TYH66")), 4, now, now), "partial recognition resets");
    expect(!gate.observe(reading(), 5, now, now), "valid frame after partial recognition must restart");
    expect(!gate.observe(reading(), 6, now - std::chrono::milliseconds(501), now), "old frame resets");
    expect(!gate.observe(reading(), 7, now, now), "valid frame after old frame must restart");
    expect(!gate.observe(reading(), 8, now + std::chrono::milliseconds(1), now), "future timestamp resets");
    expect(!gate.observe(reading(), 9, now, now), "valid frame after future timestamp must restart");
    expect(!gate.observe(reading(QStringLiteral("TYH663"), std::numeric_limits<float>::quiet_NaN()),
                         10, now, now), "NaN confidence resets");
    expect(!gate.observe(reading(), 11, now, now), "valid frame after NaN must restart");
    expect(gate.observe(reading(), 12, now - std::chrono::milliseconds(500), now).has_value(),
           "frame exactly at age limit is accepted");

    CodeGate gap;
    expect(!gap.observe(reading(), 1, now, now), "gap candidate starts");
    const auto later = now + std::chrono::milliseconds(501);
    expect(!gap.observe(reading(), 2, later, later), "expired previous confirmation cannot count");
    expect(gap.observe(reading(), 3, later, later).has_value(), "fresh frames confirm after a gap");

    CodeGate duplicateGap;
    expect(!duplicateGap.observe(reading(), 1, now, now), "duplicate gap candidate starts");
    expect(!duplicateGap.observe(reading(), 1, now, later), "repeated old frame expires pending confirmation");
    expect(!duplicateGap.observe(reading(), 2, later, later), "new frame after old duplicate must restart");
    expect(duplicateGap.observe(reading(), 3, later, later).has_value(), "two new frames confirm after duplicate gap");

    GateConfig config;
    config.lettersFirst = true;
    CodeGate ordered(config);
    expect(!ordered.observe(reading(QStringLiteral("6T6Y3H")), 1, now, now), "ordered gate rejects permutation");
    expect(!ordered.observe(reading(QStringLiteral("6T6Y3H")), 2, now, now), "ordered rejection cannot confirm");
    expect(!ordered.observe(reading(), 3, now, now), "ordered gate accepts first canonical frame");
    expect(ordered.observe(reading(), 4, now, now).has_value(), "ordered gate confirms canonical code");
}

void testConfigAndConsistency()
{
    const auto now = Clock::now();
    GateConfig config;
    config.confirmations = 3;
    CodeGate gate(config);
    expect(!gate.observe(reading(), 1, now, now), "three-frame config starts candidate");
    expect(!gate.observe(reading(), 2, now, now), "three-frame config waits for third frame");
    expect(!gate.observe(reading(QStringLiteral("ABC123")), 3, now, now), "different candidate resets count");
    expect(!gate.observe(reading(), 4, now, now), "returning candidate starts again");
    expect(!gate.observe(reading(), 5, now, now), "returning candidate waits for third frame");
    expect(gate.observe(reading(), 6, now, now).has_value(), "three consistent frames publish");

    config.confirmations = 1;
    CodeGate single(config);
    expect(single.observe(reading(), 1, now, now).has_value(), "one-frame mode publishes once");
    expect(!single.observe(reading(), 2, now, now), "one-frame mode still deduplicates");
    bool rejected = false;
    try {
        config.confirmations = 0;
        CodeGate invalid(config);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected, "invalid confirmation count is rejected");

    CodeGate normalized;
    expect(!normalized.observe(reading(QStringLiteral("t y h 6 6 3"), 0.90f), 1, now, now),
           "exact confidence threshold accepts first normalized frame");
    const auto normalizedCode = normalized.observe(reading(QStringLiteral("tyh663"), 0.90f), 2, now, now);
    expect(normalizedCode && *normalizedCode == QStringLiteral("TYH663"),
           "normalized OCR text publishes canonical code");
}
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    testFormat();
    testThreeLettersThreeDigitsGate();
    testConfirmationAndDedup();
    testInvalidAndAge();
    testConfigAndConsistency();
    if (failures == 0) {
        std::printf("All room-code gate checks passed.\n");
    }
    return failures == 0 ? 0 : 1;
}
