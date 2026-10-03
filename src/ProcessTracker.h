#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "Crosshair.h"

namespace dopes {

class ProcessTracker {
public:
    // Returns exe name (lowercased) of foreground window, empty if none
    static std::wstring GetForegroundExeName();
    static std::wstring GetForegroundWindowTitle();
    static HWND GetForegroundGameWindow(); // foreground window handle
    static RECT GetForegroundWindowRect(HWND hwnd);
    static std::wstring GetExeNameFromHwnd(HWND hwnd);

    // Check if foreground exe is in allow list (enabled entries)
    static bool IsForegroundAllowlisted(const std::vector<AllowListEntry>& list, std::wstring* outMatched = nullptr);

    // Enumerate running processes exe names (for dropdown)
    static std::vector<std::wstring> EnumerateRunningExes();

    // Visible top-level windows (taskbar-like): exe + window title, so the user
    // can whitelist a game without clicking into it first.
    struct TaskbarApp { std::wstring exe; std::wstring title; };
    static std::vector<TaskbarApp> EnumerateTaskbarApps();

    // Helper: find window rect for centering
    static bool GetTargetCenter(const AppConfig& cfg, POINT& outCenter, RECT& outTargetRect, bool& outIsFullscreen);
};

} // namespace dopes
