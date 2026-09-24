#pragma once
#include <windows.h>
#include <string>
#include <algorithm>

namespace dopes::utils {

inline std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
}

inline std::wstring Trim(const std::wstring& s) {
    size_t a = 0;
    while (a < s.size() && iswspace(s[a])) ++a;
    size_t b = s.size();
    while (b > a && iswspace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

inline std::wstring ExeNameFromPath(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    std::wstring name = (pos == std::wstring::npos) ? path : path.substr(pos + 1);
    return ToLower(Trim(name));
}

inline std::wstring GetExeDirectory() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    size_t pos = p.find_last_of(L"\\/");
    if (pos != std::wstring::npos) return p.substr(0, pos + 1);
    return L"./";
}

inline std::wstring GetConfigPath() {
    // Try exe directory first, fallback to AppData
    std::wstring exeDir = GetExeDirectory();
    return exeDir + L"crosshair_config.json";
}

inline std::wstring GetAppDataConfigPath() {
    wchar_t* appData = nullptr;
    // Use SHGetKnownFolderPath if available, fallback
    wchar_t buf[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH) > 0) {
        return std::wstring(buf) + L"\\DopesCrosshairTool\\config.json";
    }
    return GetConfigPath();
}

inline COLORREF ParseColor(const std::wstring& s, COLORREF def) {
    // expects "r,g,b" or "#RRGGBB"
    if (s.empty()) return def;
    if (s[0] == L'#') {
        if (s.size() == 7) {
            unsigned int v = 0;
            if (swscanf_s(s.c_str(), L"#%06x", &v) == 1) {
                int r = (v >> 16) & 0xFF;
                int g = (v >> 8) & 0xFF;
                int b = v & 0xFF;
                return RGB(r, g, b);
            }
        }
        return def;
    }
    int r=0,g=0,b=0;
    if (swscanf_s(s.c_str(), L"%d,%d,%d", &r,&g,&b)==3) {
        return RGB(std::clamp(r,0,255), std::clamp(g,0,255), std::clamp(b,0,255));
    }
    return def;
}

inline std::wstring ColorToString(COLORREF c) {
    wchar_t buf[32];
    swprintf_s(buf, L"%d,%d,%d", GetRValue(c), GetGValue(c), GetBValue(c));
    return buf;
}

inline void EnsureAppDataDir() {
    wchar_t buf[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH) > 0) {
        std::wstring dir = std::wstring(buf) + L"\\DopesCrosshairTool";
        CreateDirectoryW(dir.c_str(), nullptr);
    }
}

inline std::string WToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n-1, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}
inline std::wstring Utf8ToW(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n-1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

} // namespace dopes::utils
