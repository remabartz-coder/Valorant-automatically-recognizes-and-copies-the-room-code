#pragma once

#include "types.h"

#include <memory>

namespace queuepilot {

class OcrEngine {
public:
    OcrEngine();
    ~OcrEngine();

    OcrEngine(const OcrEngine&) = delete;
    OcrEngine& operator=(const OcrEngine&) = delete;

    void load(const QString& modelPath, const QString& keysPath, int threads = 2);
    bool loaded() const;
    OcrResult recognize(const QImage& singleLineCrop);
    OcrResult recognizeCode(const QImage& singleLineCrop);
    OcrResult recognizeDetectedCode(const QImage& image, const QRect& textCore);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
