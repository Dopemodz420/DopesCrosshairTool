#include "ProcessTracker.h"
#include "Utils.h"
#include <psapi.h>
#include <tlhelp32.h>
#include <algorithm>

#pragma comment(lib, "psapi.lib")

namespace dopes {

std::wstring ProcessTracker::GetExeNameFromHwnd(HWND hwnd) {
    if (!hwnd) return L"";
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return L"";
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) {
        // Try lesser access
        hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
        if (!hProc) return L"";
    }
    wchar_t path[MAX_PATH]{};
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameW(hProc, 0, path, &size)) {
        CloseHandle(hProc);
        return utils::ExeNameFromPath(path);
    }
    CloseHandle(hProc);
    return L"";
}

std::wstring ProcessTracker::GetForegroundExeName() {
    HWND fg = GetForegroundWindow();
    return GetExeNameFromHwnd(fg);
}

std::wstring ProcessTracker::GetForegroundWindowTitle() {
    HWND fg = GetForegroundWindow();
    if (!fg) return L"";
    wchar_t title[512]{};
    GetWindowTextW(fg, title, 512);
    return title;
}

HWND ProcessTracker::GetForegroundGameWindow() {
    return GetForegroundWindow();
}

RECT ProcessTracker::GetForegroundWindowRect(HWND hwnd) {
    RECT r{};
    if (!hwnd) {
        r.left = 0; r.top = 0; r.right = GetSystemMetrics(SM_CXSCREEN); r.bottom = GetSystemMetrics(SM_CYSCREEN);
        return r;
    }
    // Try DWM extended frame if available; fallback to GetWindowRect + GetClientRect
    GetWindowRect(hwnd, &r);
    // If window is fullscreen borderless, this is fine.
    // For windowed, we want client area center? Actually crosshair should be at window center.
    // Use GetClientRect + ClientToScreen to get client center.
    RECT client{};
    GetClientRect(hwnd, &client);
    POINT tl{ client.left, client.top };
    POINT br{ client.right, client.bottom };
    ClientToScreen(hwnd, &tl);
    ClientToScreen(hwnd, &br);
    // If client area is valid and not empty, use it as target
    if (br.x > tl.x && br.y > tl.y) {
        // Optionally, if window has title bar, client rect is correct inner area
        // Use client rect for FPS games which are usually fullscreen or borderless
        // We'll return client rect if it's reasonably large (>= 1/2 window)
        int winW = r.right - r.left;
        int winH = r.bottom - r.top;
        int cliW = br.x - tl.x;
        int cliH = br.y - tl.y;
        if (cliW > winW / 2 && cliH > winH / 2) {
            r.left = tl.x; r.top = tl.y; r.right = br.x; r.bottom = br.y;
        }
    }
    return r;
}

bool ProcessTracker::IsForegroundAllowlisted(const std::vector<AllowListEntry>& list, std::wstring* outMatched) {
    if (list.empty()) return false;
    std::wstring fg = GetForegroundExeName();
    if (fg.empty()) return false;
    fg = utils::ToLower(fg);
    for (auto& e : list) {
        if (!e.enabled) continue;
        std::wstring n = utils::ToLower(utils::Trim(e.exeName));
        if (n.empty()) continue;
        if (fg == n) {
            if (outMatched) *outMatched = fg;
            return true;
        }
    }
    return false;
}

std::vector<std::wstring> ProcessTracker::EnumerateRunningExes() {
    std::vector<std::wstring> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            std::wstring name = utils::ToLower(pe.szExeFile);
            if (std::find(out.begin(), out.end(), name) == out.end())
                out.push_back(name);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    std::sort(out.begin(), out.end());
    return out;
}

static BOOL CALLBACK TaskbarEnumCb(HWND hwnd, LPARAM lp) {
    auto* out = (std::vector<ProcessTracker::TaskbarApp>*)lp;
    if (out->size() >= 64) return FALSE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE; // owned popups/dialogs are not taskbar apps
    LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return TRUE;
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    if (!(style & WS_CAPTION)) {
        // Captionless fullscreen (exclusive/borderless game) still counts if it covers a monitor
        RECT wr{}; GetWindowRect(hwnd, &wr);
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{}; mi.cbSize = sizeof(mi);
        bool covers = false;
        if (GetMonitorInfoW(mon, &mi)) {
            RECT mr = mi.rcMonitor;
            covers = (wr.left <= mr.left && wr.top <= mr.top && wr.right >= mr.right && wr.bottom >= mr.bottom);
        }
        if (!covers) return TRUE;
    }
    wchar_t title[256]{};
    GetWindowTextW(hwnd, title, 256);
    std::wstring exe = ProcessTracker::GetExeNameFromHwnd(hwnd);
    if (exe.empty()) return TRUE;
    std::wstring lowExe = utils::ToLower(exe);
    if (lowExe == L"dopescrosshairtool.exe") return TRUE; // never whitelist ourselves
    if (lowExe == L"explorer.exe" && wcslen(title) == 0) return TRUE;
    std::wstring t = (wcslen(title) > 0) ? title : L"(fullscreen window)";
    for (auto& e : *out) if (e.exe == lowExe && e.title == t) return TRUE;
    out->push_back({ lowExe, t });
    return TRUE;
}

std::vector<ProcessTracker::TaskbarApp> ProcessTracker::EnumerateTaskbarApps() {
    std::vector<TaskbarApp> out;
    EnumWindows(TaskbarEnumCb, (LPARAM)&out);
    std::sort(out.begin(), out.end(), [](const TaskbarApp& a, const TaskbarApp& b) {
        if (a.exe != b.exe) return a.exe < b.exe;
        return a.title < b.title;
    });
    return out;
}

bool ProcessTracker::GetTargetCenter(const AppConfig& cfg, POINT& outCenter, RECT& outTargetRect, bool& outIsFullscreen) {
    outIsFullscreen = false;
    if (cfg.followTargetWindow && cfg.onlyWhenForeground) {
        // Try foreground window
        HWND fg = GetForegroundWindow();
        if (fg) {
            RECT r = GetForegroundWindowRect(fg);
            outTargetRect = r;
            outCenter.x = (r.left + r.right) / 2 + cfg.crosshair.offsetX;
            outCenter.y = (r.top + r.bottom) / 2 + cfg.crosshair.offsetY;
            // Detect fullscreen (rect covers monitor)
            HMONITOR mon = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi{}; mi.cbSize = sizeof(mi);
            GetMonitorInfoW(mon, &mi);
            RECT monR = mi.rcMonitor;
            if (r.left <= monR.left && r.top <= monR.top && r.right >= monR.right && r.bottom >= monR.bottom)
                outIsFullscreen = true;
            return true;
        }
    }
    // Fallback: primary monitor center or virtual screen center
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    // Use monitor of cursor for multi-monitor
    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{}; mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
        RECT r = mi.rcMonitor;
        outTargetRect = r;
        outCenter.x = (r.left + r.right) / 2 + cfg.crosshair.offsetX;
        outCenter.y = (r.top + r.bottom) / 2 + cfg.crosshair.offsetY;
        sw = r.right - r.left;
        sh = r.bottom - r.top;
        (void)sw; (void)sh;
    } else {
        outTargetRect = {0,0,sw,sh};
        outCenter.x = sw/2 + cfg.crosshair.offsetX;
        outCenter.y = sh/2 + cfg.crosshair.offsetY;
    }
    return true;
}

} // namespace dopes
