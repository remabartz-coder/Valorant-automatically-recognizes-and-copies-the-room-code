#include "game_input.h"

#include <QCoreApplication>
#include <QString>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    int failures = 0;
    const auto require = [&](bool condition, const char* description) {
        if (!condition) {
            std::cerr << description << '\n';
            ++failures;
        }
    };

    for (const QString& code : {QStringLiteral("ABC12"), QStringLiteral("ABC1234"),
                                QStringLiteral("ABC-12"), QStringLiteral("ABC 12"),
                                QStringLiteral("AZ1973"), QStringLiteral("D0B453"),
                                QStringLiteral("AB1234"), QStringLiteral("ABCD12"),
                                QStringLiteral("ABCDEF"), QStringLiteral("123456"),
                                QStringLiteral("AB\u4E2D123"),
                                QStringLiteral("ABC\uFF11\uFF12\uFF13")}) {
        QString error;
        require(!queuepilot::GameInput::copyCode(code, &error), "Invalid code was accepted by clipboard validation.");
        require(!error.isEmpty(), "Invalid clipboard code did not report an error.");
        error.clear();
        require(!queuepilot::GameInput::fillCode(code, true, false, &error), "Invalid code was accepted by input validation.");
        require(!error.isEmpty(), "Invalid input code did not report an error.");
    }

    // A worker-thread call validates the format but must stop before changing the clipboard.
    for (const QString& code : {QStringLiteral("TYH663"), QStringLiteral("tyh663"),
                                QStringLiteral("6T6Y3H"), QStringLiteral("KOG460"),
                                QStringLiteral("DOB453"), QStringLiteral("azi973")}) {
        bool result = true;
        QString error;
        std::thread worker([&] { result = queuepilot::GameInput::copyCode(code, &error); });
        worker.join();
        require(!result, "Clipboard call from a worker thread succeeded.");
        require(error.contains(QStringLiteral("GUI thread")), "Valid code did not reach the clipboard thread guard.");
    }

    return failures == 0 ? 0 : 1;
}
