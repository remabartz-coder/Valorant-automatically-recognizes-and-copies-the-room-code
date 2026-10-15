#pragma once

#include "code_gate.h"

#include <QRectF>
#include <functional>
#include <memory>
#include <vector>

namespace queuepilot {
class OcrEngine;
struct DetectedTextLine {
    // Pixel coordinates in the input image. The DB core excludes outer glyph strokes.
    QRect bounds;
    QRect coreBounds;
    float confidence = 0;
};
struct LocatedCode {
    std::optional<QRectF> roi;
    QString message;
    QString code;
    float confidence = 0;
};
class CodeLocator {
public:
    CodeLocator();
    ~CodeLocator();
    CodeLocator(const CodeLocator&) = delete;
    CodeLocator& operator=(const CodeLocator&) = delete;
    void load(const QString& detectorPath, int threads = 2);
    bool loaded() const;
    std::vector<DetectedTextLine> detectTextLines(const QImage& image,
                                                std::function<bool()> cancelled = {});
    LocatedCode locate(const QImage& frame, OcrEngine& recognizer, GateConfig config = {},
                       std::function<bool()> cancelled = {});
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
