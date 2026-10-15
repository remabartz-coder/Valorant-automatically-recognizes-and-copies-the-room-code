#include "ocr_engine.h"
#include "code_gate.h"

#include <QFile>
#include <QStringConverter>
#include <QStringList>
#include <QTextStream>
#include <QPainter>

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace queuepilot {
namespace {

std::runtime_error fail(const QString& message)
{
    return std::runtime_error(message.toStdString());
}

QStringList readDictionary(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        throw fail(QStringLiteral("Cannot open OCR dictionary: %1").arg(path));
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    QStringList dictionary;
    while (!stream.atEnd()) {
        const QString key = stream.readLine();
        if (key.isEmpty()) {
            throw fail(QStringLiteral("OCR dictionary contains an empty entry at line %1")
                           .arg(dictionary.size() + 1));
        }
        dictionary.append(key);
    }
    if (dictionary.isEmpty()) {
        throw fail(QStringLiteral("OCR dictionary is empty"));
    }
    return dictionary;
}

struct TextLine {
    QRect bounds;
    QColor background;
};

std::optional<TextLine> isolatedCodeLine(const QImage& source)
{
    if (source.width() < 24 || source.height() < 12 || source.width() > 2048 || source.height() > 1024)
        return std::nullopt;
    const QImage image = source.convertToFormat(QImage::Format_RGB32);
    std::array<int, 16> histogram{};
    for (int y = 0; y < image.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) ++histogram[qGray(row[x]) / 16];
    }
    const int mode = static_cast<int>(std::max_element(histogram.begin(), histogram.end()) - histogram.begin());
    const int level = mode * 16 + 8;
    int backgroundPixels = 0;
    std::array<std::int64_t, 3> backgroundSum{};
    std::vector<unsigned char> foreground(static_cast<std::size_t>(image.width()) * image.height());
    for (int y = 0; y < image.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const int difference = std::abs(qGray(row[x]) - level);
            if (difference < 24) {
                ++backgroundPixels;
                backgroundSum[0] += qRed(row[x]); backgroundSum[1] += qGreen(row[x]);
                backgroundSum[2] += qBlue(row[x]);
            }
            foreground[static_cast<std::size_t>(y) * image.width() + x] = difference >= 60;
        }
    }
    // Only normalize isolated text on a mostly uniform panel; arbitrary video remains untouched.
    if (backgroundPixels < image.width() * image.height() * .60) return std::nullopt;
    std::vector<QRect> glyphs;
    std::vector<int> stack;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const int origin = y * image.width() + x;
            if (!foreground[origin]) continue;
            foreground[origin] = 0;
            stack.clear(); stack.push_back(origin);
            QRect bounds(x, y, 1, 1);
            int area = 0;
            while (!stack.empty()) {
                const int current = stack.back(); stack.pop_back(); ++area;
                const int px = current % image.width(), py = current / image.width();
                bounds = bounds.united(QRect(px, py, 1, 1));
                for (const QPoint step : {QPoint(-1, 0), QPoint(1, 0), QPoint(0, -1), QPoint(0, 1)}) {
                    const int nx = px + step.x(), ny = py + step.y();
                    if (nx < 0 || nx >= image.width() || ny < 0 || ny >= image.height()) continue;
                    const int adjacent = ny * image.width() + nx;
                    if (foreground[adjacent]) {
                        foreground[adjacent] = 0;
                        stack.push_back(adjacent);
                    }
                }
            }
            if (bounds.left() <= 1 || bounds.top() <= 1 || bounds.right() >= image.width() - 2 ||
                bounds.bottom() >= image.height() - 2) continue;
            if (bounds.height() < 6 || bounds.height() < image.height() * .08 ||
                area < bounds.height() || bounds.width() > bounds.height() * 1.4) continue;
            glyphs.push_back(bounds);
        }
    }
    if (glyphs.size() != 6) return std::nullopt;
    std::sort(glyphs.begin(), glyphs.end(), [](const QRect& a, const QRect& b) { return a.x() < b.x(); });
    QRect line = glyphs.front();
    for (const QRect& glyph : glyphs) line = line.united(glyph);
    if (line.width() < line.height() * 2 || line.width() > line.height() * 7) return std::nullopt;
    for (std::size_t i = 0; i < glyphs.size(); ++i) {
        const QRect& glyph = glyphs[i];
        if (glyph.height() < line.height() * .72 || glyph.top() - line.top() > line.height() * .20 ||
            line.bottom() - glyph.bottom() > line.height() * .20) return std::nullopt;
        if (i && (glyph.left() <= glyphs[i - 1].right() ||
                  glyph.left() - glyphs[i - 1].right() > line.height() * .75)) return std::nullopt;
    }
    return TextLine{line, QColor(static_cast<int>(backgroundSum[0] / backgroundPixels),
                                static_cast<int>(backgroundSum[1] / backgroundPixels),
                                static_cast<int>(backgroundSum[2] / backgroundPixels))};
}

QImage normalizedLine(const QImage& source, const TextLine& line, double paddingRatio)
{
    const int margin = std::max(3, qRound(line.bounds.height() * paddingRatio));
    QImage result(line.bounds.width() + 2 * margin, line.bounds.height() + 2 * margin, QImage::Format_RGB32);
    result.fill(line.background);
    QPainter painter(&result);
    painter.drawImage(QPoint(margin, margin), source, line.bounds);
    return result;
}

}

struct OcrEngine::Impl {
    Ort::Env environment{ORT_LOGGING_LEVEL_WARNING, "QueuePilotOcr"};
    Ort::Session session{nullptr};
    Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::string inputName;
    std::string outputName;
    QStringList dictionary;
    QStringList symbols;
    int height = 48;
    int fixedWidth = 0;
    std::int64_t classCount = 0;

    void initialize(const QString& modelPath, const QString& keysPath, int threads)
    {
        dictionary = readDictionary(keysPath);
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(std::max(1, threads));
        options.SetInterOpNumThreads(1);
        options.SetExecutionMode(ORT_SEQUENTIAL);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
#ifdef _WIN32
        const std::wstring path = modelPath.toStdWString();
#else
        const std::string path = modelPath.toStdString();
#endif
        session = Ort::Session(environment, path.c_str(), options);
        if (session.GetInputCount() != 1 || session.GetOutputCount() < 1) {
            throw fail(QStringLiteral("OCR requires a single-input recognition model"));
        }
        Ort::AllocatorWithDefaultOptions allocator;
        inputName = session.GetInputNameAllocated(0, allocator).get();
        outputName = session.GetOutputNameAllocated(0, allocator).get();
        const auto inputType = session.GetInputTypeInfo(0);
        const auto inputInfo = inputType.GetTensorTypeAndShapeInfo();
        const auto shape = inputInfo.GetShape();
        if (inputInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
            shape.size() != 4 || (shape[0] > 0 && shape[0] != 1) || shape[1] != 3) {
            throw fail(QStringLiteral("OCR model input must be float32 [1, 3, H, W]"));
        }
        if (shape[2] > 0) {
            if (shape[2] > 1024) {
                throw fail(QStringLiteral("OCR model height is unsupported"));
            }
            height = static_cast<int>(shape[2]);
        }
        if (shape[3] > 0) {
            if (shape[3] > 8192) {
                throw fail(QStringLiteral("OCR model width is unsupported"));
            }
            fixedWidth = static_cast<int>(shape[3]);
        }
    }

    void prepareSymbols(std::int64_t count)
    {
        if (classCount == count) {
            return;
        }
        symbols = dictionary;
        // Paddle's use_space_char appends a class even when the official English dictionary already ends in space.
        if (count == dictionary.size() + 2) {
            symbols.append(QStringLiteral(" "));
        } else if (count != dictionary.size() + 1) {
            throw fail(QStringLiteral("OCR model has %1 classes, but dictionary has %2 entries")
                           .arg(count).arg(dictionary.size()));
        }
        classCount = count;
    }

    OcrResult recognize(const QImage& crop)
    {
        if (crop.isNull() || crop.width() < 1 || crop.height() < 1) {
            return {};
        }
        const double ratio = static_cast<double>(crop.width()) / crop.height();
        const int naturalWidth = static_cast<int>(std::min(4096.0, std::ceil(height * ratio)));
        const int width = fixedWidth > 0 ? fixedWidth : std::max(320, naturalWidth);
        const int resizedWidth = std::clamp(naturalWidth, 1, width);
        const QImage rgb = crop.scaled(resizedWidth, height, Qt::IgnoreAspectRatio,
                                      Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB888);
        const std::size_t plane = static_cast<std::size_t>(height) * width;
        // PaddleOCR pads in normalized space; zero corresponds to neutral gray.
        std::vector<float> input(3 * plane, 0.0f);
        for (int y = 0; y < height; ++y) {
            const auto* row = rgb.constScanLine(y);
            for (int x = 0; x < resizedWidth; ++x) {
                const auto offset = static_cast<std::size_t>(y) * width + x;
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    // The supplied PaddleOCR model was trained with OpenCV BGR input.
                    input[channel * plane + offset] = row[3 * x + 2 - channel] / 127.5f - 1.0f;
                }
            }
        }
        const std::array<std::int64_t, 4> shape{1, 3, height, width};
        auto tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(),
                                                     shape.data(), shape.size());
        const char* inputNames[] = {inputName.c_str()};
        const char* outputNames[] = {outputName.c_str()};
        const auto start = Clock::now();
        auto outputs = session.Run(Ort::RunOptions{nullptr}, inputNames, &tensor, 1,
                                   outputNames, 1);
        const double inferenceMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        if (!outputs.front().IsTensor()) {
            throw fail(QStringLiteral("OCR model output is not a tensor"));
        }
        const auto info = outputs.front().GetTensorTypeAndShapeInfo();
        const auto outputShape = info.GetShape();
        if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
            outputShape.size() != 3 || outputShape[0] != 1 ||
            outputShape[1] < 1 || outputShape[2] < 2) {
            throw fail(QStringLiteral("OCR model output must be float32 [1, timesteps, classes]"));
        }
        const auto timesteps = outputShape[1];
        const auto classes = outputShape[2];
        prepareSymbols(classes);
        const auto* values = outputs.front().GetTensorData<float>();
        OcrResult result;
        result.inferenceMs = inferenceMs;
        float minimumConfidence = 1;
        int decodedCount = 0;
        std::int64_t previous = -1;
        for (std::int64_t t = 0; t < timesteps; ++t) {
            const auto* scores = values + t * classes;
            std::int64_t best = 0;
            double sum = 0;
            bool probabilities = true;
            for (std::int64_t c = 0; c < classes; ++c) {
                if (!std::isfinite(scores[c])) {
                    throw fail(QStringLiteral("OCR model returned a non-finite score"));
                }
                if (scores[c] > scores[best]) {
                    best = c;
                }
                probabilities = probabilities && scores[c] >= 0 && scores[c] <= 1;
                sum += scores[c];
            }
            probabilities = probabilities && std::abs(sum - 1.0) < 0.02;
            float confidence = scores[best];
            if (!probabilities) {
                double denominator = 0;
                for (std::int64_t c = 0; c < classes; ++c) {
                    denominator += std::exp(static_cast<double>(scores[c]) - scores[best]);
                }
                confidence = static_cast<float>(1.0 / denominator);
            }
            // CTC blank is class 0. A blank separates repeated characters such as "66".
            if (best != 0 && best != previous) {
                result.text += symbols[static_cast<qsizetype>(best - 1)];
                minimumConfidence = std::min(minimumConfidence, confidence);
                ++decodedCount;
            }
            previous = best;
        }
        // One uncertain character can change the room, so report the weakest emitted character.
        result.confidence = decodedCount > 0 ? minimumConfidence : 0;
        return result;
    }
};

OcrEngine::OcrEngine() = default;
OcrEngine::~OcrEngine() = default;

void OcrEngine::load(const QString& modelPath, const QString& keysPath, int threads)
{
    impl_.reset();
    auto next = std::make_unique<Impl>();
    next->initialize(modelPath, keysPath, threads);
    QImage warmup(320, next->height, QImage::Format_RGB888);
    warmup.fill(Qt::white);
    next->recognize(warmup);
    impl_ = std::move(next);
}

bool OcrEngine::loaded() const
{
    return impl_ != nullptr;
}

OcrResult OcrEngine::recognize(const QImage& singleLineCrop)
{
    if (!impl_) {
        throw fail(QStringLiteral("OCR model is not loaded"));
    }
    return impl_->recognize(singleLineCrop);
}

OcrResult OcrEngine::recognizeCode(const QImage& singleLineCrop)
{
    OcrResult result = recognize(singleLineCrop);
    result.rawText = result.text;
    result.rawConfidence = result.confidence;
    if (result.confidence >= .90f && CodeGate::validCode(CodeGate::normalizeCandidate(result.text)))
        return result;
    const auto line = isolatedCodeLine(singleLineCrop);
    if (!line) return result;
    // Try the compact pair first for small native UI glyphs, then a wider pair for
    // compressed video. A pair is accepted only when both independent crops agree;
    // scores are never compared and characters are never repaired.
    for (const auto padding : {std::pair{.33, .43}, std::pair{.60, .80}}) {
        const OcrResult first = recognize(normalizedLine(singleLineCrop, *line, padding.first));
        result.inferenceMs += first.inferenceMs;
        const QString candidate = CodeGate::normalizeCandidate(first.text);
        if (!CodeGate::validCode(candidate) || first.confidence < .90f) continue;
        const OcrResult second = recognize(normalizedLine(singleLineCrop, *line, padding.second));
        result.inferenceMs += second.inferenceMs;
        if (candidate == CodeGate::normalizeCandidate(second.text) &&
            second.confidence >= .90f) {
            result.text = first.text;
            result.confidence = std::min(first.confidence, second.confidence);
            result.lineNormalized = true;
            return result;
        }
    }
    return result;
}

OcrResult OcrEngine::recognizeDetectedCode(const QImage& image, const QRect& textCore)
{
    if (image.isNull() || textCore.width() < 8 || textCore.height() < 3 ||
        !image.rect().contains(textCore)) return {};
    const auto expanded = [&](double ratio) {
        const int padding = std::max(4, qRound(textCore.height() * ratio + 2));
        return image.copy(textCore.adjusted(-padding, -padding, padding, padding).intersected(image.rect()));
    };
    OcrResult first = recognizeCode(expanded(.80));
    const QString candidate = CodeGate::normalizeCandidate(first.text);
    if (!CodeGate::validCode(candidate) || first.confidence < .90f) {
        first.confidence = 0;
        return first;
    }
    const OcrResult second = recognizeCode(expanded(1.20));
    first.inferenceMs += second.inferenceMs;
    if (candidate != CodeGate::normalizeCandidate(second.text) ||
        !CodeGate::validCode(candidate) || second.confidence < .90f) {
        // Detector cores are intentionally smaller than glyphs. A tight, confident
        // misread must not win over a conflicting crop with more surrounding pixels.
        first.confidence = 0;
        return first;
    }
    first.confidence = std::min(first.confidence, second.confidence);
    first.lineNormalized = true;
    return first;
}

}
