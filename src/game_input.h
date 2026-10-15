#pragma once

#include <QString>

namespace queuepilot {

class GameInput final {
public:
    static bool copyCode(const QString& code, QString* error = nullptr);
    static bool targetMatchesForeground();
    static bool fillCode(const QString& code, bool submit,
                         bool typedFallback = false, QString* error = nullptr);
};

}
