#pragma once

#include "types.h"
#include <QList>
#include <QString>
#include <functional>
#include <memory>

namespace queuepilot {

struct WindowInfo {
    QString title;
    quintptr handle = 0;
};

class WindowCapture final {
public:
    WindowCapture();
    ~WindowCapture();
    WindowCapture(const WindowCapture&) = delete;
    WindowCapture& operator=(const WindowCapture&) = delete;

    static QList<WindowInfo> windows();
    void start(quintptr handle, std::function<void(VideoFrame)> frame,
               std::function<void(QString)> status);
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
