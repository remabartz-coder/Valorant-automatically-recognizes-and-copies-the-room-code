#include "window_capture.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QPainter>
#include <QTimer>
#include <QWidget>
#include <array>
#include <cstdlib>
#include <iostream>
#include <mutex>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

constexpr std::array<QRgb, 3> patchColors{
    qRgb(230, 61, 70), qRgb(39, 176, 124), qRgb(41, 137, 216)};

class Fixture final : public QWidget {
public:
    Fixture() : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint) {
        setAttribute(Qt::WA_ShowWithoutActivating);
        setWindowTitle(QStringLiteral("QueuePilot capture validation"));
        resize(360, 210);
    }

    void advance() {
        ++tick_;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        for (int index = 0; index < 3; ++index) {
            const int left = width() * index / 3;
            const int right = width() * (index + 1) / 3;
            painter.fillRect(QRect(left, 0, right - left, height()), QColor(patchColors[index]));
        }
        painter.fillRect(QRect(10 + (tick_ * 9) % (width() - 40), height() - 30, 20, 20), Qt::white);
    }

private:
    int tick_ = 0;
};

QSize nativeWindowSize(quintptr handle) {
    RECT bounds{};
    if (!GetWindowRect(reinterpret_cast<HWND>(handle), &bounds)) return {};
    return QSize(bounds.right - bounds.left, bounds.bottom - bounds.top);
}

bool colorNear(QRgb actual, QRgb expected) {
    return std::abs(qRed(actual) - qRed(expected)) <= 12 &&
           std::abs(qGreen(actual) - qGreen(expected)) <= 12 &&
           std::abs(qBlue(actual) - qBlue(expected)) <= 12;
}

bool patchesVisible(const QImage& image) {
    if (image.isNull() || image.width() < 60 || image.height() < 60) return false;
    for (int index = 0; index < 3; ++index) {
        const int x = image.width() * (2 * index + 1) / 6;
        const int y = image.height() / 2;
        if (!colorNear(image.pixel(x, y), patchColors[index])) return false;
    }
    return true;
}

struct SharedFrames {
    std::mutex mutex;
    QImage latest;
    std::uint64_t total = 0;
    std::uint64_t changed = 0;
    QString status;
};

}

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    if (QApplication::platformName() != QStringLiteral("windows")) {
        std::cerr << "WGC validation requires the native Windows Qt platform and a desktop compositor.\n";
        return 1;
    }

    Fixture fixture;
    SharedFrames frames;
    queuepilot::WindowCapture capture;
    QTimer animate;
    QTimer inspect;
    QElapsedTimer elapsed;
    bool finished = false;
    int phase = 0;
    QSize resized;
    std::uint64_t changesBeforeResize = 0;
    std::uint64_t stoppedCount = 0;
    qint64 stoppedAt = 0;

    const auto finish = [&](bool success, const QString& message) {
        if (finished) return;
        finished = true;
        inspect.stop();
        animate.stop();
        capture.stop();
        fixture.hide();
        std::cout << message.toStdString() << '\n';
        application.exit(success ? 0 : 1);
    };

    QObject::connect(&animate, &QTimer::timeout, &fixture, [&] { fixture.advance(); });
    animate.setInterval(33);
    QObject::connect(&inspect, &QTimer::timeout, &application, [&] {
        QImage image;
        std::uint64_t total;
        std::uint64_t changed;
        QString status;
        {
            std::lock_guard lock(frames.mutex);
            image = frames.latest;
            total = frames.total;
            changed = frames.changed;
            status = frames.status;
        }
        if (status.startsWith(QStringLiteral("Window capture failed"))) {
            finish(false, status);
            return;
        }
        if (elapsed.elapsed() > 9000) {
            finish(false, QStringLiteral("Capture timed out: %1 frames, %2 changed, status=%3")
                .arg(total).arg(changed).arg(status));
            return;
        }
        if (phase == 0 && changed >= 3 && patchesVisible(image)) {
            fixture.resize(540, 300);
            resized = nativeWindowSize(static_cast<quintptr>(fixture.winId()));
            changesBeforeResize = changed;
            phase = 1;
        } else if (phase == 1 && changed >= changesBeforeResize + 3 && patchesVisible(image) &&
                   std::abs(image.width() - resized.width()) <= 2 &&
                   std::abs(image.height() - resized.height()) <= 2) {
            capture.stop();
            {
                std::lock_guard lock(frames.mutex);
                stoppedCount = frames.total;
            }
            stoppedAt = elapsed.elapsed();
            phase = 2;
        } else if (phase == 2 && elapsed.elapsed() - stoppedAt >= 120) {
            if (total != stoppedCount) {
                finish(false, QStringLiteral("Capture delivered frames after stop()."));
                return;
            }
            finish(true, QStringLiteral("WGC passed: colored pixels, changing frames, resize, and no callbacks after stop (%1 frames).")
                .arg(total));
        }
    });
    inspect.setInterval(20);

    fixture.show();
    const quintptr handle = static_cast<quintptr>(fixture.winId());
    QTimer::singleShot(100, &application, [&] {
        elapsed.start();
        const HWND native = reinterpret_cast<HWND>(handle);
        std::cout << "Fixture HWND=0x" << std::hex << handle
                  << " style=0x" << GetWindowLongPtrW(native, GWL_STYLE)
                  << " exstyle=0x" << GetWindowLongPtrW(native, GWL_EXSTYLE)
                  << std::dec << " visible=" << IsWindowVisible(native)
                  << " iconic=" << IsIconic(native)
                  << " topLevel=" << (GetAncestor(native, GA_ROOT) == native) << '\n';
        capture.start(handle, [&](queuepilot::VideoFrame frame) {
            std::lock_guard lock(frames.mutex);
            if (frames.latest != frame.image) ++frames.changed;
            frames.latest = std::move(frame.image);
            ++frames.total;
        }, [&](QString status) {
            std::lock_guard lock(frames.mutex);
            frames.status = std::move(status);
        });
        animate.start();
        inspect.start();
    });
    return application.exec();
}
