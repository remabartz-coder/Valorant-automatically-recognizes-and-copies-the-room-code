#include "main_window.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QTimer>

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) if (QByteArray(argv[i]) == "--offscreen") qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    app.setApplicationName("QueuePilot");
    app.setOrganizationName("QueuePilot");
    app.setApplicationVersion(QUEUEPILOT_VERSION);
    app.setWindowIcon(QIcon(QStringLiteral(":/branding/queuepilot.png")));
    app.setStyle("Fusion");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"demo", "Start generated test frames."});
    parser.addOption({"offscreen", "Render without opening a desktop window."});
    parser.addOption({"no-output", "Disable clipboard, speech and game input for verification."});
    parser.addOption({"source", "Start a room link or direct media source.", "url"});
    parser.addOption({"screenshot", "Save window screenshot then exit.", "path"});
    parser.process(app);
    queuepilot::MainWindow window;
    window.setOutputsEnabled(!parser.isSet("no-output"));
    if (parser.isSet("screenshot")) window.setAttribute(Qt::WA_ShowWithoutActivating);
    window.show();
    if (parser.isSet("demo")) window.startDemo();
    else if (parser.isSet("source")) {
        window.startSource(parser.value("source"));
        if (parser.isSet("screenshot")) window.startRecognition();
    }
    if (parser.isSet("screenshot")) QTimer::singleShot(6500, &window, [&] {
        const bool ok = window.savePreview(parser.value("screenshot"));
        app.exit(ok ? 0 : 1);
    });
    return app.exec();
}
