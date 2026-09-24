#include "Crosshair.h"
#include <algorithm>
#include <windows.h>

namespace dopes {

void CrosshairConfig::Clamp() {
    size = std::clamp(size, 1, 100);
    thickness = std::clamp(thickness, 1, 20);
    gap = std::clamp(gap, 0, 40);
    dotSize = std::clamp(dotSize, 1, 20);
    circleRadius = std::clamp(circleRadius, 2, 80);
    circleThickness = std::clamp(circleThickness, 1, 10);
    outlineThickness = std::clamp(outlineThickness, 1, 6);
    opacity = std::clamp(opacity, 0, 255);
    pngOpacity = std::clamp(pngOpacity, 0, 255);
    pngScale = std::clamp(pngScale, 0.1f, 5.0f);
    offsetX = std::clamp(offsetX, -500, 500);
    offsetY = std::clamp(offsetY, -500, 500);
    rotation = std::clamp(rotation, 0, 360);
    squareSize = std::clamp(squareSize, 4, 80);
    plusThickness = std::clamp(plusThickness, 1, 20);
    colorMaskR = std::clamp(colorMaskR, 0, 255);
    colorMaskG = std::clamp(colorMaskG, 0, 255);
    colorMaskB = std::clamp(colorMaskB, 0, 255);
    colorMaskA = std::clamp(colorMaskA, 0, 255);
    imageScale = std::clamp(imageScale, 10, 500);
    // Keep pngScale in sync with imageScale if customProfile is active (approx)
    if (customProfile) {
        // If imageScale changed, sync pngScale for rendering; allow independent pngOpacity
        // Don't overwrite if user manually edits pngScale elsewhere — keep both
    }
}

bool AppConfig::IsElevated() const {
    BOOL elevated = FALSE;
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION el{};
        DWORD sz = 0;
        if (GetTokenInformation(token, TokenElevation, &el, sizeof(el), &sz))
            elevated = el.TokenIsElevated;
        CloseHandle(token);
    }
    return elevated != FALSE;
}

std::wstring AppConfig::VkToString(int vk) {
    if (vk == 0) return L"Not bound";
    if (vk == VK_RBUTTON) return L"RMouse";
    if (vk == VK_LBUTTON) return L"LMouse";
    if (vk == VK_MBUTTON) return L"MMouse";
    if (vk == VK_XBUTTON1) return L"XButton1";
    if (vk == VK_XBUTTON2) return L"XButton2";
    if (vk >= 0x30 && vk <= 0x5A) {
        wchar_t c[2] = {(wchar_t)vk, 0};
        return c;
    }
    // Use GetKeyNameText
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    wchar_t name[64]{};
    LONG lparam = (sc << 16);
    if (GetKeyNameTextW(lparam, name, 64) > 0) return name;
    wchar_t buf[16]; swprintf_s(buf, L"VK %d", vk);
    return buf;
}

int AppConfig::StringToVk(const std::wstring& s) {
    if (s==L"RMouse") return VK_RBUTTON;
    if (s==L"LMouse") return VK_LBUTTON;
    if (s==L"MMouse") return VK_MBUTTON;
    if (s==L"Not bound" || s.empty()) return 0;
    if (s.size()==1) return towupper(s[0]);
    return 0;
}

void AppConfig::SetDefaults() {
    crosshair = CrosshairConfig{};
    crosshair.customProfile = false;
    crosshair.colorMaskR = 255; crosshair.colorMaskG = 255; crosshair.colorMaskB = 255; crosshair.colorMaskA = 255;
    crosshair.imageScale = 100;
    crosshair.Clamp();
    allowList.clear();
    overlayEnabled = true;
    onlyWhenForeground = true;
    hideWhenNotAllowlisted = true;
    hotkeyVk = VK_F8;
    hotkeyCtrl = false;
    hotkeyAlt = false;
    hotkeyShift = false;
    targetFps = 60;
    followTargetWindow = true;
    overlayMode = OverlayMode::Standard;
    monitorIndex = -1;
    requireAdminForRealOverlay = true;
    visibilityVk = VK_RBUTTON; // default example like photo RMouse? But default to Not bound for safety
    visibilityVk = 0;
    visibilityMode = VisibilityMode::Hold;
    inversedHold = false;
    visibilityToggleState = false;
    enableLean = false;
    leanLeftVk = 0;
    leanRightVk = 0;
    leanAngle = 30;
    switchVk = 0;
    startMinimized = false;
    minimizeToTray = true;
    autoStart = false;
    runAsAdmin = false;
    playToggleSounds = true;
    svgTint = RGB(0, 255, 128);
    svgRecolorBlack = true;
    themeAccentLime = RGB(140, 255, 74);
    themeAccentTeal = RGB(0, 210, 160);
    themePcbTrace = RGB(0, 210, 148);
    themePcbDot = RGB(0, 255, 180);
    themeBorder = RGB(0, 98, 72);
    themeButton = RGB(36, 36, 40);
    themeButtonHover = RGB(0, 82, 62);
    themeSidebar = RGB(0, 66, 50);
    themeSidebarHover = RGB(0, 53, 38);
    themeSidebarActive = RGB(0, 90, 66);
    titleGlow = RGB(0, 255, 128);
    titleGlowAlpha = 85;
    toggleOnSound = 1;
    toggleOffSound = 1;
    autoCheckUpdate = true;
    updateFeedUrl.clear();
    skippedVersion.clear();
}

} // namespace dopes
