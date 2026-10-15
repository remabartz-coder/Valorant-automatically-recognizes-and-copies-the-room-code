#pragma once
#include "recognition_worker.h"
#include "video_decoder.h"
#include "window_capture.h"
#include "stream_resolver.h"
#include "speech_notifier.h"
#include <QMainWindow>
#include <QTimer>

class QComboBox;
class QLineEdit;
class QCheckBox;
class QPushButton;
class QToolButton;
class QLabel;
class QSpinBox;
class QDoubleSpinBox;
class QTableWidget;

namespace queuepilot {
class PreviewWidget;
class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    void startDemo();
    void startSource(const QString& source);
    void startRecognition();
    void stopRecognition();
    void setOutputsEnabled(bool enabled);
    bool savePreview(const QString& path);
protected:
    bool nativeEvent(const QByteArray&, void*, qintptr*) override;
private:
    void start();
    void stop();
    void detection(Detection value, std::uint64_t session);
    void log(QString action, QString code = {}, double elapsed = -1);
    void fill(bool automatic);
    void refreshWindows();
    void demoFrame();
    void saveSettings();
    void status(QString message, std::uint64_t session);
    void updateControls();
    GateConfig gateConfig() const;
    void updateRecognitionConfig();
    void invalidateDetection(const QString& message = {});
    void applyRoi(QRectF roi);
    void locate();
    void refreshCrop();
    void refreshVideoInfo(const QSize& resolution = {});
    void updateFillHotkey();
    PreviewWidget* preview_ = nullptr;
    QComboBox* sourceType_ = nullptr;
    QComboBox* windows_ = nullptr;
    QComboBox* quality_ = nullptr;
    QComboBox* inputMode_ = nullptr;
    QLineEdit* source_ = nullptr;
    QLineEdit* cookies_ = nullptr;
    QCheckBox* armed_ = nullptr;
    QCheckBox* submit_ = nullptr;
    QCheckBox* typed_ = nullptr;
    QCheckBox* roiLocked_ = nullptr;
    QCheckBox* voiceAnnouncement_ = nullptr;
    QComboBox* codeFormat_ = nullptr;
    QSpinBox* confirmations_ = nullptr;
    QSpinBox* samplingFps_ = nullptr;
    QDoubleSpinBox* confidence_ = nullptr;
    QPushButton* start_ = nullptr;
    QPushButton* stop_ = nullptr;
    QPushButton* recognitionStart_ = nullptr;
    QPushButton* recognitionStop_ = nullptr;
    QPushButton* locate_ = nullptr;
    QToolButton* copy_ = nullptr;
    QToolButton* browse_ = nullptr;
    QToolButton* refresh_ = nullptr;
    QLabel* state_ = nullptr;
    QLabel* recognitionState_ = nullptr;
    QLabel* videoInfo_ = nullptr;
    QLabel* cropPreview_ = nullptr;
    QLabel* code_ = nullptr;
    QLabel* candidate_ = nullptr;
    QLabel* timing_ = nullptr;
    QLabel* throughput_ = nullptr;
    QTableWidget* events_ = nullptr;
    StreamResolver resolver_;
    VideoDecoder decoder_;
    WindowCapture capture_;
    RecognitionWorker worker_;
    SpeechNotifier speech_;
    QTimer previewTimer_;
    QTimer demoTimer_;
    Clock::time_point startedAt_;
    Clock::time_point frameEpochAt_;
    Clock::time_point codeAt_;
    Clock::time_point armedAt_;
    Clock::time_point previewReceivedAt_;
    Clock::time_point metricsUpdatedAt_;
    QString latestCode_;
    QString lastCandidate_;
    QString blockedCode_;
    QString actualQuality_;
    QSize sourceResolution_;
    std::uint64_t session_ = 0;
    std::uint64_t sourceGeneration_ = 0;
    std::uint64_t revision_ = 0;
    std::uint64_t demoSequence_ = 0;
    std::uint64_t previewSequence_ = 0;
    bool running_ = false;
    bool recognitionEnabled_ = false;
    bool liveInput_ = false;
    bool hasDetection_ = false;
    bool locating_ = false;
    bool selectingRoi_ = false;
    bool reconnecting_ = false;
    bool modelAvailable_ = false;
    bool outputsEnabled_ = true;
    bool fillHotkey_ = false;
    bool stopHotkey_ = false;
    int reconnectAttempts_ = 0;
};
}
