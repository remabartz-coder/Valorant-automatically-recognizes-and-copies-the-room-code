#include "preview_widget.h"

#include <QApplication>
#include <QColor>
#include <QMouseEvent>
#include <cstdio>

namespace {
int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void sendMouse(queuepilot::PreviewWidget& preview, QEvent::Type type, QPointF point,
               Qt::MouseButton button, Qt::MouseButtons buttons) {
    QMouseEvent event(type, point, point, preview.mapToGlobal(point.toPoint()),
                      button, buttons, Qt::NoModifier);
    QApplication::sendEvent(&preview, &event);
}
}

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    application.setStyle(QStringLiteral("Fusion"));
    queuepilot::PreviewWidget preview;
    preview.resize(640, 360);
    QImage frame(640, 360, QImage::Format_RGB32);
    frame.fill(QColor(70, 130, 180));
    preview.setFrame(frame);
    const QRectF initial(.35, .38, .30, .14);
    preview.setRoi(initial);
    int starts = 0;
    int changes = 0;
    QRectF emittedRoi;
    QObject::connect(&preview, &queuepilot::PreviewWidget::roiSelectionStarted, &application, [&] { ++starts; });
    QObject::connect(&preview, &queuepilot::PreviewWidget::roiChanged, &application, [&](QRectF roi) {
        ++changes;
        emittedRoi = roi;
    });
    const auto press = [&] { sendMouse(preview, QEvent::MouseButtonPress, QPointF(64, 54), Qt::LeftButton, Qt::LeftButton); };
    const auto move = [&] { sendMouse(preview, QEvent::MouseMove, QPointF(224, 144), Qt::NoButton, Qt::LeftButton); };
    const auto release = [&] { sendMouse(preview, QEvent::MouseButtonRelease, QPointF(224, 144), Qt::LeftButton, Qt::NoButton); };

    expect(!preview.selectionLocked(), "preview selection starts unlocked");
    preview.setSelectionLocked(true);
    press(); move(); release();
    expect(preview.roi() == initial && starts == 0 && changes == 0,
           "locked pointer events cannot start selection, alter ROI, or emit changes");
    preview.setSelectionLocked(false);
    press(); move(); release();
    const QRectF selected(.10, .15, .25, .25);
    expect(starts == 1 && changes == 1 && preview.roi() == selected && emittedRoi == selected,
           "unlocked drag emits one start and one finished ROI change");

    press();
    sendMouse(preview, QEvent::MouseMove, QPointF(384, 252), Qt::NoButton, Qt::LeftButton);
    expect(starts == 2 && changes == 1 && preview.roi() != selected,
           "a second drag changes the visible region without prematurely committing it");
    preview.setSelectionLocked(true);
    expect(preview.roi() == selected && emittedRoi == selected && changes == 2,
           "locking mid-drag restores the original ROI and emits its completion exactly once");
    preview.setSelectionLocked(true);
    release();
    expect(starts == 2 && changes == 2 && preview.roi() == selected,
           "repeated locking and a late release cannot emit an extra selection completion");

    preview.clearFrame();
    expect(preview.selectionLocked() && preview.roi() == selected && preview.croppedFrame().isNull(),
           "clearing the picture preserves selection lock and ROI");
    preview.setFrame(frame);
    press(); move(); release();
    expect(preview.selectionLocked() && preview.roi() == selected && starts == 2 && changes == 2,
           "new frames do not silently unlock a protected ROI");
    preview.setSelectionLocked(false);
    press();
    sendMouse(preview, QEvent::MouseMove, QPointF(384, 252), Qt::NoButton, Qt::LeftButton);
    expect(starts == 3 && changes == 2 && preview.roi() != selected,
           "an active drag changes the visible ROI before the picture is cleared");
    preview.clearFrame();
    expect(!preview.selectionLocked() && preview.roi() == selected && emittedRoi == selected &&
           changes == 3 && preview.croppedFrame().isNull(),
           "clearing during a drag restores the original ROI and emits its completion once");
    release();
    expect(starts == 3 && changes == 3 && preview.roi() == selected,
           "release after clearing cannot commit an abandoned selection");
    preview.setFrame(frame);
    press(); release();
    expect(starts == 4 && changes == 4, "a new frame restores normal selection after a cleared drag");
    if (failures == 0) std::printf("Preview selection lock checks passed.\n");
    return failures == 0 ? 0 : 1;
}
