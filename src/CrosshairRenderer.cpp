#include "CrosshairRenderer.h"
#include "Utils.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include "../include/nanosvg.h"
#include "../include/nanosvgrast.h"

#pragma comment(lib, "gdiplus.lib")

namespace dopes {

CrosshairRenderer::CrosshairRenderer() {}
CrosshairRenderer::~CrosshairRenderer() { Shutdown(); }

bool CrosshairRenderer::Initialize() {
    if (m_inited) return true;
    Gdiplus::GdiplusStartup(&m_token, &m_gdiplusStartup, nullptr);
    m_inited = true;
    return true;
}
void CrosshairRenderer::Shutdown() {
    DestroyCache();
    if (m_inited) {
        Gdiplus::GdiplusShutdown(m_token);
        m_inited = false;
    }
}
void CrosshairRenderer::EnsureCache(int w, int h) {
    if(m_cacheDC && m_cacheW==w && m_cacheH==h) return;
    DestroyCache();
    HDC screenDC = GetDC(nullptr);
    m_cacheDC = CreateCompatibleDC(screenDC);
    BITMAPINFO bmi{}; bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth=w; bmi.bmiHeader.biHeight=-h;
    bmi.bmiHeader.biPlanes=1; bmi.bmiHeader.biBitCount=32; bmi.bmiHeader.biCompression=BI_RGB;
    m_cacheBmp = CreateDIBSection(m_cacheDC, &bmi, DIB_RGB_COLORS, &m_cacheBits, nullptr, 0);
    if(m_cacheBmp) m_cacheOld = (HBITMAP)SelectObject(m_cacheDC, m_cacheBmp);
    m_cacheW=w; m_cacheH=h;
    ReleaseDC(nullptr, screenDC);
}
void CrosshairRenderer::DestroyCache() {
    if(m_cacheDC){
        if(m_cacheOld) SelectObject(m_cacheDC, m_cacheOld);
        if(m_cacheBmp) DeleteObject(m_cacheBmp);
        DeleteDC(m_cacheDC);
        m_cacheDC=nullptr; m_cacheBmp=nullptr; m_cacheOld=nullptr; m_cacheBits=nullptr; m_cacheW=0; m_cacheH=0;
    }
}

static Gdiplus::Color ToGdiColor(COLORREF c, int alpha) {
    alpha = std::clamp(alpha, 0, 255);
    return Gdiplus::Color((BYTE)alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

static COLORREF ApplyColorMask(COLORREF c, const CrosshairConfig& cfg) {
    if (!cfg.customProfile) return c;
    int r = GetRValue(c) * cfg.colorMaskR / 255;
    int g = GetGValue(c) * cfg.colorMaskG / 255;
    int b = GetBValue(c) * cfg.colorMaskB / 255;
    return RGB(r,g,b);
}
static int ApplyAlphaMask(int alpha, const CrosshairConfig& cfg) {
    if (!cfg.customProfile) return alpha;
    return alpha * cfg.colorMaskA / 255;
}

static float EffectiveScale(const CrosshairConfig& cfg) {
    if (!cfg.customProfile) return 1.0f;
    return cfg.imageScale / 100.0f;
}

static int GetCurrentLeanAngle(const AppConfig& app) {
    if (!app.enableLean) return 0;
    bool leftDown = app.leanLeftVk && (GetAsyncKeyState(app.leanLeftVk) & 0x8000);
    bool rightDown = app.leanRightVk && (GetAsyncKeyState(app.leanRightVk) & 0x8000);
    if (leftDown && !rightDown) return -app.leanAngle;
    if (rightDown && !leftDown) return app.leanAngle;
    return 0;
}

void CrosshairRenderer::ApplyLeanAndScale(Gdiplus::Graphics& g, int cx, int cy, const AppConfig& appCfg) {
    int lean = GetCurrentLeanAngle(appCfg);
    int rot = appCfg.crosshair.rotation;
    int total = lean + rot;
    if (total != 0) {
        g.TranslateTransform((Gdiplus::REAL)cx, (Gdiplus::REAL)cy);
        g.RotateTransform((Gdiplus::REAL)total);
        g.TranslateTransform((Gdiplus::REAL)-cx, (Gdiplus::REAL)-cy);
    }
}

bool CrosshairRenderer::DrawCrosshair(Gdiplus::Graphics& g, int cx, int cy, const CrosshairConfig& cfg) {
    using namespace Gdiplus;
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.SetCompositingQuality(CompositingQualityHighQuality);

    float scl = EffectiveScale(cfg);
    int size = int(cfg.size * scl);
    int thickness = int(cfg.thickness * scl); if(thickness<1) thickness=1;
    int gap = int(cfg.gap * scl);
    int alpha = ApplyAlphaMask(cfg.opacity, cfg);
    COLORREF col = ApplyColorMask(cfg.color, cfg);
    COLORREF outlineCol = ApplyColorMask(cfg.outlineColor, cfg);
    int outlineT = int(cfg.outlineThickness * scl); if(outlineT<1) outlineT=1;
    int dotR = int(cfg.dotSize * scl); if(dotR<1) dotR=1;
    int circR = int(cfg.circleRadius * scl);
    int circT = int(cfg.circleThickness * scl); if(circT<1) circT=1;

    auto drawLineWithOutline = [&](int x1, int y1, int x2, int y2) {
        if (cfg.outlineEnabled && outlineT > 0) {
            Pen outlinePen(ToGdiColor(outlineCol, alpha), (REAL)(thickness + outlineT * 2));
            outlinePen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
            g.DrawLine(&outlinePen, x1, y1, x2, y2);
        }
        Pen pen(ToGdiColor(col, alpha), (REAL)thickness);
        pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        g.DrawLine(&pen, x1, y1, x2, y2);
    };

    auto drawCircleWithOutline = [&](int radius, int thick) {
        int diam = radius * 2;
        int x = cx - radius;
        int y = cy - radius;
        if (cfg.outlineEnabled && outlineT > 0) {
            Pen outlinePen(ToGdiColor(outlineCol, alpha), (REAL)(thick + outlineT * 2));
            g.DrawEllipse(&outlinePen, x, y, diam, diam);
        }
        Pen pen(ToGdiColor(col, alpha), (REAL)thick);
        g.DrawEllipse(&pen, x, y, diam, diam);
    };

    switch (cfg.type) {
    case CrosshairType::Cross: {
        drawLineWithOutline(cx - gap - size, cy, cx - gap, cy);
        drawLineWithOutline(cx + gap, cy, cx + gap + size, cy);
        drawLineWithOutline(cx, cy - gap - size, cx, cy - gap);
        drawLineWithOutline(cx, cy + gap, cx, cy + gap + size);
        break;
    }
    case CrosshairType::CrossDot: {
        drawLineWithOutline(cx - gap - size, cy, cx - gap, cy);
        drawLineWithOutline(cx + gap, cy, cx + gap + size, cy);
        drawLineWithOutline(cx, cy - gap - size, cx, cy - gap);
        drawLineWithOutline(cx, cy + gap, cx, cy + gap + size);
        [[fallthrough]];
    }
    case CrosshairType::Dot: {
        int r = dotR;
        if (cfg.outlineEnabled) {
            SolidBrush outlineBrush(ToGdiColor(outlineCol, alpha));
            g.FillEllipse(&outlineBrush, cx - r - outlineT, cy - r - outlineT, (r + outlineT)*2, (r + outlineT)*2);
        }
        SolidBrush brush(ToGdiColor(col, alpha));
        g.FillEllipse(&brush, cx - r, cy - r, r*2, r*2);
        break;
    }
    case CrosshairType::Circle: {
        drawCircleWithOutline(circR, circT);
        if (cfg.centerDot) {
            int r = dotR;
            if (cfg.outlineEnabled) {
                SolidBrush ob(ToGdiColor(outlineCol, alpha));
                g.FillEllipse(&ob, cx - r - 1, cy - r - 1, (r+1)*2, (r+1)*2);
            }
            SolidBrush b(ToGdiColor(col, alpha));
            g.FillEllipse(&b, cx - r, cy - r, r*2, r*2);
        }
        break;
    }
    case CrosshairType::TShape: {
        drawLineWithOutline(cx - size, cy, cx + size, cy);
        drawLineWithOutline(cx, cy, cx, cy + size + gap);
        break;
    }
    case CrosshairType::Plus: {
        int s = int(cfg.squareSize * scl);
        int th = int(cfg.plusThickness * scl); if(th<1) th=1;
        // Plus: thick cross without gap, centered
        auto drawPlus = [&](int x1,int y1,int x2,int y2){
            if(cfg.outlineEnabled && outlineT>0){
                Pen op(ToGdiColor(outlineCol, alpha), (REAL)(th + outlineT*2));
                op.SetLineCap(LineCapSquare, LineCapSquare, DashCapFlat);
                g.DrawLine(&op, x1,y1,x2,y2);
            }
            Pen p(ToGdiColor(col, alpha), (REAL)th);
            p.SetLineCap(LineCapSquare, LineCapSquare, DashCapFlat);
            g.DrawLine(&p, x1,y1,x2,y2);
        };
        drawPlus(cx - s, cy, cx + s, cy);
        drawPlus(cx, cy - s, cx, cy + s);
        break;
    }
    case CrosshairType::XShape: {
        int s = int(cfg.squareSize * scl);
        int th = int(cfg.plusThickness * scl); if(th<1) th=1;
        auto drawX = [&](int x1,int y1,int x2,int y2){
            if(cfg.outlineEnabled && outlineT>0){
                Pen op(ToGdiColor(outlineCol, alpha), (REAL)(th + outlineT*2));
                op.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
                g.DrawLine(&op, x1,y1,x2,y2);
            }
            Pen p(ToGdiColor(col, alpha), (REAL)th);
            p.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
            g.DrawLine(&p, x1,y1,x2,y2);
        };
        drawX(cx - s, cy - s, cx + s, cy + s);
        drawX(cx + s, cy - s, cx - s, cy + s);
        break;
    }
    case CrosshairType::Square: {
        int s = int(cfg.squareSize * scl);
        int th = int(cfg.plusThickness * scl); if(th<1) th=1;
        int x = cx - s; int y = cy - s; int wh = s*2;
        if(cfg.outlineEnabled && outlineT>0){
            Pen op(ToGdiColor(outlineCol, alpha), (REAL)(th + outlineT*2));
            g.DrawRectangle(&op, x, y, wh, wh);
        }
        Pen p(ToGdiColor(col, alpha), (REAL)th);
        g.DrawRectangle(&p, x, y, wh, wh);
        break;
    }
    case CrosshairType::Diamond: {
        int s = int(cfg.squareSize * scl);
        int th = int(cfg.plusThickness * scl); if(th<1) th=1;
        Gdiplus::GraphicsPath path;
        path.AddLine(cx, cy - s, cx + s, cy);
        path.AddLine(cx + s, cy, cx, cy + s);
        path.AddLine(cx, cy + s, cx - s, cy);
        path.CloseFigure();
        if(cfg.outlineEnabled && outlineT>0){
            Pen op(ToGdiColor(outlineCol, alpha), (REAL)(th + outlineT*2));
            op.SetLineJoin(LineJoinRound);
            g.DrawPath(&op, &path);
        }
        Pen p(ToGdiColor(col, alpha), (REAL)th);
        p.SetLineJoin(LineJoinRound);
        g.DrawPath(&p, &path);
        break;
    }
    default:
        break;
    }

    if (cfg.centerDot && cfg.type != CrosshairType::Dot && cfg.type != CrosshairType::CrossDot && cfg.type != CrosshairType::Circle) {
        int r = dotR;
        SolidBrush brush(ToGdiColor(col, alpha));
        if (cfg.outlineEnabled) {
            SolidBrush ob(ToGdiColor(outlineCol, alpha));
            g.FillEllipse(&ob, cx - r - 1, cy - r - 1, (r+1)*2, (r+1)*2);
        }
        g.FillEllipse(&brush, cx - r, cy - r, r*2, r*2);
    }

    return true;
}

bool CrosshairRenderer::DrawCrosshairInternal(Gdiplus::Graphics& g, int cx, int cy, const AppConfig& appCfg) {
    // Apply lean via transform, then draw with effective cfg
    ApplyLeanAndScale(g, cx, cy, appCfg);
    return DrawCrosshair(g, cx, cy, appCfg.crosshair);
}

bool CrosshairRenderer::DrawPng(Gdiplus::Graphics& g, int cx, int cy, const CrosshairConfig& cfg) {
    if (cfg.pngPath.empty()) return false;
    using namespace Gdiplus;
    std::unique_ptr<Image> img(Image::FromFile(cfg.pngPath.c_str()));
    if (!img || img->GetLastStatus() != Ok) return false;

    int w = img->GetWidth();
    int h = img->GetHeight();
    float baseScale = cfg.pngScale;
    float maskScale = EffectiveScale(cfg);
    float scale = baseScale * maskScale;
    int rw = int(w * scale);
    int rh = int(h * scale);
    int x = cx - rw/2;
    int y = cy - rh/2;

    float op = cfg.pngOpacity / 255.0f;
    // Apply color mask as tint if customProfile
    float rMul = 1.0f, gMul=1.0f, bMul=1.0f, aMul= op;
    if (cfg.customProfile) {
        rMul = cfg.colorMaskR / 255.0f;
        gMul = cfg.colorMaskG / 255.0f;
        bMul = cfg.colorMaskB / 255.0f;
        aMul = op * (cfg.colorMaskA / 255.0f);
    }
    ColorMatrix cm = {
        rMul,0,0,0,0,
        0,gMul,0,0,0,
        0,0,bMul,0,0,
        0,0,0,aMul,0,
        0,0,0,0,1
    };
    ImageAttributes ia;
    ia.SetColorMatrix(&cm, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);

    g.DrawImage(img.get(), Rect(x, y, rw, rh), 0, 0, w, h, UnitPixel, &ia);
    return true;
}

bool CrosshairRenderer::DrawSvg(Gdiplus::Graphics& g, int cx, int cy, const CrosshairConfig& cfg, const AppConfig& appCfg) {
    if(cfg.pngPath.empty()) return false;
    std::wstring low = utils::ToLower(cfg.pngPath);
    if(low.size()<4 || low.substr(low.size()-4)!=L".svg") return false;
    std::string narrow = utils::WToUtf8(cfg.pngPath);
    NSVGimage* image = nsvgParseFromFile(narrow.c_str(), "px", 96);
    if(!image || image->width < 1 || image->height < 1) {
        if(image) nsvgDelete(image);
        return false;
    }
    // Recolor black Kenney pack to svgTint if enabled — so black SVGs become visible/tintable
    if(appCfg.svgRecolorBlack){
        unsigned int tint = (GetRValue(appCfg.svgTint) | (GetGValue(appCfg.svgTint)<<8) | (GetBValue(appCfg.svgTint)<<16));
        auto isBlack = [](unsigned int c){ int r=c&0xFF, g=(c>>8)&0xFF, b=(c>>16)&0xFF; return r<12 && g<12 && b<12; };
        unsigned int black = (0 | (0<<8) | (0<<16));
        for(NSVGshape* sh=image->shapes; sh; sh=sh->next){
            if(sh->fill.type==NSVG_PAINT_COLOR && sh->fill.color==black) sh->fill.color = tint;
            if(sh->stroke.type==NSVG_PAINT_COLOR && sh->stroke.color==black) sh->stroke.color = tint;
            if(sh->fill.type==NSVG_PAINT_COLOR && isBlack(sh->fill.color)) sh->fill.color = tint;
            if(sh->stroke.type==NSVG_PAINT_COLOR && isBlack(sh->stroke.color)) sh->stroke.color = tint;
        }
    }
    float baseScale = cfg.pngScale;
    float maskScale = 1.0f;
    // approximate per-app scale (lean/scale already via transform, but keep imageScale)
    if(cfg.customProfile) maskScale = cfg.imageScale / 100.0f;
    float scale = baseScale * maskScale;
    if(scale < 0.05f) scale = 0.05f;
    if(scale > 10.0f) scale = 10.0f;
    int rw = int(image->width * scale);
    int rh = int(image->height * scale);
    if(rw < 1) rw = 1;
    if(rh < 1) rh = 1;
    if(rw > 1024) rw = 1024;
    if(rh > 1024) rh = 1024;
    std::vector<unsigned char> rgba((size_t)rw * rh * 4, 0);
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if(!rast){ nsvgDelete(image); return false; }
    // Center offset handled by caller via cx,cy — rasterize at 0,0 then draw centered
    nsvgRasterize(rast, image, 0, 0, scale, rgba.data(), rw, rh, rw*4);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);
    // Empty?
    bool any = false;
    for(size_t i=3;i<rgba.size();i+=4) if(rgba[i]!=0){ any=true; break; }
    if(!any) return false;
    // Convert RGBA (nanosvg) → BGRA for GDI+ PixelFormat32bppARGB (which expects B,G,R,A in memory)
    std::vector<unsigned char> bgra(rgba.size());
    for(size_t i=0;i<rgba.size(); i+=4){
        bgra[i+0]=rgba[i+2]; // B
        bgra[i+1]=rgba[i+1]; // G
        bgra[i+2]=rgba[i+0]; // R
        bgra[i+3]=rgba[i+3]; // A
    }
    // Create bitmap from bgra
    Gdiplus::Bitmap bmp(rw, rh, rw*4, PixelFormat32bppARGB, bgra.data());
    if(bmp.GetLastStatus()!=Gdiplus::Ok) return false;
    int x = cx - rw/2;
    int y = cy - rh/2;
    float op = cfg.pngOpacity / 255.0f;
    float rMul=1.0f,gMul=1.0f,bMul=1.0f,aMul=op;
    if(cfg.customProfile){
        rMul=cfg.colorMaskR/255.0f; gMul=cfg.colorMaskG/255.0f; bMul=cfg.colorMaskB/255.0f; aMul=op*(cfg.colorMaskA/255.0f);
    }
    Gdiplus::ColorMatrix cm={
        rMul,0,0,0,0,
        0,gMul,0,0,0,
        0,0,bMul,0,0,
        0,0,0,aMul,0,
        0,0,0,0,1
    };
    Gdiplus::ImageAttributes ia;
    ia.SetColorMatrix(&cm, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);
    g.DrawImage(&bmp, Gdiplus::Rect(x,y,rw,rh), 0,0,rw,rh, Gdiplus::UnitPixel, &ia);
    return true;
}

bool CrosshairRenderer::Render(HDC hdc, int w, int h, int cx, int cy, const CrosshairConfig& cfg) {
    if (!m_inited) return false;
    using namespace Gdiplus;
    Graphics g(hdc);
    g.Clear(Color(0,0,0,0));
    if (cfg.usePng && !cfg.pngPath.empty()) {
        if (GetFileAttributesW(cfg.pngPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            std::wstring low = utils::ToLower(cfg.pngPath);
            if(low.size()>=4 && low.substr(low.size()-4)==L".svg"){
                AppConfig tmp; tmp.crosshair=cfg; tmp.svgTint=RGB(255,255,255); tmp.svgRecolorBlack=false;
                if(DrawSvg(g,cx,cy,cfg,tmp)) return true;
            } else {
                if (DrawPng(g, cx, cy, cfg)) return true;
            }
        }
    }
    return DrawCrosshair(g, cx, cy, cfg);
}

bool CrosshairRenderer::Render(HDC hdc, int w, int h, int cx, int cy, const AppConfig& appCfg) {
    if (!m_inited) return false;
    using namespace Gdiplus;
    Graphics g(hdc);
    g.Clear(Color(0,0,0,0));
    // Save transform for lean
    GraphicsState state = g.Save();
    int lean = GetCurrentLeanAngle(appCfg);
    if (lean != 0) {
        g.TranslateTransform((REAL)cx, (REAL)cy);
        g.RotateTransform((REAL)lean);
        g.TranslateTransform((REAL)-cx, (REAL)-cy);
    }
    bool ok=false;
    const auto& cfg = appCfg.crosshair;
    if (cfg.usePng && !cfg.pngPath.empty()) {
        if (GetFileAttributesW(cfg.pngPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            std::wstring low = utils::ToLower(cfg.pngPath);
            if(low.size()>=4 && low.substr(low.size()-4)==L".svg"){
                ok = DrawSvg(g, cx, cy, cfg, appCfg);
            } else {
                ok = DrawPng(g, cx, cy, cfg);
            }
            if (ok) { g.Restore(state); return true; }
        }
    }
    ok = DrawCrosshair(g, cx, cy, cfg);
    g.Restore(state);
    return ok;
}

bool CrosshairRenderer::RenderToLayeredWindow(HWND hwnd, const CrosshairConfig& cfg, POINT center) {
    AppConfig tmp; tmp.crosshair=cfg;
    return RenderToLayeredWindow(hwnd, tmp, center);
}

bool CrosshairRenderer::RenderToLayeredWindow(HWND hwnd, const AppConfig& appCfg, POINT center) {
    if (!hwnd) return false;
    int vsX, vsY, vsW, vsH;
    // RealOverlay vs Standard: choose monitor
    if (appCfg.overlayMode == OverlayMode::RealOverlay) {
        if (appCfg.monitorIndex == -1) {
            vsX = GetSystemMetrics(SM_XVIRTUALSCREEN);
            vsY = GetSystemMetrics(SM_YVIRTUALSCREEN);
            vsW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
            vsH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        } else {
            struct EnumData { int target=0; int cur=0; RECT rc{0,0,0,0}; bool found=false; };
            EnumData data; data.target = appCfg.monitorIndex;
            EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hMon, HDC, LPRECT rcMon, LPARAM lp)->BOOL{
                EnumData* d=(EnumData*)lp;
                if(d->cur==d->target){ d->rc=*rcMon; d->found=true; return FALSE; }
                d->cur++; return TRUE;
            }, (LPARAM)&data);
            if (data.found) {
                vsX=data.rc.left; vsY=data.rc.top; vsW=data.rc.right-data.rc.left; vsH=data.rc.bottom-data.rc.top;
            } else {
                HMONITOR hMon = MonitorFromPoint(POINT{0,0}, MONITOR_DEFAULTTOPRIMARY);
                MONITORINFO mi{}; mi.cbSize=sizeof(mi);
                if (GetMonitorInfoW(hMon, &mi)) { vsX=mi.rcMonitor.left; vsY=mi.rcMonitor.top; vsW=mi.rcMonitor.right-mi.rcMonitor.left; vsH=mi.rcMonitor.bottom-mi.rcMonitor.top; }
                else { vsX=GetSystemMetrics(SM_XVIRTUALSCREEN); vsY=GetSystemMetrics(SM_YVIRTUALSCREEN); vsW=GetSystemMetrics(SM_CXVIRTUALSCREEN); vsH=GetSystemMetrics(SM_CYVIRTUALSCREEN); }
            }
        }
    } else {
        vsX = GetSystemMetrics(SM_XVIRTUALSCREEN);
        vsY = GetSystemMetrics(SM_YVIRTUALSCREEN);
        vsW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        vsH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    }

    // Persistent DIB — fixes freeze after 2-5 min from per-frame CreateDIBSection heap fragmentation
    EnsureCache(vsW, vsH);
    HDC memDC = m_cacheDC;
    void* bits = m_cacheBits;
    if(!memDC || !bits) return false;
    HDC screenDC = GetDC(nullptr);
    if (bits) memset(bits, 0, (size_t)vsW * vsH * 4);
    int localX = center.x - vsX;
    int localY = center.y - vsY;
    bool ok = Render(memDC, vsW, vsH, localX, localY, appCfg);
    POINT pPos{ vsX, vsY };
    SIZE pSize{ vsW, vsH };
    POINT pSrc{ 0,0 };
    BLENDFUNCTION bf{}; bf.BlendOp=AC_SRC_OVER; bf.BlendFlags=0; bf.SourceConstantAlpha=255; bf.AlphaFormat=AC_SRC_ALPHA;
    BOOL upd = UpdateLayeredWindow(hwnd, screenDC, &pPos, &pSize, memDC, &pSrc, 0, &bf, ULW_ALPHA);
    if (!upd) UpdateLayeredWindow(hwnd, nullptr, nullptr, &pSize, memDC, &pSrc, 0, &bf, ULW_ALPHA);
    ReleaseDC(nullptr, screenDC);
    return ok && upd;
}

HBITMAP CrosshairRenderer::RenderPreview(int w, int h, const CrosshairConfig& cfg) {
    AppConfig tmp; tmp.crosshair=cfg;
    return RenderPreview(w,h,tmp);
}
HBITMAP CrosshairRenderer::RenderPreview(int w, int h, const AppConfig& appCfg) {
    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);
    HBITMAP bmp = CreateCompatibleBitmap(screenDC, w, h);
    HBITMAP old = (HBITMAP)SelectObject(memDC, bmp);
    HBRUSH bg = CreateSolidBrush(RGB(30,30,30));
    RECT r{0,0,w,h}; FillRect(memDC, &r, bg); DeleteObject(bg);
    for (int y=0; y<h; y+=16) for (int x=0; x<w; x+=16) if (((x/16)+(y/16))%2==0){ HBRUSH b=CreateSolidBrush(RGB(45,45,45)); RECT cr{x,y,x+16,y+16}; FillRect(memDC,&cr,b); DeleteObject(b); }
    Render(memDC, w, h, w/2, h/2, appCfg);
    SelectObject(memDC, old);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
    return bmp;
}

} // namespace dopes
