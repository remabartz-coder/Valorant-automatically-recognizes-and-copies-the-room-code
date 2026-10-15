#include "game_input.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QThread>
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace queuepilot {
namespace {

bool fail(QString* error, const QString& message) {
    if (error) *error = message;
    return false;
}

bool validCode(const QString& code) {
    if (code.size() != 6) return false;
    int letters = 0, digits = 0;
    for (const QChar character : code) {
        const ushort value = character.unicode();
        if ((value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z')) ++letters;
        else if (value >= '0' && value <= '9') ++digits;
        else return false;
    }
    return letters == 3 && digits == 3;
}

bool modifiersHeld() {
    constexpr std::array<int, 10> keys{
        VK_LSHIFT, VK_RSHIFT, VK_LCONTROL, VK_RCONTROL, VK_LMENU,
        VK_RMENU, VK_LWIN, VK_RWIN, VK_CONTROL, VK_MENU};
    for (const int key : keys) {
        if ((GetAsyncKeyState(key) & 0x8000) != 0) return true;
    }
    return false;
}

INPUT keyEvent(WORD key, bool release = false) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = key;
    input.ki.dwFlags = release ? KEYEVENTF_KEYUP : 0;
    return input;
}

bool inputReady(QString* error) {
    if (!GameInput::targetMatchesForeground())
        return fail(error, QStringLiteral("The VALORANT game must be the foreground window."));
    if (modifiersHeld())
        return fail(error, QStringLiteral("Release Shift, Ctrl, Alt and Windows keys before filling."));
    return true;
}

bool sendKeys(std::vector<INPUT> events, QString* error) {
    if (!inputReady(error)) return false;
    const UINT sent = SendInput(static_cast<UINT>(events.size()), events.data(), sizeof(INPUT));
    if (sent == events.size()) return true;

    // SendInput may insert only part of a chord; balance only the keys we pressed.
    std::vector<WORD> pressed;
    for (UINT index = 0; index < sent; ++index) {
        const INPUT& event = events[index];
        if ((event.ki.dwFlags & KEYEVENTF_KEYUP) == 0) {
            pressed.push_back(event.ki.wVk);
        } else {
            const auto found = std::find(pressed.begin(), pressed.end(), event.ki.wVk);
            if (found != pressed.end()) pressed.erase(found);
        }
    }
    std::vector<INPUT> releases;
    for (auto iterator = pressed.rbegin(); iterator != pressed.rend(); ++iterator)
        releases.push_back(keyEvent(*iterator, true));
    if (!releases.empty())
        SendInput(static_cast<UINT>(releases.size()), releases.data(), sizeof(INPUT));
    return fail(error, QStringLiteral("Windows rejected keyboard input. Check game privilege level or use clipboard mode."));
}

bool chord(WORD modifier, WORD key, QString* error) {
    return sendKeys({keyEvent(modifier), keyEvent(key), keyEvent(key, true),
                     keyEvent(modifier, true)}, error);
}

}

bool GameInput::copyCode(const QString& code, QString* error) {
    if (error) error->clear();
    if (!validCode(code))
        return fail(error, QStringLiteral("A room code must contain exactly three ASCII letters and three digits."));
    if (!QCoreApplication::instance() ||
        QThread::currentThread() != QCoreApplication::instance()->thread())
        return fail(error, QStringLiteral("Clipboard updates must run on the application GUI thread."));

    const QString normalized = code.toUpper();
    const SIZE_T bytes = static_cast<SIZE_T>(normalized.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) return fail(error, QStringLiteral("Unable to allocate clipboard data."));
    void* destination = GlobalLock(memory);
    if (!destination) {
        GlobalFree(memory);
        return fail(error, QStringLiteral("Unable to lock clipboard data."));
    }
    std::memcpy(destination, normalized.utf16(), bytes - sizeof(wchar_t));
    static_cast<wchar_t*>(destination)[normalized.size()] = L'\0';
    GlobalUnlock(memory);

    bool opened = false;
    const HWND active = GetActiveWindow();
    const HWND temporaryOwner = active ? nullptr : CreateWindowExW(
        0, L"STATIC", L"QueuePilotClipboard", 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    const HWND owner = active ? active : temporaryOwner;
    if (!owner) {
        GlobalFree(memory);
        return fail(error, QStringLiteral("Unable to create a clipboard owner window."));
    }
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (OpenClipboard(owner)) {
            opened = true;
            break;
        }
        if (attempt < 4) Sleep(5);
    }
    if (!opened) {
        if (temporaryOwner) DestroyWindow(temporaryOwner);
        GlobalFree(memory);
        return fail(error, QStringLiteral("The clipboard is busy. Try again."));
    }
    if (!EmptyClipboard() || !SetClipboardData(CF_UNICODETEXT, memory)) {
        CloseClipboard();
        if (temporaryOwner) DestroyWindow(temporaryOwner);
        GlobalFree(memory);
        return fail(error, QStringLiteral("Unable to write the room code to the clipboard."));
    }
    CloseClipboard();
    if (temporaryOwner) DestroyWindow(temporaryOwner);
    return true;
}

bool GameInput::targetMatchesForeground() {
    const HWND foreground = GetForegroundWindow();
    if (!foreground) return false;
    DWORD processId = 0;
    GetWindowThreadProcessId(foreground, &processId);
    if (!processId) return false;
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) return false;
    std::array<wchar_t, 32768> path{};
    DWORD length = static_cast<DWORD>(path.size());
    const BOOL queried = QueryFullProcessImageNameW(process, 0, path.data(), &length);
    CloseHandle(process);
    if (!queried) return false;
    const QString executable = QFileInfo(QString::fromWCharArray(path.data(), static_cast<int>(length))).fileName();
    return executable.compare(QStringLiteral("VALORANT-Win64-Shipping.exe"), Qt::CaseInsensitive) == 0;
}

bool GameInput::fillCode(const QString& code, bool submit, bool typedFallback, QString* error) {
    if (error) error->clear();
    if (!validCode(code))
        return fail(error, QStringLiteral("A room code must contain exactly three ASCII letters and three digits."));
    if (!inputReady(error)) return false;
    if (!typedFallback && !copyCode(code, error)) return false;
    if (!chord(VK_CONTROL, 'A', error)) return false;
    Sleep(12);

    if (typedFallback) {
        for (const QChar character : code.toUpper()) {
            const WORD key = character.unicode();
            const bool letter = key >= 'A' && key <= 'Z';
            if (letter && (GetKeyState(VK_CAPITAL) & 1) == 0) {
                if (!chord(VK_SHIFT, key, error)) return false;
            } else {
                if (!sendKeys({keyEvent(key), keyEvent(key, true)}, error)) return false;
            }
            Sleep(3);
        }
    } else if (!chord(VK_CONTROL, 'V', error)) {
        return false;
    }

    if (submit) {
        Sleep(35);
        if (!sendKeys({keyEvent(VK_RETURN), keyEvent(VK_RETURN, true)}, error)) return false;
    }
    return true;
}

}
