#include "code_gate.h"
#include "ocr_engine.h"

#include <QDir>
#include <QGuiApplication>
#include <QPainter>

#include <cstdio>
#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace queuepilot;

namespace {
QImage padded(const QImage& source, int margin)
{
    QImage result(source.width() + 2 * margin, source.height() + 2 * margin, QImage::Format_RGB888);
    result.fill(source.pixelColor(0, 0));
    QPainter painter(&result);
    painter.drawImage(margin, margin, source);
    return result;
}

QImage binary(const QImage& source, int threshold)
{
    QImage result = source.convertToFormat(QImage::Format_Grayscale8);
    for (int y = 0; y < result.height(); ++y) {
        auto* row = result.scanLine(y);
        for (int x = 0; x < result.width(); ++x) row[x] = row[x] >= threshold ? 255 : 0;
    }
    return result;
}
}

int main(int argc, char** argv)
{
    QGuiApplication application(argc, argv);
    const auto arguments = application.arguments();
    if (arguments.size() != 8 && arguments.size() != 9 && arguments.size() != 10) {
        std::fprintf(stderr, "Usage: ocr_probe MODELS_DIRECTORY IMAGE X Y WIDTH HEIGHT OUTPUT_DIRECTORY [EXPECTED_RAW_CODE] [core]\n");
        return 2;
    }
    try {
        const QImage image(arguments[2]);
        if (image.isNull()) throw std::runtime_error("Cannot load input image");
        bool coordinatesValid = true;
        int coordinates[4];
        for (int i = 0; i < 4; ++i) {
            bool valid = false;
            coordinates[i] = arguments[3 + i].toInt(&valid);
            coordinatesValid = coordinatesValid && valid;
        }
        const QRect rectangle(coordinates[0], coordinates[1], coordinates[2], coordinates[3]);
        if (!coordinatesValid || rectangle.isEmpty() || !image.rect().contains(rectangle))
            throw std::runtime_error("Crop must be a nonempty rectangle inside the image");
        const QImage crop = image.copy(rectangle);
        QDir output(arguments[7]);
        if (!QDir().mkpath(output.absolutePath())) throw std::runtime_error("Cannot create diagnostic output directory");
        const QDir models(arguments[1]);
        OcrEngine engine;
        engine.load(models.filePath("recognizer.onnx"), models.filePath("keys.txt"));
        if (arguments.size() == 10 && arguments[9] == QStringLiteral("core")) {
            const auto result = engine.recognizeDetectedCode(image, rectangle);
            std::printf("detected-core=[%s] confidence=%.6f raw=[%s] rawConfidence=%.6f inference=%.2fms\n",
                        result.text.toUtf8().constData(), result.confidence, result.rawText.toUtf8().constData(),
                        result.rawConfidence, result.inferenceMs);
            return CodeGate::normalizeCandidate(result.text) == arguments[8] && result.confidence >= .90f ? 0 : 1;
        }
        std::vector<std::pair<QString, QImage>> variants;
        variants.emplace_back(QStringLiteral("raw"), crop);
        variants.emplace_back(QStringLiteral("gray"), crop.convertToFormat(QImage::Format_Grayscale8));
        QImage inverted = crop;
        inverted.invertPixels();
        variants.emplace_back(QStringLiteral("inverted"), inverted);
        variants.emplace_back(QStringLiteral("nearest-2x"), crop.scaled(crop.size() * 2, Qt::IgnoreAspectRatio, Qt::FastTransformation));
        variants.emplace_back(QStringLiteral("smooth-2x"), crop.scaled(crop.size() * 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        variants.emplace_back(QStringLiteral("padded-4"), padded(crop, 4));
        variants.emplace_back(QStringLiteral("binary-128"), binary(crop, 128));
        variants.emplace_back(QStringLiteral("binary-180"), binary(crop, 180));
        std::printf("Input=%dx%d crop=%d,%d %dx%d\n", image.width(), image.height(),
                    rectangle.x(), rectangle.y(), rectangle.width(), rectangle.height());
        bool expectedRawMatches = true;
        for (const auto& [name, variant] : variants) {
            if (!variant.save(output.filePath(name + QStringLiteral(".png"))))
                throw std::runtime_error("Cannot save diagnostic crop");
            const auto result = engine.recognize(variant);
            const auto normalized = CodeGate::normalizeCandidate(result.text);
            if (arguments.size() == 9 && name == QStringLiteral("raw")) {
                expectedRawMatches = normalized == arguments[8];
                if (!expectedRawMatches) std::fprintf(stderr, "FAIL: raw recognition differs from the confirmed room code\n");
            }
            std::printf("%s raw=[%s] normalized=[%s] confidence=%.6f valid=%s inference=%.2fms\n",
                        name.toUtf8().constData(), result.text.toUtf8().constData(),
                        normalized.toUtf8().constData(), result.confidence,
                        CodeGate::validCode(normalized) ? "yes" : "no", result.inferenceMs);
            if (name == QStringLiteral("raw")) {
                const auto codeResult = engine.recognizeCode(variant);
                std::printf("code-mode raw=[%s] confidence=%.6f lineNormalized=%s original=[%s] originalConfidence=%.6f inference=%.2fms\n",
                            codeResult.text.toUtf8().constData(), codeResult.confidence,
                            codeResult.lineNormalized ? "yes" : "no", codeResult.rawText.toUtf8().constData(),
                            codeResult.rawConfidence, codeResult.inferenceMs);
            }
        }
        if (!expectedRawMatches) return 1;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "OCR probe error: %s\n", error.what());
        return 1;
    }
    return 0;
}
