#include "code_locator.h"
#include "ocr_engine.h"
#include <QDir>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QPainter>
#include <QRawFont>
#include <cstdio>
#include <exception>
#include <stdexcept>

using namespace queuepilot;
namespace {
int failures = 0;
void expect(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
QFont testFont()
{
    QFont font(QStringLiteral("Segoe UI"));
    font.setPixelSize(64);
    font.setWeight(QFont::DemiBold);
    const QRawFont raw = QRawFont::fromFont(font);
    if (!raw.isValid()) throw std::runtime_error("Test font could not be loaded");
    for (const QChar character : QStringLiteral("TYH663ABC123"))
        if (!raw.supportsCharacter(character)) throw std::runtime_error("Test font lacks required glyphs");
    return font;
}
QImage scene(const QFont& font, bool first, bool second)
{
    QImage image(960, 540, QImage::Format_RGB888);
    image.fill(QColor(25, 25, 25));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setPen(QColor(250, 250, 250));
    painter.setFont(font);
    painter.drawText(QPoint(40, 70), QStringLiteral("LIVE"));
    if (first) painter.drawText(QPoint(100, 230), QStringLiteral("TYH663"));
    if (second) painter.drawText(QPoint(540, 430), QStringLiteral("ABC123"));
    return image;
}

QImage denseScene(QFont font, const QSize& size, bool second = false)
{
    QImage image(size, QImage::Format_RGB888);
    image.fill(QColor(25, 25, 25));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setPen(QColor(250, 250, 250));
    font.setPixelSize(size.width() > 1500 ? 24 : 30);
    painter.setFont(font);
    for (int row = 0; row < 6; ++row) {
        for (int column = 0; column < 5; ++column) {
            painter.drawText(QPoint(30 + column * (size.width() - 60) / 5,
                                   45 + row * (size.height() - 180) / 6), QStringLiteral("PLAYERS"));
        }
    }
    font.setPixelSize(size.width() > 1500 ? 20 : 40);
    painter.setFont(font);
    painter.drawText(QPoint(100, size.height() - 60), QStringLiteral("TYH663"));
    if (second) painter.drawText(QPoint(size.width() - 320, size.height() - 60), QStringLiteral("ABC123"));
    return image;
}

QImage overBudgetScene(QFont font)
{
    QImage image(3200, 2400, QImage::Format_RGB888);
    image.fill(QColor(25, 25, 25));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setPen(QColor(250, 250, 250));
    font.setPixelSize(24);
    painter.setFont(font);
    for (int row = 0; row < 18; ++row)
        for (int column = 0; column < 16; ++column)
            painter.drawText(QPoint(30 + column * 190, 45 + row * 125), QStringLiteral("PLAYERS"));
    painter.drawText(QPoint(30, 2360), QStringLiteral("TYH663"));
    return image;
}
}
int main(int argc, char** argv)
{
#ifndef Q_OS_WIN
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    QGuiApplication application(argc, argv);
    const auto arguments = application.arguments();
    if (arguments.size() != 2) { std::fprintf(stderr, "Usage: locator_smoke MODELS_DIRECTORY\n"); return 2; }
    try {
        const QDir models(arguments[1]);
        OcrEngine recognizer;
        recognizer.load(models.filePath("recognizer.onnx"), models.filePath("keys.txt"));
        CodeLocator locator;
        locator.load(models.filePath("detector.onnx"));
        const QFont font = testFont();
        const QImage one = scene(font, true, false);
        const auto start = Clock::now();
        const auto found = locator.locate(one, recognizer);
        std::printf("Single-code location: %s code=%s confidence=%.4f elapsed=%.2fms\n",
                    found.message.toUtf8().constData(), found.code.toUtf8().constData(), found.confidence,
                    std::chrono::duration<double, std::milli>(Clock::now() - start).count());
        expect(found.roi.has_value() && found.code == QStringLiteral("TYH663"), "single code is located accurately");
        if (found.roi) {
            const QRect crop = QRectF(found.roi->x() * one.width(), found.roi->y() * one.height(),
                                      found.roi->width() * one.width(), found.roi->height() * one.height()).toAlignedRect();
            expect(CodeGate::normalizeCandidate(recognizer.recognizeCode(one.copy(crop)).text) == QStringLiteral("TYH663"),
                   "returned region contains the complete recognizable code");
            const QRect text = QFontMetrics(font).tightBoundingRect(QStringLiteral("TYH663")).translated(100, 230);
            expect(crop.intersected(text).width() * crop.intersected(text).height() >= text.width() * text.height() * .9,
                   "location bounds cover the code glyphs");
        }
        const auto absent = locator.locate(scene(font, false, false), recognizer);
        expect(!absent.roi && absent.code.isEmpty(), "ordinary text without a room code is not selected");
        const auto ambiguous = locator.locate(scene(font, true, true), recognizer);
        expect(!ambiguous.roi && ambiguous.code.isEmpty(), "two different codes are ambiguous and not selected");
        const QImage dense = denseScene(font, QSize(960, 720));
        GateConfig roomConfig;
        roomConfig.lettersFirst = true;
        const auto denseLines = locator.detectTextLines(dense);
        std::printf("Dense scene text regions: %zu\n", denseLines.size());
        expect(denseLines.size() > 24, "dense scene really exceeds the former 24-region cutoff");
        const auto denseFound = locator.locate(dense, recognizer, roomConfig);
        std::printf("Dense-code location: %s code=%s confidence=%.4f\n",
                    denseFound.message.toUtf8().constData(), denseFound.code.toUtf8().constData(), denseFound.confidence);
        expect(denseFound.roi.has_value() && denseFound.code == QStringLiteral("TYH663"),
               "one code among more than 24 ordinary text regions is located");
        const auto denseAmbiguous = locator.locate(denseScene(font, QSize(960, 720), true), recognizer, roomConfig);
        expect(!denseAmbiguous.roi && denseAmbiguous.code.isEmpty(),
               "dense text does not hide ambiguity from a second code");
        const auto smallFound = locator.locate(denseScene(font, QSize(2420, 1668)), recognizer, roomConfig);
        std::printf("Small-code location: %s code=%s confidence=%.4f\n",
                    smallFound.message.toUtf8().constData(), smallFound.code.toUtf8().constData(), smallFound.confidence);
        expect(smallFound.roi.has_value() && smallFound.code == QStringLiteral("TYH663"),
               "native-resolution tiles locate a small code in a large dense frame");
        const auto overBudget = locator.locate(overBudgetScene(font), recognizer, roomConfig);
        expect(!overBudget.roi && overBudget.code.isEmpty(),
               "an incomplete candidate scan cannot claim a unique code");
        expect(overBudget.message.contains(QStringLiteral("\u9884\u7b97")),
               "candidate budget rejection explains why uniqueness was not established");
        const auto cancelled = locator.locate(one, recognizer, {}, [] { return true; });
        expect(!cancelled.roi, "cancelled location cannot return a region");
        int cancellationChecks = 0;
        const auto cancelledDuringScan = locator.locate(dense, recognizer, {}, [&] { return ++cancellationChecks > 20; });
        expect(!cancelledDuringScan.roi && cancelledDuringScan.code.isEmpty(),
               "cancellation during a dense scan never returns a partial match");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Locator test error: %s\n", error.what());
        return 1;
    }
    if (!failures) std::printf("All locator checks passed.\n");
    return failures ? 1 : 0;
}
