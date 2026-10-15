#include "code_locator.h"
#include "ocr_engine.h"
#include <QSet>
#include <onnxruntime_cxx_api.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace queuepilot {
namespace {
std::runtime_error fail(const QString& message) { return std::runtime_error(message.toStdString()); }
bool isCancelled(const std::function<bool()>& cancelled) { return cancelled && cancelled(); }
LocatedCode cancelledResult() { return {{}, QStringLiteral("\u5b9a\u4f4d\u5df2\u53d6\u6d88"), {}, 0}; }

double linePriority(const DetectedTextLine& line)
{
    const double aspect = static_cast<double>(line.coreBounds.width()) / line.coreBounds.height();
    return line.confidence - 0.08 * std::abs(std::log(aspect / 4.0));
}

bool sameLine(const QRect& first, const QRect& second)
{
    const QRect overlap = first.intersected(second);
    if (overlap.isEmpty()) return false;
    const double firstArea = static_cast<double>(first.width()) * first.height();
    const double secondArea = static_cast<double>(second.width()) * second.height();
    const double overlapArea = static_cast<double>(overlap.width()) * overlap.height();
    return overlapArea / std::min(firstArea, secondArea) > .75 &&
           overlapArea / std::max(firstArea, secondArea) > .45;
}

void mergeLines(std::vector<DetectedTextLine>& candidates, std::vector<DetectedTextLine> additional,
                const QPoint& offset = {})
{
    for (auto& line : additional) {
        line.bounds.translate(offset);
        line.coreBounds.translate(offset);
        const auto existing = std::find_if(candidates.begin(), candidates.end(), [&](const auto& candidate) {
            return sameLine(candidate.coreBounds, line.coreBounds);
        });
        if (existing == candidates.end()) candidates.push_back(line);
        else if (line.confidence > existing->confidence) *existing = line;
    }
}

std::vector<int> tileOrigins(int length, int side)
{
    if (length <= side) return {0};
    const int count = (length - side + side - 129) / (side - 128) + 1;
    std::vector<int> origins;
    for (int index = 0; index < count; ++index)
        origins.push_back(qRound(static_cast<double>(length - side) * index / (count - 1)));
    return origins;
}
}
struct CodeLocator::Impl {
    Ort::Env environment{ORT_LOGGING_LEVEL_WARNING, "QueuePilotLocator"};
    Ort::Session session{nullptr};
    Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::string inputName, outputName;
    int fixedHeight = 0, fixedWidth = 0;

    void initialize(const QString& path, int threads)
    {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(std::max(1, threads));
        options.SetInterOpNumThreads(1);
        options.SetExecutionMode(ORT_SEQUENTIAL);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
#ifdef _WIN32
        const auto modelPath = path.toStdWString();
#else
        const auto modelPath = path.toStdString();
#endif
        session = Ort::Session(environment, modelPath.c_str(), options);
        if (session.GetInputCount() != 1 || session.GetOutputCount() < 1)
            throw fail(QStringLiteral("Text detector must have one input and at least one output"));
        Ort::AllocatorWithDefaultOptions allocator;
        inputName = session.GetInputNameAllocated(0, allocator).get();
        outputName = session.GetOutputNameAllocated(0, allocator).get();
        const auto inputType = session.GetInputTypeInfo(0);
        const auto info = inputType.GetTensorTypeAndShapeInfo();
        const auto shape = info.GetShape();
        if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || shape.size() != 4 ||
            (shape[0] > 0 && shape[0] != 1) || shape[1] != 3)
            throw fail(QStringLiteral("Text detector input must be float32 [1, 3, H, W]"));
        if (shape[2] > 2048 || shape[3] > 2048)
            throw fail(QStringLiteral("Text detector dimensions are unsupported"));
        fixedHeight = shape[2] > 0 ? static_cast<int>(shape[2]) : 0;
        fixedWidth = shape[3] > 0 ? static_cast<int>(shape[3]) : 0;
    }

    std::vector<DetectedTextLine> detect(const QImage& frame, const std::function<bool()>& cancelled)
    {
        const double ratio = std::min(2.0, 960.0 / std::max(frame.width(), frame.height()));
        const int height = fixedHeight > 0 ? fixedHeight : std::max(32, qRound(frame.height() * ratio / 32) * 32);
        const int width = fixedWidth > 0 ? fixedWidth : std::max(32, qRound(frame.width() * ratio / 32) * 32);
        const QImage image = frame.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                 .convertToFormat(QImage::Format_RGB888);
        const std::size_t plane = static_cast<std::size_t>(height) * width;
        std::vector<float> input(plane * 3);
        constexpr std::array<float, 3> means{0.485f, 0.456f, 0.406f};
        constexpr std::array<float, 3> deviations{0.229f, 0.224f, 0.225f};
        for (int y = 0; y < height; ++y) {
            const auto* row = image.constScanLine(y);
            for (int x = 0; x < width; ++x) {
                const auto offset = static_cast<std::size_t>(y) * width + x;
                for (std::size_t channel = 0; channel < 3; ++channel)
                    input[channel * plane + offset] =
                        (row[3 * x + 2 - channel] / 255.0f - means[channel]) / deviations[channel];
            }
        }
        if (isCancelled(cancelled)) return {};
        const std::array<std::int64_t, 4> shape{1, 3, height, width};
        auto tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(), shape.data(), shape.size());
        const char* inputNames[] = {inputName.c_str()};
        const char* outputNames[] = {outputName.c_str()};
        auto outputs = session.Run(Ort::RunOptions{nullptr}, inputNames, &tensor, 1, outputNames, 1);
        if (isCancelled(cancelled)) return {};
        if (!outputs.front().IsTensor()) throw fail(QStringLiteral("Text detector output is not a tensor"));
        const auto info = outputs.front().GetTensorTypeAndShapeInfo();
        const auto output = info.GetShape();
        if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || output.size() != 4 ||
            output[0] != 1 || output[1] != 1 || output[2] < 1 || output[3] < 1 ||
            output[2] > 4096 || output[3] > 4096)
            throw fail(QStringLiteral("Text detector output must be float32 [1, 1, H, W]"));
        const int mapHeight = static_cast<int>(output[2]), mapWidth = static_cast<int>(output[3]);
        const auto count = static_cast<std::size_t>(mapHeight) * mapWidth;
        const float* scores = outputs.front().GetTensorData<float>();
        std::vector<unsigned char> visited(count, 0);
        std::vector<int> component;
        std::vector<DetectedTextLine> candidates;
        const double scaleX = static_cast<double>(frame.width()) / mapWidth;
        const double scaleY = static_cast<double>(frame.height()) / mapHeight;
        for (int y = 0; y < mapHeight; ++y) {
            if (isCancelled(cancelled)) return {};
            for (int x = 0; x < mapWidth; ++x) {
                const int seed = y * mapWidth + x;
                if (visited[seed] || scores[seed] < 0.30f || !std::isfinite(scores[seed])) continue;
                component.clear();
                component.push_back(seed);
                visited[seed] = 1;
                int left = x, right = x, top = y, bottom = y;
                double sum = 0;
                // DB masks describe text lines; eight-connected components retain those regions.
                for (std::size_t index = 0; index < component.size(); ++index) {
                    const int pixel = component[index], pixelX = pixel % mapWidth, pixelY = pixel / mapWidth;
                    left = std::min(left, pixelX); right = std::max(right, pixelX);
                    top = std::min(top, pixelY); bottom = std::max(bottom, pixelY);
                    sum += scores[pixel];
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int nextX = pixelX + dx, nextY = pixelY + dy;
                            if (nextX < 0 || nextX >= mapWidth || nextY < 0 || nextY >= mapHeight) continue;
                            const int next = nextY * mapWidth + nextX;
                            if (visited[next] || scores[next] < 0.30f || !std::isfinite(scores[next])) continue;
                            visited[next] = 1;
                            component.push_back(next);
                        }
                    }
                }
                const int componentWidth = right - left + 1, componentHeight = bottom - top + 1;
                const double probability = sum / component.size();
                const double aspect = static_cast<double>(componentWidth) / componentHeight;
                if (component.size() < 12 || componentHeight < 2 || componentWidth < 8 ||
                    probability < 0.55 || aspect < 1.2 || aspect > 14) continue;
                const double originalWidth = componentWidth * scaleX, originalHeight = componentHeight * scaleY;
                // Preserve the context required by both fixed guarded recognition crops.
                const double paddingX = std::max(2.0, originalHeight * 1.2 + 2);
                const double paddingY = std::max(2.0, originalHeight * 1.2 + 2);
                const QRect core = QRectF(left * scaleX, top * scaleY, originalWidth, originalHeight)
                                       .toAlignedRect().intersected(frame.rect());
                const QRect rectangle = QRectF(left * scaleX - paddingX, top * scaleY - paddingY,
                                               originalWidth + 2 * paddingX, originalHeight + 2 * paddingY)
                                            .toAlignedRect().intersected(frame.rect());
                if (rectangle.width() < 12 || rectangle.height() < 8) continue;
                candidates.push_back({rectangle, core, static_cast<float>(probability)});
            }
        }
        std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
            return linePriority(a) > linePriority(b);
        });
        return candidates;
    }
};
CodeLocator::CodeLocator() = default;
CodeLocator::~CodeLocator() = default;
void CodeLocator::load(const QString& detectorPath, int threads)
{
    impl_.reset();
    auto next = std::make_unique<Impl>();
    next->initialize(detectorPath, threads);
    impl_ = std::move(next);
}
bool CodeLocator::loaded() const { return impl_ != nullptr; }
std::vector<DetectedTextLine> CodeLocator::detectTextLines(const QImage& image, std::function<bool()> cancelled)
{
    if (!impl_) throw fail(QStringLiteral("Text detector is not loaded"));
    if (image.isNull() || isCancelled(cancelled)) return {};
    return impl_->detect(image, cancelled);
}
LocatedCode CodeLocator::locate(const QImage& frame, OcrEngine& recognizer, GateConfig config,
                                std::function<bool()> cancelled)
{
    if (!impl_) throw fail(QStringLiteral("Text detector is not loaded"));
    if (frame.isNull()) return {{}, QStringLiteral("\u6ca1\u6709\u53ef\u5b9a\u4f4d\u7684\u753b\u9762"), {}, 0};
    if (isCancelled(cancelled)) return cancelledResult();
    auto candidates = impl_->detect(frame, cancelled);
    if (isCancelled(cancelled)) return cancelledResult();
    // Native-resolution tiles retain small invitation codes that disappear in the overview.
    if (frame.width() > 1100 || frame.height() > 1100) {
        constexpr int tileSide = 960;
        const auto xs = tileOrigins(frame.width(), tileSide);
        const auto ys = tileOrigins(frame.height(), tileSide);
        if (xs.size() * ys.size() > 24)
            return {{}, QStringLiteral("\u753b\u9762\u8fc7\u5927\uff0c\u8bf7\u5148\u6846\u9009\u9080\u8bf7\u7a97\u53e3\u540e\u91cd\u8bd5"), {}, 0};
        for (const int y : ys) {
            for (const int x : xs) {
                if (isCancelled(cancelled)) return cancelledResult();
                const QRect tile(x, y, std::min(tileSide, frame.width() - x), std::min(tileSide, frame.height() - y));
                auto local = impl_->detect(frame.copy(tile), cancelled);
                // A tile must not turn a clipped fragment of a longer line into a six-character code.
                std::erase_if(local, [&](const auto& line) {
                    return (x > 0 && line.bounds.left() <= 1) ||
                           (y > 0 && line.bounds.top() <= 1) ||
                           (tile.right() < frame.width() - 1 && line.bounds.right() >= tile.width() - 2) ||
                           (tile.bottom() < frame.height() - 1 && line.bounds.bottom() >= tile.height() - 2);
                });
                mergeLines(candidates, std::move(local), tile.topLeft());
            }
        }
    }
    if (isCancelled(cancelled)) return cancelledResult();
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return linePriority(a) > linePriority(b);
    });
    constexpr std::size_t maximumCandidates = 256;
    const bool complete = candidates.size() <= maximumCandidates;
    QSet<QString> codes;
    LocatedCode chosen;
    for (std::size_t index = 0; index < std::min(candidates.size(), maximumCandidates); ++index) {
        const auto& candidate = candidates[index];
        if (isCancelled(cancelled)) return cancelledResult();
        const OcrResult result = recognizer.recognizeDetectedCode(frame, candidate.coreBounds);
        const QString code = CodeGate::normalizeCandidate(result.text);
        if (!std::isfinite(result.confidence) || result.confidence < config.minConfidence ||
            !CodeGate::validCode(code, config.lettersFirst)) continue;
        codes.insert(code);
        if (codes.size() > 1)
            return {{}, QStringLiteral("\u53d1\u73b0\u591a\u4e2a\u4e0d\u540c\u623f\u95f4\u7801\uff0c\u8bf7\u624b\u52a8\u6846\u9009"), {}, 0};
        if (!chosen.roi || result.confidence > chosen.confidence) {
            chosen.roi = QRectF(static_cast<double>(candidate.bounds.x()) / frame.width(),
                                static_cast<double>(candidate.bounds.y()) / frame.height(),
                                static_cast<double>(candidate.bounds.width()) / frame.width(),
                                static_cast<double>(candidate.bounds.height()) / frame.height());
            chosen.code = code;
            chosen.confidence = result.confidence;
        }
    }
    if (isCancelled(cancelled)) return cancelledResult();
    if (!complete)
        return {{}, QStringLiteral("\u6587\u5b57\u533a\u57df\u8d85\u51fa\u5b8c\u6574\u68c0\u67e5\u9884\u7b97\uff0c\u8bf7\u5148\u6846\u9009\u9080\u8bf7\u7a97\u53e3\u540e\u91cd\u8bd5"), {}, 0};
    chosen.message = chosen.roi ? QStringLiteral("\u5df2\u627e\u5230\u623f\u95f4\u7801\u533a\u57df")
                               : QStringLiteral("\u672a\u627e\u5230\u53ef\u786e\u8ba4\u7684\u516d\u4f4d\u623f\u95f4\u7801");
    return chosen;
}
}
