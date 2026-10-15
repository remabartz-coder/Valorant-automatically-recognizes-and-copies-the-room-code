#include "code_gate.h"
#include "ocr_engine.h"

#include <QFont>
#include <QFontMetrics>
#include <QFileInfo>
#include <QDir>
#include <QGuiApplication>
#include <QPainter>
#include <QRawFont>
#include <QTemporaryFile>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <stdexcept>

using namespace queuepilot;

namespace {
QImage codeImage(const QString& code, int pixels, bool lightText)
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
    const QFontMetrics metrics(font);
    const QRect bounds = metrics.tightBoundingRect(code);
    constexpr int margin = 10;
    QImage image(bounds.width() + 2 * margin, bounds.height() + 2 * margin,
                 QImage::Format_RGB888);
    image.fill(lightText ? QColor(25, 25, 25) : QColor(250, 250, 250));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setFont(font);
    painter.setPen(lightText ? QColor(250, 250, 250) : QColor(25, 25, 25));
    painter.drawText(QPoint(margin - bounds.left(), margin - bounds.top()), code);
    return image;
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
    const auto arguments = application.arguments();
    if (arguments.size() != 3) {
        std::fprintf(stderr, "Usage: ocr_smoke MODEL.onnx DICTIONARY.txt\n");
        return 2;
    }
    int failures = 0;
    try {
        OcrEngine engine;
        if (engine.loaded()) {
            throw std::runtime_error("Uninitialized OCR engine reports loaded");
        }
        engine.load(arguments[1], arguments[2]);
        if (!engine.loaded()) {
            throw std::runtime_error("OCR engine did not load");
        }
        const std::array<QString, 5> codes{QStringLiteral("TYH663"), QStringLiteral("ABC123"),
                                         QStringLiteral("H6T3Y6"), QStringLiteral("AZI973"),
                                         QStringLiteral("DOB453")};
        for (const auto& code : codes) {
            for (const int pixels : {42, 64}) {
                for (const bool lightText : {false, true}) {
                    const QImage image = codeImage(code, pixels, lightText);
                    const auto start = Clock::now();
                    const auto result = engine.recognize(image);
                    const auto totalMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
                    const auto normalized = CodeGate::normalizeCandidate(result.text);
                    std::printf("%s %dpx %s -> %s confidence=%.4f inference=%.2fms total=%.2fms\n",
                                code.toUtf8().constData(), pixels, lightText ? "light-on-dark" : "dark-on-light",
                                normalized.toUtf8().constData(), result.confidence, result.inferenceMs, totalMs);
                    if (normalized != code) {
                        std::fprintf(stderr, "FAIL: recognizer changed the rendered room code\n");
                        ++failures;
                    }
                    if (result.confidence < 0 || result.confidence > 1 || result.inferenceMs <= 0) {
                        std::fprintf(stderr, "FAIL: invalid OCR measurements\n");
                        ++failures;
                    }
                }
            }
        }

        for (const auto& code : {QStringLiteral("AZI973"), QStringLiteral("DOB453")}) {
            for (const bool lightText : {false, true}) {
                const QImage text = codeImage(code, 42, lightText);
                QImage surrounded(text.width() + 100, text.height() + 80, QImage::Format_RGB888);
                surrounded.fill(text.pixelColor(0, 0));
                {
                    QPainter painter(&surrounded);
                    painter.drawImage(50, 40, text);
                    painter.setPen(QPen(QColor(30, 220, 160), 1));
                    painter.drawRect(surrounded.rect().adjusted(1, 1, -2, -2));
                }
                const auto result = engine.recognizeCode(surrounded);
                const auto normalized = CodeGate::normalizeCandidate(result.text);
                std::printf("Margin/border %s %s -> %s confidence=%.4f\n", code.toUtf8().constData(),
                            lightText ? "light-on-dark" : "dark-on-light", normalized.toUtf8().constData(), result.confidence);
                // Normalization still goes through the unmodified confidence and frame gate.
                CodeGate gate({2, .90f, false, 500});
                const auto now = Clock::now();
                const auto first = gate.observe(result, 1, now, now);
                const auto second = gate.observe(result, 2, now, now);
                if (first || (second && *second != code)) {
                    std::fprintf(stderr, "FAIL: loose ROI published an unconfirmed or incorrect room code\n");
                    ++failures;
                }
                if (result.confidence < .90f && second) {
                    std::fprintf(stderr, "FAIL: loose ROI bypassed the default confidence threshold\n");
                    ++failures;
                }
                if (normalized != code || result.confidence < .90f || !result.lineNormalized) {
                    std::fprintf(stderr, "FAIL: isolated margin/border text was not recovered safely\n");
                    ++failures;
                }
            }
        }

        for (const auto& code : {QStringLiteral("AZI973"), QStringLiteral("DOB453")}) {
            const QString path = QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).filePath(
                QStringLiteral("fixtures/%1.png").arg(code));
            const QImage image(path);
            if (image.isNull()) throw std::runtime_error("Missing real screenshot fixture");
            const auto raw = engine.recognize(image);
            const auto result = engine.recognizeCode(image);
            std::printf("Real screenshot %s -> %s confidence=%.6f raw=%.6f lineNormalized=%s\n",
                        code.toUtf8().constData(), result.text.toUtf8().constData(), result.confidence,
                        result.rawConfidence, result.lineNormalized ? "yes" : "no");
            if (CodeGate::normalizeCandidate(result.text) != code || result.rawText != raw.text ||
                std::abs(result.rawConfidence - raw.confidence) > .00001f) {
                std::fprintf(stderr, "FAIL: real screenshot text or raw evidence changed\n");
                ++failures;
            }
        }

        for (const auto& code : {QStringLiteral("KOG460"), QStringLiteral("ZUJ215"),
                                 QStringLiteral("TEJ065"), QStringLiteral("EHQ220")}) {
            const QString path = QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).filePath(
                QStringLiteral("fixtures/%1-native-wide.png").arg(code));
            const QImage image(path);
            if (image.isNull()) throw std::runtime_error("Missing native video wide-ROI fixture");
            const auto raw = engine.recognize(image);
            const auto result = engine.recognizeCode(image);
            std::printf("Native wide video %s %dx%d -> %s confidence=%.6f raw=[%s] rawConfidence=%.6f inference=%.2fms\n",
                        code.toUtf8().constData(), image.width(), image.height(), result.text.toUtf8().constData(),
                        result.confidence, result.rawText.toUtf8().constData(), result.rawConfidence, result.inferenceMs);
            if (image.width() < 500 || image.height() < 70 || result.text != code || result.confidence < .90f ||
                !result.lineNormalized || result.rawText != raw.text ||
                std::abs(result.rawConfidence - raw.confidence) > .00001f) {
                std::fprintf(stderr, "FAIL: wide native video input field was not recovered with its raw evidence\n");
                ++failures;
            }
            CodeGate gate({2, .90f, false, 500});
            const auto now = Clock::now();
            if (gate.observe(result, 1, now, now) || gate.observe(result, 1, now, now) ||
                gate.observe(result, 2, now, now) != code) {
                std::fprintf(stderr, "FAIL: native video normalization bypassed independent-frame confirmation\n");
                ++failures;
            }
            if (code != QStringLiteral("ZUJ215")) {
                const QRect core = code == QStringLiteral("TEJ065") ? QRect(253, 33, 86, 13) : QRect(251, 33, 90, 13);
                const auto detected = engine.recognizeDetectedCode(image, core);
                std::printf("Native detected core %s -> %s confidence=%.6f inference=%.2fms\n",
                            code.toUtf8().constData(), detected.text.toUtf8().constData(),
                            detected.confidence, detected.inferenceMs);
                if (detected.text != code || detected.confidence < .90f) {
                    std::fprintf(stderr, "FAIL: real detector core did not yield the original native room code\n");
                    ++failures;
                }
            }
        }

        {
            const QString path = QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()).filePath(
                QStringLiteral("fixtures/hidden-native-wide.png"));
            const QImage image(path);
            if (image.isNull()) throw std::runtime_error("Missing hidden native video fixture");
            for (const auto& result : {engine.recognizeCode(image),
                                      engine.recognizeDetectedCode(image, QRect(251, 33, 90, 13))}) {
                CodeGate gate({2, .90f, false, 500});
                const auto now = Clock::now();
                if (gate.observe(result, 1, now, now) || gate.observe(result, 2, now, now)) {
                    std::fprintf(stderr, "FAIL: obscured native input field published a room code\n");
                    ++failures;
                }
            }
            if (engine.recognizeDetectedCode(image, QRect(-20, 10, 90, 13)).confidence != 0 ||
                engine.recognizeDetectedCode(image, QRect(10, 10, 2, 2)).confidence != 0) {
                std::fprintf(stderr, "FAIL: invalid detector bounds were accepted\n");
                ++failures;
            }
        }

        for (const bool lightText : {false, true}) {
            QImage blank(360, 160, QImage::Format_RGB32);
            blank.fill(lightText ? QColor(25, 25, 25) : QColor(250, 250, 250));
            {
                QPainter painter(&blank);
                painter.setPen(QPen(QColor(30, 220, 160), 1));
                painter.drawRect(blank.rect().adjusted(1, 1, -2, -2));
            }
            const auto result = engine.recognizeCode(blank);
            CodeGate gate({2, .90f, false, 500});
            const auto now = Clock::now();
            if (result.lineNormalized || gate.observe(result, 1, now, now) || gate.observe(result, 2, now, now)) {
                std::fprintf(stderr, "FAIL: empty bordered panel generated a room code\n");
                ++failures;
            }
            const QImage first = codeImage(QStringLiteral("AZI973"), 42, lightText);
            const QImage second = codeImage(QStringLiteral("DOB453"), 42, lightText);
            QImage multiple(std::max(first.width(), second.width()) + 80,
                            first.height() + second.height() + 80, QImage::Format_RGB32);
            multiple.fill(blank.pixelColor(0, 0));
            {
                QPainter painter(&multiple);
                painter.drawImage(40, 20, first);
                painter.drawImage(40, first.height() + 60, second);
            }
            const auto multipleResult = engine.recognizeCode(multiple);
            CodeGate multipleGate({2, .90f, false, 500});
            if (multipleResult.lineNormalized || multipleGate.observe(multipleResult, 1, now, now) ||
                multipleGate.observe(multipleResult, 2, now, now)) {
                std::fprintf(stderr, "FAIL: multiple text lines were normalized as one room code\n");
                ++failures;
            }
        }

        for (const auto& text : {QStringLiteral("AZI97"), QStringLiteral("AZI9731"),
                                 QStringLiteral("LOBBY"), QStringLiteral("123456"),
                                 QStringLiteral("AB1234"), QStringLiteral("ABCD12"),
                                 QStringLiteral("ABCDEF"), QStringLiteral("K0G460")}) {
            const QImage image = codeImage(text, 42, true);
            const auto result = engine.recognizeCode(image);
            CodeGate gate({2, .90f, false, 500});
            const auto now = Clock::now();
            if (gate.observe(result, 1, now, now) || gate.observe(result, 2, now, now)) {
                std::fprintf(stderr, "FAIL: malformed or ordinary text bypassed the default code format\n");
                ++failures;
            }
        }

        QTemporaryFile wrongKeys;
        if (!wrongKeys.open() || wrongKeys.write("A\n") != 2 || !wrongKeys.flush()) {
            throw std::runtime_error("Cannot create mismatched dictionary test file");
        }
        bool rejected = false;
        OcrEngine mismatched;
        try {
            mismatched.load(arguments[1], wrongKeys.fileName());
        } catch (const std::exception& error) {
            rejected = true;
            std::printf("Mismatched dictionary rejected: %s\n", error.what());
        }
        if (!rejected || mismatched.loaded()) {
            std::fprintf(stderr, "FAIL: mismatched dictionary was accepted\n");
            ++failures;
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "OCR smoke test error: %s\n", error.what());
        return 1;
    }
    return failures == 0 ? 0 : 1;
}
