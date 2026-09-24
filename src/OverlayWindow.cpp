#include <algorithm>
#include "OverlayWindow.h"
#include "ProcessTracker.h"
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

namespace dopes {

static const wchar_t* kOverlayClass = L"DopesCrosshairOverlay";

OverlayWindow::OverlayWindow() {}
OverlayWindow::~OverlayWindow() { Destroy(); }

bool OverlayWindow::CreateLayeredWindow(int vsX, int vsY, int vsW, int vsH) {
    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kOverlayClass, L"DopesCrosshairOverlay",
        WS_POPUP, vsX, vsY, vsW, vsH,
        nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (!m_hwnd) return false;
    EnsureLayeredStyle();
    EnsureDwmAttributes();
    return true;
}
bool OverlayWindow::CreateHardwareWindow(int vsX, int vsY, int vsW, int vsH) {
    // NOREDIRECTIONBITMAP + FLIP_DISCARD = independent flip, stays over exclusive Fullscreen
    constexpr DWORD exHW = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | 0x00200000 /* WS_EX_NOREDIRECTIONBITMAP */;
    m_hwnd = CreateWindowExW(exHW, kOverlayClass, L"DopesCrosshairOverlay",
        WS_POPUP, vsX, vsY, vsW, vsH,
        nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (!m_hwnd) return false;
    // No layered style - hardware path uses swapchain present
    EnsureDwmAttributes();
    if (!InitHardwareDevice(vsW, vsH)) {
        DestroyWindow(m_hwnd); m_hwnd = nullptr;
        return false;
    }
    m_useHardware = true;
    return true;
}
bool OverlayWindow::Create() {
    if (m_hwnd) return true;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &OverlayWindow::WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kOverlayClass;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassExW(&wc);

    int vsX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vsY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vsW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vsH = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    // Default: try hardware (Fullscreen-capable) first when elevated or RealOverlay
    // Fall back to layered (always works windowed). Hardware is required for exclusive Fullscreen.
    bool wantHardware = (m_cfg.overlayMode == OverlayMode::RealOverlay) || m_cfg.requireAdminForRealOverlay;
    // At startup m_cfg is defaults (RealOverlay), so try hardware
    if (wantHardware) {
        if (CreateHardwareWindow(vsX, vsY, vsW, vsH)) {
            m_renderer.Initialize(); m_rendererInited = true;
            ShowWindow(m_hwnd, SW_HIDE); m_visible = false; return true;
        }
        // hardware failed -> fall through to layered
        m_useHardware = false;
    }
    if (!CreateLayeredWindow(vsX, vsY, vsW, vsH)) return false;
    m_useHardware = false;
    m_renderer.Initialize(); m_rendererInited = true;
    ShowWindow(m_hwnd, SW_HIDE); m_visible = false;
    return true;
}

bool OverlayWindow::IsHardwareAvailable() const { return m_useHardware && m_hwDevice && m_hwSwap && m_hwRTV; }
bool OverlayWindow::InitHardwareDevice(int w, int h) {
    DestroyHardwareDevice();
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL lvl;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &m_hwDevice, &lvl, &m_hwCtx);
    if (FAILED(hr)) return false;
    IDXGIDevice* dxgiDev = nullptr; m_hwDevice->QueryInterface(IID_PPV_ARGS(&dxgiDev));
    IDXGIAdapter* ad = nullptr; dxgiDev->GetAdapter(&ad);
    IDXGIFactory2* fac = nullptr; ad->GetParent(IID_PPV_ARGS(&fac));
    DXGI_SWAP_CHAIN_DESC1 d{}; d.Width = w; d.Height = h;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; d.BufferCount = 2;
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; d.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    d.Scaling = DXGI_SCALING_STRETCH; d.Flags = 0;
    hr = fac->CreateSwapChainForHwnd(m_hwDevice, m_hwnd, &d, nullptr, nullptr, &m_hwSwap);
    fac->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);
    dxgiDev->Release(); ad->Release(); fac->Release();
    if (FAILED(hr)) { DestroyHardwareDevice(); return false; }
    ID3D11Texture2D* back = nullptr; m_hwSwap->GetBuffer(0, IID_PPV_ARGS(&back));
    hr = m_hwDevice->CreateRenderTargetView(back, nullptr, &m_hwRTV);
    back->Release();
    if (FAILED(hr)) { DestroyHardwareDevice(); return false; }
    m_hwW = w; m_hwH = h;
    return true;
}
void OverlayWindow::DestroyHardwareDevice() {
    DestroyHwCache();
    if (m_hwRTV) { m_hwRTV->Release(); m_hwRTV = nullptr; }
    if (m_hwSwap) { m_hwSwap->Release(); m_hwSwap = nullptr; }
    if (m_hwCtx) { m_hwCtx->Release(); m_hwCtx = nullptr; }
    if (m_hwDevice) { m_hwDevice->Release(); m_hwDevice = nullptr; }
    m_hwW = m_hwH = 0;
}
void OverlayWindow::ResizeHardware(int w, int h) {
    if (!m_useHardware || !m_hwSwap || !m_hwRTV) return;
    if (w == m_hwW && h == m_hwH) return;
    m_hwCtx->OMSetRenderTargets(0, nullptr, nullptr);
    m_hwRTV->Release(); m_hwRTV = nullptr;
    HRESULT hr = m_hwSwap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) { DestroyHardwareDevice(); InitHardwareDevice(w, h); return; }
    ID3D11Texture2D* back = nullptr; m_hwSwap->GetBuffer(0, IID_PPV_ARGS(&back));
    m_hwDevice->CreateRenderTargetView(back, nullptr, &m_hwRTV);
    back->Release(); m_hwW = w; m_hwH = h;
    DestroyHwCache(); // cache size changed
}
void OverlayWindow::EnsureHwCache(int w,int h){
    if(m_hwCacheDC && m_hwCacheW==w && m_hwCacheH==h) return;
    DestroyHwCache();
    HDC screenDC = GetDC(nullptr);
    m_hwCacheDC = CreateCompatibleDC(screenDC);
    BITMAPINFO bmi{}; bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth=w; bmi.bmiHeader.biHeight=-h;
    bmi.bmiHeader.biPlanes=1; bmi.bmiHeader.biBitCount=32; bmi.bmiHeader.biCompression=BI_RGB;
    m_hwCacheBmp = CreateDIBSection(m_hwCacheDC, &bmi, DIB_RGB_COLORS, &m_hwCacheBits, nullptr, 0);
    if(m_hwCacheBmp) m_hwCacheOld = (HBITMAP)SelectObject(m_hwCacheDC, m_hwCacheBmp);
    m_hwCacheW=w; m_hwCacheH=h;
    if((int)m_hwBits.size()!= w*h) m_hwBits.assign((size_t)w*h, 0);
    ReleaseDC(nullptr,screenDC);
}
void OverlayWindow::DestroyHwCache(){
    if(m_hwCacheDC){
        if(m_hwCacheOld) SelectObject(m_hwCacheDC, m_hwCacheOld);
        if(m_hwCacheBmp) DeleteObject(m_hwCacheBmp);
        DeleteDC(m_hwCacheDC);
        m_hwCacheDC=nullptr; m_hwCacheBmp=nullptr; m_hwCacheOld=nullptr; m_hwCacheBits=nullptr; m_hwCacheW=0; m_hwCacheH=0;
    }
    m_hwBits.clear(); m_hwBits.shrink_to_fit();
}
void OverlayWindow::Destroy() {
    DestroyHardwareDevice();
    if (m_rendererInited) { m_renderer.Shutdown(); m_rendererInited = false; }
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    m_useHardware = false;
    m_dwmAttrsApplied = false;
    UnregisterClassW(kOverlayClass, GetModuleHandleW(nullptr));
}

void OverlayWindow::EnsureLayeredStyle() {
    if (!m_hwnd) return;
    LONG ex = GetWindowLongW(m_hwnd, GWL_EXSTYLE);
    ex |= WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    SetWindowLongW(m_hwnd, GWL_EXSTYLE, ex);
}
void OverlayWindow::EnsureDwmAttributes() {
    if (!m_hwnd || m_dwmAttrsApplied) return;
    // Real Overlay (External Overlay parity): DWM hints so overlay is "almost at driver level" - above FSE like Game Bar.
    // All are no-op if OS doesn't support them; no injection.
    BOOL disallowPeek = TRUE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_EXCLUDED_FROM_PEEK, &disallowPeek, sizeof(disallowPeek));
    BOOL disableTransitions = TRUE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &disableTransitions, sizeof(disableTransitions));
    // Exclude from capture (Windows 10 2004+) - keeps overlay out of game capture/OBS, like Game Bar widget
    // WDA_EXCLUDEFROMCAPTURE = 0x00000011, WDA_NONE = 0x00; use dynamically so SDK < 10 doesn't fail link
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (hUser) {
        auto pSetAffinity = (BOOL(WINAPI*)(HWND,DWORD))GetProcAddress(hUser, "SetWindowDisplayAffinity");
        if (pSetAffinity) pSetAffinity(m_hwnd, 0x00000011); // WDA_EXCLUDEFROMCAPTURE
    }
    // Extend frame to enable DWM composition shadow/rounding bypass
    MARGINS m{ -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(m_hwnd, &m);
    m_dwmAttrsApplied = true;
}
void OverlayWindow::ApplyRealOverlayPosition(bool force) {
    if (!m_hwnd) return;
    int vsX, vsY, vsW, vsH;
    if (m_cfg.overlayMode == OverlayMode::RealOverlay && m_cfg.monitorIndex != -1) {
        if (!force && m_cachedMonIndex == m_cfg.monitorIndex) {
            vsX = m_cachedMonRect.left; vsY = m_cachedMonRect.top;
            vsW = m_cachedMonRect.right - m_cachedMonRect.left;
            vsH = m_cachedMonRect.bottom - m_cachedMonRect.top;
        } else {
            std::vector<std::pair<int, RECT>> sms;
            EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR hm,HDC,LPRECT,LPARAM lp)->BOOL{
                auto* vec=(std::vector<std::pair<int,RECT>>*)lp;
                MONITORINFOEXW mi{}; mi.cbSize=sizeof(mi);
                if(GetMonitorInfoW(hm,(LPMONITORINFO)&mi)){
                    int num=0;
                    const wchar_t* p=wcsstr(mi.szDevice, L"DISPLAY");
                    if(p) num=_wtoi(p+7);
                    vec->push_back({num, mi.rcMonitor});
                }
                return TRUE;
            }, (LPARAM)&sms);
            std::sort(sms.begin(), sms.end(), [](auto& a, auto& b){return a.first < b.first;});
            RECT rc{}; bool found=false;
            if(m_cfg.monitorIndex>=0 && m_cfg.monitorIndex < (int)sms.size()){
                rc = sms[m_cfg.monitorIndex].second; found=true;
            }
            if(found){ vsX=rc.left; vsY=rc.top; vsW=rc.right-rc.left; vsH=rc.bottom-rc.top; }
            else {
                HMONITOR hMon = MonitorFromPoint(POINT{0,0}, MONITOR_DEFAULTTOPRIMARY);
                MONITORINFO mi{}; mi.cbSize=sizeof(mi);
                if (GetMonitorInfoW(hMon, &mi)) { vsX=mi.rcMonitor.left; vsY=mi.rcMonitor.top; vsW=mi.rcMonitor.right-mi.rcMonitor.left; vsH=mi.rcMonitor.bottom-mi.rcMonitor.top; }
                else { vsX=GetSystemMetrics(SM_XVIRTUALSCREEN); vsY=GetSystemMetrics(SM_YVIRTUALSCREEN); vsW=GetSystemMetrics(SM_CXVIRTUALSCREEN); vsH=GetSystemMetrics(SM_CYVIRTUALSCREEN); }
            }
            m_cachedMonRect = {vsX, vsY, vsX+vsW, vsY+vsH};
            m_cachedMonIndex = m_cfg.monitorIndex;
        }
    } else {
        vsX = GetSystemMetrics(SM_XVIRTUALSCREEN);
        vsY = GetSystemMetrics(SM_YVIRTUALSCREEN);
        vsW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        vsH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        m_cachedMonIndex = -999;
    }
    RECT cur{}; GetWindowRect(m_hwnd,&cur);
    if (force || cur.left != vsX || cur.top != vsY || (cur.right - cur.left) != vsW || (cur.bottom - cur.top) != vsH) {
        SetWindowPos(m_hwnd, HWND_TOPMOST, vsX, vsY, vsW, vsH, SWP_NOACTIVATE | (m_visible? SWP_SHOWWINDOW : 0));
    }
}

void OverlayWindow::SetConfig(const AppConfig& cfg) {
    bool modeChanged = (m_cfg.overlayMode != cfg.overlayMode) || (m_cfg.monitorIndex != cfg.monitorIndex);
    m_cfg = cfg;
    m_cfg.crosshair.Clamp();
    m_enabled = cfg.overlayEnabled;
    m_lastCenter = { -9999,-9999 };
    if (modeChanged && m_hwnd) {
        bool wantHW = (m_cfg.overlayMode == OverlayMode::RealOverlay);
        if (wantHW != m_useHardware) {
            // recreate window with correct style (hardware = Fullscreen-capable, layered = windowed)
            RECT cur{}; GetWindowRect(m_hwnd, &cur);
            Destroy();
            WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.style = CS_HREDRAW|CS_VREDRAW;
            wc.lpfnWndProc = &OverlayWindow::WndProc; wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.lpszClassName = kOverlayClass;
            wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
            RegisterClassExW(&wc);
            int vsX = cur.left, vsY = cur.top, vsW = cur.right - cur.left, vsH = cur.bottom - cur.top;
            if (vsW <= 0 || vsH <= 0) { vsX = GetSystemMetrics(SM_XVIRTUALSCREEN); vsY = GetSystemMetrics(SM_YVIRTUALSCREEN); vsW = GetSystemMetrics(SM_CXVIRTUALSCREEN); vsH = GetSystemMetrics(SM_CYVIRTUALSCREEN); }
            bool ok = wantHW ? CreateHardwareWindow(vsX, vsY, vsW, vsH) : CreateLayeredWindow(vsX, vsY, vsW, vsH);
            if (!ok && wantHW) { CreateLayeredWindow(vsX, vsY, vsW, vsH); m_useHardware = false; }
            else m_useHardware = wantHW && ok;
            m_renderer.Initialize(); m_rendererInited = true;
            ShowWindow(m_hwnd, SW_HIDE); m_visible = false; m_dwmAttrsApplied = false; EnsureDwmAttributes();
            m_lastCenter = { -9999,-9999 };
        }
    }
}

void OverlayWindow::SetEnabled(bool enabled) {
    m_enabled = enabled;
    m_cfg.overlayEnabled = enabled;
    if (!enabled && m_visible) {
        ShowWindow(m_hwnd, SW_HIDE);
        m_visible = false;
    }
}

void OverlayWindow::ForceRedraw() {
    m_lastCenter = { -9999,-9999 };
    Update();
}

bool OverlayWindow::DoRenderLayered(POINT center) {
    return m_renderer.RenderToLayeredWindow(m_hwnd, m_cfg, center);
}
bool OverlayWindow::DoRenderHardware(POINT center) {
    if (!IsHardwareAvailable()) return false;
    // Hardware overlay - must stay over exclusive Fullscreen (independent flip)
    // Unbind RTV before CopyResource (cannot copy to bound RTV)
    m_hwCtx->OMSetRenderTargets(0, nullptr, nullptr);
    D3D11_VIEWPORT vp{}; vp.Width = (float)m_hwW; vp.Height = (float)m_hwH; vp.MaxDepth = 1.0f;
    m_hwCtx->RSSetViewports(1, &vp);
    // Draw crosshair into backbuffer via GDI staging -> UpdateSubresource
    // Create temporary DIB, render GDI, then create texture and copy to backbuffer via Draw
    // Fast path: use GDI to render to memory DC then UpdateSubresource to staging and Copy
    // Instead: create a CPU texture and copy
    // For now: render via GDI directly to a memory bitmap then Map hardware staging if available
    // Simplified: use CrosshairRenderer::Render to get bits via DIB, then create a D3D11 texture and draw
    {
        // Build a 32bpp DIB for current window size, centered at local pos
        int vsX, vsY; // window origin
        RECT wr{}; GetWindowRect(m_hwnd, &wr); vsX = wr.left; vsY = wr.top;
        int localX = center.x - vsX;
        int localY = center.y - vsY;
        // Create temp bitmap same size as backbuffer but we only draw crosshair near center
        // Use D3D11_USAGE_DEFAULT texture updated via UpdateSubresource
        std::vector<uint32_t> bits((size_t)m_hwW * m_hwH, 0);
        // Quick GDI render to bits: create memory DC with DIB
        HDC screenDC = GetDC(nullptr);
        HDC memDC = CreateCompatibleDC(screenDC);
        BITMAPINFO bmi{}; bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = m_hwW; bmi.bmiHeader.biHeight = -m_hwH;
        bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;
        void* pBits = nullptr;
        HBITMAP hBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
        HBITMAP old = (HBITMAP)SelectObject(memDC, hBmp);
        if (pBits) memset(pBits, 0, (size_t)m_hwW * m_hwH * 4);
        // Render crosshair into memDC at localX,localY
        m_renderer.Render(memDC, m_hwW, m_hwH, localX, localY, m_cfg);
        if (pBits) memcpy(bits.data(), pBits, (size_t)m_hwW * m_hwH * 4);
        SelectObject(memDC, old); DeleteObject(hBmp); DeleteDC(memDC); ReleaseDC(nullptr, screenDC);
        // Upload bits to a temp D3D texture then copy to backbuffer
        D3D11_TEXTURE2D_DESC td{}; td.Width = m_hwW; td.Height = m_hwH; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = bits.data(); sd.SysMemPitch = m_hwW * 4;
        ID3D11Texture2D* tmp = nullptr;
        HRESULT hr = m_hwDevice->CreateTexture2D(&td, &sd, &tmp);
        if (SUCCEEDED(hr) && tmp) {
            ID3D11Texture2D* back = nullptr; m_hwSwap->GetBuffer(0, IID_PPV_ARGS(&back));
            m_hwCtx->CopyResource(back, tmp);
            back->Release(); tmp->Release();
        }
    }
    return true;
}
void OverlayWindow::DoRender(POINT center) {
    if (!m_hwnd) return;
    bool ok = false;
    if (m_useHardware) {
        // Ensure size matches current monitor rect
        RECT wr{}; GetWindowRect(m_hwnd, &wr);
        int curW = wr.right - wr.left, curH = wr.bottom - wr.top;
        if (curW != m_hwW || curH != m_hwH) ResizeHardware(curW, curH);
        ok = DoRenderHardware(center);
        if (ok) {
            DXGI_PRESENT_PARAMETERS pp{}; // no dirty rects
            m_hwSwap->Present(1, 0);
        } else {
            // fallback to layered if hardware failed
            ok = DoRenderLayered(center);
        }
    } else {
        ok = DoRenderLayered(center);
    }
    if (!m_visible) {
        ShowWindow(m_hwnd, SW_SHOWNA);
        m_visible = true;
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
    }
    (void)ok;
}

void OverlayWindow::Update() {
    if (!m_hwnd) return;
    if (!m_enabled) {
        if (m_visible) {
            ShowWindow(m_hwnd, SW_HIDE);
            m_visible = false;
        }
        return;
    }

    // Determine if we should show - simplified for reliability: show whenever enabled unless explicitly hidden
    bool shouldShow = m_enabled;
    std::wstring matched;
    if (shouldShow && m_cfg.hideWhenNotAllowlisted && !m_cfg.allowList.empty()) {
        bool allowlisted = ProcessTracker::IsForegroundAllowlisted(m_cfg.allowList, &matched);
        shouldShow = allowlisted;
    }
    if (shouldShow && m_cfg.onlyWhenForeground && m_cfg.hideWhenNotAllowlisted && m_cfg.allowList.empty()) {
        // empty list with hideWhenNotAllowlisted true previously meant show - keep true
        shouldShow = true;
    }

    // Visibility hotkey (photo: RMouse + PressToggle/Hold + Inversed Hold)
    if (m_cfg.visibilityVk != 0) {
        bool isDown = (GetAsyncKeyState(m_cfg.visibilityVk) & 0x8000) != 0;
        bool visVisible = true;
        if (m_cfg.visibilityMode == VisibilityMode::Hold) {
            visVisible = isDown;
            if (m_cfg.inversedHold) visVisible = !visVisible;
        } else {
            // Press to toggle
            if (isDown && !m_visPrevDown) {
                m_visibilityToggled = !m_visibilityToggled;
            }
            visVisible = m_visibilityToggled;
        }
        m_visPrevDown = isDown;
        shouldShow = shouldShow && visVisible;
    }

    if (!shouldShow) {
        if (m_visible) {
            ShowWindow(m_hwnd, SW_HIDE);
            m_visible = false;
        }
        m_lastShouldShow = false;
        return;
    }

    // Get target center
    POINT center{};
    RECT targetRect{};
    bool isFs=false;
    ProcessTracker::GetTargetCenter(m_cfg, center, targetRect, isFs);

    // Throttle: only redraw if center moved or time elapsed (based on FPS) - use QPC for smoother 144Hz Real Overlay
    static LARGE_INTEGER freq{}; static bool freqInit=false;
    if(!freqInit){ QueryPerformanceFrequency(&freq); freqInit=true; }
    LARGE_INTEGER nowQ{}; QueryPerformanceCounter(&nowQ);
    DWORD now = GetTickCount(); // keep for compatibility
    int interval = 1000 / std::max(1, m_cfg.targetFps);
    bool moved = (center.x != m_lastCenter.x || center.y != m_lastCenter.y);
    bool timeElapsed = (now - m_lastRenderTick) >= (DWORD)interval;
    bool visibilityChanged = (shouldShow != m_lastShouldShow);

    if (!moved && !timeElapsed && !visibilityChanged && m_visible) {
        return;
    }

    // Ensure window covers correct area (Real Overlay monitor-specific vs virtual screen)
    // Now cached + hardware-accelerated path - no alloc per frame
    ApplyRealOverlayPosition(false);

    DoRender(center);
    m_lastCenter = center;
    m_lastRenderTick = now;
    m_lastShouldShow = shouldShow;
}

LRESULT CALLBACK OverlayWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    OverlayWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        self = (OverlayWindow*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->m_hwnd = hwnd;
    } else {
        self = (OverlayWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    if (self) return self->HandleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT OverlayWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        // Reapply position
        {
            int vsX = GetSystemMetrics(SM_XVIRTUALSCREEN);
            int vsY = GetSystemMetrics(SM_YVIRTUALSCREEN);
            int vsW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
            int vsH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
            SetWindowPos(m_hwnd, HWND_TOPMOST, vsX, vsY, vsW, vsH, SWP_NOACTIVATE | (m_visible? SWP_SHOWWINDOW:0));
            ForceRedraw();
        }
        break;
    case WM_DESTROY:
        break;
    }
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

} // namespace dopes


