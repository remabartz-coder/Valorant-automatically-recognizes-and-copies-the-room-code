#include "main_window.h"
#include "preview_widget.h"
#include "game_input.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QFrame>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QTableWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <windows.h>
#include <algorithm>
#include <cmath>

namespace queuepilot {
namespace {
QString dataDir() {
    const QString path = QDir(QCoreApplication::applicationDirPath()).filePath("data");
    QDir().mkpath(path);
    return path;
}
QString persistentSource(const QString& source) {
    const QString input = source.trimmed();
    const QUrl url(input, QUrl::StrictMode);
    const QFileInfo local(url.isLocalFile() ? url.toLocalFile() : input);
    if (local.isFile()) return local.absoluteFilePath();
    if (!url.isValid() || url.scheme() != QStringLiteral("https") ||
        url.host().compare(QStringLiteral("live.douyin.com"), Qt::CaseInsensitive) != 0 ||
        !url.userInfo().isEmpty() || (url.port() != -1 && url.port() != 443)) return {};
    const QRegularExpression roomId(QStringLiteral("\\A[0-9]{6,20}\\z"));
    QString id;
    if (url.path().isEmpty() || url.path() == QStringLiteral("/")) {
        id = QUrlQuery(url).queryItemValue(QStringLiteral("live_web_rid"));
    } else if (url.path().startsWith(QLatin1Char('/'))) {
        id = url.path().mid(1);
    }
    return roomId.match(id).hasMatch() ? QStringLiteral("https://live.douyin.com/") + id : QString{};
}
QToolButton* iconButton(QWidget* parent, QStyle::StandardPixmap icon, const QString& tooltip) {
    auto* button = new QToolButton(parent);
    button->setIcon(parent->style()->standardIcon(icon));
    button->setToolTip(tooltip);
    button->setFixedSize(32, 32);
    return button;
}
QString qualityName(const QString& id) {
    if (id == "origin") return QStringLiteral("原画");
    if (id == "uhd") return QStringLiteral("蓝光");
    if (id == "hd") return QStringLiteral("超清");
    if (id == "sd") return QStringLiteral("高清");
    if (id == "ld") return QStringLiteral("标清");
    if (id == "auto") return QStringLiteral("自动");
    return id.isEmpty() ? QStringLiteral("--") : id;
}
bool decoderTerminal(const QString& message) {
    return message.startsWith("Video source must be") || message.startsWith("Video request headers") ||
        message.startsWith("Video decoder allocation") || message.startsWith("Could not open the video") ||
        message.startsWith("Could not read video") || message.startsWith("The source has no supported video") ||
        message.startsWith("The video codec is not supported") || message.startsWith("Video codec") ||
        message.startsWith("Video frame") || message.startsWith("The video frame dimensions") ||
        message.startsWith("Video color conversion") || message.startsWith("Video reading") ||
        message.startsWith("Video packet") || message.startsWith("Video decoding stopped") ||
        message.startsWith("The local video") || message == "Video source ended.";
}
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), resolver_(this) {
    setWindowTitle(QStringLiteral("QueuePilot · 房间码助手"));
    resize(1200, 820);
    setMinimumSize(960, 680);
    modelAvailable_ = QFile::exists(QDir(QCoreApplication::applicationDirPath()).filePath("models/recognizer.onnx"));
    auto* central = new QWidget(this);
    central->setObjectName("appSurface");
    setCentralWidget(central);
    auto* outer = new QVBoxLayout(central);
    outer->setContentsMargins(20, 16, 20, 16);
    outer->setSpacing(12);
    auto* toolbar = new QHBoxLayout;
    toolbar->setSpacing(6);
    auto* title = new QLabel("QueuePilot");
    title->setObjectName("appTitle");
    toolbar->addWidget(title);
    auto* subtitle = new QLabel(QStringLiteral("房间码助手"));
    subtitle->setProperty("muted", true);
    toolbar->addSpacing(6);
    toolbar->addWidget(subtitle);
    toolbar->addStretch();
    start_ = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay), QStringLiteral("开始播放"));
    stop_ = new QPushButton(style()->standardIcon(QStyle::SP_MediaStop), QStringLiteral("停止播放"));
    stop_->setToolTip(QStringLiteral("停止播放、识别和自动填入（Ctrl+Alt+F10）"));
    recognitionStart_ = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay), QStringLiteral("开始识别"));
    recognitionStart_->setObjectName("primaryAction");
    recognitionStop_ = new QPushButton(style()->standardIcon(QStyle::SP_MediaPause), QStringLiteral("停止识别"));
    toolbar->addWidget(start_);
    toolbar->addWidget(stop_);
    auto* toolbarDivider = new QFrame;
    toolbarDivider->setFrameShape(QFrame::VLine);
    toolbarDivider->setFixedHeight(20);
    toolbar->addSpacing(4);
    toolbar->addWidget(toolbarDivider);
    toolbar->addSpacing(4);
    toolbar->addWidget(recognitionStart_);
    toolbar->addWidget(recognitionStop_);
    outer->addLayout(toolbar);
    auto* statusRow = new QHBoxLayout;
    state_ = new QLabel(QStringLiteral("播放: 待机"));
    state_->setObjectName("playbackState");
    state_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    state_->setWordWrap(true);
    state_->setFixedHeight(32);
    statusRow->addWidget(state_, 1);
    recognitionState_ = new QLabel(QStringLiteral("识别: 暂停"));
    recognitionState_->setObjectName("recognitionState");
    recognitionState_->setWordWrap(true);
    recognitionState_->setFixedSize(270, 32);
    statusRow->addWidget(recognitionState_);
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(1);
    splitter->setChildrenCollapsible(false);
    outer->addWidget(splitter, 1);
    auto* settingsTabs = new QTabWidget;
    settingsTabs->setObjectName("settingsTabs");
    settingsTabs->setDocumentMode(true);
    settingsTabs->setUsesScrollButtons(false);
    settingsTabs->setMinimumWidth(264);
    settingsTabs->setMaximumWidth(288);
    const auto settingsForm = [settingsTabs](const QString& name) {
        auto* page = new QWidget;
        auto* layout = new QFormLayout(page);
        layout->setContentsMargins(14, 18, 14, 14);
        layout->setVerticalSpacing(8);
        layout->setRowWrapPolicy(QFormLayout::WrapAllRows);
        layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        layout->setFormAlignment(Qt::AlignTop);
        settingsTabs->addTab(page, name);
        return layout;
    };
    auto* form = settingsForm(QStringLiteral("来源"));
    sourceType_ = new QComboBox;
    sourceType_->setObjectName("sourceType");
    sourceType_->addItems({QStringLiteral("直播链接 / 媒体流"), QStringLiteral("窗口采集"),
                           QStringLiteral("本地视频"), QStringLiteral("测试画面")});
    form->addRow(QStringLiteral("画面来源"), sourceType_);
    source_ = new QLineEdit;
    source_->setObjectName("sourceAddress");
    source_->setPlaceholderText(QStringLiteral("直播链接或媒体地址"));
    source_->setToolTip(QStringLiteral("修改地址会停止当前播放和自动填入"));
    source_->setClearButtonEnabled(true);
    auto* sourceRow = new QHBoxLayout;
    sourceRow->addWidget(source_, 1);
    browse_ = iconButton(this, QStyle::SP_DialogOpenButton, QStringLiteral("选择本地视频"));
    sourceRow->addWidget(browse_);
    form->addRow(QStringLiteral("地址"), sourceRow);
    windows_ = new QComboBox;
    windows_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    windows_->setMinimumContentsLength(10);
    auto* windowRow = new QHBoxLayout;
    windowRow->addWidget(windows_, 1);
    refresh_ = iconButton(this, QStyle::SP_BrowserReload, QStringLiteral("刷新可采集窗口"));
    windowRow->addWidget(refresh_);
    form->addRow(QStringLiteral("窗口"), windowRow);
    quality_ = new QComboBox;
    for (const QString& id : {QStringLiteral("auto"), QStringLiteral("origin"), QStringLiteral("uhd"),
                              QStringLiteral("hd"), QStringLiteral("sd"), QStringLiteral("ld")})
        quality_->addItem(qualityName(id), id);
    form->addRow(QStringLiteral("画质"), quality_);
    cookies_ = new QLineEdit;
    cookies_->setEchoMode(QLineEdit::Password);
    cookies_->setPlaceholderText(QStringLiteral("可选，仅本次会话"));
    cookies_->setToolTip(QStringLiteral("仅用于 HTTPS 抖音直播页面；不发送给媒体流，不写入配置或日志"));
    form->addRow(QStringLiteral("Cookie"), cookies_);
    auto* recognitionForm = settingsForm(QStringLiteral("识别"));
    samplingFps_ = new QSpinBox;
    samplingFps_->setRange(5, 60);
    samplingFps_->setValue(30);
    samplingFps_->setSuffix(" FPS");
    recognitionForm->addRow(QStringLiteral("采样频率"), samplingFps_);
    codeFormat_ = new QComboBox;
    codeFormat_->setObjectName("codeFormat");
    codeFormat_->addItems({QStringLiteral("3 个大写字母 + 3 个数字"),
                           QStringLiteral("前三位大写字母，后三位数字")});
    recognitionForm->addRow(QStringLiteral("码型"), codeFormat_);
    confidence_ = new QDoubleSpinBox;
    confidence_->setObjectName("confidenceThreshold");
    confidence_->setRange(.90, 1.00);
    confidence_->setDecimals(2);
    confidence_->setSingleStep(.01);
    confidence_->setValue(.90);
    confidence_->setToolTip(QStringLiteral("六个字符中最低的识别概率"));
    recognitionForm->addRow(QStringLiteral("置信阈值"), confidence_);
    confirmations_ = new QSpinBox;
    confirmations_->setObjectName("confirmationFrames");
    confirmations_->setRange(2, 5);
    confirmations_->setValue(2);
    recognitionForm->addRow(QStringLiteral("确认帧数"), confirmations_);
    locate_ = new QPushButton(style()->standardIcon(QStyle::SP_FileDialogContentsView), QStringLiteral("自动定位房间码"));
    locate_->setToolTip(QStringLiteral("定位当前画面中的房间码区域"));
    auto* outputForm = settingsForm(QStringLiteral("输出"));
    inputMode_ = new QComboBox;
    inputMode_->setObjectName("inputMode");
    inputMode_->addItems({QStringLiteral("自动复制"), QStringLiteral("热键填入"), QStringLiteral("自动填入")});
    inputMode_->setToolTip(QStringLiteral("热键填入使用 F8，仅对前台无畏契约窗口发送"));
    outputForm->addRow(QStringLiteral("模式"), inputMode_);
    voiceAnnouncement_ = new QCheckBox(QStringLiteral("识别成功语音播报"));
    voiceAnnouncement_->setObjectName("voiceAnnouncement");
    outputForm->addRow(voiceAnnouncement_);
    armed_ = new QCheckBox(QStringLiteral("启用本轮自动填入"));
    outputForm->addRow(armed_);
    submit_ = new QCheckBox(QStringLiteral("填入后发送 Enter"));
    submit_->setToolTip(QStringLiteral("须先确认游戏输入框支持 Enter 提交"));
    outputForm->addRow(submit_);
    typed_ = new QCheckBox(QStringLiteral("使用逐字符输入"));
    outputForm->addRow(typed_);

    auto* content = new QWidget;
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(18, 0, 0, 0);
    contentLayout->setSpacing(12);
    auto* previewToolbar = new QHBoxLayout;
    previewToolbar->setSpacing(12);
    auto* previewTitle = new QLabel(QStringLiteral("画面预览"));
    previewTitle->setProperty("sectionHeading", true);
    previewToolbar->addWidget(previewTitle);
    videoInfo_ = new QLabel;
    videoInfo_->setObjectName("videoInfo");
    videoInfo_->setWordWrap(true);
    videoInfo_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    videoInfo_->setFixedHeight(32);
    previewToolbar->addWidget(videoInfo_, 1);
    roiLocked_ = new QCheckBox(QStringLiteral("锁定框选"));
    roiLocked_->setObjectName("roiLocked");
    roiLocked_->setToolTip(QStringLiteral("固定当前选区，禁止拖动和自动定位"));
    previewToolbar->addWidget(roiLocked_);
    previewToolbar->addWidget(locate_);
    contentLayout->addLayout(previewToolbar);
    preview_ = new PreviewWidget;
    preview_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    contentLayout->addWidget(preview_, 1);
    contentLayout->addLayout(statusRow);
    auto* resultRow = new QHBoxLayout;
    resultRow->setSpacing(12);
    auto* resultLabels = new QVBoxLayout;
    auto* resultCaption = new QLabel(QStringLiteral("确认房间码"));
    resultCaption->setProperty("muted", true);
    resultLabels->addWidget(resultCaption);
    code_ = new QLabel("------");
    code_->setObjectName("confirmedCode");
    code_->setFixedWidth(112);
    code_->setFixedHeight(38);
    resultLabels->addWidget(code_);
    resultRow->addLayout(resultLabels);
    copy_ = iconButton(this, QStyle::SP_FileIcon, QStringLiteral("复制确认码"));
    resultRow->addWidget(copy_);
    auto* metrics = new QVBoxLayout;
    candidate_ = new QLabel(QStringLiteral("候选: --"));
    candidate_->setObjectName("rawCandidate");
    candidate_->setWordWrap(true);
    candidate_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    candidate_->setFixedHeight(36);
    timing_ = new QLabel(QStringLiteral("推理: --    帧处理: --"));
    timing_->setObjectName("recognitionTiming");
    timing_->setProperty("muted", true);
    timing_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    timing_->setFixedHeight(18);
    throughput_ = new QLabel(QStringLiteral("已处理: 0    实际识别: 0.0 FPS"));
    throughput_->setObjectName("recognitionThroughput");
    throughput_->setProperty("muted", true);
    throughput_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    throughput_->setFixedHeight(18);
    metrics->setSpacing(6);
    metrics->addWidget(candidate_);
    metrics->addWidget(timing_);
    metrics->addWidget(throughput_);
    resultRow->addLayout(metrics, 1);
    auto* cropColumn = new QVBoxLayout;
    auto* cropCaption = new QLabel(QStringLiteral("识别区域"));
    cropCaption->setProperty("muted", true);
    cropColumn->addWidget(cropCaption);
    cropPreview_ = new QLabel(QStringLiteral("无画面"));
    cropPreview_->setAlignment(Qt::AlignCenter);
    cropPreview_->setFixedSize(156, 54);
    cropPreview_->setObjectName("cropPreview");
    cropColumn->addWidget(cropPreview_);
    resultRow->addLayout(cropColumn);
    auto* resultPanel = new QWidget;
    resultPanel->setFixedHeight(100);
    resultRow->setContentsMargins(0, 0, 0, 0);
    resultPanel->setLayout(resultRow);
    contentLayout->addWidget(resultPanel);
    auto* eventsTitle = new QLabel(QStringLiteral("事件记录"));
    eventsTitle->setProperty("sectionHeading", true);
    contentLayout->addWidget(eventsTitle);
    events_ = new QTableWidget(0, 3);
    events_->setObjectName("eventLog");
    events_->setShowGrid(false);
    events_->setAlternatingRowColors(true);
    events_->setWordWrap(false);
    events_->setTextElideMode(Qt::ElideRight);
    events_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    events_->setHorizontalHeaderLabels({QStringLiteral("时间"), QStringLiteral("房间码"), QStringLiteral("事件")});
    events_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    events_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    events_->setColumnWidth(0, 90);
    events_->setColumnWidth(1, 64);
    events_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    events_->verticalHeader()->hide();
    events_->verticalHeader()->setDefaultSectionSize(28);
    events_->setFrameShape(QFrame::NoFrame);
    events_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    events_->setSelectionBehavior(QAbstractItemView::SelectRows);
    events_->setFixedHeight(120);
    contentLayout->addWidget(events_);
    splitter->addWidget(settingsTabs);
    splitter->addWidget(content);
    splitter->setStretchFactor(1, 1);
    setStyleSheet(
        "QWidget{font-family:'Microsoft YaHei UI';font-size:12px;color:#26302c;}"
        "QMainWindow,QWidget#appSurface{background:#f6f8f7;}"
        "QTabWidget#settingsTabs{background:#ffffff;} QTabWidget#settingsTabs::pane{background:#ffffff;border:0;}"
        "QTabBar::tab{background:#ffffff;color:#86928a;border:0;border-bottom:2px solid #e6ebe8;min-width:62px;padding:11px 8px;}"
        "QTabBar::tab:selected{color:#236e48;border-bottom:2px solid #208354;font-weight:600;}"
        "QTabBar::tab:hover{color:#263d2e;background:#f1f6f3;}"
        "QLabel#appTitle{font-family:'Segoe UI';font-size:20px;font-weight:600;color:#17221c;}"
        "QLabel[sectionHeading=\"true\"]{font-weight:600;color:#26302c;}"
        "QLabel[muted=\"true\"],QLabel#videoInfo,QLabel#playbackState,QLabel#recognitionState{color:#758078;font-size:11px;}"
        "QLabel#confirmedCode{font-family:Consolas;font-size:26px;font-weight:600;color:#187a55;}"
        "QLabel#rawCandidate{font-size:11px;}"
        "QLineEdit,QComboBox,QSpinBox,QDoubleSpinBox{background:#ffffff;border:1px solid #dce1df;"
        "border-radius:5px;min-height:26px;padding:2px 6px;selection-background-color:#d5eee1;selection-color:#173d29;}"
        "QLineEdit:focus,QComboBox:focus,QSpinBox:focus,QDoubleSpinBox:focus{border-color:#419a71;}"
        "QLineEdit:disabled,QComboBox:disabled,QSpinBox:disabled,QDoubleSpinBox:disabled{background:#f0f2f3;color:#9aa39f;}"
        "QComboBox::drop-down{border:0;width:22px;}"
        "QPushButton,QToolButton{background:#ffffff;border:1px solid #dce1df;border-radius:5px;}"
        "QPushButton{min-height:28px;padding:2px 10px;}"
        "QPushButton:hover,QToolButton:hover{background:#edf3ef;border-color:#a9bdb0;}"
        "QPushButton:pressed,QToolButton:pressed{background:#dfebe3;}"
        "QPushButton:disabled,QToolButton:disabled{background:#f2f4f4;color:#a4ada8;border-color:#e6eae8;}"
        "QPushButton#primaryAction{background:#208354;color:#ffffff;border-color:#208354;}"
        "QPushButton#primaryAction:hover{background:#196f46;}"
        "QPushButton#primaryAction:disabled{background:#e7efea;color:#91a79b;border-color:#e0e9e3;}"
        "QCheckBox{spacing:7px;min-height:22px;} QCheckBox:disabled{color:#9aa39f;}"
        "QLabel#cropPreview{background:#171c1a;color:#9ca9a1;border:1px solid #dce1df;border-radius:4px;}"
        "QTableWidget{background:#ffffff;alternate-background-color:#f8faf9;selection-background-color:#e5f1e9;"
        "selection-color:#224c34;border-top:1px solid #e0e6e2;}"
        "QHeaderView::section{background:#edf1ef;color:#78837c;font-weight:400;border:0;padding:6px 8px;}"
        "QSplitter::handle,QFrame[frameShape=\"5\"]{background:#e0e6e2;}"
        "QScrollBar:vertical{background:transparent;width:8px;margin:0;}"
        "QScrollBar::handle:vertical{background:#cbd3ce;border-radius:4px;min-height:24px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"
        "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}"
        "QToolTip{background:#ffffff;color:#26302c;border:1px solid #dce1df;padding:5px;}"
    );

    QSettings settings(QDir(dataDir()).filePath("settings.ini"), QSettings::IniFormat);
    const QString savedSource = persistentSource(settings.value("source").toString());
    source_->setText(savedSource);
    if (savedSource.isEmpty()) settings.remove("source");
    else settings.setValue("source", savedSource);
    confidence_->setValue(settings.value("confidence", .90).toDouble());
    confirmations_->setValue(settings.value("confirmations", 2).toInt());
    const int storedFormat = settings.value("codeFormat",
        settings.value("lettersFirst", false).toBool() ? 2 : 1).toInt();
    codeFormat_->setCurrentIndex(settings.value("codeFormatVersion", 0).toInt() >= 1
        ? std::clamp(storedFormat, 0, 1) : (storedFormat == 2 ? 1 : 0));
    samplingFps_->setValue(settings.value("samplingFps", 30).toInt());
    const int qualityIndex = quality_->findData(settings.value("quality", "auto"));
    quality_->setCurrentIndex(qualityIndex >= 0 ? qualityIndex : 0);
    preview_->setRoi(settings.value("roi", QRectF(.35, .38, .3, .14)).toRectF());
    roiLocked_->setChecked(settings.value("roiLocked", false).toBool());
    preview_->setSelectionLocked(roiLocked_->isChecked());
    voiceAnnouncement_->setChecked(settings.value("voiceAnnouncement", true).toBool());
    speech_.setEnabled(false);
    refreshWindows();
    refreshVideoInfo();
    connect(refresh_, &QToolButton::clicked, this, &MainWindow::refreshWindows);
    connect(browse_, &QToolButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("选择测试视频"), {},
                                                        "Video (*.mp4 *.mkv *.flv *.mov *.avi);;All files (*)");
        if (!file.isEmpty()) { sourceType_->setCurrentIndex(2); source_->setText(file); }
    });
    connect(sourceType_, &QComboBox::currentIndexChanged, this, [this] {
        if (running_) stop();
        updateControls();
    });
    connect(source_, &QLineEdit::textEdited, this, [this] {
        if (running_) stop();
        state_->setText(QStringLiteral("地址已修改"));
    });
    connect(start_, &QPushButton::clicked, this, &MainWindow::start);
    connect(stop_, &QPushButton::clicked, this, &MainWindow::stop);
    connect(recognitionStart_, &QPushButton::clicked, this, &MainWindow::startRecognition);
    connect(recognitionStop_, &QPushButton::clicked, this, &MainWindow::stopRecognition);
    connect(locate_, &QPushButton::clicked, this, &MainWindow::locate);
    connect(copy_, &QToolButton::clicked, this, [this] {
        if (!outputsEnabled_ || latestCode_.isEmpty()) return;
        QString error;
        if (GameInput::copyCode(latestCode_, &error)) log(QStringLiteral("手动复制"), latestCode_);
        else log(error);
    });
    connect(inputMode_, &QComboBox::currentIndexChanged, this, [this] {
        armed_->setChecked(false);
        updateFillHotkey();
        updateControls();
    });
    connect(voiceAnnouncement_, &QCheckBox::toggled, this, [this](bool enabled) {
        speech_.setEnabled(outputsEnabled_ && recognitionEnabled_ && enabled);
        saveSettings();
    });
    connect(armed_, &QCheckBox::toggled, this, [this](bool active) {
        if (active) {
            armedAt_ = Clock::now();
            blockedCode_ = CodeGate::validCode(lastCandidate_) ? lastCandidate_ : latestCode_;
            log(QStringLiteral("本轮自动填入已启用"));
        }
    });
    connect(preview_, &PreviewWidget::roiSelectionStarted, this, [this] {
        selectingRoi_ = true;
        locating_ = false;
        invalidateDetection(QStringLiteral("正在框选"));
        if (running_) revision_ = worker_.setEnabled(false);
    });
    connect(preview_, &PreviewWidget::roiChanged, this, &MainWindow::applyRoi);
    connect(roiLocked_, &QCheckBox::toggled, this, [this](bool locked) {
        preview_->setSelectionLocked(locked);
        if (locked && locating_) {
            locating_ = false;
            revision_ = worker_.setRoi(preview_->roi());
            revision_ = worker_.setEnabled(recognitionEnabled_ && !selectingRoi_);
            invalidateDetection();
        }
        updateControls();
        saveSettings();
    });
    connect(samplingFps_, &QSpinBox::valueChanged, this, [this](int fps) {
        locating_ = false;
        invalidateDetection();
        if (running_) {
            revision_ = worker_.setSamplingFps(fps);
            revision_ = worker_.setEnabled(recognitionEnabled_ && !selectingRoi_);
        }
        saveSettings();
    });
    connect(confidence_, &QDoubleSpinBox::valueChanged, this, &MainWindow::updateRecognitionConfig);
    connect(confirmations_, &QSpinBox::valueChanged, this, &MainWindow::updateRecognitionConfig);
    connect(codeFormat_, &QComboBox::currentIndexChanged, this, &MainWindow::updateRecognitionConfig);
    connect(quality_, &QComboBox::currentIndexChanged, this, [this] {
        saveSettings();
        if (!running_ || sourceType_->currentIndex() != 0) return;
        const bool resume = recognitionEnabled_;
        start();
        if (resume) startRecognition();
    });
    connect(&resolver_, &StreamResolver::qualitySelected, this, [this](QString, QString actual) {
        if (!running_) return;
        actualQuality_ = actual;
        refreshVideoInfo();
    });
    connect(&resolver_, &StreamResolver::resolved, this, [this](QString url, QString referer, QByteArray cookies) {
        if (!running_) return;
        liveInput_ = sourceType_->currentIndex() < 2 && !QUrl(url).isLocalFile() && !QFileInfo(url).isFile();
        reconnecting_ = false;
        decoder_.stop();
        frameEpochAt_ = Clock::now();
        revision_ = worker_.setRoi(preview_->roi());
        invalidateDetection();
        const auto session = session_;
        const auto generation = ++sourceGeneration_;
        decoder_.start({url, referer, cookies, true, false},
                       [this](VideoFrame frame) { worker_.offer(std::move(frame)); },
                       [this, session, generation](QString message) {
                           QMetaObject::invokeMethod(this, [this, session, generation, message] {
                               if (generation == sourceGeneration_) status(message, session);
                           });
                       });
    });
    connect(&resolver_, &StreamResolver::failed, this, [this](QString message) {
        if (!running_) return;
        stop();
        state_->setText(message);
        log(message);
    });
    previewTimer_.setInterval(33);
    connect(&previewTimer_, &QTimer::timeout, this, [this] {
        const VideoFrame latest = worker_.latest();
        if (latest.image.isNull() || latest.receivedAt < frameEpochAt_) {
            locate_->setEnabled(false);
            if (recognitionEnabled_ && !locating_) recognitionState_->setText(QStringLiteral("识别: 无画面"));
            return;
        }
        if (latest.sequence != previewSequence_ || latest.receivedAt != previewReceivedAt_) {
            previewSequence_ = latest.sequence;
            previewReceivedAt_ = latest.receivedAt;
            preview_->setFrame(latest.image);
            if (sourceResolution_ != latest.image.size()) refreshVideoInfo(latest.image.size());
            refreshCrop();
        }
        const bool fresh = Clock::now() - latest.receivedAt <= std::chrono::milliseconds(800);
        locate_->setEnabled(fresh && modelAvailable_ && !locating_ && !selectingRoi_ && !roiLocked_->isChecked());
        if (!fresh && hasDetection_) {
            const bool wasArmed = armed_->isChecked();
            invalidateDetection(QStringLiteral("画面停帧"));
            if (wasArmed) log(QStringLiteral("画面停帧，自动填入已解除"));
        }
    });
    demoTimer_.setInterval(33);
    connect(&demoTimer_, &QTimer::timeout, this, &MainWindow::demoFrame);
    stopHotkey_ = RegisterHotKey(reinterpret_cast<HWND>(winId()), 2, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F10);
    if (!stopHotkey_) log(QStringLiteral("Ctrl+Alt+F10 热键注册失败，请使用停止播放按钮"));
    updateFillHotkey();
    updateControls();
}

MainWindow::~MainWindow() {
    stop();
    if (fillHotkey_) UnregisterHotKey(reinterpret_cast<HWND>(winId()), 1);
    if (stopHotkey_) UnregisterHotKey(reinterpret_cast<HWND>(winId()), 2);
}
void MainWindow::setOutputsEnabled(bool enabled) {
    outputsEnabled_ = enabled;
    speech_.setEnabled(enabled && recognitionEnabled_ && voiceAnnouncement_->isChecked());
    if (!enabled) armed_->setChecked(false);
    updateFillHotkey();
    updateControls();
}
void MainWindow::updateFillHotkey() {
    const bool needed = outputsEnabled_ && inputMode_->currentIndex() == 1;
    if (!needed && fillHotkey_) {
        UnregisterHotKey(reinterpret_cast<HWND>(winId()), 1);
        fillHotkey_ = false;
    } else if (needed && !fillHotkey_) {
        fillHotkey_ = RegisterHotKey(reinterpret_cast<HWND>(winId()), 1, MOD_NOREPEAT, VK_F8);
        if (!fillHotkey_) log(QStringLiteral("F8 热键注册失败，热键填入不可用"));
    }
}
void MainWindow::refreshWindows() {
    const quintptr selected = windows_->currentData().value<quintptr>();
    windows_->clear();
    for (const auto& window : WindowCapture::windows()) windows_->addItem(window.title, QVariant::fromValue(window.handle));
    const int index = windows_->findData(QVariant::fromValue(selected));
    if (index >= 0) windows_->setCurrentIndex(index);
}
void MainWindow::startDemo() { sourceType_->setCurrentIndex(3); start(); startRecognition(); }
void MainWindow::startSource(const QString& source) { sourceType_->setCurrentIndex(0); source_->setText(source); start(); }
void MainWindow::start() {
    stop();
    const QString modelDir = QDir(QCoreApplication::applicationDirPath()).filePath("models");
    modelAvailable_ = QFile::exists(QDir(modelDir).filePath("recognizer.onnx"));
    running_ = true;
    startedAt_ = frameEpochAt_ = Clock::now();
    demoSequence_ = 0;
    previewSequence_ = 0;
    previewReceivedAt_ = metricsUpdatedAt_ = {};
    revision_ = 0;
    reconnectAttempts_ = 0;
    liveInput_ = sourceType_->currentIndex() < 2;
    const auto session = ++session_;
    invalidateDetection();
    actualQuality_.clear();
    sourceResolution_ = {};
    throughput_->setText(QStringLiteral("已处理: 0    实际识别: 0.0 FPS"));
    timing_->setText(QStringLiteral("推理: --    帧处理: --"));
    refreshVideoInfo();
    updateControls();
    saveSettings();
    state_->setText(QStringLiteral("播放: 连接中"));
    worker_.start(modelDir, gateConfig(),
                  preview_->roi(), [this, session](Detection value) {
                      QMetaObject::invokeMethod(this, [this, session, value] { detection(value, session); });
                  }, [this, session](QString message) {
                      QMetaObject::invokeMethod(this, [this, session, message] { status(message, session); });
                  }, samplingFps_->value(), false);
    previewTimer_.start();
    const auto frame = [this](VideoFrame value) { worker_.offer(std::move(value)); };
    const auto report = [this, session](QString message) {
        QMetaObject::invokeMethod(this, [this, session, message] { status(message, session); });
    };
    if (sourceType_->currentIndex() == 3) {
        actualQuality_ = QStringLiteral("测试画面");
        state_->setText(QStringLiteral("播放: 测试画面"));
        demoTimer_.start();
        demoFrame();
    } else if (sourceType_->currentIndex() == 1) {
        if (!windows_->currentData().isValid()) { stop(); state_->setText(QStringLiteral("请选择可采集窗口")); return; }
        actualQuality_ = QStringLiteral("窗口原始画面");
        capture_.start(windows_->currentData().value<quintptr>(), frame, report);
    } else resolver_.resolve(source_->text(), cookies_->text().toUtf8(), quality_->currentData().toString());
}
void MainWindow::stop() {
    running_ = false;
    recognitionEnabled_ = false;
    speech_.setEnabled(false);
    locating_ = selectingRoi_ = reconnecting_ = false;
    ++session_;
    ++sourceGeneration_;
    invalidateDetection();
    resolver_.cancel();
    demoTimer_.stop();
    previewTimer_.stop();
    decoder_.stop();
    capture_.stop();
    worker_.stop();
    preview_->clearFrame();
    previewSequence_ = 0;
    previewReceivedAt_ = {};
    refreshCrop();
    state_->setText(QStringLiteral("播放: 已停止"));
    recognitionState_->setText(QStringLiteral("识别: 暂停"));
    updateControls();
}
void MainWindow::startRecognition() {
    if (!running_ || recognitionEnabled_) return;
    if (!modelAvailable_) { recognitionState_->setText(QStringLiteral("识别: 缺少模型")); return; }
    recognitionEnabled_ = true;
    speech_.setEnabled(outputsEnabled_ && voiceAnnouncement_->isChecked());
    selectingRoi_ = locating_ = false;
    revision_ = worker_.setEnabled(true);
    invalidateDetection(QStringLiteral("等待房间码"));
    log(QStringLiteral("开始识别"));
}
void MainWindow::stopRecognition() {
    if (!running_) return;
    recognitionEnabled_ = false;
    speech_.setEnabled(false);
    locating_ = selectingRoi_ = false;
    revision_ = worker_.setEnabled(false);
    invalidateDetection();
    recognitionState_->setText(QStringLiteral("识别: 暂停"));
    log(QStringLiteral("识别已暂停"));
}
void MainWindow::updateControls() {
    start_->setEnabled(!running_);
    stop_->setEnabled(running_);
    recognitionStart_->setEnabled(running_ && !recognitionEnabled_ && modelAvailable_);
    recognitionStop_->setEnabled(running_ && recognitionEnabled_);
    sourceType_->setEnabled(true);
    source_->setEnabled(sourceType_->currentIndex() == 0 || sourceType_->currentIndex() == 2);
    browse_->setEnabled(!running_);
    windows_->setEnabled(!running_ && sourceType_->currentIndex() == 1);
    refresh_->setEnabled(!running_ && sourceType_->currentIndex() == 1);
    cookies_->setEnabled(!running_ && sourceType_->currentIndex() == 0);
    quality_->setEnabled(sourceType_->currentIndex() == 0);
    confidence_->setEnabled(true);
    confirmations_->setEnabled(true);
    codeFormat_->setEnabled(true);
    const VideoFrame frame = worker_.latest();
    const bool fresh = !frame.image.isNull() && frame.receivedAt >= frameEpochAt_ &&
        Clock::now() - frame.receivedAt <= std::chrono::milliseconds(800);
    locate_->setEnabled(running_ && fresh && modelAvailable_ && !locating_ && !selectingRoi_ && !roiLocked_->isChecked());
    armed_->setEnabled(outputsEnabled_ && running_ && recognitionEnabled_ && liveInput_ && hasDetection_ &&
                       !locating_ && !selectingRoi_ && inputMode_->currentIndex() == 2);
    copy_->setEnabled(outputsEnabled_ && !latestCode_.isEmpty());
    submit_->setEnabled(outputsEnabled_ && inputMode_->currentIndex() != 0);
    typed_->setEnabled(outputsEnabled_ && inputMode_->currentIndex() != 0);
    voiceAnnouncement_->setEnabled(outputsEnabled_);
}
GateConfig MainWindow::gateConfig() const {
    return {confirmations_->value(), static_cast<float>(confidence_->value()), codeFormat_->currentIndex() == 1,
            500};
}
void MainWindow::updateRecognitionConfig() {
    locating_ = false;
    if (running_) {
        revision_ = worker_.setGateConfig(gateConfig());
        revision_ = worker_.setEnabled(recognitionEnabled_ && !selectingRoi_);
    }
    invalidateDetection();
    saveSettings();
}
void MainWindow::invalidateDetection(const QString& message) {
    armed_->setChecked(false);
    latestCode_.clear();
    lastCandidate_.clear();
    blockedCode_.clear();
    codeAt_ = {};
    hasDetection_ = false;
    code_->setText("------");
    candidate_->setText(QStringLiteral("候选: --"));
    if (!message.isEmpty()) recognitionState_->setText(QStringLiteral("识别: %1").arg(message));
    else recognitionState_->setText(recognitionEnabled_ ? QStringLiteral("识别: 等待房间码") : QStringLiteral("识别: 暂停"));
    updateControls();
}
void MainWindow::applyRoi(QRectF roi) {
    selectingRoi_ = locating_ = false;
    preview_->setRoi(roi);
    if (running_) {
        revision_ = worker_.setRoi(preview_->roi());
        revision_ = worker_.setEnabled(recognitionEnabled_);
    }
    invalidateDetection();
    refreshCrop();
    saveSettings();
}
void MainWindow::locate() {
    if (!running_ || locating_ || roiLocked_->isChecked()) return;
    locating_ = true;
    revision_ = worker_.setEnabled(false);
    invalidateDetection(QStringLiteral("正在定位"));
    const auto session = session_;
    const bool accepted = worker_.requestLocation([this, session](Location location) {
        QMetaObject::invokeMethod(this, [this, session, location] {
            if (!running_ || session != session_ || location.revision != revision_ || roiLocked_->isChecked()) return;
            locating_ = false;
            if (location.roi) {
                applyRoi(*location.roi);
                recognitionState_->setText(recognitionEnabled_ ? QStringLiteral("识别: 区域已定位") : QStringLiteral("识别: 暂停，区域已定位"));
            } else {
                revision_ = worker_.setEnabled(recognitionEnabled_);
                invalidateDetection(location.message.isEmpty() ? QStringLiteral("未定位到房间码") : location.message);
            }
            log(location.message);
        });
    });
    if (!accepted) {
        locating_ = false;
        revision_ = worker_.setEnabled(recognitionEnabled_);
        invalidateDetection(QStringLiteral("无法定位，等待新画面"));
    }
}
void MainWindow::refreshCrop() {
    const QImage crop = preview_->croppedFrame();
    if (crop.isNull()) { cropPreview_->setPixmap({}); cropPreview_->setText(QStringLiteral("无画面")); return; }
    cropPreview_->setText({});
    cropPreview_->setPixmap(QPixmap::fromImage(crop.scaled(cropPreview_->size() - QSize(4, 4), Qt::KeepAspectRatio, Qt::FastTransformation)));
}
void MainWindow::refreshVideoInfo(const QSize& resolution) {
    if (resolution.isValid()) sourceResolution_ = resolution;
    const QString dimensions = sourceResolution_.isValid()
        ? QStringLiteral("%1 × %2").arg(sourceResolution_.width()).arg(sourceResolution_.height()) : QStringLiteral("--");
    const QString quality = actualQuality_ == "auto" ? QStringLiteral("源画质（未标注）") : qualityName(actualQuality_);
    videoInfo_->setText(QStringLiteral("%1  /  %2").arg(quality, dimensions));
}
void MainWindow::status(QString message, std::uint64_t session) {
    if (!running_ || session != session_) return;
    log(message);
    if (message.startsWith(QStringLiteral("识别"))) {
        if (message.startsWith(QStringLiteral("识别错误"))) {
            stop();
            recognitionState_->setText(message);
            state_->setText(QStringLiteral("播放: 已停止，请重新开始播放"));
        } else if (recognitionEnabled_ && !hasDetection_) recognitionState_->setText(QStringLiteral("识别: 等待房间码"));
        return;
    }
    state_->setText(message);
    const bool disconnected = decoderTerminal(message) ||
        message.startsWith("Window capture failed") || message == "The captured window was closed.";
    if (disconnected) {
        locating_ = false;
        frameEpochAt_ = Clock::now();
        revision_ = worker_.setRoi(preview_->roi());
        revision_ = worker_.setEnabled(recognitionEnabled_ && !selectingRoi_);
        invalidateDetection(QStringLiteral("无画面"));
        preview_->clearFrame();
        previewSequence_ = 0;
        previewReceivedAt_ = {};
        refreshCrop();
    }
    if (disconnected && liveInput_ && sourceType_->currentIndex() == 0 && !reconnecting_ && reconnectAttempts_ < 3) {
        reconnecting_ = true;
        ++reconnectAttempts_;
        ++sourceGeneration_;
        state_->setText(QStringLiteral("播放: 重连中 (%1/3)").arg(reconnectAttempts_));
        QTimer::singleShot(1000 * reconnectAttempts_, this, [this, session] {
            if (running_ && session_ == session)
                resolver_.resolve(source_->text(), cookies_->text().toUtf8(), quality_->currentData().toString());
        });
    }
}
void MainWindow::detection(Detection value, std::uint64_t session) {
    if (!running_ || !recognitionEnabled_ || locating_ || selectingRoi_ || session != session_ || value.revision != revision_) return;
    if (value.receivedAt < frameEpochAt_ || Clock::now() - value.receivedAt > std::chrono::milliseconds(500)) return;
    bool copied = false;
    QString copyError;
    double copyMs = 0;
    if (value.confirmed) {
        latestCode_ = *value.confirmed;
        codeAt_ = value.receivedAt;
        if (outputsEnabled_) {
            copied = GameInput::copyCode(latestCode_, &copyError);
            copyMs = std::chrono::duration<double, std::milli>(Clock::now() - value.receivedAt).count();
            if (copied && voiceAnnouncement_->isChecked()) speech_.sayRecognitionSucceeded();
        }
    }
    lastCandidate_ = CodeGate::normalizeCandidate(value.result.text);
    hasDetection_ = true;
    const auto config = gateConfig();
    const bool valid = CodeGate::validCode(lastCandidate_, config.lettersFirst);
    const bool currentConfirmed = valid && lastCandidate_ == latestCode_ &&
        value.result.confidence >= static_cast<float>(confidence_->value());
    codeAt_ = currentConfirmed ? value.receivedAt : Clock::time_point{};
    candidate_->setText(QStringLiteral("%1: %2    最低置信: %3%")
        .arg(value.result.lineNormalized ? QStringLiteral("行裁剪候选") : QStringLiteral("原始候选"))
        .arg(value.result.text.isEmpty() ? QStringLiteral("--") : value.result.text.left(24))
        .arg(value.result.confidence * 100, 0, 'f', 1));
    candidate_->setToolTip(value.result.lineNormalized
        ? candidate_->text() + QStringLiteral("\n原始文字: %1    原始最低置信: %2%")
            .arg(value.result.rawText.isEmpty() ? QStringLiteral("--") : value.result.rawText)
            .arg(value.result.rawConfidence * 100, 0, 'f', 1)
        : candidate_->text());
    if (lastCandidate_.isEmpty()) recognitionState_->setText(QStringLiteral("识别: 未读到文字"));
    else if (!CodeGate::validCode(lastCandidate_)) recognitionState_->setText(QStringLiteral("识别: 需要 3 个大写字母和 3 个数字"));
    else if (!valid) recognitionState_->setText(QStringLiteral("识别: 被所选码型规则过滤"));
    else if (value.result.confidence < config.minConfidence)
        recognitionState_->setText(QStringLiteral("识别: 置信度不足（%1% < %2%）")
            .arg(value.result.confidence * 100, 0, 'f', 1).arg(config.minConfidence * 100, 0, 'f', 1));
    else recognitionState_->setText(currentConfirmed || value.confirmed
        ? QStringLiteral("识别: 已确认") : QStringLiteral("识别: 等待跨帧确认"));
    recognitionState_->setToolTip(recognitionState_->text());
    const auto now = Clock::now();
    if (value.confirmed || now - metricsUpdatedAt_ >= std::chrono::milliseconds(200)) {
        metricsUpdatedAt_ = now;
        timing_->setText(QStringLiteral("推理: %1 ms    帧处理: %2 ms").arg(value.result.inferenceMs, 0, 'f', 1).arg(value.processingMs, 0, 'f', 1));
        throughput_->setText(QStringLiteral("已处理: %1    实际识别: %2 FPS").arg(value.processedFrames).arg(value.actualFps, 0, 'f', 1));
        timing_->setToolTip(timing_->text());
        throughput_->setToolTip(throughput_->text());
    }
    updateControls();
    if (!value.confirmed) return;
    latestCode_ = *value.confirmed;
    codeAt_ = value.receivedAt;
    code_->setText(latestCode_);
    recognitionState_->setText(QStringLiteral("识别: 已确认"));
    updateControls();
    if (!outputsEnabled_) { log(QStringLiteral("测试确认（输出关闭）"), latestCode_, value.processingMs); return; }
    if (!copied) { log(copyError, latestCode_); return; }
    if (inputMode_->currentIndex() == 2 && armed_->isChecked() && codeAt_ > armedAt_ && latestCode_ != blockedCode_) fill(true);
    log(QStringLiteral("已确认并复制"), latestCode_, copyMs);
}
void MainWindow::fill(bool automatic) {
    if (!outputsEnabled_ || !recognitionEnabled_ || locating_ || selectingRoi_) return;
    if (!liveInput_) { log(QStringLiteral("测试来源仅允许复制")); return; }
    const VideoFrame latest = worker_.latest();
    if (!running_ || latest.image.isNull() || latest.receivedAt < frameEpochAt_ ||
        Clock::now() - latest.receivedAt > std::chrono::milliseconds(500) || latestCode_.isEmpty() ||
        codeAt_ == Clock::time_point{} || lastCandidate_ != latestCode_ ||
        Clock::now() - codeAt_ > std::chrono::milliseconds(500)) {
        log(QStringLiteral("没有当前有效房间码"));
        return;
    }
    if (automatic) armed_->setChecked(false);
    QString error;
    if (GameInput::fillCode(latestCode_, submit_->isChecked(), typed_->isChecked(), &error)) log(QStringLiteral("输入事件已发送"), latestCode_);
    else log(error, latestCode_);
}
bool MainWindow::nativeEvent(const QByteArray&, void* message, qintptr* result) {
    const auto* msg = static_cast<MSG*>(message);
    if (msg->message != WM_HOTKEY) return false;
    if (msg->wParam == 1 && inputMode_->currentIndex() == 1) fill(false);
    else if (msg->wParam == 2) stop();
    *result = 0;
    return true;
}
void MainWindow::log(QString action, QString code, double elapsed) {
    const QString time = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    events_->insertRow(0);
    events_->setItem(0, 0, new QTableWidgetItem(time));
    events_->setItem(0, 1, new QTableWidgetItem(code));
    events_->setItem(0, 2, new QTableWidgetItem(action));
    while (events_->rowCount() > 100) events_->removeRow(events_->rowCount() - 1);
    QJsonObject event{{"time", QDateTime::currentDateTime().toString(Qt::ISODateWithMs)}, {"code", code}, {"event", action}};
    if (elapsed >= 0) event.insert("processing_ms", elapsed);
    QFile file(QDir(dataDir()).filePath("events.jsonl"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Append)) file.write(QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n');
}
void MainWindow::saveSettings() {
    QSettings settings(QDir(dataDir()).filePath("settings.ini"), QSettings::IniFormat);
    const QString source = persistentSource(source_->text());
    if (source.isEmpty()) settings.remove("source");
    else settings.setValue("source", source);
    settings.setValue("confidence", confidence_->value());
    settings.setValue("confirmations", confirmations_->value());
    settings.setValue("codeFormat", codeFormat_->currentIndex());
    settings.setValue("codeFormatVersion", 1);
    settings.setValue("lettersFirst", codeFormat_->currentIndex() == 1);
    settings.setValue("roi", preview_->roi());
    settings.setValue("roiLocked", roiLocked_->isChecked());
    settings.setValue("voiceAnnouncement", voiceAnnouncement_->isChecked());
    settings.setValue("samplingFps", samplingFps_->value());
    settings.setValue("quality", quality_->currentData());
}
void MainWindow::demoFrame() {
    const double seconds = std::chrono::duration<double>(Clock::now() - startedAt_).count();
    const QStringList codes{"TYH663", "ABC123", "H6T3Y6"};
    const int cycle = static_cast<int>(seconds / 4.0);
    const bool visible = std::fmod(seconds, 4.0) > 1.0;
    QImage image(1280, 720, QImage::Format_RGB888);
    image.fill(QColor("#1c2427"));
    QPainter p(&image);
    p.setPen(QColor("#dae2e5"));
    QFont heading("Microsoft YaHei UI");
    heading.setPixelSize(24);
    p.setFont(heading);
    p.drawText(QRect(64, 48, 1100, 40), Qt::AlignLeft, QStringLiteral("测试视频源"));
    QFont codeFont("Arial");
    codeFont.setPixelSize(64);
    codeFont.setBold(true);
    p.setFont(codeFont);
    p.setPen(Qt::white);
    if (visible) p.drawText(QRect(448, 273, 384, 101), Qt::AlignCenter, codes[cycle % codes.size()]);
    p.end();
    worker_.offer({std::move(image), ++demoSequence_, Clock::now(), seconds});
}
bool MainWindow::savePreview(const QString& path) { return grab().save(path); }
}
