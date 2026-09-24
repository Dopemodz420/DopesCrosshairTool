#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace dopes {

enum class CrosshairType : int {
    Cross = 0,
    Dot = 1,
    Circle = 2,
    CrossDot = 3,
    TShape = 4,
    CustomPng = 5,
    Plus = 6,
    XShape = 7,
    Square = 8,
    Diamond = 9
};

inline const wchar_t* CrosshairTypeName(CrosshairType t) {
    switch (t) {
    case CrosshairType::Cross:     return L"Cross";
    case CrosshairType::Dot:       return L"Dot";
    case CrosshairType::Circle:    return L"Circle";
    case CrosshairType::CrossDot:  return L"Cross + Dot";
    case CrosshairType::TShape:    return L"T-Shape";
    case CrosshairType::CustomPng: return L"Custom PNG";
    case CrosshairType::Plus:      return L"Plus";
    case CrosshairType::XShape:    return L"X-Shape";
    case CrosshairType::Square:    return L"Square";
    case CrosshairType::Diamond:   return L"Diamond";
    default: return L"Cross";
    }
}

enum class VisibilityMode : int {
    PressToggle = 0,
    Hold = 1
};

enum class OverlayMode : int {
    Standard = 0,      // virtual-screen layered (compatible)
    RealOverlay = 1    // elevated, monitor-specific, fullscreen-safe
};

struct CrosshairConfig {
    CrosshairType type = CrosshairType::Cross;
    COLORREF color = RGB(0, 255, 0);
    COLORREF outlineColor = RGB(0, 0, 0);
    bool outlineEnabled = true;
    int outlineThickness = 2;

    int size = 12;
    int thickness = 2;
    int gap = 4;
    bool centerDot = false;
    int dotSize = 4;
    int circleRadius = 10;
    int circleThickness = 2;

    int opacity = 230;

    int offsetX = 0;
    int offsetY = 0;
    int rotation = 0; // degrees 0-360 for basic crosshairs
    int squareSize = 16; // for Square/Diamond
    int plusThickness = 4; // for Plus/X

    bool usePng = false;
    std::wstring pngPath;
    float pngScale = 1.0f;
    int pngOpacity = 255;

    // --- New per-crosshair custom profile (from External Overlay photo) ---
    bool customProfile = false;
    // Color mask RGBA (applied as tint / color filter when customProfile = true)
    int colorMaskR = 255;
    int colorMaskG = 255;
    int colorMaskB = 255;
    int colorMaskA = 255;
    int imageScale = 100; // percents 10-500

    void Clamp();
};

struct AllowListEntry {
    std::wstring exeName;
    bool enabled = true;
};

struct AppConfig {
    CrosshairConfig crosshair;
    std::vector<AllowListEntry> allowList;
    bool overlayEnabled = true;
    bool onlyWhenForeground = true;
    bool hideWhenNotAllowlisted = true;
    int hotkeyVk = VK_F8;
    bool hotkeyCtrl = false;
    bool hotkeyAlt = false;
    bool hotkeyShift = false;
    int targetFps = 60;
    bool followTargetWindow = true;

    // --- Real Overlay ---
    OverlayMode overlayMode = OverlayMode::RealOverlay;
    int monitorIndex = 0; // 0 = primary, -1 = virtual screen, 1+ = specific monitor
    bool requireAdminForRealOverlay = true;

    // --- Visibility hotkey (photo) ---
    int visibilityVk = 0; // 0 = not bound, VK_RBUTTON = 0x02 for RMouse example
    VisibilityMode visibilityMode = VisibilityMode::Hold;
    bool inversedHold = false;
    bool visibilityToggleState = false; // runtime, not persisted

    // --- Lean ---
    bool enableLean = false;
    int leanLeftVk = 0;
    int leanRightVk = 0;
    int leanAngle = 30; // degrees 0-45

    // --- Switch crosshair hotkey ---
    int switchVk = 0;

    // --- System tray / startup ---
    bool startMinimized = false;
    bool minimizeToTray = true;
    bool autoStart = false;
    bool runAsAdmin = false;

    // --- Audio ---
    bool playToggleSounds = true;

    // --- SVG tint for black Kenney pack ---
    COLORREF svgTint = RGB(0, 255, 128); // lime-teal default so black SVG pops on dark bg
    bool svgRecolorBlack = true;

    // --- Theme / Accent Colors (user customizable) ---
    COLORREF themeAccentLime = RGB(140, 255, 74);
    COLORREF themeAccentTeal = RGB(0, 210, 160);
    COLORREF themePcbTrace = RGB(0, 210, 148);
    COLORREF themePcbDot = RGB(0, 255, 180);
    COLORREF themeBorder = RGB(0, 98, 72);
    COLORREF themeButton = RGB(36, 36, 40);
    COLORREF themeButtonHover = RGB(0, 82, 62);
    COLORREF themeSidebar = RGB(0, 66, 50);        // selected sidebar bg
    COLORREF themeSidebarHover = RGB(0, 53, 38);   // sidebar hover
    COLORREF themeSidebarActive = RGB(0, 90, 66);  // sidebar active/pressed
    COLORREF titleGlow = RGB(0, 255, 128);
    int titleGlowAlpha = 85;
    // --- Toggle sounds ---
    int toggleOnSound = 1;  // 0=None 1=Subtle Asterisk 2=Soft Exclamation 3=Soft Beep 4=Bright Beep 5=Click 6=Chime
    int toggleOffSound = 1; // same list index for OFF (paired)

    // --- Auto update ---
    bool autoCheckUpdate = true;
    std::wstring updateFeedUrl; // empty = DefaultUpdateFeedUrl()
    std::string skippedVersion; // user dismissed

    void SetDefaults();
    bool IsElevated() const;
    static std::wstring VkToString(int vk);
    static int StringToVk(const std::wstring& s);
};

} // namespace dopes

