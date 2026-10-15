#include "main_window.h"
#include "preview_widget.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QLabel>
#include <QLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QThread>
#include <QTemporaryFile>
#include <QToolButton>

#include <cstdio>
#include <functional>
#include <utility>
#include <vector>
#include <thread>
#include <windows.h>

using queuepilot::MainWindow;
using queuepilot::PreviewWidget;

namespace {
int failures = 0;
QElapsedTimer elapsed;
constexpr qint64 overallWaitMs = 18000;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL at %lld ms: %s\n", static_cast<long long>(elapsed.elapsed()), message);
        ++failures;
    }
}
bool f8Available() {
    bool available = false;
    std::thread probe([&] {
        available = RegisterHotKey(nullptr, 0x71, MOD_NOREPEAT, VK_F8);
        if (available) UnregisterHotKey(nullptr, 0x71);
    });
    probe.join();
    return available;
}

bool waitFor(const std::function<bool()>& predicate, int timeoutMs) {
    const qint64 stepEnd = qMin(elapsed.elapsed() + timeoutMs, overallWaitMs);
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (predicate()) return true;
        QThread::msleep(5);
    } while (elapsed.elapsed() < stepEnd);
    return predicate();
}

template <typename Widget>
Widget* textWidget(MainWindow& window, const QString& text) {
    for (Widget* widget : window.findChildren<Widget*>()) {
        if (widget->text() == text) return widget;
    }
    return nullptr;
}

bool isDemoCode(const QString& text) {
    return text == QStringLiteral("TYH663") || text == QStringLiteral("ABC123") ||
        text == QStringLiteral("H6T3Y6");
}

bool writeCodeVideo(QTemporaryFile& file, const QString& code) {
    if (!file.open()) return false;
    QImage image(384, 128, QImage::Format_RGB32);
    image.fill(QColor(25, 25, 25));
    QPainter painter(&image);
    QFont font(QStringLiteral("Arial"));
    font.setPixelSize(64);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.drawText(image.rect(), Qt::AlignCenter, code);
    painter.end();
    const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    const QByteArray header = QByteArrayLiteral("YUV4MPEG2 W384 H128 F10:1 Ip A1:1 Cmono\n");
    if (file.write(header) != header.size()) return false;
    // Constant video content keeps rule changes independent of the rotating demo's phase.
    for (int frame = 0; frame < 200; ++frame) {
        if (file.write("FRAME\n") != 6) return false;
        for (int y = 0; y < gray.height(); ++y) {
            if (file.write(reinterpret_cast<const char*>(gray.constScanLine(y)), gray.width()) != gray.width())
                return false;
        }
    }
    return file.flush();
}

QPointF previewPoint(const PreviewWidget& preview, qreal x, qreal y) {
    const QSize image = QSize(1280, 720).scaled(preview.size(), Qt::KeepAspectRatio);
    return QPointF((preview.width() - image.width()) / 2.0 + x * image.width(),
                   (preview.height() - image.height()) / 2.0 + y * image.height());
}

void sendMouse(PreviewWidget& preview, QEvent::Type type, QPointF position,
               Qt::MouseButton button, Qt::MouseButtons buttons) {
    QMouseEvent event(type, position, position, preview.mapToGlobal(position.toPoint()),
                      button, buttons, Qt::NoModifier);
    QApplication::sendEvent(&preview, &event);
}

void settleLayout(MainWindow& window) {
    window.layout()->activate();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
    window.grab();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
}

void checkStableGeometry(MainWindow& window, PreviewWidget& preview, QTabWidget& tabs) {
    const QStringList names = {
        QStringLiteral("rawCandidate"), QStringLiteral("recognitionTiming"),
        QStringLiteral("recognitionThroughput"), QStringLiteral("playbackState"),
        QStringLiteral("recognitionState"), QStringLiteral("videoInfo")
    };
    std::vector<std::pair<QLabel*, QString>> labels;
    for (const auto& name : names) {
        auto* label = window.findChild<QLabel*>(name);
        expect(label != nullptr, "dynamic recognition labels expose stable test identifiers");
        if (label) labels.emplace_back(label, label->text());
    }
    auto* crop = window.findChild<QLabel*>(QStringLiteral("cropPreview"));
    auto* events = window.findChild<QTableWidget*>(QStringLiteral("eventLog"));
    expect(crop && events, "crop and event history are available for geometry regression");
    if (labels.size() != static_cast<std::size_t>(names.size()) || !crop || !events) return;
    const QString originalCropText = crop->text();
    const QPixmap originalCrop = crop->pixmap();
    const int originalTab = tabs.currentIndex();
    const QString longText(256, QLatin1Char('W'));
    const QStringList updates = {
        QStringLiteral("--"),
        QStringLiteral("Raw candidate: ABC123 / minimum confidence: 99.99% / ") + longText,
        QStringLiteral("ABC123\nInference: 1234.5 ms\nProcessed frames: 99999999\n") + longText
    };
    for (const QSize size : {QSize(960, 680), QSize(1200, 820)}) {
        window.resize(size);
        settleLayout(window);
        const QSize expectedWindow = window.size();
        const QRect expectedPreview = preview.geometry();
        expect(expectedWindow == size && expectedPreview.width() > 0 && expectedPreview.height() > 0,
               "minimum and default layouts fit the requested window size");
        for (qsizetype iteration = 0; iteration < updates.size(); ++iteration) {
            for (const auto& label : labels) label.first->setText(updates[iteration]);
            events->insertRow(0);
            for (int column = 0; column < events->columnCount(); ++column)
                events->setItem(0, column, new QTableWidgetItem(updates[iteration]));
            QPixmap cropImage(iteration == 0 ? QSize(1, 1) :
                              iteration == 1 ? QSize(2048, 96) : QSize(96, 768));
            cropImage.fill(Qt::green);
            crop->setPixmap(cropImage);
            for (int tab = 0; tab < tabs.count(); ++tab) {
                tabs.setCurrentIndex(tab);
                settleLayout(window);
                expect(window.size() == expectedWindow && preview.geometry() == expectedPreview,
                       "candidate, metrics, status, history, crop and tab changes preserve preview geometry");
            }
        }
        for (qsizetype iteration = 0; iteration < updates.size(); ++iteration) events->removeRow(0);
        for (const auto& label : labels) label.first->setText(label.second);
        if (originalCrop.isNull()) crop->setText(originalCropText);
        else crop->setPixmap(originalCrop);
        tabs.setCurrentIndex(originalTab);
        settleLayout(window);
        expect(window.size() == expectedWindow && preview.geometry() == expectedPreview,
               "restoring short labels and empty crop preserves preview geometry");
    }
    window.resize(960, 680);
    settleLayout(window);
}
}

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    application.setStyle(QStringLiteral("Fusion"));
    application.setQuitOnLastWindowClosed(false);
    elapsed.start();
    const QString previousClipboard = QApplication::clipboard()->text();
    const bool f8InitiallyAvailable = f8Available();
    const QString settingsPath = QDir(QCoreApplication::applicationDirPath()).filePath("data/settings.ini");
    QDir().mkpath(QFileInfo(settingsPath).absolutePath());
    {
        QSettings settings(settingsPath, QSettings::IniFormat);
        settings.setValue("source", QStringLiteral("https://live.douyin.com/test.flv?token=private"));
    }
    MainWindow window;
    window.setAttribute(Qt::WA_ShowWithoutActivating);
    auto* voiceAnnouncement = window.findChild<QCheckBox*>(QStringLiteral("voiceAnnouncement"));
    expect(voiceAnnouncement && voiceAnnouncement->isChecked(), "recognition success voice announcements default to enabled");
    window.setOutputsEnabled(false);
    expect(voiceAnnouncement && !voiceAnnouncement->isEnabled() && voiceAnnouncement->isChecked(),
           "disabled test output prevents voice announcements and retains their saved preference");
    window.resize(960, 680);

    auto* play = textWidget<QPushButton>(window, QStringLiteral("\u5f00\u59cb\u64ad\u653e"));
    auto* stop = textWidget<QPushButton>(window, QStringLiteral("\u505c\u6b62\u64ad\u653e"));
    auto* recognize = textWidget<QPushButton>(window, QStringLiteral("\u5f00\u59cb\u8bc6\u522b"));
    auto* pause = textWidget<QPushButton>(window, QStringLiteral("\u505c\u6b62\u8bc6\u522b"));
    auto* locate = textWidget<QPushButton>(window, QStringLiteral("\u81ea\u52a8\u5b9a\u4f4d\u623f\u95f4\u7801"));
    auto* confirmed = textWidget<QLabel>(window, QStringLiteral("------"));
    auto* armed = textWidget<QCheckBox>(window, QStringLiteral("\u542f\u7528\u672c\u8f6e\u81ea\u52a8\u586b\u5165"));
    auto* preview = window.findChild<PreviewWidget*>();
    auto* sourceType = window.findChild<QComboBox*>(QStringLiteral("sourceType"));
    auto* source = window.findChild<QLineEdit*>(QStringLiteral("sourceAddress"));
    auto* inputMode = window.findChild<QComboBox*>(QStringLiteral("inputMode"));
    auto* format = window.findChild<QComboBox*>(QStringLiteral("codeFormat"));
    auto* threshold = window.findChild<QDoubleSpinBox*>(QStringLiteral("confidenceThreshold"));
    auto* roiLocked = window.findChild<QCheckBox*>(QStringLiteral("roiLocked"));
    auto* settingsTabs = window.findChild<QTabWidget*>(QStringLiteral("settingsTabs"));
    QSpinBox* sampling = nullptr;
    for (auto* spin : window.findChildren<QSpinBox*>()) {
        if (spin->minimum() == 5 && spin->maximum() == 60) sampling = spin;
        if (spin->minimum() == 2 && spin->maximum() == 5) spin->setValue(2);
    }
    for (auto* spin : window.findChildren<QDoubleSpinBox*>()) spin->setValue(.90);
    expect(play && stop && recognize && pause && locate && confirmed && armed && preview && sampling && sourceType && source && inputMode && format && threshold && roiLocked && settingsTabs,
           "main window exposes playback, recognition, location and sampling controls");
    if (failures != 0) return 1;
    expect(source->text().isEmpty(), "startup removes previously saved signed media URLs and has no default streamer");
    expect(!QSettings(settingsPath, QSettings::IniFormat).contains("source"), "startup also removes unsafe source from disk");
    if (f8InitiallyAvailable && QApplication::platformName() == QStringLiteral("windows")) {
        expect(f8Available(), "default clipboard mode leaves F8 available to other applications");
        inputMode->setCurrentIndex(1);
        expect(f8Available(), "disabled output does not register F8 in hotkey mode");
        window.setOutputsEnabled(true);
        expect(!f8Available(), "enabled hotkey mode reserves F8");
        inputMode->setCurrentIndex(2);
        expect(f8Available(), "leaving hotkey mode releases F8");
        inputMode->setCurrentIndex(1);
        window.setOutputsEnabled(false);
        expect(f8Available(), "disabling output releases F8");
        inputMode->setCurrentIndex(0);
    } else {
        if (f8InitiallyAvailable) expect(f8Available(), "offscreen and disabled-output UI does not reserve F8");
        std::printf("Native F8 ownership checks skipped: no native Windows platform or key already reserved.\n");
    }
    expect(settingsTabs->count() == 3 && settingsTabs->tabText(0) == QStringLiteral("\u6765\u6e90") &&
           settingsTabs->tabText(1) == QStringLiteral("\u8bc6\u522b") &&
           settingsTabs->tabText(2) == QStringLiteral("\u8f93\u51fa"),
           "settings are organized into Source, Recognition and Output tabs");
    settingsTabs->setCurrentIndex(0);
    checkStableGeometry(window, *preview, *settingsTabs);
    if (failures != 0) return 1;
    preview->setRoi(QRectF(.35, .38, .30, .14));
    roiLocked->setChecked(false);
    format->setCurrentIndex(0);
    sampling->setValue(30);

    window.startDemo();
    expect(!play->isEnabled() && stop->isEnabled(), "demo playback starts independently");
    expect(waitFor([&] { return isDemoCode(confirmed->text()); }, 4000),
           "demo publishes a confirmed room code");
    expect(!preview->croppedFrame().isNull(), "demo produces a visible ROI crop");
    expect(!armed->isEnabled() && !armed->isChecked(), "test output mode keeps automatic input disabled");
    expect(window.grab().save(QDir(QCoreApplication::applicationDirPath()).filePath("ui-minimum.png")),
           "minimum window renders to an image");
    if (failures != 0) {
        stop->click();
        return 1;
    }

    const Qt::WindowStates normalState = window.windowState();
    window.setWindowState(normalState | Qt::WindowMinimized);
    expect(window.isMinimized() && !window.isVisible() && !window.isActiveWindow(),
           "background recognition regression keeps the test window minimized without showing or activating it");
    pause->click();
    expect(confirmed->text() == QStringLiteral("------"), "minimized recognition pause clears the old confirmation");
    recognize->click();
    expect(waitFor([&] {
        return isDemoCode(confirmed->text()) && !preview->croppedFrame().isNull();
    }, 1500), "minimized recognition confirms a new code from fresh background frames");
    expect(window.isMinimized() && !window.isVisible() && !window.isActiveWindow(),
           "background confirmation does not show or activate the window");
    expect(QApplication::clipboard()->text() == previousClipboard,
           "background recognition respects disabled clipboard output");
    window.setWindowState(normalState);
    settleLayout(window);

    int selectionStarts = 0;
    QObject::connect(preview, &PreviewWidget::roiSelectionStarted, &window, [&] { ++selectionStarts; });
    const QRectF lockedRoi = preview->roi();
    roiLocked->setChecked(true);
    expect(preview->selectionLocked() && !locate->isEnabled(), "locking the ROI disables manual selection and automatic location");
    sendMouse(*preview, QEvent::MouseButtonPress, previewPoint(*preview, .10, .15), Qt::LeftButton, Qt::LeftButton);
    sendMouse(*preview, QEvent::MouseMove, previewPoint(*preview, .25, .30), Qt::NoButton, Qt::LeftButton);
    sendMouse(*preview, QEvent::MouseButtonRelease, previewPoint(*preview, .25, .30), Qt::LeftButton, Qt::NoButton);
    expect(selectionStarts == 0 && preview->roi() == lockedRoi && isDemoCode(confirmed->text()),
           "dragging a locked preview leaves the ROI and recognition untouched");
    expect(!recognize->isEnabled() && pause->isEnabled() && stop->isEnabled(),
           "locking a region keeps playback and recognition running");

    const QImage beforePause = preview->croppedFrame();
    pause->click();
    expect(confirmed->text() == QStringLiteral("------"), "pausing recognition clears its confirmed code");
    expect(recognize->isEnabled() && !pause->isEnabled() && stop->isEnabled(),
           "pausing recognition keeps playback active and permits resume");
    expect(waitFor([&] { return !preview->croppedFrame().isNull() && preview->croppedFrame() != beforePause; }, 5000),
           "video preview continues changing while recognition is paused");
    expect(confirmed->text() == QStringLiteral("------"), "paused recognition publishes no code");

    recognize->click();
    expect(waitFor([&] { return isDemoCode(confirmed->text()); }, 4000),
           "resuming recognition reconfirms a code from fresh frames");
    expect(!recognize->isEnabled() && pause->isEnabled(), "resumed recognition updates its controls");
    expect(preview->selectionLocked() && !locate->isEnabled(), "locked preview resumes recognition without re-enabling automatic location");
    if (failures != 0) {
        stop->click();
        return 1;
    }

    roiLocked->setChecked(false);
    expect(!preview->selectionLocked(), "unlocking the ROI permits a new manual selection");
    const QRectF beforeInterruptedDrag = preview->roi();
    sendMouse(*preview, QEvent::MouseButtonPress, previewPoint(*preview, .10, .15), Qt::LeftButton, Qt::LeftButton);
    sendMouse(*preview, QEvent::MouseMove, previewPoint(*preview, .25, .30), Qt::NoButton, Qt::LeftButton);
    expect(selectionStarts == 1, "an unlocked drag starts selection and temporarily pauses recognition");
    roiLocked->setChecked(true);
    sendMouse(*preview, QEvent::MouseButtonRelease, previewPoint(*preview, .25, .30), Qt::LeftButton, Qt::NoButton);
    expect(preview->roi() == beforeInterruptedDrag && preview->selectionLocked(),
           "locking during a drag restores its original ROI");
    expect(waitFor([&] { return isDemoCode(confirmed->text()); }, 800),
           "locking during selection restores recognition instead of leaving the worker paused");
    roiLocked->setChecked(false);
    const QRectF beforeClearedDrag = preview->roi();
    sendMouse(*preview, QEvent::MouseButtonPress, previewPoint(*preview, .10, .15), Qt::LeftButton, Qt::LeftButton);
    sendMouse(*preview, QEvent::MouseMove, previewPoint(*preview, .25, .30), Qt::NoButton, Qt::LeftButton);
    expect(selectionStarts == 2, "another unlocked drag starts before the picture is cleared");
    preview->clearFrame();
    expect(preview->roi() == beforeClearedDrag && preview->croppedFrame().isNull(),
           "clearing an active selection restores its original ROI and empties its crop");
    sendMouse(*preview, QEvent::MouseButtonRelease, previewPoint(*preview, .25, .30), Qt::LeftButton, Qt::NoButton);
    expect(waitFor([&] { return isDemoCode(confirmed->text()); }, 800),
           "fresh demo frames resume recognition after an active drag is cleared");

    pause->click();
    expect(waitFor([&] { return locate->isEnabled(); }, 500), "fresh paused preview permits automatic location");
    locate->click();
    expect(waitFor([&] {
        for (auto* label : window.findChildren<QLabel*>()) {
            if (label->text().contains(QStringLiteral("\u533a\u57df\u5df2\u5b9a\u4f4d"))) return true;
        }
        return false;
    }, 3000), "automatic location finds a room-code ROI while recognition is paused");
    expect(!preview->croppedFrame().isNull() && preview->roi().width() > 0 && preview->roi().height() > 0,
           "located ROI has a nonempty preview");
    expect(confirmed->text() == QStringLiteral("------"), "automatic location alone cannot publish a confirmation");
    recognize->click();
    expect(waitFor([&] { return isDemoCode(confirmed->text()); }, 1500),
           "recognition confirms again after applying the located ROI");

    sampling->setValue(5);
    expect(confirmed->text() == QStringLiteral("------"), "sampling changes invalidate the previous confirmation");
    expect(waitFor([&] { return isDemoCode(confirmed->text()); }, 1500),
           "five-FPS sampling continues to confirm codes");
    sampling->setValue(60);
    expect(waitFor([&] { return isDemoCode(confirmed->text()); }, 1500),
           "sixty-FPS sampling continues to confirm codes");

    QTemporaryFile orderedVideo(QDir::tempPath() + QStringLiteral("/queuepilot-ordered-XXXXXX.y4m"));
    QTemporaryFile mixedVideo(QDir::tempPath() + QStringLiteral("/queuepilot-mixed-XXXXXX.y4m"));
    expect(writeCodeVideo(orderedVideo, QStringLiteral("TYH663")) &&
           writeCodeVideo(mixedVideo, QStringLiteral("H6T3Y6")),
           "constant ordered and mixed-order videos can be created");
    if (failures != 0) {
        stop->click();
        return 1;
    }
    orderedVideo.close();
    mixedVideo.close();
    const auto startRuleVideo = [&](const QString& path) {
        sourceType->setCurrentIndex(2);
        source->setText(path);
        preview->setRoi(QRectF(0, 0, 1, 1));
        play->click();
        recognize->click();
    };
    startRuleVideo(orderedVideo.fileName());
    expect(waitFor([&] { return confirmed->text() == QStringLiteral("TYH663"); }, 1500),
           "fixed ordered fixture confirms before runtime rule changes");
    expect(format->isEnabled() && threshold->isEnabled(), "recognition rules can be changed during playback");
    expect(format->count() == 2, "every available format requires three uppercase letters and three digits");
    threshold->setValue(threshold->maximum());
    expect(confirmed->text() == QStringLiteral("------"), "confidence change invalidates the old confirmed code");
    threshold->setValue(.90);
    expect(waitFor([&] { return confirmed->text() == QStringLiteral("TYH663"); }, 1500),
           "restoring confidence reconfirms the code without restarting playback");
    format->setCurrentIndex(1);
    expect(confirmed->text() == QStringLiteral("------"), "format change clears the previous confirmation");
    expect(waitFor([&] { return confirmed->text() == QStringLiteral("TYH663"); }, 1500),
           "ordered format continues recognizing matching codes");
    format->setCurrentIndex(0);

    auto* raw = window.findChild<QLabel*>(QStringLiteral("rawCandidate"));
    stop->click();
    startRuleVideo(mixedVideo.fileName());
    expect(raw && waitFor([&] { return raw->text().contains(QStringLiteral("H6T3Y6")); }, 1500),
           "mixed-order fixture becomes the current raw OCR candidate");
    if (failures != 0) {
        stop->click();
        return 1;
    }
    format->setCurrentIndex(1);
    expect(confirmed->text() == QStringLiteral("------"), "ordered format clears the previous confirmation");
    expect(waitFor([&] {
        if (!raw->text().contains(QStringLiteral("H6T3Y6"))) return false;
        for (auto* label : window.findChildren<QLabel*>()) {
            if (label->text().contains(QStringLiteral("\u88ab\u6240\u9009\u7801\u578b\u89c4\u5219\u8fc7\u6ee4"))) return true;
        }
        return false;
    }, 1200), "ordered format exposes the rejection reason for a mixed-order code");
    expect(confirmed->text() == QStringLiteral("------") && raw->text().contains(QStringLiteral("H6T3Y6")),
           "filtered OCR stays visible without publishing a confirmation");
    format->setCurrentIndex(0);
    expect(waitFor([&] { return confirmed->text() == QStringLiteral("H6T3Y6"); }, 1500),
           "restoring balanced format confirms the same mixed-order code without restarting playback");
    window.resize(1200, 820);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    expect(window.grab().save(QDir(QCoreApplication::applicationDirPath()).filePath("ui-default.png")),
           "default window renders to an image");
    settingsTabs->setCurrentIndex(1);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    expect(settingsTabs->currentIndex() == 1 &&
           window.grab().save(QDir(QCoreApplication::applicationDirPath()).filePath("ui-recognition.png")),
           "recognition settings tab renders to an image");
    settingsTabs->setCurrentIndex(2);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    expect(settingsTabs->currentIndex() == 2 &&
           window.grab().save(QDir(QCoreApplication::applicationDirPath()).filePath("ui-output.png")),
           "output settings tab renders to an image");
    settingsTabs->setCurrentIndex(0);

    roiLocked->setChecked(true);
    stop->click();
    expect(play->isEnabled() && !stop->isEnabled() && !recognize->isEnabled() && !pause->isEnabled(),
           "stopping playback disables recognition controls");
    expect(confirmed->text() == QStringLiteral("------") && preview->croppedFrame().isNull(),
           "stopping playback clears the code and preview");
    expect(roiLocked->isChecked() && preview->selectionLocked(), "stopping playback preserves the ROI lock");

    window.startDemo();
    expect(roiLocked->isChecked() && preview->selectionLocked() && !locate->isEnabled(),
           "restarting playback preserves the ROI lock and location guard");
    expect(sourceType->isEnabled(), "source selector remains editable during playback");
    expect(!source->isEnabled(), "generated frames do not require a source address");
    sourceType->setCurrentIndex(0);
    expect(play->isEnabled() && !stop->isEnabled() && source->isEnabled() && preview->croppedFrame().isNull(),
           "switching from demo to a live source stops playback and enables address entry");
    roiLocked->setChecked(false);
    const auto beginDirectSource = [&] {
        window.startSource(QStringLiteral("https://media.example/test.flv"));
        expect(source->isEnabled() && stop->isEnabled(), "live address remains editable while connecting");
    };
    beginDirectSource();
    source->selectAll();
    QKeyEvent edit(QEvent::KeyPress, Qt::Key_H, Qt::NoModifier, QStringLiteral("h"));
    QApplication::sendEvent(source, &edit);
    expect(source->text() == QStringLiteral("h") && play->isEnabled() && !stop->isEnabled() && !recognize->isEnabled(),
           "user typing preserves the edited address and stops the old source immediately");
    expect(!armed->isChecked() && confirmed->text() == QStringLiteral("------"),
           "address editing clears the old code and automatic input");
    beginDirectSource();
    source->selectAll();
    QKeyEvent clear(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
    QApplication::sendEvent(source, &clear);
    expect(source->text().isEmpty() && play->isEnabled() && !stop->isEnabled(),
           "clearing a live address also stops the old source");
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    expect(play->isEnabled() && !stop->isEnabled(), "cancelled source callbacks cannot restart playback after editing");
    const auto saveSource = [&](const QString& address) {
        source->setText(address);
        roiLocked->setChecked(!roiLocked->isChecked());
        return QSettings(settingsPath, QSettings::IniFormat).value("source").toString();
    };
    expect(saveSource(QStringLiteral("https://live.douyin.com/?live_web_rid=12345678901&token=private#private")) ==
           QStringLiteral("https://live.douyin.com/12345678901"), "room persistence removes query credentials and fragments");
    expect(saveSource(QStringLiteral("https://live.douyin.com/12345678901?token=private")) ==
           QStringLiteral("https://live.douyin.com/12345678901"), "canonical room persistence omits extra query values");
    for (const QString& address : {QStringLiteral("https://live.douyin.com/live.flv?token=private"),
                                  QStringLiteral("https://media.example/test.m3u8?token=private"),
                                  QStringLiteral("https://name:secret@live.douyin.com/12345678901"),
                                  QStringLiteral("http://live.douyin.com/12345678901"),
                                  QStringLiteral("https://live.douyin.com:8443/12345678901")}) {
        expect(saveSource(address).isEmpty() && !QSettings(settingsPath, QSettings::IniFormat).contains("source"),
               "unsafe or signed media sources are removed from settings");
    }
    sourceType->setCurrentIndex(2);
    expect(saveSource(QStringLiteral("https://media.example/test.mp4?token=private")).isEmpty(),
           "local-video mode cannot persist a manually entered media URL");
    QTemporaryFile localVideo(QDir(QCoreApplication::applicationDirPath()).filePath("source-test-XXXXXX.mp4"));
    expect(localVideo.open(), "local source persistence fixture can be created");
    expect(saveSource(localVideo.fileName()) == QFileInfo(localVideo.fileName()).absoluteFilePath(),
           "existing local video path is saved without inventing a URL");
    source->clear();
    roiLocked->setChecked(!roiLocked->isChecked());
    expect(QApplication::clipboard()->text() == previousClipboard,
           "disabled output leaves the clipboard unchanged");
    if (failures == 0) {
        std::printf("Native UI smoke passed: safe source persistence, scoped hotkeys, stable geometry, silent output, minimized recognition, demo, pause/resume, location, sampling, address editing and stop (%lld ms).\n",
                    static_cast<long long>(elapsed.elapsed()));
    }
    return failures == 0 ? 0 : 1;
}
