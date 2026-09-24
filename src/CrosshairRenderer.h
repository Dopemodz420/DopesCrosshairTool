#pragma once
#include "Crosshair.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

namespace dopes {

class CrosshairRenderer {
public:
    CrosshairRenderer();
    ~CrosshairRenderer();

    bool Initialize();
    void Shutdown();

    // Render crosshair centered at (cx,cy) into a 32bpp bitmap DC of size w x h
    // Returns true if anything drawn
    bool Render(HDC hdc, int w, int h, int cx, int cy, const CrosshairConfig& cfg);
    bool Render(HDC hdc, int w, int h, int cx, int cy, const AppConfig& appCfg);

    // Render to layered window bitmap (creates temporary bitmap and calls UpdateLayeredWindow)
    bool RenderToLayeredWindow(HWND hwnd, const CrosshairConfig& cfg, POINT center);
    bool RenderToLayeredWindow(HWND hwnd, const AppConfig& appCfg, POINT center);

    // For preview: render into memory bitmap and return HBITMAP (caller deletes)
    HBITMAP RenderPreview(int w, int h, const CrosshairConfig& cfg);
    HBITMAP RenderPreview(int w, int h, const AppConfig& appCfg);

private:
    bool DrawCrosshair(Gdiplus::Graphics& g, int cx, int cy, const CrosshairConfig& cfg);
    bool DrawPng(Gdiplus::Graphics& g, int cx, int cy, const CrosshairConfig& cfg);
    bool DrawSvg(Gdiplus::Graphics& g, int cx, int cy, const CrosshairConfig& cfg, const AppConfig& appCfg);
    bool DrawCrosshairInternal(Gdiplus::Graphics& g, int cx, int cy, const AppConfig& appCfg);
    void ApplyLeanAndScale(Gdiplus::Graphics& g, int cx, int cy, const AppConfig& appCfg);

    Gdiplus::GdiplusStartupInput m_gdiplusStartup{};
    ULONG_PTR m_token = 0;
    bool m_inited = false;

    // Persistent DIB for layered window — fixes per-frame CreateDIBSection heap fragmentation freeze after 2-5 min
    HDC m_cacheDC = nullptr;
    HBITMAP m_cacheBmp = nullptr;
    HBITMAP m_cacheOld = nullptr;
    void* m_cacheBits = nullptr;
    int m_cacheW = 0, m_cacheH = 0;
    void EnsureCache(int w, int h);
    void DestroyCache();
};

} // namespace dopes
