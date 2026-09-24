#include "ConfigManager.h"
#include "Utils.h"
#include <fstream>
#include <sstream>
#include <windows.h>

namespace dopes {

std::wstring ConfigManager::DefaultPath() { return utils::GetConfigPath(); }
std::wstring ConfigManager::AppDataPath() { return utils::GetAppDataConfigPath(); }

// Very small JSON helper: we write our own simple parser for known keys
// Format is straightforward; we generate pretty JSON on save, and parse with string searches.

static std::string WToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n-1, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}
static std::wstring Utf8ToW(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n-1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

static std::string ReadFile(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static bool WriteFile(const std::wstring& path, const std::string& data) {
    // Ensure directory exists
    size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        std::wstring dir = path.substr(0, pos);
        CreateDirectoryW(dir.c_str(), nullptr);
        // For AppData nested
        utils::EnsureAppDataDir();
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), data.size());
    return f.good();
}

// Simple JSON value extraction
static bool ExtractInt(const std::string& json, const std::string& key, int& out) {
    std::string pat = "\"" + key + "\"";
    size_t p = json.find(pat);
    if (p == std::string::npos) return false;
    p = json.find(':', p);
    if (p == std::string::npos) return false;
    size_t e = json.find_first_of(",}\n\r", p+1);
    std::string v = json.substr(p+1, e - p - 1);
    // trim
    size_t a = v.find_first_not_of(" \t\"");
    size_t b = v.find_last_not_of(" \t\"");
    if (a == std::string::npos) return false;
    v = v.substr(a, b - a + 1);
    try { out = std::stoi(v); return true; } catch(...) { return false; }
}
static bool ExtractBool(const std::string& json, const std::string& key, bool& out) {
    std::string pat = "\"" + key + "\"";
    size_t p = json.find(pat);
    if (p == std::string::npos) return false;
    p = json.find(':', p);
    if (p == std::string::npos) return false;
    size_t e = json.find_first_of(",}\n\r", p+1);
    std::string v = json.substr(p+1, e - p - 1);
    size_t a = v.find_first_not_of(" \t\"");
    size_t b = v.find_last_not_of(" \t\"");
    if (a == std::string::npos) return false;
    v = v.substr(a, b - a + 1);
    std::transform(v.begin(), v.end(), v.begin(), ::tolower);
    if (v=="true"||v=="1") { out=true; return true; }
    if (v=="false"||v=="0") { out=false; return true; }
    return false;
}
static bool ExtractFloat(const std::string& json, const std::string& key, float& out) {
    int tmp; // reuse int extractor but handle float
    std::string pat = "\"" + key + "\"";
    size_t p = json.find(pat);
    if (p == std::string::npos) return false;
    p = json.find(':', p);
    if (p == std::string::npos) return false;
    size_t e = json.find_first_of(",}\n\r", p+1);
    std::string v = json.substr(p+1, e - p - 1);
    size_t a = v.find_first_not_of(" \t\"");
    size_t b = v.find_last_not_of(" \t\"");
    if (a == std::string::npos) return false;
    v = v.substr(a, b - a + 1);
    try { out = std::stof(v); return true; } catch(...) { return false; }
}
static bool ExtractString(const std::string& json, const std::string& key, std::string& out) {
    std::string pat = "\"" + key + "\"";
    size_t p = json.find(pat);
    if (p == std::string::npos) return false;
    p = json.find(':', p);
    if (p == std::string::npos) return false;
    size_t q1 = json.find('"', p+1);
    if (q1 == std::string::npos) return false;
    // find closing quote handling escapes \\ and \"
    size_t q2 = q1 + 1;
    std::string raw;
    raw.reserve(256);
    while (q2 < json.size()) {
        char c = json[q2];
        if (c == '\\' && q2 + 1 < json.size()) {
            char n = json[q2+1];
            if (n == '\\') { raw.push_back('\\'); q2 += 2; continue; }
            if (n == '"')  { raw.push_back('"');  q2 += 2; continue; }
            if (n == 'n')  { raw.push_back('\n'); q2 += 2; continue; }
            if (n == 't')  { raw.push_back('\t'); q2 += 2; continue; }
            raw.push_back(n); q2 += 2; continue;
        }
        if (c == '"') break;
        raw.push_back(c);
        ++q2;
    }
    if (q2 >= json.size() || json[q2] != '"') return false;
    out = raw;
    return true;
}
static bool ExtractColor(const std::string& json, const std::string& key, COLORREF& out) {
    std::string s;
    if (!ExtractString(json, key, s)) return false;
    // s is "r,g,b" or "#RRGGBB"
    if (s.empty()) return false;
    if (s[0]=='#') {
        unsigned int v=0;
        if (sscanf_s(s.c_str(), "#%06x", &v)==1) {
            int r=(v>>16)&0xFF, g=(v>>8)&0xFF, b=v&0xFF;
            out = RGB(r,g,b);
            return true;
        }
        return false;
    }
    int r,g,b;
    if (sscanf_s(s.c_str(), "%d,%d,%d", &r,&g,&b)==3) {
        out = RGB(r,g,b);
        return true;
    }
    return false;
}

bool ConfigManager::Load(AppConfig& out, const std::wstring& pathIn) {
    out.SetDefaults();
    std::wstring path = pathIn.empty() ? DefaultPath() : pathIn;
    std::string json;
    if (pathIn.empty()) {
        // Prefer AppData (writable, survives exe moves) — fixes restart loss
        std::string jsonApp = ReadFile(AppDataPath());
        if (!jsonApp.empty()) { json = jsonApp; path = AppDataPath(); }
        else {
            json = ReadFile(DefaultPath());
            if (json.empty()) return false;
            path = DefaultPath();
        }
    } else {
        json = ReadFile(path);
        if (json.empty()) return false;
    }
    // Parse known keys
    int v=0; bool b=false; float f=0; std::string s;
    if (ExtractInt(json, "type", v)) out.crosshair.type = (CrosshairType)std::clamp(v,0,9);
    if (ExtractColor(json, "color", out.crosshair.color)) {}
    if (ExtractColor(json, "outlineColor", out.crosshair.outlineColor)) {}
    if (ExtractBool(json, "outlineEnabled", b)) out.crosshair.outlineEnabled = b;
    if (ExtractInt(json, "outlineThickness", v)) out.crosshair.outlineThickness = v;
    if (ExtractInt(json, "size", v)) out.crosshair.size = v;
    if (ExtractInt(json, "thickness", v)) out.crosshair.thickness = v;
    if (ExtractInt(json, "gap", v)) out.crosshair.gap = v;
    if (ExtractBool(json, "centerDot", b)) out.crosshair.centerDot = b;
    if (ExtractInt(json, "dotSize", v)) out.crosshair.dotSize = v;
    if (ExtractInt(json, "circleRadius", v)) out.crosshair.circleRadius = v;
    if (ExtractInt(json, "circleThickness", v)) out.crosshair.circleThickness = v;
    if (ExtractInt(json, "opacity", v)) out.crosshair.opacity = v;
    if (ExtractInt(json, "offsetX", v)) out.crosshair.offsetX = v;
    if (ExtractInt(json, "offsetY", v)) out.crosshair.offsetY = v;
    if (ExtractBool(json, "usePng", b)) out.crosshair.usePng = b;
    if (ExtractString(json, "pngPath", s)) out.crosshair.pngPath = Utf8ToW(s);
    if (ExtractFloat(json, "pngScale", f)) out.crosshair.pngScale = f;
    if (ExtractInt(json, "pngOpacity", v)) out.crosshair.pngOpacity = v;
    if (ExtractBool(json, "customProfile", b)) out.crosshair.customProfile = b;
    if (ExtractInt(json, "colorMaskR", v)) out.crosshair.colorMaskR = v;
    if (ExtractInt(json, "colorMaskG", v)) out.crosshair.colorMaskG = v;
    if (ExtractInt(json, "colorMaskB", v)) out.crosshair.colorMaskB = v;
    if (ExtractInt(json, "colorMaskA", v)) out.crosshair.colorMaskA = v;
    if (ExtractInt(json, "imageScale", v)) out.crosshair.imageScale = v;
    if (ExtractInt(json, "rotation", v)) out.crosshair.rotation = v;
    if (ExtractInt(json, "squareSize", v)) out.crosshair.squareSize = v;
    if (ExtractInt(json, "plusThickness", v)) out.crosshair.plusThickness = v;

    if (ExtractBool(json, "overlayEnabled", b)) out.overlayEnabled = b;
    if (ExtractBool(json, "onlyWhenForeground", b)) out.onlyWhenForeground = b;
    if (ExtractBool(json, "hideWhenNotAllowlisted", b)) out.hideWhenNotAllowlisted = b;
    if (ExtractBool(json, "followTargetWindow", b)) out.followTargetWindow = b;
    if (ExtractInt(json, "hotkeyVk", v)) out.hotkeyVk = v;
    if (ExtractBool(json, "hotkeyCtrl", b)) out.hotkeyCtrl = b;
    if (ExtractBool(json, "hotkeyAlt", b)) out.hotkeyAlt = b;
    if (ExtractBool(json, "hotkeyShift", b)) out.hotkeyShift = b;
    if (ExtractInt(json, "targetFps", v)) out.targetFps = v;
    if (ExtractInt(json, "overlayMode", v)) out.overlayMode = (OverlayMode)std::clamp(v,0,1);
    if (ExtractInt(json, "monitorIndex", v)) out.monitorIndex = v;
    if (ExtractBool(json, "requireAdminForRealOverlay", b)) out.requireAdminForRealOverlay = b;
    if (ExtractInt(json, "visibilityVk", v)) out.visibilityVk = v;
    if (ExtractInt(json, "visibilityMode", v)) out.visibilityMode = (VisibilityMode)std::clamp(v,0,1);
    if (ExtractBool(json, "inversedHold", b)) out.inversedHold = b;
    if (ExtractBool(json, "enableLean", b)) out.enableLean = b;
    if (ExtractInt(json, "leanLeftVk", v)) out.leanLeftVk = v;
    if (ExtractInt(json, "leanRightVk", v)) out.leanRightVk = v;
    if (ExtractInt(json, "leanAngle", v)) out.leanAngle = v;
    if (ExtractInt(json, "switchVk", v)) out.switchVk = v;
    if (ExtractBool(json, "startMinimized", b)) out.startMinimized = b;
    if (ExtractBool(json, "minimizeToTray", b)) out.minimizeToTray = b;
    if (ExtractBool(json, "autoStart", b)) out.autoStart = b;
    if (ExtractBool(json, "runAsAdmin", b)) out.runAsAdmin = b;
    if (ExtractBool(json, "playToggleSounds", b)) out.playToggleSounds = b;
    if (ExtractColor(json, "svgTint", out.svgTint)) {}
    if (ExtractBool(json, "svgRecolorBlack", b)) out.svgRecolorBlack = b;
    if (ExtractColor(json, "themeAccentLime", out.themeAccentLime)) {}
    if (ExtractColor(json, "themeAccentTeal", out.themeAccentTeal)) {}
    if (ExtractColor(json, "themePcbTrace", out.themePcbTrace)) {}
    if (ExtractColor(json, "themePcbDot", out.themePcbDot)) {}
    if (ExtractColor(json, "themeBorder", out.themeBorder)) {}
    if (ExtractColor(json, "themeButton", out.themeButton)) {}
    if (ExtractColor(json, "themeButtonHover", out.themeButtonHover)) {}
    if (ExtractColor(json, "themeSidebar", out.themeSidebar)) {}
    if (ExtractColor(json, "themeSidebarHover", out.themeSidebarHover)) {}
    if (ExtractColor(json, "themeSidebarActive", out.themeSidebarActive)) {}
    if (ExtractColor(json, "titleGlow", out.titleGlow)) {}
    if (ExtractInt(json, "titleGlowAlpha", v)) out.titleGlowAlpha = std::clamp(v,0,255);
    if (ExtractInt(json, "toggleOnSound", v)) out.toggleOnSound = std::clamp(v,0,6);
    if (ExtractInt(json, "toggleOffSound", v)) out.toggleOffSound = std::clamp(v,0,6);
    if (ExtractBool(json, "autoCheckUpdate", b)) out.autoCheckUpdate = b;
    if (ExtractString(json, "updateFeedUrl", s)) out.updateFeedUrl = Utf8ToW(s);
    if (ExtractString(json, "skippedVersion", s)) out.skippedVersion = s;

    // allowList: extract array entries "exeName": "..."
    // Simple: find "allowList" then iterate objects
    out.allowList.clear();
    size_t p = json.find("\"allowList\"");
    if (p != std::string::npos) {
        size_t arrStart = json.find('[', p);
        size_t arrEnd = json.find(']', arrStart);
        if (arrStart != std::string::npos && arrEnd != std::string::npos) {
            std::string arr = json.substr(arrStart, arrEnd - arrStart);
            size_t cur = 0;
            while ((cur = arr.find("\"exeName\"", cur)) != std::string::npos) {
                size_t colon = arr.find(':', cur);
                size_t q1 = arr.find('"', colon+1);
                size_t q2 = arr.find('"', q1+1);
                if (q1==std::string::npos||q2==std::string::npos) break;
                std::string exe = arr.substr(q1+1, q2 - q1 - 1);
                bool enabled = true;
                // look ahead for enabled
                size_t enPos = arr.find("\"enabled\"", q2);
                size_t nextObj = arr.find('{', q2);
                // if enabled appears before next object start? Actually enabled is within same object.
                // Simplify: search between q2 and next '}'.
                size_t objEnd = arr.find('}', q2);
                if (enPos != std::string::npos && enPos < objEnd) {
                    size_t c2 = arr.find(':', enPos);
                    size_t e2 = arr.find_first_of(",}", c2+1);
                    std::string ev = arr.substr(c2+1, e2 - c2 - 1);
                    // trim
                    ev.erase(0, ev.find_first_not_of(" \t\n\r\""));
                    ev.erase(ev.find_last_not_of(" \t\n\r\"")+1);
                    std::transform(ev.begin(), ev.end(), ev.begin(), ::tolower);
                    enabled = (ev=="true"||ev=="1");
                }
                AllowListEntry e;
                e.exeName = Utf8ToW(exe);
                e.enabled = enabled;
                out.allowList.push_back(e);
                cur = q2 + 1;
            }
        }
    }

    out.crosshair.Clamp();
    return true;
}

bool ConfigManager::Save(const AppConfig& cfg, const std::wstring& pathIn) {
    std::wstring path = pathIn.empty() ? DefaultPath() : pathIn;
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"type\": " << (int)cfg.crosshair.type << ",\n";
    ss << "  \"color\": \"" << WToUtf8(utils::ColorToString(cfg.crosshair.color)) << "\",\n";
    ss << "  \"outlineColor\": \"" << WToUtf8(utils::ColorToString(cfg.crosshair.outlineColor)) << "\",\n";
    ss << "  \"outlineEnabled\": " << (cfg.crosshair.outlineEnabled ? "true":"false") << ",\n";
    ss << "  \"outlineThickness\": " << cfg.crosshair.outlineThickness << ",\n";
    ss << "  \"size\": " << cfg.crosshair.size << ",\n";
    ss << "  \"thickness\": " << cfg.crosshair.thickness << ",\n";
    ss << "  \"gap\": " << cfg.crosshair.gap << ",\n";
    ss << "  \"centerDot\": " << (cfg.crosshair.centerDot ? "true":"false") << ",\n";
    ss << "  \"dotSize\": " << cfg.crosshair.dotSize << ",\n";
    ss << "  \"circleRadius\": " << cfg.crosshair.circleRadius << ",\n";
    ss << "  \"circleThickness\": " << cfg.crosshair.circleThickness << ",\n";
    ss << "  \"opacity\": " << cfg.crosshair.opacity << ",\n";
    ss << "  \"offsetX\": " << cfg.crosshair.offsetX << ",\n";
    ss << "  \"offsetY\": " << cfg.crosshair.offsetY << ",\n";
    ss << "  \"usePng\": " << (cfg.crosshair.usePng ? "true":"false") << ",\n";
    ss << "  \"pngPath\": \"" << WToUtf8(cfg.crosshair.pngPath) << "\",\n";
    // escape backslashes for JSON
    // we already output raw path; replace \ with \\
    // simplest: re-process pngPath
    // Do manual replace on the written string would be easier to rebuild
    // For now, raw is okay if we don't escape - parser tolerates single \
    // But generate correctly:
    ss << "  \"customProfile\": " << (cfg.crosshair.customProfile ? "true":"false") << ",\n";
    ss << "  \"colorMaskR\": " << cfg.crosshair.colorMaskR << ",\n";
    ss << "  \"colorMaskG\": " << cfg.crosshair.colorMaskG << ",\n";
    ss << "  \"colorMaskB\": " << cfg.crosshair.colorMaskB << ",\n";
    ss << "  \"colorMaskA\": " << cfg.crosshair.colorMaskA << ",\n";
    ss << "  \"imageScale\": " << cfg.crosshair.imageScale << ",\n";
    ss << "  \"rotation\": " << cfg.crosshair.rotation << ",\n";
    ss << "  \"squareSize\": " << cfg.crosshair.squareSize << ",\n";
    ss << "  \"plusThickness\": " << cfg.crosshair.plusThickness << ",\n";
    ss << "  \"pngScale\": " << cfg.crosshair.pngScale << ",\n";
    ss << "  \"pngOpacity\": " << cfg.crosshair.pngOpacity << ",\n";
    ss << "  \"overlayEnabled\": " << (cfg.overlayEnabled ? "true":"false") << ",\n";
    ss << "  \"onlyWhenForeground\": " << (cfg.onlyWhenForeground ? "true":"false") << ",\n";
    ss << "  \"hideWhenNotAllowlisted\": " << (cfg.hideWhenNotAllowlisted ? "true":"false") << ",\n";
    ss << "  \"followTargetWindow\": " << (cfg.followTargetWindow ? "true":"false") << ",\n";
    ss << "  \"hotkeyVk\": " << cfg.hotkeyVk << ",\n";
    ss << "  \"hotkeyCtrl\": " << (cfg.hotkeyCtrl ? "true":"false") << ",\n";
    ss << "  \"hotkeyAlt\": " << (cfg.hotkeyAlt ? "true":"false") << ",\n";
    ss << "  \"hotkeyShift\": " << (cfg.hotkeyShift ? "true":"false") << ",\n";
    ss << "  \"targetFps\": " << cfg.targetFps << ",\n";
    ss << "  \"overlayMode\": " << (int)cfg.overlayMode << ",\n";
    ss << "  \"monitorIndex\": " << cfg.monitorIndex << ",\n";
    ss << "  \"requireAdminForRealOverlay\": " << (cfg.requireAdminForRealOverlay ? "true":"false") << ",\n";
    ss << "  \"visibilityVk\": " << cfg.visibilityVk << ",\n";
    ss << "  \"visibilityMode\": " << (int)cfg.visibilityMode << ",\n";
    ss << "  \"inversedHold\": " << (cfg.inversedHold ? "true":"false") << ",\n";
    ss << "  \"enableLean\": " << (cfg.enableLean ? "true":"false") << ",\n";
    ss << "  \"leanLeftVk\": " << cfg.leanLeftVk << ",\n";
    ss << "  \"leanRightVk\": " << cfg.leanRightVk << ",\n";
    ss << "  \"leanAngle\": " << cfg.leanAngle << ",\n";
    ss << "  \"switchVk\": " << cfg.switchVk << ",\n";
    ss << "  \"startMinimized\": " << (cfg.startMinimized ? "true":"false") << ",\n";
    ss << "  \"minimizeToTray\": " << (cfg.minimizeToTray ? "true":"false") << ",\n";
    ss << "  \"autoStart\": " << (cfg.autoStart ? "true":"false") << ",\n";
    ss << "  \"runAsAdmin\": " << (cfg.runAsAdmin ? "true":"false") << ",\n";
    ss << "  \"playToggleSounds\": " << (cfg.playToggleSounds ? "true":"false") << ",\n";
    ss << "  \"svgTint\": \"" << WToUtf8(utils::ColorToString(cfg.svgTint)) << "\",\n";
    ss << "  \"svgRecolorBlack\": " << (cfg.svgRecolorBlack ? "true":"false") << ",\n";
    ss << "  \"themeAccentLime\": \"" << WToUtf8(utils::ColorToString(cfg.themeAccentLime)) << "\",\n";
    ss << "  \"themeAccentTeal\": \"" << WToUtf8(utils::ColorToString(cfg.themeAccentTeal)) << "\",\n";
    ss << "  \"themePcbTrace\": \"" << WToUtf8(utils::ColorToString(cfg.themePcbTrace)) << "\",\n";
    ss << "  \"themePcbDot\": \"" << WToUtf8(utils::ColorToString(cfg.themePcbDot)) << "\",\n";
    ss << "  \"themeBorder\": \"" << WToUtf8(utils::ColorToString(cfg.themeBorder)) << "\",\n";
    ss << "  \"themeButton\": \"" << WToUtf8(utils::ColorToString(cfg.themeButton)) << "\",\n";
    ss << "  \"themeButtonHover\": \"" << WToUtf8(utils::ColorToString(cfg.themeButtonHover)) << "\",\n";
    ss << "  \"themeSidebar\": \"" << WToUtf8(utils::ColorToString(cfg.themeSidebar)) << "\",\n";
    ss << "  \"themeSidebarHover\": \"" << WToUtf8(utils::ColorToString(cfg.themeSidebarHover)) << "\",\n";
    ss << "  \"themeSidebarActive\": \"" << WToUtf8(utils::ColorToString(cfg.themeSidebarActive)) << "\",\n";
    ss << "  \"titleGlow\": \"" << WToUtf8(utils::ColorToString(cfg.titleGlow)) << "\",\n";
    ss << "  \"titleGlowAlpha\": " << cfg.titleGlowAlpha << ",\n";
    ss << "  \"toggleOnSound\": " << cfg.toggleOnSound << ",\n";
    ss << "  \"toggleOffSound\": " << cfg.toggleOffSound << ",\n";
    ss << "  \"autoCheckUpdate\": " << (cfg.autoCheckUpdate ? "true":"false") << ",\n";
    ss << "  \"updateFeedUrl\": \"" << WToUtf8(cfg.updateFeedUrl) << "\",\n";
    {
        std::string esc=cfg.skippedVersion; std::string e; for(char c:esc) if(c=='"') e+="\\\""; else if(c=='\\') e+="\\\\"; else e+=c;
        ss << "  \"skippedVersion\": \"" << e << "\",\n";
    }
    ss << "  \"allowList\": [\n";
    for (size_t i=0;i<cfg.allowList.size();++i) {
        ss << "    { \"exeName\": \"" << WToUtf8(cfg.allowList[i].exeName) << "\", \"enabled\": " << (cfg.allowList[i].enabled?"true":"false") << " }";
        if (i+1<cfg.allowList.size()) ss << ",";
        ss << "\n";
    }
    ss << "  ]\n";
    ss << "}\n";
    std::string data = ss.str();
    // Fix pngPath backslashes: replace \ with \\ inside that line
    // Do simple fix: after we already built, ensure json is valid by replacing single \ to \\
    // Find pngPath line
    // Instead rebuild correctly:
    // We'll post-process: duplicate backslashes in pngPath value part only
    // Simplify: regenerate with escaped path
    std::string escapedPng = WToUtf8(cfg.crosshair.pngPath);
    std::string esc;
    esc.reserve(escapedPng.size()*2);
    for(char c: escapedPng) { if(c=='\\') esc += "\\\\"; else if(c=='"') esc += "\\\""; else esc+=c; }
    // replace the pngPath line in data
    std::string search = "\"pngPath\": \"" + WToUtf8(cfg.crosshair.pngPath) + "\"";
    std::string replace = "\"pngPath\": \"" + esc + "\"";
    size_t pos = data.find(search);
    if (pos != std::string::npos) data.replace(pos, search.size(), replace);

    bool ok = WriteFile(path, data);
    if (!ok && path == DefaultPath()) {
        // fallback to AppData
        ok = WriteFile(AppDataPath(), data);
    }
    // Also save to AppData as backup
    if (path == DefaultPath()) {
        WriteFile(AppDataPath(), data);
    }
    return ok;
}

} // namespace dopes
