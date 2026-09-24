#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <vector>
#include "Crosshair.h"
#include "CrosshairRenderer.h"

namespace dopes {

class OverlayWindow {
public:
    OverlayWindow();
    ~OverlayWindow();

    bool Create();
    void Destroy();
    HWND Handle() const { return m_hwnd; }
    bool IsVisible() const { return m_visible; }

    void SetConfig(const AppConfig& cfg);
    void SetEnabled(bool enabled);
    void Update(); // called on timer, decides show/hide and redraw

    void ForceRedraw();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void EnsureLayeredStyle();
    void EnsureDwmAttributes(); // Real Overlay: driver-level hints (no DLL)
    void DoRender(POINT center);
    bool DoRenderLayered(POINT center);
    bool DoRenderHardware(POINT center);
    void ApplyRealOverlayPosition(bool force=false);

    bool CreateLayeredWindow(int vsX, int vsY, int vsW, int vsH);
    bool CreateHardwareWindow(int vsX, int vsY, int vsW, int vsH);
    bool InitHardwareDevice(int w, int h);
    void DestroyHardwareDevice();
    void ResizeHardware(int w, int h);
    bool IsHardwareAvailable() const;

    HWND m_hwnd = nullptr;
    bool m_visible = false;
    bool m_enabled = true;
    bool m_useHardware = false; // true = FLIP_DISCARD NOREDIRECTIONBITMAP path (Fullscreen-capable)
    AppConfig m_cfg;
    CrosshairRenderer m_renderer;
    bool m_rendererInited = false;

    // D3D11 hardware overlay (Fullscreen + Borderless Fullscreen)
    ID3D11Device* m_hwDevice = nullptr;
    ID3D11DeviceContext* m_hwCtx = nullptr;
    IDXGISwapChain1* m_hwSwap = nullptr;
    ID3D11RenderTargetView* m_hwRTV = nullptr;
    int m_hwW = 0, m_hwH = 0;
    // Persistent staging for hardware — avoids per-frame 8MB alloc/free freeze after 2-5 min
    std::vector<uint32_t> m_hwBits;
    HDC m_hwCacheDC = nullptr;
    HBITMAP m_hwCacheBmp = nullptr;
    HBITMAP m_hwCacheOld = nullptr;
    void* m_hwCacheBits = nullptr;
    int m_hwCacheW = 0, m_hwCacheH = 0;
    void EnsureHwCache(int w,int h);
    void DestroyHwCache();

    // Cached monitor rect for RealOverlay (avoid Enum each frame)
    RECT m_cachedMonRect{0,0,0,0};
    int m_cachedMonIndex = -999;
    bool m_dwmAttrsApplied = false;

    // For throttling
    DWORD m_lastRenderTick = 0;
    POINT m_lastCenter{ -9999,-9999 };
    bool m_lastShouldShow = false;

    // Visibility hotkey state
    bool m_visPrevDown = false;
    bool m_visibilityToggled = true;
};

} // namespace dopes
