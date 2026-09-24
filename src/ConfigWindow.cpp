#include "ConfigWindow.h"
#include "ConfigManager.h"
#include "ProcessTracker.h"
#include "Utils.h"

#include <imgui.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <mmsystem.h>
extern HANDLE g_hMutex;
#include <cmath>
#include <algorithm>
#define NANOSVG_IMPLEMENTATION
#include "../include/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "../include/nanosvgrast.h"
#pragma comment(lib, "winmm.lib")
#include "Updater.h"
#include "Version.h"
#include <fstream>
#include <objidl.h>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

static LRESULT CALLBACK FlashWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam){
    if(msg==WM_PAINT){
        PAINTSTRUCT ps; HDC hdc=BeginPaint(hwnd,&ps);
        RECT rc; GetClientRect(hwnd,&rc);
        // Lime border 8px
        HBRUSH br=CreateSolidBrush(RGB(0,210,148));
        FrameRect(hdc,&rc,br); // outer
        RECT inner={rc.left+8,rc.top+8,rc.right-8,rc.bottom-8};
        FrameRect(hdc,&inner,br);
        DeleteObject(br);
        // Text centered
        wchar_t txt[128]; int w=rc.right-rc.left, h=rc.bottom-rc.top;
        swprintf_s(txt, L"DISPLAY TEST  %dx%d  at (%d,%d)", w,h, rc.left, rc.top);
        SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, RGB(0,255,180));
        HFONT f=CreateFontW(32,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,0,0,L"Segoe UI");
        HFONT old=(HFONT)SelectObject(hdc,f);
        DrawTextW(hdc,txt,-1,&rc,DT_SINGLELINE|DT_CENTER|DT_VCENTER);
        SelectObject(hdc,old); DeleteObject(f);
        EndPaint(hwnd,&ps);
        return 0;
    }
    if(msg==WM_TIMER){
        DestroyWindow(hwnd);
        return 0;
    }
    if(msg==WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(hwnd,msg,wParam,lParam);
}

namespace dopes {

static constexpr ImVec4 kAccent{ 0.00f, 0.82f, 0.58f, 1.0f }; // Green/Teal primary
static constexpr ImVec4 kAccent2{ 0.08f, 0.68f, 0.52f, 1.0f }; // Deeper teal for gradient / second dash
static constexpr ImVec4 kAccentDim{ 0.00f, 0.55f, 0.38f, 1.0f };
static constexpr ImVec4 kDanger{ 0.92f, 0.32f, 0.34f, 1.0f };
static constexpr ImVec4 kGood{ 0.18f, 0.90f, 0.62f, 1.0f }; // vivid green-teal
static constexpr ImVec4 kMuted{ 0.68f, 0.70f, 0.72f, 1.0f }; // light gray
static constexpr ImVec4 kBgBlack{ 0.06f, 0.06f, 0.07f, 1.0f };
static constexpr ImVec4 kBgGray{ 0.13f, 0.13f, 0.14f, 1.0f };
static constexpr float kSidebarW = 164.0f;

ConfigWindow::ConfigWindow(OverlayWindow* overlay, ID3D11Device* device, ID3D11DeviceContext* context)
    : m_overlay(overlay), m_device(device), m_context(context)
{
    m_renderer.Initialize();
    RefreshProcessList();
    m_cfg.SetDefaults();
    // Init editor pixels
    m_editor.imgSize = 64;
    m_editor.pixels.assign(64*64, 0);
    m_editor.penR=255; m_editor.penG=0; m_editor.penB=0; m_editor.penA=255;
}

ConfigWindow::~ConfigWindow()
{
    if (m_testFlashHwnd && IsWindow(m_testFlashHwnd)) DestroyWindow(m_testFlashHwnd);
    if (m_previewSRV) m_previewSRV->Release();
    if (m_previewTex) m_previewTex->Release();
    for(auto srv: m_librarySRVs) if(srv) srv->Release();
    for(auto tex: m_libraryTexs) if(tex) tex->Release();
    if (m_camoSRV) m_camoSRV->Release();
    if (m_camoTex) m_camoTex->Release();
    if (m_dmLogoSRV) m_dmLogoSRV->Release();
    if (m_dmLogoTex) m_dmLogoTex->Release();
    m_renderer.Shutdown();
}

void ConfigWindow::SetConfig(const AppConfig& cfg)
{
    m_cfg = cfg;
    m_previewDirty = true;
    m_mainColorF[0] = GetRValue(cfg.crosshair.color) / 255.0f;
    m_mainColorF[1] = GetGValue(cfg.crosshair.color) / 255.0f;
    m_mainColorF[2] = GetBValue(cfg.crosshair.color) / 255.0f;
    m_outlineColorF[0] = GetRValue(cfg.crosshair.outlineColor) / 255.0f;
    m_outlineColorF[1] = GetGValue(cfg.crosshair.outlineColor) / 255.0f;
    m_outlineColorF[2] = GetBValue(cfg.crosshair.outlineColor) / 255.0f;
    m_colorInit = true;
}

void ConfigWindow::UpdateAnimations(float dt)
{
    m_pcbGlowPos += m_pcbGlowDir * dt * 0.15f;
    if (m_pcbGlowPos >= 1.0f) { m_pcbGlowPos = 1.0f; m_pcbGlowDir = -1.0f; }
    else if (m_pcbGlowPos <= 0.0f) { m_pcbGlowPos = 0.0f; m_pcbGlowDir = 1.0f; }
    m_pcbGlowPosSlow += dt * 0.07f;
    if (m_pcbGlowPosSlow > 1.0f) m_pcbGlowPosSlow -= 1.0f;
    if (m_letterProgress < 1.0f) {
        m_letterProgress += dt * 0.1125f;
        if (m_letterProgress > 1.0f) m_letterProgress = 1.0f;
    }
    m_camoOffset += dt * 0.012f;
    if (m_camoOffset > 1.0f) m_camoOffset -= 1.0f;
}

void ConfigWindow::RefreshProcessList()
{
    m_processes = ProcessTracker::EnumerateRunningExes();
    if (m_selectedProcess >= (int)m_processes.size()) m_selectedProcess = 0;
}

void ConfigWindow::HandleDropFiles(HDROP hDrop)
{
    wchar_t path[MAX_PATH]{};
    if (DragQueryFileW(hDrop, 0, path, MAX_PATH))
    {
        std::wstring p(path);
        std::wstring lower = utils::ToLower(p);
        if (lower.ends_with(L".png") || lower.ends_with(L".bmp") || lower.ends_with(L".jpg") || lower.ends_with(L".jpeg"))
        {
            m_cfg.crosshair.pngPath = p;
            m_cfg.crosshair.usePng = true;
            m_previewDirty = true;
            if (m_overlay) m_overlay->SetConfig(m_cfg);
            // Also load into editor if open
            if (m_editor.open) EditorLoadFromPng(p);
        }
    }
    DragFinish(hDrop);
}

void ConfigWindow::ApplyTheme()
{
    auto toVec = [](COLORREF col, float a=1.0f){ return ImVec4(GetRValue(col)/255.0f, GetGValue(col)/255.0f, GetBValue(col)/255.0f, a); };
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 18.0f; style.ChildRounding = 14.0f; style.FrameRounding = 8.0f;
    style.PopupRounding = 10.0f; style.GrabRounding = 9.0f; style.FramePadding = ImVec2(10, 2);
    style.GrabMinSize = 8.0f;
    style.WindowPadding = ImVec2(20, 18); style.ItemSpacing = ImVec2(10, 10);
    style.ScrollbarSize = 10.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabMinSize = 30.0f;
    auto& c = style.Colors;
    // Use customizable theme colors - defaults are Black/Gray/Green/Teal
    ImVec4 accent = toVec(m_cfg.themeAccentTeal, 1.0f);
    ImVec4 accentLime = toVec(m_cfg.themeAccentLime, 1.0f);
    ImVec4 pcbTrace = toVec(m_cfg.themePcbTrace, 1.0f);
    ImVec4 pcbDot = toVec(m_cfg.themePcbDot, 1.0f);
    ImVec4 borderCol = toVec(m_cfg.themeBorder, 0.85f);
    ImVec4 btn = toVec(m_cfg.themeButton, 1.0f);
    ImVec4 btnHover = toVec(m_cfg.themeButtonHover, 1.0f);
    // Derive active slightly brighter
    ImVec4 btnActive = ImVec4(std::min(1.0f, btnHover.x+0.15f), std::min(1.0f, btnHover.y+0.15f), std::min(1.0f, btnHover.z+0.15f), 1.0f);
    c[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.07f, 0.42f);
    c[ImGuiCol_ChildBg] = ImVec4(0.14f, 0.14f, 0.15f, 0.62f);
    c[ImGuiCol_Border] = borderCol;
    c[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.185f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.22f, 0.23f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.24f, 0.245f, 1.0f);
    c[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.04f, 0.045f, 1.0f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.06f, 0.06f, 0.065f, 1.0f);
    c[ImGuiCol_Button] = btn;
    c[ImGuiCol_ButtonHovered] = btnHover;
    c[ImGuiCol_ButtonActive] = btnActive;
    c[ImGuiCol_Header] = ImVec4(accent.x*0.62f, accent.y*0.62f, accent.z*0.62f, 1.0f);
    c[ImGuiCol_HeaderHovered] = accent;
    c[ImGuiCol_HeaderActive] = ImVec4(std::min(1.0f, accent.x+0.12f), std::min(1.0f, accent.y+0.12f), std::min(1.0f, accent.z+0.12f), 1.0f);
    c[ImGuiCol_CheckMark] = accent; c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = pcbDot;
    c[ImGuiCol_Separator] = ImVec4(pcbTrace.x, pcbTrace.y, pcbTrace.z, 0.35f);
    c[ImGuiCol_SeparatorHovered] = ImVec4(pcbTrace.x, pcbTrace.y, pcbTrace.z, 0.65f);
    c[ImGuiCol_SeparatorActive] = ImVec4(accent.x, accent.y, accent.z, 0.90f);
    // Scrollbar: rounded to match thumb, no black square
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.08f, 0.08f, 0.085f, 0.0f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.30f, 0.30f, 0.32f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.00f, 0.55f, 0.40f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.00f, 0.70f, 0.52f, 1.0f);
    c[ImGuiCol_Text] = ImVec4(0.92f, 0.92f, 0.925f, 1.0f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.55f, 0.57f, 1.0f);
}

bool ConfigWindow::LoadTextureFromFile(const std::wstring& path, ID3D11ShaderResourceView** outSRV, ID3D11Texture2D** outTex, int* outW, int* outH)
{
    if (!m_device || !outSRV) return false;
    Gdiplus::Bitmap bmp(path.c_str());
    if (bmp.GetLastStatus() != Gdiplus::Ok) return false;
    int w = bmp.GetWidth();
    int h = bmp.GetHeight();
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return false;
    std::vector<uint32_t> pixels((size_t)w * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            Gdiplus::Color c; bmp.GetPixel(x, y, &c);
            // Store as BGRA for DXGI_FORMAT_B8G8R8A8_UNORM (A high, B mid-high, G mid-low, R low)
            uint32_t bgra = (c.GetA() << 24) | (c.GetB() << 16) | (c.GetG() << 8) | c.GetR();
            pixels[y * w + x] = bgra;
        }
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{}; data.pSysMem = pixels.data(); data.SysMemPitch = w * 4;
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(m_device->CreateTexture2D(&desc, &data, &tex))) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = desc.Format; srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; srvDesc.Texture2D.MipLevels = 1;
    ID3D11ShaderResourceView* srv = nullptr;
    if (FAILED(m_device->CreateShaderResourceView(tex, &srvDesc, &srv))) { tex->Release(); return false; }
    *outSRV = srv;
    if (outTex) *outTex = tex; else tex->Release();
    if (outW) *outW = w;
    if (outH) *outH = h;
    return true;
}
void ConfigWindow::EnsureCamoTexture()
{
    if (m_camoLoaded) return;
    m_camoLoaded = true; // try once
    std::wstring exeDir = utils::GetExeDirectory();
    std::vector<std::wstring> candidates = {
        exeDir + L"assets\\GeoCamoBlack.png",
        exeDir + L"..\\assets\\GeoCamoBlack.png",
        exeDir + L"GeoCamoBlack.png",
        L"D:\\DopesAIDevelopment\\Projects\\DopesCrosshairTool\\assets\\GeoCamoBlack.png"
    };
    for (auto& p : candidates) {
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
            if (LoadTextureFromFile(p, &m_camoSRV, &m_camoTex, &m_camoW, &m_camoH)) break;
        }
    }
}
void ConfigWindow::EnsureDMLogoTexture()
{
    if (m_dmLogoLoaded) return;
    m_dmLogoLoaded = true;
    std::wstring exeDir = utils::GetExeDirectory();
    std::vector<std::wstring> candidates = {
        exeDir + L"assets\\DMlogo.png",
        exeDir + L"..\\assets\\DMlogo.png",
        exeDir + L"DMlogo.png",
        L"D:\\DopesAIDevelopment\\Projects\\DopesCrosshairTool\\assets\\DMlogo.png"
    };
    for (auto& p : candidates) {
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
            if (LoadTextureFromFile(p, &m_dmLogoSRV, &m_dmLogoTex, &m_dmLogoW, &m_dmLogoH)) break;
        }
    }
}
void ConfigWindow::DrawCamoBackground(ImDrawList* dl, const ImVec2& pos, const ImVec2& size)
{
    EnsureCamoTexture();
    // ONE photo, padded corners to match border (20px), inside outer border, behind panels
    const float pad = 6.0f;
    const float rnd = 20.0f;
    ImVec2 ipos(pos.x + pad, pos.y + pad);
    ImVec2 isize(size.x - pad*2.0f, size.y - pad*2.0f);
    float irnd = std::max(0.0f, rnd - pad + 1.0f);
    if (m_camoSRV) {
        ImTextureID tid = (ImTextureID)m_camoSRV;
        // ONE stretched photo - not tiled - centered crop, slight slow drift for life
        float zoom = 1.06f + 0.015f * sinf(m_camoOffset * 6.0f); // 1.06 ± 0.015 breathe
        float iw = isize.x * zoom;
        float ih = isize.y * zoom;
        // keep centered, slight pan drift
        float px = ipos.x + (isize.x - iw) * 0.5f + sinf(m_camoOffset * 2.1f) * 6.0f;
        float py = ipos.y + (isize.y - ih) * 0.5f + cosf(m_camoOffset * 1.7f) * 4.0f;
        ImVec2 p0(px, py);
        ImVec2 p1(px + iw, py + ih);
        dl->PushClipRect(ipos, ImVec2(ipos.x + isize.x, ipos.y + isize.y), true);
        // Single photo - uv 0-1 fills padded inner rect, aspect stretched to window (no seam)
        dl->AddImage(tid, p0, p1, ImVec2(0,0), ImVec2(1,1), IM_COL32(255,255,255, 200));
        dl->AddRectFilled(ipos, ImVec2(ipos.x + isize.x, ipos.y + isize.y), IM_COL32(8, 9, 11, 48), irnd);
        dl->PopClipRect();
        dl->AddRect(ipos, ImVec2(ipos.x + isize.x, ipos.y + isize.y), IM_COL32(255,255,255, 9), irnd, 0, 0.8f);
    } else {
        dl->AddRectFilled(ipos, ImVec2(ipos.x + isize.x, ipos.y + isize.y), IM_COL32(14, 14, 15, 255), irnd);
    }
}
void ConfigWindow::DrawAnimatedBorder(ImDrawList* dl, const ImVec2& pos, const ImVec2& size, float time)
{
    float r = 20.0f;
    float thick = 2.25f;
    int lr = GetRValue(m_cfg.themeAccentLime), lg = GetGValue(m_cfg.themeAccentLime), lb = GetBValue(m_cfg.themeAccentLime);
    int tr = GetRValue(m_cfg.themeAccentTeal), tg = GetGValue(m_cfg.themeAccentTeal), tb = GetBValue(m_cfg.themeAccentTeal);
    float pulse = 0.5f + 0.5f * sinf(time * 1.35f);
    float t = pulse * pulse * (3.0f - 2.0f * pulse);
    uint8_t rr = (uint8_t)((lr * (1 - t) + tr * t));
    uint8_t gg = (uint8_t)((lg * (1 - t) + tg * t));
    uint8_t bb = (uint8_t)((lb * (1 - t) + tb * t));
    ImU32 col = IM_COL32(rr, gg, bb, 255);
    ImU32 glow = IM_COL32(rr, gg, bb, (int)(54 + 52 * pulse)); // 54..106 alpha
    // 1) continuous rounded rect - guaranteed closed loop (fixes 2nd photo gap)
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), col, r, 0, thick);
    // 2) soft outer glow pulse (slightly thicker, translucent)
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), glow, r, 0, thick + 2.8f);
    // 3) inner hairline highlight for depth
    dl->AddRect(ImVec2(pos.x + 0.7f, pos.y + 0.7f), ImVec2(pos.x + size.x - 0.7f, pos.y + size.y - 0.7f), IM_COL32(255, 255, 255, 18), r - 0.5f, 0, 0.9f);
    // 4) micro pulse on thickness (breath)
    float breath = 0.0f + 1.4f * sinf(time * 1.35f + 1.2f);
    if (breath > 0) {
        float bt = 0.9f + breath * 0.22f;
        dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(rr, gg, bb, 28), r, 0, bt);
    }
}

void ConfigWindow::DrawPcbWindowFrame(const ImVec2& pos, const ImVec2& size, ImU32 borderColor, ImU32 bgColor)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // 1) Camo background - INSIDE animated border with corner padding, BEHIND panels + PCB traces (very bottom layer)
    DrawCamoBackground(dl, pos, size);
    // 2) Inner translucent panel fill with same outer rounding - camo peeks at padded corners
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bgColor, 20.0f);
    // 3) Static thin inner hairline (subtle) - outer animated pulsing border will be topmost
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), borderColor, 20.0f, 0, 0.85f);
    // 4) Animated pulsing Lime ↔ Teal continuous border - drawn LAST so fully closed loop, outside padded camo
    float tm = (float)ImGui::GetTime();
    DrawAnimatedBorder(dl, pos, size, tm);
}
void ConfigWindow::BeginPcbWindow(bool* p_open, ImVec4 accentColor)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.02f, 0.03f, 0.0f)); // transparent so camo shows
    ImGui::Begin("OverlayMenu", p_open, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
    ImVec2 winPos = ImGui::GetWindowPos();
    ImVec2 winSize = ImGui::GetWindowSize();
    ImVec4 bgBlend = ImVec4(0.08f, 0.08f, 0.09f, 0.42f); // much more translucent so GeoCamoBlack shows through
    ImU32 bgColor = ImGui::GetColorU32(bgBlend);
    ImU32 borderColor = ImGui::GetColorU32(accentColor);
    DrawPcbWindowFrame(winPos, winSize, borderColor, bgColor);
    ImGui::SetCursorPosY(38);
}
void ConfigWindow::EndPcbWindow()
{
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}
ImVec2 ConfigWindow::PolylinePoint(const ImVec2* pts, int count, float t)
{
    if (count < 2) return pts[0];
    if (t <= 0.0f) return pts[0];
    float total = 0.0f;
    for (int i=1;i<count;++i){ float dx=pts[i].x-pts[i-1].x, dy=pts[i].y-pts[i-1].y; total+=sqrtf(dx*dx+dy*dy); }
    float target = t*total;
    for (int i=1;i<count;++i){ float dx=pts[i].x-pts[i-1].x, dy=pts[i].y-pts[i-1].y; float len=sqrtf(dx*dx+dy*dy); if(target<=len){ float s=len>0?target/len:0; return ImVec2(pts[i-1].x+dx*s, pts[i-1].y+dy*s);} target-=len; }
    return pts[count-1];
}
void ConfigWindow::DrawPolyline(ImDrawList* dl, const ImVec2* pts, int count, ImU32 col, float thick)
{
    for(int i=1;i<count;++i) dl->AddLine(pts[i-1], pts[i], col, thick);
}
void ConfigWindow::DrawGlowHead(ImDrawList* dl, const ImVec2& pos, float coreR, float haloR)
{
    int dr = GetRValue(m_cfg.themePcbDot), dg = GetGValue(m_cfg.themePcbDot), db = GetBValue(m_cfg.themePcbDot);
    dl->AddCircleFilled(pos, coreR, IM_COL32(dr,dg,db,230));
    dl->AddCircleFilled(pos, haloR, IM_COL32(dr*0.70, dg*0.70, db*0.70, 70));
}
void ConfigWindow::DrawCornerPcbNetwork(ImDrawList* dl, const ImVec2& corner, float progress, bool mirrorX, bool mirrorY)
{
    int tr = GetRValue(m_cfg.themePcbTrace), tg = GetGValue(m_cfg.themePcbTrace), tb = GetBValue(m_cfg.themePcbTrace);
    const ImU32 trace = IM_COL32(tr,tg,tb,255);
    auto P = [&](float dx,float dy)->ImVec2{ return ImVec2(mirrorX?corner.x+dx:corner.x-dx, mirrorY?corner.y-dy:corner.y+dy); };
    ImVec2 a[]={P(14,12),P(120,12),P(134,26),P(260,26),P(274,38),P(380,38)};
    ImVec2 b[]={P(14,24),P(70,24),P(84,38),P(150,38)};
    ImVec2 c1[]={P(100,12),P(100,22)};
    ImVec2 c2[]={P(200,26),P(200,36)};
    DrawPolyline(dl,a,6,trace,1.2f);
    DrawPolyline(dl,b,4,trace,1.2f);
    DrawPolyline(dl,c1,2,trace,1.2f);
    DrawPolyline(dl,c2,2,trace,1.2f);
    dl->AddCircleFilled(a[0],2.5f,trace);
    dl->AddCircleFilled(b[0],2.5f,trace);
    dl->AddCircle(a[2],2.0f,trace,0,1.0f);
    dl->AddCircle(a[4],2.0f,trace,0,1.0f);
    dl->AddCircle(b[2],2.0f,trace,0,1.0f);
    dl->AddCircle(c1[1],2.0f,trace,0,1.0f);
    dl->AddCircle(c2[1],2.0f,trace,0,1.0f);
    dl->AddCircle(a[5],3.0f,trace,0,1.0f);
    dl->AddCircle(b[3],3.0f,trace,0,1.0f);
    DrawGlowHead(dl, PolylinePoint(a,6,progress));
    DrawGlowHead(dl, PolylinePoint(b,4,1.0f-progress));
}
void ConfigWindow::SectionTitle(const char* eyebrow, const char* title, const char* body)
{
    ImGui::Indent(10.0f);
    ImGui::TextColored(kAccent, "%s", eyebrow);
    ImGui::SetWindowFontScale(1.35f);
    ImGui::TextUnformatted(title);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextColored(kMuted, "%s", body);
    ImGui::Unindent(10.0f);
    ImGui::Spacing();
}

static void CardBegin(const char* id, ImVec2 size, ImGuiWindowFlags flags=0)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.105f,0.11f,0.15f,1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14.0f);
    ImGui::BeginChild(id, size, ImGuiChildFlags_Borders, flags);
}
static void CardEnd(){ ImGui::EndChild(); ImGui::PopStyleVar(); ImGui::PopStyleColor(); }

bool ConfigWindow::ColorPickerWithPreview(const char* label, COLORREF& color)
{
    float col[3] = { GetRValue(color)/255.0f, GetGValue(color)/255.0f, GetBValue(color)/255.0f };
    bool changed = false;
    if (ImGui::ColorEdit3(label, col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel))
    {
        color = RGB(int(col[0]*255), int(col[1]*255), int(col[2]*255));
        changed = true;
        m_previewDirty = true;
    }
    return changed;
}

void ConfigWindow::EnsurePreviewTexture(int w, int h)
{
    if (m_previewTex && m_previewW==w && m_previewH==h) return;
    if (m_previewSRV) { m_previewSRV->Release(); m_previewSRV=nullptr; }
    if (m_previewTex) { m_previewTex->Release(); m_previewTex=nullptr; }
    m_previewW=w; m_previewH=h;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width=w; desc.Height=h; desc.MipLevels=1; desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count=1; desc.Usage=D3D11_USAGE_DYNAMIC; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE; desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    m_device->CreateTexture2D(&desc, nullptr, &m_previewTex);
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format=desc.Format; srvDesc.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; srvDesc.Texture2D.MipLevels=1;
    m_device->CreateShaderResourceView(m_previewTex, &srvDesc, &m_previewSRV);
}

void ConfigWindow::UpdatePreviewTexture()
{
    bool cfgChanged = memcmp(&m_cfg.crosshair, &m_lastPreviewCfg.crosshair, sizeof(CrosshairConfig))!=0;
    if (!m_previewDirty && !cfgChanged) return;
    m_previewDirty=false;
    m_lastPreviewCfg=m_cfg;
    const int PW = 320, PH = 220;
    EnsurePreviewTexture(PW, PH);
    HBITMAP bmp = m_renderer.RenderPreview(PW, PH, m_cfg);
    if (!bmp) return;
    BITMAP bm{}; GetObjectW(bmp, sizeof(bm), &bm);
    BITMAPINFO bmi{}; bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); bmi.bmiHeader.biWidth=PW; bmi.bmiHeader.biHeight=-PH; bmi.bmiHeader.biPlanes=1; bmi.bmiHeader.biBitCount=32; bmi.bmiHeader.biCompression=BI_RGB;
    std::vector<uint32_t> pixels(PW*PH);
    HDC dc = GetDC(nullptr);
    GetDIBits(dc, bmp, 0, PH, pixels.data(), &bmi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    DeleteObject(bmp);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(m_context->Map(m_previewTex, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        for(int y=0;y<PH;++y){
            uint32_t* dst = (uint32_t*)((uint8_t*)mapped.pData + y*mapped.RowPitch);
            uint32_t* src = pixels.data() + y*PW;
            memcpy(dst, src, PW*4);
        }
        m_context->Unmap(m_previewTex,0);
    }
}

void ConfigWindow::BrowsePng()
{
    wchar_t file[MAX_PATH]=L"";
    OPENFILENAMEW ofn{}; ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=nullptr;
    ofn.lpstrFilter=L"Image Files\0*.png;*.bmp;*.jpg;*.jpeg\0PNG Files\0*.png\0All Files\0*.*\0";
    ofn.lpstrFile=file; ofn.nMaxFile=MAX_PATH; ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST; ofn.lpstrDefExt=L"png";
    if (GetOpenFileNameW(&ofn)){
        m_cfg.crosshair.pngPath=file;
        m_cfg.crosshair.usePng=true;
        m_previewDirty=true;
        if(m_overlay) m_overlay->SetConfig(m_cfg);
        EditorLoadFromPng(file);
    }
}

// Editor helpers
void ConfigWindow::EditorPushUndo(){
    m_editor.undoStack.push_back(m_editor.pixels);
    if(m_editor.undoStack.size()>50) m_editor.undoStack.erase(m_editor.undoStack.begin());
    m_editor.redoStack.clear();
}
void ConfigWindow::EditorUndo(){
    if(m_editor.undoStack.empty()) return;
    m_editor.redoStack.push_back(m_editor.pixels);
    m_editor.pixels = m_editor.undoStack.back();
    m_editor.undoStack.pop_back();
}
void ConfigWindow::EditorRedo(){
    if(m_editor.redoStack.empty()) return;
    m_editor.undoStack.push_back(m_editor.pixels);
    m_editor.pixels = m_editor.redoStack.back();
    m_editor.redoStack.pop_back();
}
void ConfigWindow::EditorNew(){
    EditorPushUndo();
    std::fill(m_editor.pixels.begin(), m_editor.pixels.end(), 0);
}
void ConfigWindow::EditorResize(int newSize){
    newSize = std::clamp(newSize, 16, 256);
    if(newSize==m_editor.imgSize) return;
    EditorPushUndo();
    std::vector<uint32_t> newPixels(newSize*newSize,0);
    int copy = std::min(m_editor.imgSize, newSize);
    for(int y=0;y<copy;++y) for(int x=0;x<copy;++x) newPixels[y*newSize+x]=m_editor.pixels[y*m_editor.imgSize+x];
    m_editor.imgSize=newSize;
    m_editor.pixels.swap(newPixels);
}
void ConfigWindow::EditorLoadFromPng(const std::wstring& path){
    if(path.empty() || GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES) return;
    Gdiplus::Bitmap bmp(path.c_str());
    if(bmp.GetLastStatus()!=Gdiplus::Ok) return;
    int w=bmp.GetWidth(), h=bmp.GetHeight();
    int sz = std::max(w,h);
    // pick nearest 64/128/256 etc
    int target = 64;
    if(sz>64 && sz<=128) target=128;
    else if(sz>128) target=256;
    if(sz!=64 && sz!=128 && sz!=256) target=64;
    // Actually just set to bmp size clamped
    target = std::clamp(sz,16,256);
    // Snap to 64/128/256 if close? keep exact
    m_editor.imgSize=target;
    m_editor.pixels.assign(target*target,0);
    // Sample bitmap into pixels (scale if needed)
    for(int y=0;y<target;++y) for(int x=0;x<target;++x){
        int sx = x*w/target;
        int sy = y*h/target;
        Gdiplus::Color c; bmp.GetPixel(sx,sy,&c);
        uint32_t rgba = (c.GetA()<<24) | (c.GetB()<<16) | (c.GetG()<<8) | c.GetR();
        // Store as 0xAABBGGRR? We'll store as 0xAARRGGBB for GDI?
        // For editor we store as 0xAABBGGRR (R in low byte)
        uint32_t store = (c.GetA()<<24) | (c.GetR()<<16) | (c.GetG()<<8) | c.GetB();
        // Convert to our format: 0xAARRGGBB? Keep consistent with save
        m_editor.pixels[y*target+x]=store;
    }
}
void ConfigWindow::EditorSave(){
    // Use saveName if provided, else fallback
    char nameBuf[64]; strncpy_s(nameBuf, m_editor.saveName, sizeof(nameBuf)-1);
    std::string nameStr(nameBuf);
    if(nameStr.empty()) nameStr="New_crosshair";
    // Sanitize name
    for(char& c: nameStr) if(c=='\\' || c=='/' || c==':' || c=='*' || c=='?' || c=='"' || c=='<' || c=='>' || c=='|') c='_';
    std::wstring wname = utils::Utf8ToW(nameStr);
    EditorSaveAs(wname);
}
void ConfigWindow::EditorSaveAs(const std::wstring& name){
    int sz=m_editor.imgSize;
    if(sz<=0) return;
    Gdiplus::Bitmap bmp(sz, sz, PixelFormat32bppARGB);
    for(int y=0;y<sz;++y) for(int x=0;x<sz;++x){
        uint32_t v=m_editor.pixels[y*sz+x];
        int a=(v>>24)&0xFF, r=(v>>16)&0xFF, g=(v>>8)&0xFF, b=v&0xFF;
        Gdiplus::Color c(a,r,g,b);
        bmp.SetPixel(x,y,c);
    }
    std::wstring dir = GetLibraryDir();
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring outPath = dir + name + L".png";
    UINT num=0, szEnc=0;
    Gdiplus::GetImageEncodersSize(&num,&szEnc);
    std::vector<uint8_t> buf(szEnc);
    Gdiplus::ImageCodecInfo* info=(Gdiplus::ImageCodecInfo*)buf.data();
    Gdiplus::GetImageEncoders(num, szEnc, info);
    CLSID pngClsid{};
    for(UINT i=0;i<num;++i) if(wcscmp(info[i].MimeType,L"image/png")==0){ pngClsid=info[i].Clsid; break; }
    bmp.Save(outPath.c_str(), &pngClsid, nullptr);
    m_cfg.crosshair.pngPath=outPath;
    m_cfg.crosshair.usePng=true;
    m_cfg.crosshair.pngScale = 1.0f;
    m_previewDirty=true;
    if(m_overlay) m_overlay->SetConfig(m_cfg);
    ConfigManager::Save(m_cfg);
    RefreshLibrary();
}

// Library helpers - Documents\DopesCrosshairTool\custom-crosshairs\ + exe fallback
std::wstring ConfigWindow::GetLibraryDir(){
    wchar_t doc[MAX_PATH]{};
    std::wstring dir;
    if (SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, doc) == S_OK) {
        std::wstring base = std::wstring(doc) + L"\\DopesCrosshairTool\\";
        CreateDirectoryW(base.c_str(), nullptr);
        dir = base + L"custom-crosshairs\\";
        CreateDirectoryW(dir.c_str(), nullptr);
        // Also ensure exe fallback exists for migration
        std::wstring exeDir = utils::GetExeDirectory();
        CreateDirectoryW((exeDir + L"custom-crosshairs\\").c_str(), nullptr);
        CreateDirectoryW((exeDir + L"crosshairs\\").c_str(), nullptr);
        return dir;
    }
    // Fallback to exe dir
    std::wstring exeDir = utils::GetExeDirectory();
    dir = exeDir + L"custom-crosshairs\\";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}
std::wstring ConfigWindow::GetLibraryDirFallbackExe(){
    std::wstring exeDir = utils::GetExeDirectory();
    return exeDir + L"custom-crosshairs\\";
}
void ConfigWindow::LoadFavorites(){
    std::wstring favPath = GetLibraryDir() + L"favorites.txt";
    m_favorites.clear();
    std::ifstream f(utils::WToUtf8(favPath));
    if(!f) return;
    std::string line;
    while(std::getline(f,line)){
        if(line.empty()) continue;
        m_favorites.push_back(utils::Utf8ToW(line));
    }
}
void ConfigWindow::SaveFavorites(){
    std::wstring favPath = GetLibraryDir() + L"favorites.txt";
    std::ofstream f(utils::WToUtf8(favPath), std::ios::trunc);
    for(auto& s: m_favorites) f << utils::WToUtf8(s) << "\n";
}
bool ConfigWindow::IsFavorite(const std::wstring& file){
    std::wstring name = file;
    size_t p = name.find_last_of(L"\\/");
    if(p!=std::wstring::npos) name = name.substr(p+1);
    // strip .png
    size_t dot = name.find_last_of(L".");
    if(dot!=std::wstring::npos) name = name.substr(0,dot);
    for(auto& f: m_favorites) if(utils::ToLower(f)==utils::ToLower(name)) return true;
    return false;
}
void ConfigWindow::ToggleFavorite(const std::wstring& file){
    std::wstring name=file; size_t p=name.find_last_of(L"\\/"); if(p!=std::wstring::npos) name=name.substr(p+1); size_t dot=name.find_last_of(L"."); if(dot!=std::wstring::npos) name=name.substr(0,dot);
    for(auto it=m_favorites.begin(); it!=m_favorites.end(); ++it) if(utils::ToLower(*it)==utils::ToLower(name)){ m_favorites.erase(it); SaveFavorites(); return; }
    m_favorites.push_back(name); SaveFavorites();
}
void ConfigWindow::RefreshLibrary(){
    // clear old SRVs
    for(auto srv: m_librarySRVs) if(srv) srv->Release();
    for(auto tex: m_libraryTexs) if(tex) tex->Release();
    m_librarySRVs.clear(); m_libraryTexs.clear();
    m_libraryFiles.clear(); m_libraryNames.clear();
    LoadFavorites();
    auto addFromDir = [&](const std::wstring& dir){
        const wchar_t* pats[] = {L"*.png", L"*.svg", L"*.gif"};
        for(auto pat: pats){
            WIN32_FIND_DATAW fd{};
            HANDLE h = FindFirstFileW((dir + pat).c_str(), &fd);
            if(h==INVALID_HANDLE_VALUE) continue;
            do{
                if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                std::wstring full = dir + fd.cFileName;
                std::wstring lowFull = utils::ToLower(full);
                bool dup=false;
                for(auto& e: m_libraryFiles) if(utils::ToLower(e)==lowFull){ dup=true; break; }
                if(dup) continue;
                m_libraryFiles.push_back(full);
                std::wstring wname = fd.cFileName;
                size_t dot = wname.find_last_of(L".");
                if(dot!=std::wstring::npos) wname=wname.substr(0,dot);
                std::string nameA = utils::WToUtf8(wname);
                m_libraryNames.push_back(nameA);
                m_librarySRVs.push_back(nullptr);
                m_libraryTexs.push_back(nullptr);
            } while(FindNextFileW(h,&fd));
            FindClose(h);
        }
    };
    std::wstring docDir = GetLibraryDir();
    addFromDir(docDir);
    // fallback exe dirs (custom-crosshairs + legacy crosshairs) for migration
    std::wstring exeDir = utils::GetExeDirectory();
    addFromDir(exeDir + L"custom-crosshairs\\");
    addFromDir(exeDir + L"crosshairs\\");
    // Bundled crossover 530 pack (READ ONLY source: D:\DopesAIDevelopment\Examples\crossover-main\src\static\crosshairs)
    // Copied to assets\crossover at build time - categories preserved as "Category - Name"
    addFromDir(exeDir + L"assets\\crossover\\");
    addFromDir(exeDir + L"..\\assets\\crossover\\");
    addFromDir(L"D:\\DopesAIDevelopment\\Projects\\DopesCrosshairTool\\assets\\crossover\\");
    // sort by name
    // simple bubble sort keeping parallel arrays
    for(size_t i=0;i<m_libraryNames.size();++i) for(size_t j=i+1;j<m_libraryNames.size();++j) if(_stricmp(m_libraryNames[i].c_str(), m_libraryNames[j].c_str())>0){
        std::swap(m_libraryFiles[i], m_libraryFiles[j]);
        std::swap(m_libraryNames[i], m_libraryNames[j]);
        std::swap(m_librarySRVs[i], m_librarySRVs[j]);
        std::swap(m_libraryTexs[i], m_libraryTexs[j]);
    }
}
ID3D11ShaderResourceView* ConfigWindow::GetThumbnailFor(const std::wstring& path, int thumbSize){
    int idx=-1;
    for(size_t i=0;i<m_libraryFiles.size();++i) if(m_libraryFiles[i]==path){ idx=(int)i; break; }
    if(idx==-1) return nullptr;
    if(m_librarySRVs[idx]) return m_librarySRVs[idx];
    std::wstring low = utils::ToLower(path);
    bool isSvg = low.size()>=4 && low.substr(low.size()-4)==L".svg";
    int texW=thumbSize, texH=thumbSize;
    std::vector<uint32_t> pixels(texW*texH);
    for(int y=0;y<texH;++y) for(int x=0;x<texW;++x){
        bool checker=((x/8)+(y/8))%2==0;
        pixels[y*texW+x]= checker? 0xFF3C3C3C : 0xFF2A2A2A;
    }
    if(isSvg){
        // SVG via nanosvg - rasterize with recolor for black Kenney pack
        std::string narrow = utils::WToUtf8(path);
        NSVGimage* image = nsvgParseFromFile(narrow.c_str(), "px", 96);
        if(image){
            // Recolor black shapes to svgTint if enabled
            if(m_cfg.svgRecolorBlack){
                unsigned int tint = NSVG_RGB(GetRValue(m_cfg.svgTint), GetGValue(m_cfg.svgTint), GetBValue(m_cfg.svgTint));
                for(NSVGshape* sh=image->shapes; sh; sh=sh->next){
                    if(sh->fill.type==NSVG_PAINT_COLOR && sh->fill.color==NSVG_RGB(0,0,0)) sh->fill.color = tint;
                    if(sh->stroke.type==NSVG_PAINT_COLOR && sh->stroke.color==NSVG_RGB(0,0,0)) sh->stroke.color = tint;
                    // also handle near-black
                    if(sh->fill.type==NSVG_PAINT_COLOR && sh->fill.color==NSVG_RGB(1,1,1)) {} // keep white
                }
            }
            float scale = std::min(texW / image->width, texH / image->height) * 0.88f;
            float tx = (texW - image->width * scale) * 0.5f;
            float ty = (texH - image->height * scale) * 0.5f;
            std::vector<unsigned char> rgba(texW*texH*4, 0);
            NSVGrasterizer* rast = nsvgCreateRasterizer();
            if(rast){
                nsvgRasterize(rast, image, tx, ty, scale, rgba.data(), texW, texH, texW*4);
                nsvgDeleteRasterizer(rast);
                for(int y=0;y<texH;++y) for(int x=0;x<texW;++x){
                    int i=(y*texW+x)*4;
                    unsigned char r=rgba[i+0], g=rgba[i+1], b=rgba[i+2], a=rgba[i+3];
                    if(a==0) continue;
                    uint32_t col = (a<<24)|(b<<16)|(g<<8)|r; // B8G8R8A8
                    pixels[y*texW+x]=col;
                }
            }
            nsvgDelete(image);
        } else {
            // fallback: keep checker only
        }
    } else {
        Gdiplus::Bitmap bmp(path.c_str());
        if(bmp.GetLastStatus()!=Gdiplus::Ok) return nullptr;
        int w=bmp.GetWidth(), h=bmp.GetHeight();
        float scale = std::min(texW/(float)w, texH/(float)h) * 0.85f;
        int rw=int(w*scale), rh=int(h*scale);
        int offX=(texW-rw)/2, offY=(texH-rh)/2;
        for(int y=0;y<rh;++y) for(int x=0;x<rw;++x){
            int sx=int(x/scale), sy=int(y/scale);
            if(sx>=w||sy>=h) continue;
            Gdiplus::Color c; bmp.GetPixel(sx,sy,&c);
            if(c.GetA()==0) continue;
            uint32_t col = (c.GetA()<<24)|(c.GetB()<<16)|(c.GetG()<<8)|c.GetR();
            pixels[(offY+y)*texW+(offX+x)] = col;
        }
    }
    // create texture
    D3D11_TEXTURE2D_DESC desc{}; desc.Width=texW; desc.Height=texH; desc.MipLevels=1; desc.ArraySize=1; desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count=1; desc.Usage=D3D11_USAGE_DEFAULT; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{}; data.pSysMem=pixels.data(); data.SysMemPitch=texW*4;
    ID3D11Texture2D* tex=nullptr;
    if(FAILED(m_device->CreateTexture2D(&desc,&data,&tex))) return nullptr;
    ID3D11ShaderResourceView* srv=nullptr;
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{}; srvDesc.Format=desc.Format; srvDesc.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; srvDesc.Texture2D.MipLevels=1;
    if(FAILED(m_device->CreateShaderResourceView(tex,&srvDesc,&srv))){ tex->Release(); return nullptr; }
    m_libraryTexs[idx]=tex;
    m_librarySRVs[idx]=srv;
    return srv;
}

// Hotkey capture helper
static bool PollHotkeyCapture(int& outVk){
    for(int vk=1; vk<256; ++vk){
        if(vk==VK_LBUTTON || vk==VK_RBUTTON || vk==VK_MBUTTON || vk==VK_XBUTTON1 || vk==VK_XBUTTON2){
            if(GetAsyncKeyState(vk) & 0x8000){ outVk=vk; return true; }
        } else {
            if(GetAsyncKeyState(vk) & 0x8000){
                // Avoid mouse move etc, require key down
                // Filter to common keys: letters, F keys, etc.
                if((vk>=0x30 && vk<=0x5A) || (vk>=VK_F1 && vk<=VK_F24) || vk==VK_SPACE || vk==VK_CONTROL || vk==VK_SHIFT || vk==VK_MENU){
                    outVk=vk; return true;
                }
                // Also allow any other for flexibility
                outVk=vk; return true;
            }
        }
    }
    return false;
}

// Page renderers - Overview now: Available crosshairs grid (left) + right panel with Custom profile settings (moved from Settings bottom tabs)
void ConfigWindow::RenderDashboard()
{
    // Ensure library is loaded
    static bool libInit=false;
    if(!libInit){ RefreshLibrary(); libInit=true; }

    SectionTitle("OVERVIEW", "Available crosshairs", "Click to select. Editor saves here.");
    const float avail = ImGui::GetContentRegionAvail().x;
    const float leftW = (avail - 14.0f) * 0.45f;
    const float rightW = avail - leftW - 14.0f;

    // LEFT: crosshair library grid
    CardBegin("libraryCard", ImVec2(leftW, -1), ImGuiWindowFlags_None);
    ImGui::TextColored(kMuted, "Favorites");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Filter
    if (ImGui::InputTextWithHint("##filter", "Filter...", m_libraryFilter, sizeof(m_libraryFilter))) {}
    ImGui::Spacing();

    // Grid params - in-line, no names below
    const float thumbSize = 64.0f;
    const float cellW = thumbSize + 20.0f;
    const float cellH = thumbSize + 12.0f;
    float availW = ImGui::GetContentRegionAvail().x;
    int cols = std::max(1, (int)(availW / cellW));
    if(cols > 4) cols = 4; // cap to avoid overcrowd
    if(availW < cellW*2 + 20) cols = 1; // single column on narrow
    int shown = 0;
    for(size_t i=0;i<m_libraryFiles.size();++i){
        if(!IsFavorite(m_libraryFiles[i])) continue;
        std::string nameLower = m_libraryNames[i];
        std::string filtLower = m_libraryFilter;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
        std::transform(filtLower.begin(), filtLower.end(), filtLower.begin(), ::tolower);
        if(!filtLower.empty() && nameLower.find(filtLower)==std::string::npos) continue;
        // filter applied
        if(shown>=12){ // Overview capped to avoid clutter - full 530 in Library
            // leave hint after loop
            break;
        }
        int col = shown % cols;
        if(col!=0) ImGui::SameLine();
        shown++;
        ImGui::BeginGroup();
        // Thumbnail
        ID3D11ShaderResourceView* srv = GetThumbnailFor(m_libraryFiles[i], (int)thumbSize);
        bool selected = (m_cfg.crosshair.usePng && m_cfg.crosshair.pngPath == m_libraryFiles[i]);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1 = ImVec2(p0.x + thumbSize, p0.y + thumbSize);
        // Background with selection highlight
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImU32 bg = selected ? IM_COL32(80, 60, 90, 255) : IM_COL32(45,45,55,255);
        dl->AddRectFilled(p0, p1, bg, 4.0f);
        if(selected) dl->AddRect(p0, p1, IM_COL32(199,97,140,255), 4.0f, 0, 2.0f);
        else dl->AddRect(p0, p1, IM_COL32(90,92,110,120), 4.0f, 0, 1.0f);
        if(srv){
            ImGui::SetCursorScreenPos(ImVec2(p0.x+2, p0.y+2));
            ImGui::Image((ImTextureID)srv, ImVec2(thumbSize-4, thumbSize-4));
        } else {
            ImGui::SetCursorScreenPos(p0);
            ImGui::Dummy(ImVec2(thumbSize, thumbSize));
        }
        // Click area
        ImGui::SetCursorScreenPos(p0);
        if(ImGui::InvisibleButton(("##thumb"+std::to_string(i)).c_str(), ImVec2(thumbSize, thumbSize))){
            m_cfg.crosshair.pngPath = m_libraryFiles[i];
            m_cfg.crosshair.usePng = true;
            m_previewDirty=true;
            if(m_overlay) m_overlay->SetConfig(m_cfg);
            ConfigManager::Save(m_cfg);
            m_selectedLibrary = (int)i;
        }
        bool hovered = ImGui::IsItemHovered();
        if(hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)){
            ToggleFavorite(m_libraryFiles[i]);
        }
        // Favorite star - grid stays in-line
        if(IsFavorite(m_libraryFiles[i])){
            ImVec2 starPos(p1.x - 14, p0.y + 2);
            dl->AddText(starPos, IM_COL32(255,220,100,255), "*");
        }
        if(selected){
            dl->AddCircleFilled(ImVec2(p1.x - 8, p1.y - 8), 5.0f, IM_COL32(0,210,148,255));
        }
        ImGui::EndGroup();
        // Hover popup after 1 sec
        if(hovered){
            if(m_hoveredIdx != (int)i){
                m_hoveredIdx = (int)i;
                m_hoverStartTime = ImGui::GetTime();
            }
            if(ImGui::GetTime() - m_hoverStartTime > 1.0){
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(m_libraryNames[i].c_str());
                ImGui::TextColored(kMuted, "Left-click: Apply  |  Right-click: Favorite");
                ImGui::EndTooltip();
            }
        }
    }
    if(shown==0){
        ImGui::TextColored(kMuted, "No favorites yet. Star some in Library.");
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    // Bottom padding so not cut by rounded border
    if(ImGui::Button("Refresh Library", ImVec2(-1,28))) RefreshLibrary();
    ImGui::Spacing();
    CardEnd();

    ImGui::SameLine();

    // RIGHT: combined settings panel (the two bottom tabs from Settings moved here)
    CardBegin("overviewRightPanel", ImVec2(rightW, -1), ImGuiWindowFlags_None);
    ImGui::TextColored(kMuted, "Per-crosshair settings");
    ImGui::Separator();
    // Custom profile
    ImGui::Checkbox("Custom profile for this crosshair", &m_cfg.crosshair.customProfile);
    if(m_cfg.crosshair.customProfile) ImGui::TextColored(kGood, "Per-crosshair overrides active");
    else ImGui::TextColored(kMuted, "Using global defaults");
    ImGui::Spacing();
    ImGui::Text("Color mask");
    ImGui::SameLine();
    ImVec4 maskCol(m_cfg.crosshair.colorMaskR/255.0f, m_cfg.crosshair.colorMaskG/255.0f, m_cfg.crosshair.colorMaskB/255.0f, m_cfg.crosshair.colorMaskA/255.0f);
    ImGui::ColorButton("##maskPreviewO", maskCol, 0, ImVec2(22,22));
    bool maskChanged=false;
    ImGui::PushItemWidth(45); if(ImGui::InputInt("##r_ov_i", &m_cfg.crosshair.colorMaskR)){ m_cfg.crosshair.Clamp(); maskChanged=true; } ImGui::PopItemWidth(); ImGui::SameLine(); ImGui::PushItemWidth(70); maskChanged|=ImGui::SliderInt("R##ov", &m_cfg.crosshair.colorMaskR, 0,255); ImGui::PopItemWidth(); ImGui::SameLine();
    ImGui::PushItemWidth(45); if(ImGui::InputInt("##g_ov_i", &m_cfg.crosshair.colorMaskG)){ m_cfg.crosshair.Clamp(); maskChanged=true; } ImGui::PopItemWidth(); ImGui::SameLine(); ImGui::PushItemWidth(70); maskChanged|=ImGui::SliderInt("G##ov", &m_cfg.crosshair.colorMaskG, 0,255); ImGui::PopItemWidth();
    ImGui::PushItemWidth(45); if(ImGui::InputInt("##b_ov_i", &m_cfg.crosshair.colorMaskB)){ m_cfg.crosshair.Clamp(); maskChanged=true; } ImGui::PopItemWidth(); ImGui::SameLine(); ImGui::PushItemWidth(70); maskChanged|=ImGui::SliderInt("B##ov", &m_cfg.crosshair.colorMaskB, 0,255); ImGui::PopItemWidth(); ImGui::SameLine();
    ImGui::PushItemWidth(45); if(ImGui::InputInt("##a_ov_i", &m_cfg.crosshair.colorMaskA)){ m_cfg.crosshair.Clamp(); maskChanged=true; } ImGui::PopItemWidth(); ImGui::SameLine(); ImGui::PushItemWidth(70); maskChanged|=ImGui::SliderInt("A##ov", &m_cfg.crosshair.colorMaskA, 0,255); ImGui::PopItemWidth();
    if(maskChanged){ m_cfg.crosshair.Clamp(); m_previewDirty=true; if(m_overlay) m_overlay->SetConfig(m_cfg); }
    ImGui::Spacing();
    ImGui::Text("Image scale, percents");
    ImGui::PushItemWidth(70);
    if(ImGui::InputInt("##imgScaleO", &m_cfg.crosshair.imageScale, 0,0)) { m_cfg.crosshair.Clamp(); m_previewDirty=true; if(m_overlay) m_overlay->SetConfig(m_cfg); }
    ImGui::PopItemWidth();
    ImGui::SameLine(); if(ImGui::Button("-##scaleO")) { m_cfg.crosshair.imageScale=std::max(10,m_cfg.crosshair.imageScale-10); m_previewDirty=true; if(m_overlay) m_overlay->SetConfig(m_cfg); }
    ImGui::SameLine(); if(ImGui::Button("+##scaleO")) { m_cfg.crosshair.imageScale=std::min(500,m_cfg.crosshair.imageScale+10); m_previewDirty=true; if(m_overlay) m_overlay->SetConfig(m_cfg); }
    ImGui::SameLine(); if(ImGui::Button("Reset##scaleO")) { m_cfg.crosshair.imageScale=100; m_previewDirty=true; if(m_overlay) m_overlay->SetConfig(m_cfg); }
    m_cfg.crosshair.pngScale = m_cfg.crosshair.imageScale/100.0f;
    ImGui::Spacing();
    ImGui::Text("Offset from a screen center, X/Y");
    ImGui::PushItemWidth(70);
    if(ImGui::InputInt("##offXO", &m_cfg.crosshair.offsetX,0,0)) { m_cfg.crosshair.Clamp(); if(m_overlay) m_overlay->SetConfig(m_cfg); }
    ImGui::PopItemWidth(); ImGui::SameLine(); if(ImGui::Button("-##offXO")){m_cfg.crosshair.offsetX--; if(m_overlay) m_overlay->SetConfig(m_cfg);} ImGui::SameLine(); if(ImGui::Button("+##offXO")){m_cfg.crosshair.offsetX++; if(m_overlay) m_overlay->SetConfig(m_cfg);}
    ImGui::SameLine(); ImGui::PushItemWidth(70);
    if(ImGui::InputInt("##offYO", &m_cfg.crosshair.offsetY,0,0)) { m_cfg.crosshair.Clamp(); if(m_overlay) m_overlay->SetConfig(m_cfg); }
    ImGui::PopItemWidth(); ImGui::SameLine(); if(ImGui::Button("-##offYO")){m_cfg.crosshair.offsetY--; if(m_overlay) m_overlay->SetConfig(m_cfg);} ImGui::SameLine(); if(ImGui::Button("+##offYO")){m_cfg.crosshair.offsetY++; if(m_overlay) m_overlay->SetConfig(m_cfg);}
    ImGui::SameLine(); if(ImGui::Button("Reset##offO")){m_cfg.crosshair.offsetX=0; m_cfg.crosshair.offsetY=0; if(m_overlay) m_overlay->SetConfig(m_cfg);}
    ImGui::Separator();
    // Visibility
    ImGui::Text("Visibility hotkey");
    {
        std::wstring vks = AppConfig::VkToString(m_cfg.visibilityVk);
        std::string vka; if(!vks.empty()){int n=WideCharToMultiByte(CP_UTF8,0,vks.c_str(),-1,nullptr,0,nullptr,nullptr); vka.resize(n-1); WideCharToMultiByte(CP_UTF8,0,vks.c_str(),-1,vka.data(),n,nullptr,nullptr);} else vka="Not bound";
        bool binding = IsBindingVisibility;
        ImVec4 btnCol = binding ? kAccent : ImVec4(0.25f,0.30f,0.55f,1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, btnCol);
        std::string visLabel = std::string(binding?"Press key/mouse...":vka.c_str()) + "##vis";
        if(ImGui::Button(visLabel.c_str(), ImVec2(-1,24))) IsBindingVisibility=!IsBindingVisibility;
        ImGui::PopStyleColor();
        if(IsBindingVisibility){ int cap=0; if(PollHotkeyCapture(cap)){ m_cfg.visibilityVk=cap; IsBindingVisibility=false; } if(ImGui::IsKeyPressed(ImGuiKey_Escape)) IsBindingVisibility=false; }
        if(ImGui::Button("Clear Visibility Hotkey", ImVec2(-1,22))){
            m_cfg.visibilityVk=0; IsBindingVisibility=false; ConfigManager::Save(m_cfg);
        }
    }
    ImGui::Text("Visibility mode");
    int vm = (int)m_cfg.visibilityMode;
    if(ImGui::RadioButton("Press to toggle visibility##ov", vm==0)) { vm=0; m_cfg.visibilityMode=(VisibilityMode)vm; }
    if(ImGui::RadioButton("Hold for visibility##ov", vm==1)) { vm=1; m_cfg.visibilityMode=(VisibilityMode)vm; }
    if(m_cfg.visibilityMode==VisibilityMode::Hold){ ImGui::Indent(20); ImGui::Checkbox("Inversed Hold mode##ov", &m_cfg.inversedHold); ImGui::Unindent(20); }
    ImGui::Separator();
    ImGui::Checkbox("Enable crosshair lean##ov", &m_cfg.enableLean);
    if(m_cfg.enableLean){
        ImGui::Text("Hotkeys for lean left and right");
        {
            float avail = ImGui::GetContentRegionAvail().x;
            float btnW = (avail - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            std::wstring s = AppConfig::VkToString(m_cfg.leanLeftVk);
            std::string a; if(!s.empty()){int n=WideCharToMultiByte(CP_UTF8,0,s.c_str(),-1,nullptr,0,nullptr,nullptr); a.resize(n-1); WideCharToMultiByte(CP_UTF8,0,s.c_str(),-1,a.data(),n,nullptr,nullptr);} else a="Not bound";
            bool b = IsBindingLeanLeft;
            ImGui::PushStyleColor(ImGuiCol_Button, b?kAccent:ImVec4(0.25f,0.30f,0.55f,1.0f));
            std::string llLabel = b ? "Press...##llO" : std::string(a) + "##llO";
            if(ImGui::Button(llLabel.c_str(), ImVec2(btnW,24))) IsBindingLeanLeft=!IsBindingLeanLeft;
            ImGui::PopStyleColor(); ImGui::SameLine();
            std::wstring s2 = AppConfig::VkToString(m_cfg.leanRightVk);
            std::string a2; if(!s2.empty()){int n=WideCharToMultiByte(CP_UTF8,0,s2.c_str(),-1,nullptr,0,nullptr,nullptr); a2.resize(n-1); WideCharToMultiByte(CP_UTF8,0,s2.c_str(),-1,a2.data(),n,nullptr,nullptr);} else a2="Not bound";
            bool b2 = IsBindingLeanRight;
            ImGui::PushStyleColor(ImGuiCol_Button, b2?kAccent:ImVec4(0.25f,0.30f,0.55f,1.0f));
            std::string lrLabel = b2 ? "Press...##lrO" : std::string(a2) + "##lrO";
            if(ImGui::Button(lrLabel.c_str(), ImVec2(btnW,24))) IsBindingLeanRight=!IsBindingLeanRight;
            ImGui::PopStyleColor();
            if(b){int cap=0; if(PollHotkeyCapture(cap)){m_cfg.leanLeftVk=cap; IsBindingLeanLeft=false;}}
            if(b2){int cap=0; if(PollHotkeyCapture(cap)){m_cfg.leanRightVk=cap; IsBindingLeanRight=false;}}
            ImGui::Spacing();
            if(ImGui::Button("Clear Lean Hotkeys", ImVec2(-1,22))){
                m_cfg.leanLeftVk=0; m_cfg.leanRightVk=0; IsBindingLeanLeft=false; IsBindingLeanRight=false; ConfigManager::Save(m_cfg);
            }
        }
        ImGui::Text("Angle, degrees");
        ImGui::PushItemWidth(70);
        ImGui::InputInt("##leanAngO", &m_cfg.leanAngle,0,0);
        ImGui::PopItemWidth(); ImGui::SameLine(); if(ImGui::Button("-##leanO")) m_cfg.leanAngle=std::max(0,m_cfg.leanAngle-5); ImGui::SameLine(); if(ImGui::Button("+##leanO")) m_cfg.leanAngle=std::min(90,m_cfg.leanAngle+5);
        m_cfg.leanAngle=std::clamp(m_cfg.leanAngle,0,90);
    }
    ImGui::Separator();
    ImGui::Text("Switch to crosshair hotkey");
    {
        std::wstring s = AppConfig::VkToString(m_cfg.switchVk);
        std::string a; if(!s.empty()){int n=WideCharToMultiByte(CP_UTF8,0,s.c_str(),-1,nullptr,0,nullptr,nullptr); a.resize(n-1); WideCharToMultiByte(CP_UTF8,0,s.c_str(),-1,a.data(),n,nullptr,nullptr);} else a="Not bound";
        bool b = IsBindingSwitch;
        ImGui::PushStyleColor(ImGuiCol_Button, b?kAccent:ImVec4(0.25f,0.30f,0.55f,1.0f));
        std::string swBtnLabel = std::string(b?"Press...##swO":a.c_str()) + "##switchO";
        if(ImGui::Button(swBtnLabel.c_str(), ImVec2(-1,24))) IsBindingSwitch=!IsBindingSwitch;
        ImGui::PopStyleColor();
        if(b){int cap=0; if(PollHotkeyCapture(cap)){m_cfg.switchVk=cap; IsBindingSwitch=false;}}
        if(ImGui::Button("Clear##switchO", ImVec2(-1,22))) m_cfg.switchVk=0;
    }
    ImGui::Spacing();
    ImGui::Spacing();
    CardEnd();
}

void ConfigWindow::RenderDesigner()
{
    SectionTitle("DESIGNER", "Crosshair Studio", "Vector templates - then open pixel editor for per-pixel.");
    CardBegin("designerOuter", ImVec2(0, 0), ImGuiWindowFlags_None);
    const float availOuterD = ImGui::GetContentRegionAvail().x;
    const float leftW = (availOuterD - 14.0f) * 0.52f;
    CardBegin("typeCard", ImVec2(leftW, 0));
    ImGui::TextColored(kMuted, "TYPE & COLORS");
    ImGui::Spacing();
    const char* types[]={"Cross","Dot","Circle","Cross + Dot","T-Shape","Plus","X-Shape","Square","Diamond"};
    // Map old Custom PNG (was idx 5) to Plus to preserve selection; Custom PNG now only via PNG Studio checkbox
    int curType = (int)m_cfg.crosshair.type;
    if (curType == (int)CrosshairType::CustomPng) curType = (int)CrosshairType::Plus; // hide Custom PNG from dropdown
    // need mapping for Plus+ indices shifted
    auto ToComboIdx = [](int t){ return t > 5 ? t - 1 : t; };
    auto FromComboIdx = [](int c){ return c >= 5 ? c + 1 : c; };
    int combo = ToComboIdx(curType);
    if (ImGui::Combo("Crosshair type", &combo, types, IM_ARRAYSIZE(types))){
        curType = FromComboIdx(combo);
        m_cfg.crosshair.type=(CrosshairType)curType; m_previewDirty=true; if(m_overlay)m_overlay->SetConfig(m_cfg);
    }
    ImGui::Spacing();
    ColorPickerWithPreview("Crosshair color", m_cfg.crosshair.color);
    ColorPickerWithPreview("Outline color", m_cfg.crosshair.outlineColor);
    ImGui::Checkbox("Outline", &m_cfg.crosshair.outlineEnabled);
    ImGui::SameLine(); ImGui::Checkbox("Center dot", &m_cfg.crosshair.centerDot);
    if (m_cfg.crosshair.outlineEnabled) { ImGui::PushItemWidth(50); if(ImGui::InputInt("##ot_input", &m_cfg.crosshair.outlineThickness)){ m_cfg.crosshair.Clamp(); m_previewDirty=true; } ImGui::PopItemWidth(); ImGui::SameLine(); ImGui::PushItemWidth(110); if(ImGui::SliderInt("Outline thick.", &m_cfg.crosshair.outlineThickness, 1,6)) m_previewDirty=true; ImGui::PopItemWidth(); }
    if (ImGui::SliderInt("Opacity", &m_cfg.crosshair.opacity, 0,255)) m_previewDirty=true;
    ImGui::Separator();
    ImGui::TextColored(kMuted, "GEOMETRY");
    bool geomChanged=false;
    ImGui::PushItemWidth(50); if(ImGui::InputInt("##size_input", &m_cfg.crosshair.size)){ m_cfg.crosshair.Clamp(); m_previewDirty=true; geomChanged=true; } ImGui::PopItemWidth(); ImGui::SameLine(); ImGui::PushItemWidth(110); geomChanged|=ImGui::SliderInt("Size / Length", &m_cfg.crosshair.size, 1,100); ImGui::PopItemWidth();
    ImGui::PushItemWidth(50); if(ImGui::InputInt("##thick_input", &m_cfg.crosshair.thickness)){ m_cfg.crosshair.Clamp(); m_previewDirty=true; geomChanged=true; } ImGui::PopItemWidth(); ImGui::SameLine(); ImGui::PushItemWidth(110); geomChanged|=ImGui::SliderInt("Thickness", &m_cfg.crosshair.thickness, 1,20); ImGui::PopItemWidth();
    ImGui::PushItemWidth(50); if(ImGui::InputInt("##gap_input", &m_cfg.crosshair.gap)){ m_cfg.crosshair.Clamp(); m_previewDirty=true; geomChanged=true; } ImGui::PopItemWidth(); ImGui::SameLine(); ImGui::PushItemWidth(110); geomChanged|=ImGui::SliderInt("Gap", &m_cfg.crosshair.gap, 0,40); ImGui::PopItemWidth();
    ImGui::PushItemWidth(50); if(ImGui::InputInt("##dot_input", &m_cfg.crosshair.dotSize)){ m_cfg.crosshair.Clamp(); m_previewDirty=true; geomChanged=true; } ImGui::PopItemWidth(); ImGui::SameLine(); ImGui::PushItemWidth(110); geomChanged|=ImGui::SliderInt("Dot size", &m_cfg.crosshair.dotSize, 1,20); ImGui::PopItemWidth();
    ImGui::PushItemWidth(50); if(ImGui::InputInt("##cr_input", &m_cfg.crosshair.circleRadius)){ m_cfg.crosshair.Clamp(); m_previewDirty=true; geomChanged=true; } ImGui::PopItemWidth(); ImGui::SameLine(); ImGui::PushItemWidth(110); geomChanged|=ImGui::SliderInt("Circle radius", &m_cfg.crosshair.circleRadius, 2,80); ImGui::PopItemWidth();
    ImGui::PushItemWidth(50); if(ImGui::InputInt("##ct_input", &m_cfg.crosshair.circleThickness)){ m_cfg.crosshair.Clamp(); m_previewDirty=true; geomChanged=true; } ImGui::PopItemWidth(); ImGui::SameLine(); ImGui::PushItemWidth(110); geomChanged|=ImGui::SliderInt("Circle thick.", &m_cfg.crosshair.circleThickness, 1,10); ImGui::PopItemWidth();
    ImGui::PushItemWidth(50); if(ImGui::InputInt("##offx_input", &m_cfg.crosshair.offsetX)){ m_cfg.crosshair.Clamp(); m_previewDirty=true; geomChanged=true; } ImGui::PopItemWidth(); ImGui::SameLine(); ImGui::PushItemWidth(110); geomChanged|=ImGui::SliderInt("Offset X", &m_cfg.crosshair.offsetX, -100,100); ImGui::PopItemWidth();
    ImGui::PushItemWidth(50); if(ImGui::InputInt("##offy_input", &m_cfg.crosshair.offsetY)){ m_cfg.crosshair.Clamp(); m_previewDirty=true; geomChanged=true; } ImGui::PopItemWidth(); ImGui::SameLine(); ImGui::PushItemWidth(110); geomChanged|=ImGui::SliderInt("Offset Y", &m_cfg.crosshair.offsetY, -100,100); ImGui::PopItemWidth();
    ImGui::PushItemWidth(50); if(ImGui::InputInt("##rot_input", &m_cfg.crosshair.rotation)){ m_cfg.crosshair.Clamp(); m_previewDirty=true; geomChanged=true; } ImGui::PopItemWidth(); ImGui::SameLine(); ImGui::PushItemWidth(110); geomChanged|=ImGui::SliderInt("Rotation", &m_cfg.crosshair.rotation, 0,360); ImGui::PopItemWidth();
    ImGui::PushItemWidth(50); if(ImGui::InputInt("##sq_input", &m_cfg.crosshair.squareSize)){ m_cfg.crosshair.Clamp(); m_previewDirty=true; geomChanged=true; } ImGui::PopItemWidth(); ImGui::SameLine(); ImGui::PushItemWidth(110); geomChanged|=ImGui::SliderInt("Square size", &m_cfg.crosshair.squareSize, 4,80); ImGui::PopItemWidth();
    ImGui::PushItemWidth(50); if(ImGui::InputInt("##pt_input", &m_cfg.crosshair.plusThickness)){ m_cfg.crosshair.Clamp(); m_previewDirty=true; geomChanged=true; } ImGui::PopItemWidth(); ImGui::SameLine(); ImGui::PushItemWidth(110); geomChanged|=ImGui::SliderInt("Plus thick.", &m_cfg.crosshair.plusThickness, 1,20); ImGui::PopItemWidth();
    if (geomChanged){ m_cfg.crosshair.Clamp(); m_previewDirty=true; if(m_overlay) m_overlay->SetConfig(m_cfg); }
    ImGui::Spacing();
    ImGui::Spacing();
    if(ImGui::Button("PIXEL EDITOR  >>", ImVec2(-1,36))){
        m_editor.open=true;
        // init from current png if exists else blank
        if(!m_cfg.crosshair.pngPath.empty()) EditorLoadFromPng(m_cfg.crosshair.pngPath);
        else if(m_editor.pixels.empty()){ m_editor.imgSize=64; m_editor.pixels.assign(4096,0); }
    }
    ImGui::TextColored(kMuted, "Open per-pixel editor (left paint / right erase)");
    ImGui::Spacing();
    CardEnd();
    ImGui::SameLine();
    CardBegin("previewDesign", ImVec2(0,400));
    ImGui::TextColored(kMuted, "PREVIEW");
    ImGui::Spacing();
    UpdatePreviewTexture();
    if (m_previewSRV){
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Image((ImTextureID)m_previewSRV, ImVec2((float)m_previewW, (float)m_previewH));
        ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x+m_previewW, p.y+m_previewH), IM_COL32(90,92,110,120), 8.0f, 0, 1.0f);
    }
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("RESET DEFAULTS", ImVec2(-1,32))){
        m_cfg.crosshair = CrosshairConfig{};
        m_cfg.crosshair.Clamp();
        m_previewDirty=true;
        SetConfig(m_cfg);
        if(m_overlay) m_overlay->SetConfig(m_cfg);
    }
    ImGui::Spacing();
    CardEnd();
    CardEnd(); // designerOuter
}

void ConfigWindow::RenderLibrary()
{
    static bool libInitLb=false;
    if(!libInitLb){ RefreshLibrary(); libInitLb=true; }

    SectionTitle("LIBRARY", "Crosshair Library - 530+ Sights", "Search, filter, favorite *. Click to apply. Right-click to star.");
    CardBegin("libOuter", ImVec2(0,0));

    // Top bar: search + favorites + open folder + refresh + count
    float availW = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(availW * 0.42f);
    ImGui::InputTextWithHint("##libFilter", "Search name...", m_libraryFilter, sizeof(m_libraryFilter));
    ImGui::SameLine();
    ImGui::Checkbox("Favorites only", &m_showOnlyFavorites);
    ImGui::SameLine();
    if(ImGui::SmallButton("Refresh")) RefreshLibrary();
    ImGui::SameLine();
    if(ImGui::SmallButton("Open Folder")){
        std::wstring dir = GetLibraryDir();
        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "%zu total", m_libraryFiles.size());

    // Category chips
    const char* cats[] = {"All","Dot","Cross","X","Square","Chevron","T","Classic"};
    const char* catKeys[] = {"","dot","cross","x","square","chevron","t","classic"};
    ImGui::Spacing();
    for(int i=0;i<IM_ARRAYSIZE(cats);++i){
        bool sel = (m_libraryCategory==i);
        if(sel) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f,0.52f,0.38f,1.0f)); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.00f,0.60f,0.46f,1.0f)); }
        else { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f,0.14f,0.16f,1.0f)); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f,0.20f,0.22f,1.0f)); }
        if(ImGui::SmallButton(cats[i])) m_libraryCategory = i;
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
    }
    ImGui::SameLine();
    // SVG tint for black Kenney pack
    {
        ImGui::TextColored(kMuted, " SVG:");
        float colF[3] = { GetRValue(m_cfg.svgTint)/255.0f, GetGValue(m_cfg.svgTint)/255.0f, GetBValue(m_cfg.svgTint)/255.0f };
        ImGui::SameLine();
        ImGui::PushItemWidth(110);
        if(ImGui::ColorEdit3("##svgTint", colF, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)){
            m_cfg.svgTint = RGB((int)(colF[0]*255),(int)(colF[1]*255),(int)(colF[2]*255));
            // invalidate SVG thumbnails so recolor appears
            for(auto srv: m_librarySRVs) if(srv) srv->Release();
            for(auto tex: m_libraryTexs) if(tex) tex->Release();
            m_librarySRVs.assign(m_libraryFiles.size(), nullptr);
            m_libraryTexs.assign(m_libraryFiles.size(), nullptr);
            ConfigManager::Save(m_cfg);
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ImGui::Checkbox("Recolor black", &m_cfg.svgRecolorBlack);
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Kenney SVGs are black - recolor to this color. Refresh thumbnails on change.");
        if(ImGui::IsItemDeactivatedAfterEdit()){
            for(auto srv: m_librarySRVs) if(srv) srv->Release();
            for(auto tex: m_libraryTexs) if(tex) tex->Release();
            m_librarySRVs.assign(m_libraryFiles.size(), nullptr);
            m_libraryTexs.assign(m_libraryFiles.size(), nullptr);
            ConfigManager::Save(m_cfg);
        }
    }
    ImGui::NewLine();
    ImGui::Separator();
    ImGui::Spacing();

    // Build filtered indices
    std::vector<int> filt;
    filt.reserve(m_libraryFiles.size());
    std::string filtLower = m_libraryFilter;
    std::transform(filtLower.begin(), filtLower.end(), filtLower.begin(), ::tolower);
    std::string catKey = (m_libraryCategory>0 && m_libraryCategory < IM_ARRAYSIZE(catKeys)) ? catKeys[m_libraryCategory] : "";
    std::transform(catKey.begin(), catKey.end(), catKey.begin(), ::tolower);
    for(size_t i=0;i<m_libraryFiles.size();++i){
        if(m_showOnlyFavorites && !IsFavorite(m_libraryFiles[i])) continue;
        std::string nl = m_libraryNames[i];
        std::string nlow = nl; std::transform(nlow.begin(), nlow.end(), nlow.begin(), ::tolower);
        if(!filtLower.empty() && nlow.find(filtLower)==std::string::npos) continue;
        if(!catKey.empty() && nlow.find(catKey)==std::string::npos) continue;
        filt.push_back((int)i);
    }

    if(filt.empty()){
        ImGui::TextColored(kMuted, "No matches. Try different search or category.");
        if(m_libraryFiles.empty()){
            ImGui::TextColored(kMuted, "No PNGs found. Put 530 pack in Documents\\DopesCrosshairTool\\custom-crosshairs\\");
            if(ImGui::Button("Get 530 Pack (open folder)", ImVec2(-1,28))){
                ShellExecuteW(nullptr, L"open", GetLibraryDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
        }
        CardEnd();
        return;
    }

    ImGui::TextColored(kMuted, "%zu shown", filt.size());
    ImGui::Spacing();

    // Grid with virtualization - clean aligned grid, no name below thumb
    const float thumbSize = 64.0f;
    const float pad = 12.0f;
    const float cellW = thumbSize + pad*1.6f;
    const float cellH = thumbSize + pad*1.2f; // tight, name removed → in-line
    float gridAvail = ImGui::GetContentRegionAvail().x;
    int cols = std::max(1, (int)(gridAvail / cellW));
    if(cols > 8) cols = 8;
    if(cols < 3) cols = std::max(1, cols);
    int rows = (int)((filt.size() + cols - 1) / cols);

    // Single scroll - grid fills panel, no outer double bar
    float gridH = ImGui::GetContentRegionAvail().y - 68.0f; // leave Refresh/Open buttons visible
    if(gridH < 220) gridH = 220;
    ImGui::BeginChild("libGrid", ImVec2(0, gridH), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    ImGuiListClipper clipper;
    clipper.Begin(rows);
    while(clipper.Step()){
        for(int r=clipper.DisplayStart; r<clipper.DisplayEnd; ++r){
            int base = r * cols;
            int colsThisRow = std::min(cols, (int)filt.size() - base);
            for(int c=0;c<colsThisRow;++c){
                int fIdx = filt[base + c];
                if(c>0) ImGui::SameLine();
                ImGui::BeginGroup();
                ID3D11ShaderResourceView* srv = GetThumbnailFor(m_libraryFiles[fIdx], (int)thumbSize);
                bool selected = (m_cfg.crosshair.usePng && m_cfg.crosshair.pngPath == m_libraryFiles[fIdx]);
                ImVec2 p0 = ImGui::GetCursorScreenPos();
                ImVec2 p1 = ImVec2(p0.x + thumbSize, p0.y + thumbSize);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImU32 bg = selected ? IM_COL32(0,82,62,255) : IM_COL32(34,34,38,255);
                dl->AddRectFilled(p0, p1, bg, 4.0f);
                if(selected) dl->AddRect(p0, p1, IM_COL32(0,210,148,255), 4.0f, 0, 2.0f);
                else dl->AddRect(p0, p1, IM_COL32(70,72,84,90), 4.0f, 0, 1.0f);
                if(srv){
                    ImGui::SetCursorScreenPos(ImVec2(p0.x+2, p0.y+2));
                    ImGui::Image((ImTextureID)srv, ImVec2(thumbSize-4, thumbSize-4));
                } else {
                    ImGui::SetCursorScreenPos(p0); ImGui::Dummy(ImVec2(thumbSize, thumbSize));
                }
                ImGui::SetCursorScreenPos(p0);
                std::string bid = "##lib"+std::to_string(fIdx);
                if(ImGui::InvisibleButton(bid.c_str(), ImVec2(thumbSize, thumbSize))){
                    m_cfg.crosshair.pngPath = m_libraryFiles[fIdx];
                    m_cfg.crosshair.usePng = true;
                    m_previewDirty=true;
                    if(m_overlay) m_overlay->SetConfig(m_cfg);
                    ConfigManager::Save(m_cfg);
                    m_selectedLibrary = fIdx;
                }
                bool hovered = ImGui::IsItemHovered();
                if(hovered){
                    if(ImGui::IsMouseClicked(ImGuiMouseButton_Right)) ToggleFavorite(m_libraryFiles[fIdx]);
                    if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)){
                        m_cfg.crosshair.pngPath = m_libraryFiles[fIdx];
                        m_cfg.crosshair.usePng = true;
                        m_previewDirty=true;
                        if(m_overlay) m_overlay->SetConfig(m_cfg);
                        ConfigManager::Save(m_cfg);
                    }
                }
                // Favorite star (top-right) - grid stays in-line, no name below
                if(IsFavorite(m_libraryFiles[fIdx])){
                    dl->AddText(ImVec2(p1.x - 12, p0.y + 2), IM_COL32(255,220,100,255), "*");
                }
                if(selected){
                    dl->AddCircleFilled(ImVec2(p1.x - 8, p1.y - 8), 5.0f, IM_COL32(0,210,148,255));
                }
                ImGui::EndGroup();
                // Hover popup after 1 sec - cleaner, name shown only on hover
                if(hovered){
                    if(m_hoveredIdx != fIdx){
                        m_hoveredIdx = fIdx;
                        m_hoverStartTime = ImGui::GetTime();
                    }
                    if(ImGui::GetTime() - m_hoverStartTime > 1.0){
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted(m_libraryNames[fIdx].c_str());
                        ImGui::TextColored(kMuted, "Left-click: Apply  |  Right-click: Favorite");
                        ImGui::EndTooltip();
                    }
                } else if(m_hoveredIdx==fIdx){
                    // reset when not hovered
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if(ImGui::Button("Refresh Library", ImVec2(160,28))) RefreshLibrary();
    ImGui::SameLine();
    if(ImGui::Button("Open Library Folder", ImVec2(180,28))){
        ShellExecuteW(nullptr, L"open", GetLibraryDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "530 pack → drop PNGs into this folder");
    CardEnd();
}

void ConfigWindow::RenderPngStudio()
{
    SectionTitle("PNG STUDIO", "Custom Image", "Import any PNG/BMP/JPG - drag & drop also.");
    CardBegin("pngOuter", ImVec2(0, 0), ImGuiWindowFlags_None);
    const float avail = ImGui::GetContentRegionAvail().x;
    const float leftW = (avail - 14.0f) * 0.45f;
    CardBegin("pngCard", ImVec2(leftW, 0));
    ImGui::TextColored(kMuted, "IMAGE SOURCE");
    ImGui::Spacing();
    bool usePng = m_cfg.crosshair.usePng;
    if (ImGui::Checkbox("Use PNG instead of vector", &usePng)){
        m_cfg.crosshair.usePng=usePng; m_previewDirty=true; if(m_overlay) m_overlay->SetConfig(m_cfg);
    }
    ImGui::Spacing();
    std::string pathA; if(!m_cfg.crosshair.pngPath.empty()){ int n=WideCharToMultiByte(CP_UTF8,0,m_cfg.crosshair.pngPath.c_str(),-1,nullptr,0,nullptr,nullptr); pathA.resize(n-1); WideCharToMultiByte(CP_UTF8,0,m_cfg.crosshair.pngPath.c_str(),-1,pathA.data(),n,nullptr,nullptr);} else pathA="";
    char buf[MAX_PATH]={0}; strncpy_s(buf, pathA.c_str(), sizeof(buf)-1);
    ImGui::TextColored(kMuted, "PNG path");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##pngPath", "(no file - drag & drop or Browse)", buf, sizeof(buf), ImGuiInputTextFlags_ReadOnly);
    if (pathA.empty()) ImGui::TextColored(ImVec4(0.65f,0.65f,0.68f,1.0f), "(no file)");
    if (ImGui::Button("BROWSE PNG", ImVec2(160,32))) BrowsePng();
    ImGui::SameLine();
    if (ImGui::Button("CLEAR", ImVec2(80,32))){
        m_cfg.crosshair.pngPath.clear(); m_cfg.crosshair.usePng=false; m_previewDirty=true; if(m_overlay) m_overlay->SetConfig(m_cfg);
    }
    ImGui::Spacing();
    ImGui::PushItemWidth(50); float pngScaleTmp = m_cfg.crosshair.pngScale; if(ImGui::InputFloat("##pngs_input", &pngScaleTmp, 0.1f, 0.5f, "%.1f")){ m_cfg.crosshair.pngScale = std::clamp(pngScaleTmp, 0.1f, 5.0f); m_previewDirty=true; if(m_overlay) m_overlay->SetConfig(m_cfg); } ImGui::PopItemWidth(); ImGui::SameLine(); ImGui::PushItemWidth(120); if (ImGui::SliderFloat("PNG Scale", &m_cfg.crosshair.pngScale, 0.1f, 5.0f, "%.1fx")){ m_cfg.crosshair.Clamp(); m_previewDirty=true; if(m_overlay) m_overlay->SetConfig(m_cfg); } ImGui::PopItemWidth();
    ImGui::PushItemWidth(50); if(ImGui::InputInt("##pngo_input", &m_cfg.crosshair.pngOpacity)){ m_cfg.crosshair.Clamp(); m_previewDirty=true; if(m_overlay) m_overlay->SetConfig(m_cfg); } ImGui::PopItemWidth(); ImGui::SameLine(); ImGui::PushItemWidth(120); if (ImGui::SliderInt("PNG Opacity", &m_cfg.crosshair.pngOpacity, 0,255)){ m_cfg.crosshair.Clamp(); m_previewDirty=true; if(m_overlay) m_overlay->SetConfig(m_cfg); } ImGui::PopItemWidth();
    ImGui::Spacing();
    CardEnd();
    ImGui::SameLine();
    CardBegin("pngPreview", ImVec2(0,360));
    ImGui::TextColored(kMuted, "PREVIEW");
    ImGui::Spacing();
    UpdatePreviewTexture();
    if (m_previewSRV){
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Image((ImTextureID)m_previewSRV, ImVec2((float)m_previewW, (float)m_previewH));
        ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x+m_previewW, p.y+m_previewH), IM_COL32(90,92,110,120), 8.0f, 0, 1.0f);
    }
    CardEnd();
    CardEnd(); // pngOuter
}

void ConfigWindow::RenderAllowList()
{
    SectionTitle("ALLOW LIST", "Target Programs", "Real Overlay shows only when foreground is allow-listed.");
    CardBegin("allowOuter", ImVec2(0, 0), ImGuiWindowFlags_None);
    const float avail = ImGui::GetContentRegionAvail().x;
    const float leftW = (avail - 14.0f) * 0.46f;
    CardBegin("allowListCard", ImVec2(leftW, 0));
    ImGui::TextColored(kMuted, "ALLOW-LISTED EXES");
    ImGui::Spacing();
    if (ImGui::BeginListBox("##allowbox", ImVec2(-1, 220))){
        for(int i=0;i<(int)m_cfg.allowList.size();++i){
            auto& e = m_cfg.allowList[i];
            std::string s; int n=WideCharToMultiByte(CP_UTF8,0,e.exeName.c_str(),-1,nullptr,0,nullptr,nullptr); s.resize(n-1); WideCharToMultiByte(CP_UTF8,0,e.exeName.c_str(),-1,s.data(),n,nullptr,nullptr);
            std::string label = s + (e.enabled?"":" (disabled)");
            bool sel = (m_selectedAllow==i);
            if (ImGui::Selectable(label.c_str(), sel)) m_selectedAllow=i;
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) m_cfg.allowList[i].enabled=!m_cfg.allowList[i].enabled;
        }
        ImGui::EndListBox();
    }
    ImGui::Spacing();
    ImGui::Spacing();
    if (ImGui::Button("REMOVE SELECTED", ImVec2(-1,30))){
        if (m_selectedAllow>=0 && m_selectedAllow < (int)m_cfg.allowList.size()){
            m_cfg.allowList.erase(m_cfg.allowList.begin()+m_selectedAllow);
            m_selectedAllow=-1;
            ConfigManager::Save(m_cfg);
            if(m_overlay) m_overlay->SetConfig(m_cfg);
        }
    }
    ImGui::Spacing();
    ImGui::TextColored(kMuted, "Double-click to toggle enabled.");
    ImGui::Spacing();
    CardEnd();
    ImGui::SameLine();
    CardBegin("addCard", ImVec2(0,420));
    ImGui::TextColored(kMuted, "ADD PROGRAM");
    ImGui::Spacing();
    std::vector<std::string> procA; procA.reserve(m_processes.size());
    for(auto& w: m_processes){ std::string s; int n=WideCharToMultiByte(CP_UTF8,0,w.c_str(),-1,nullptr,0,nullptr,nullptr); s.resize(n-1); WideCharToMultiByte(CP_UTF8,0,w.c_str(),-1,s.data(),n,nullptr,nullptr); procA.push_back(s); }
    std::vector<const char*> cstr; for(auto& s: procA) cstr.push_back(s.c_str());
    if (!cstr.empty()){
        if (ImGui::Combo("Running", &m_selectedProcess, cstr.data(), (int)cstr.size())){
            strncpy_s(m_addExeBuf, procA[m_selectedProcess].c_str(), sizeof(m_addExeBuf)-1);
        }
    } else ImGui::TextColored(kMuted, "(no processes)");
    if (ImGui::Button("REFRESH", ImVec2(-1,24))) RefreshProcessList();
    ImGui::Spacing();
    ImGui::PushItemWidth(180);
    ImGui::InputText("Exe name", m_addExeBuf, sizeof(m_addExeBuf), ImGuiInputTextFlags_ReadOnly);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("BROWSE...", ImVec2(90,0))) {
        wchar_t file[MAX_PATH]=L"";
        OPENFILENAMEW ofn{};
        ofn.lStructSize=sizeof(ofn);
        ofn.lpstrFilter=L"Executable Files (*.exe)\0*.exe\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile=file;
        ofn.nMaxFile=MAX_PATH;
        ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
        ofn.lpstrDefExt=L"exe";
        if (GetOpenFileNameW(&ofn)) {
            std::wstring full(file);
            std::wstring exe = utils::ExeNameFromPath(full);
            if (!exe.empty()) {
                std::string a = utils::WToUtf8(exe);
                strncpy_s(m_addExeBuf, a.c_str(), sizeof(m_addExeBuf)-1);
            }
        }
    }
    ImGui::TextColored(kMuted, "Browse for exe or pick from Running");
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();
    if (ImGui::Button("ADD TO LIST", ImVec2(-1,32))){
        std::wstring w = utils::Utf8ToW(std::string(m_addExeBuf));
        if (w.empty() && strlen(m_addExeBuf)>0){ int n=MultiByteToWideChar(CP_UTF8,0,m_addExeBuf,-1,nullptr,0); w.resize(n-1); MultiByteToWideChar(CP_UTF8,0,m_addExeBuf,-1,w.data(),n); }
        w = utils::Trim(w); w = utils::ToLower(w);
        if (!w.empty()){
            if (w.find(L".exe")==std::wstring::npos) w+=L".exe";
            bool exists=false; for(auto& e: m_cfg.allowList) if(utils::ToLower(e.exeName)==w) exists=true;
            if(!exists){ m_cfg.allowList.push_back({w,true}); ConfigManager::Save(m_cfg); if(m_overlay) m_overlay->SetConfig(m_cfg); m_addExeBuf[0]=0; }
        }
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(kMuted, "Foreground:");
    {
        std::wstring fg = ProcessTracker::GetForegroundExeName();
        std::string s; if(!fg.empty()){int n=WideCharToMultiByte(CP_UTF8,0,fg.c_str(),-1,nullptr,0,nullptr,nullptr); s.resize(n-1); WideCharToMultiByte(CP_UTF8,0,fg.c_str(),-1,s.data(),n,nullptr,nullptr);} else s="(none)";
        ImGui::TextUnformatted(s.c_str());
    }
    CardEnd();
    CardEnd(); // allowOuter
}

void ConfigWindow::RenderPixelEditor(){
    if(!m_editor.open) return;
    // Pop-out editor: separate OS window via viewports, rounded PCB frame like main, no traces in canvas
    ImGui::SetNextWindowPos(ImVec2(60,60), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(1000, 760), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.02f, 0.03f, 1.0f));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar;
    bool keepOpen = m_editor.open;
    if(!ImGui::Begin("PixelEditorPCB", &keepOpen, flags)){
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        m_editor.open = keepOpen;
        return;
    }
    // Draw PCB frame + traces (outside canvas)
    ImVec2 winPos = ImGui::GetWindowPos();
    ImVec2 winSize = ImGui::GetWindowSize();
    ImVec4 bgBlend = ImVec4(0.05f, 0.05f, 0.07f, 0.96f);
    ImU32 bgColor = ImGui::GetColorU32(bgBlend);
    ImU32 borderColor = ImGui::GetColorU32(kAccent);
    // Padded frame inset 6px from OS window edge - removes black triangle at outside corners
    ImVec2 pad(8,8);
    DrawPcbWindowFrame(ImVec2(winPos.x+pad.x, winPos.y+pad.y), ImVec2(winSize.x-pad.x*2, winSize.y-pad.y*2), borderColor, bgColor);
    ImDrawList* frameDl = ImGui::GetWindowDrawList();
    // Top-right trace only - bottom-left removed per request
    ImVec2 frameTR(winPos.x + pad.x + (winSize.x - pad.x*2), winPos.y + pad.y);
    DrawCornerPcbNetwork(frameDl, frameTR, m_pcbGlowPos);
    // Header with title and moved-down X (avoid top-right trace)
    ImDrawList* headerDl = frameDl;
    ImGui::SetCursorPosY(12);
    ImGui::TextColored(kGood, "  Pixel Editor");
    // Custom X moved down to y+52 (was 30) to not overlay top-right traces
    ImVec2 xCenter(winPos.x + winSize.x - 38.0f, winPos.y + 52.0f);
    bool xHovered = ImGui::IsMouseHoveringRect(ImVec2(xCenter.x-14,xCenter.y-14), ImVec2(xCenter.x+14,xCenter.y+14));
    headerDl->AddCircleFilled(xCenter, 13.0f, xHovered ? IM_COL32(125,42,52,255) : IM_COL32(45,45,55,255));
    headerDl->AddCircle(xCenter, 13.0f, IM_COL32(95,95,110,255), 0, 1.0f);
    ImVec2 xSz = ImGui::CalcTextSize("X");
    headerDl->AddText(ImVec2(xCenter.x - xSz.x*0.5f, xCenter.y - xSz.y*0.5f), IM_COL32(225,225,232,255), "X");
    ImGui::SetCursorScreenPos(ImVec2(xCenter.x-13, xCenter.y-13));
    if(ImGui::InvisibleButton("##editorCloseX", ImVec2(26,26))) keepOpen = false;
    m_editor.open = keepOpen;
    if(!keepOpen){ ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar(2); return; }
    ImGui::Separator();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
    // Toolbar
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f,0.20f,0.28f,1.0f));
    if(ImGui::Button("New")) EditorNew();
    ImGui::SameLine();
    if(ImGui::Button("Undo")) EditorUndo();
    ImGui::SameLine();
    if(ImGui::Button("Redo")) EditorRedo();
    ImGui::SameLine();
    ImGui::PushItemWidth(160);
    ImGui::InputTextWithHint("##saveName", "Name", m_editor.saveName, sizeof(m_editor.saveName));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if(ImGui::Button("Save PNG")) EditorSave();
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "Left paint / Right erase / Middle pipette");
    ImGui::PopStyleColor();
    ImGui::Separator();
    // Controls row: Image size, Zoom, Pen color
    int sz = m_editor.imgSize;
    ImGui::Text("Image size");
    ImGui::SameLine();
    if(ImGui::Button("-##sz")) EditorResize(sz-16);
    ImGui::SameLine();
    ImGui::Text("%d", sz);
    ImGui::SameLine();
    if(ImGui::Button("+##sz")) EditorResize(sz+16);
    ImGui::SameLine(); ImGui::Spacing(); ImGui::SameLine();
    ImGui::Text("Zoom");
    ImGui::SameLine();
    if(ImGui::Button("-##zm")) m_editor.zoom = std::clamp(m_editor.zoom - 0.5f, 1.0f, 16.0f);
    ImGui::SameLine();
    ImGui::Text("%d%%", int(m_editor.zoom*100));
    ImGui::SameLine();
    if(ImGui::Button("+##zm")) m_editor.zoom = std::clamp(m_editor.zoom + 0.5f, 1.0f, 16.0f);
    ImGui::SameLine(); ImGui::Spacing(); ImGui::SameLine();
    ImGui::Text("Pen");
    ImGui::SameLine();
    ImGui::Checkbox("Use RGBA pen", &m_editor.useRGBA);
    // RGBA sliders
    if(m_editor.useRGBA){
        ImGui::PushItemWidth(50); if(ImGui::InputInt("##er_input", &m_editor.penR)){ m_editor.penR=std::clamp(m_editor.penR,0,255); } ImGui::PopItemWidth(); ImGui::SameLine(); ImGui::PushItemWidth(80); ImGui::SliderInt("R", &m_editor.penR, 0,255); ImGui::PopItemWidth(); ImGui::SameLine();
        ImGui::PushItemWidth(50); if(ImGui::InputInt("##eg_input", &m_editor.penG)){ m_editor.penG=std::clamp(m_editor.penG,0,255); } ImGui::PopItemWidth(); ImGui::SameLine(); ImGui::PushItemWidth(80); ImGui::SliderInt("G", &m_editor.penG, 0,255); ImGui::PopItemWidth(); ImGui::SameLine();
        ImGui::PushItemWidth(50); if(ImGui::InputInt("##eb_input", &m_editor.penB)){ m_editor.penB=std::clamp(m_editor.penB,0,255); } ImGui::PopItemWidth(); ImGui::SameLine(); ImGui::PushItemWidth(80); ImGui::SliderInt("B", &m_editor.penB, 0,255); ImGui::PopItemWidth(); ImGui::SameLine();
        ImGui::PushItemWidth(50); if(ImGui::InputInt("##ea_input", &m_editor.penA)){ m_editor.penA=std::clamp(m_editor.penA,0,255); } ImGui::PopItemWidth(); ImGui::SameLine(); ImGui::PushItemWidth(80); ImGui::SliderInt("A", &m_editor.penA, 0,255); ImGui::PopItemWidth();
        ImGui::SameLine();
        ImVec4 col(m_editor.penR/255.0f, m_editor.penG/255.0f, m_editor.penB/255.0f, 1.0f);
        ImGui::ColorButton("##penSwatch", col, ImGuiColorEditFlags_NoPicker, ImVec2(24,24));
    }
    ImGui::Spacing(); ImGui::Separator();
    // Value under cursor
    {
        ImGui::Text("Value under cursor");
        ImGui::SameLine();
        // checker for transparent
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(p, ImVec2(p.x+16,p.y+16), IM_COL32(90,90,90,255));
        dl->AddRectFilled(p, ImVec2(p.x+8,p.y+8), IM_COL32(60,60,60,255));
        dl->AddRectFilled(ImVec2(p.x+8,p.y+8), ImVec2(p.x+16,p.y+16), IM_COL32(60,60,60,255));
        // draw hover color if not transparent
        if(m_editor.hoverX>=0){
            uint32_t v=m_editor.hoverValue;
            int a=(v>>24)&0xFF, r=(v>>16)&0xFF, g=(v>>8)&0xFF, b=v&0xFF;
            if(a>0) dl->AddRectFilled(p, ImVec2(p.x+16,p.y+16), IM_COL32(r,g,b,255));
            ImGui::Dummy(ImVec2(16,16));
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "%d ", r); ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.3f,1,0.3f,1), "%d ", g); ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.3f,0.5f,1,1), "%d ", b); ImGui::SameLine();
            ImGui::Text("%d", a);
        } else { ImGui::Dummy(ImVec2(16,16)); ImGui::SameLine(); ImGui::Text(" -"); }
    }
    ImGui::Separator();
    // Grid in its own scrollable panel - padded rounded border, scroll moves grid not background
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.10f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10,10));
    // Fill remaining window space, leave 12px bottom padding for resize corner
    ImVec2 avail = ImGui::GetContentRegionAvail();
    // Reserve 8px bottom for resize grip visibility
    ImVec2 panelSize = ImVec2(avail.x, avail.y - 4);
    if(panelSize.y < 100) panelSize.y = 100;
    ImGui::BeginChild("gridPanel", panelSize, ImGuiChildFlags_Borders, ImGuiWindowFlags_AlwaysHorizontalScrollbar | ImGuiWindowFlags_AlwaysVerticalScrollbar);
    // Canvas
    float cell = m_editor.zoom * 4.0f; // base cell 4px at 100%
    if(cell<4) cell=4;
    float canvasW = sz * cell;
    float canvasH = sz * cell;
    // Ensure canvas at least fills panel for proper scroll
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("canvas", ImVec2(canvasW, canvasH));
    bool isHovered = ImGui::IsItemHovered();
    bool isActive = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Checker bg
    dl->AddRectFilled(canvasPos, ImVec2(canvasPos.x+canvasW, canvasPos.y+canvasH), IM_COL32(40,40,40,255));
    for(int y=0;y<sz;++y) for(int x=0;x<sz;++x){
        bool checker = ((x+y)&1)==0;
        ImVec2 p0(canvasPos.x + x*cell, canvasPos.y + y*cell);
        ImVec2 p1(p0.x+cell, p0.y+cell);
        uint32_t v=m_editor.pixels[y*sz+x];
        int a=(v>>24)&0xFF;
        if(a==0){
            // transparent checker
            ImU32 c = checker? IM_COL32(60,60,60,255): IM_COL32(50,50,50,255);
            dl->AddRectFilled(p0,p1,c);
        } else {
            int r=(v>>16)&0xFF, g=(v>>8)&0xFF, b=v&0xFF;
            dl->AddRectFilled(p0,p1, IM_COL32(r,g,b, a));
            // if checker needed behind transparent? already
        }
    }
    // Grid lines
    for(int i=0;i<=sz;++i){
        float px = canvasPos.x + i*cell;
        float py = canvasPos.y + i*cell;
        ImU32 col = (i==sz/2)? IM_COL32(100,220,150,180) : IM_COL32(80,80,80,60);
        float thick = (i==sz/2)?1.5f:0.5f;
        dl->AddLine(ImVec2(px, canvasPos.y), ImVec2(px, canvasPos.y+canvasH), col, thick);
        dl->AddLine(ImVec2(canvasPos.x, py), ImVec2(canvasPos.x+canvasW, py), col, thick);
    }
    // Border
    dl->AddRect(canvasPos, ImVec2(canvasPos.x+canvasW, canvasPos.y+canvasH), IM_COL32(90,92,110,255), 0,0,1.0f);
    // Hover
    if(isHovered){
        ImVec2 mouse = ImGui::GetMousePos();
        int hx = int((mouse.x - canvasPos.x)/cell);
        int hy = int((mouse.y - canvasPos.y)/cell);
        if(hx>=0 && hx<sz && hy>=0 && hy<sz){
            m_editor.hoverX=hx; m_editor.hoverY=hy;
            m_editor.hoverValue=m_editor.pixels[hy*sz+hx];
            ImVec2 p0(canvasPos.x+hx*cell, canvasPos.y+hy*cell);
            dl->AddRect(p0, ImVec2(p0.x+cell,p0.y+cell), IM_COL32(199,97,140,255),0,0,2.0f);
        } else { m_editor.hoverX=-1; }
        // Painting
        bool leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        bool rightDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
        bool middleDown = ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
        if(middleDown && hx>=0){
            uint32_t v=m_editor.pixels[hy*sz+hx];
            m_editor.penR=(v>>16)&0xFF; m_editor.penG=(v>>8)&0xFF; m_editor.penB=v&0xFF; m_editor.penA=(v>>24)&0xFF;
        }
        if(leftDown || rightDown){
            if(!m_editor.dragging){
                EditorPushUndo();
                m_editor.dragging=true;
                m_editor.erasing=rightDown;
            }
            if(hx>=0 && hy>=0 && hx<sz && hy<sz){
                uint32_t col = 0;
                if(!m_editor.erasing){
                    int a=m_editor.penA, r=m_editor.penR, g=m_editor.penG, b=m_editor.penB;
                    col = (a<<24)|(r<<16)|(g<<8)|b;
                } else col=0;
                m_editor.pixels[hy*sz+hx]=col;
            }
        } else {
            if(m_editor.dragging){ m_editor.dragging=false; }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void ConfigWindow::RenderSettings()
{
    SectionTitle("SETTINGS", "Overlay + Persistence", "Moved Overlay Status here, per-crosshair now on Overview.");

    const float avail = ImGui::GetContentRegionAvail().x;
    const float leftW = (avail - 14.0f) * 0.50f;

    CardBegin("settingsCombined", ImVec2(0, 0));
    ImGui::TextColored(kMuted, "OVERLAY MODE");
    ImGui::Spacing();
    int mode = (int)m_cfg.overlayMode;
    if(ImGui::RadioButton("Standard (virtual screen)", mode==0)) { mode=0; m_cfg.overlayMode=(OverlayMode)mode; if(m_overlay) m_overlay->SetConfig(m_cfg); }
    if(ImGui::RadioButton("Real Overlay (elevated, monitor)", mode==1)) { mode=1; m_cfg.overlayMode=(OverlayMode)mode; if(m_overlay) m_overlay->SetConfig(m_cfg); }
    if(m_cfg.overlayMode==OverlayMode::RealOverlay){
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f,0.85f,0.45f,1.0f));
        ImGui::TextWrapped("More precisely, there is one small dependency - you need to run DopesCrosshairTool with administrator (elevated) privileges for Real Overlay mode to work correctly. DopesCrosshairTool doesn't inject its code (via DLLs) into the game process, so this will be completely invisible to the game and its anti-cheat. The overlay is drawn on top of everything (even full-screen games), like the Xbox Game Bar widgets, almost at the driver level.");
        ImGui::PopStyleColor();
        ImGui::Spacing();
        // Build sorted monitor list matching Windows Settings numbers (DISPLAY1 = Display 1)
        std::vector<std::pair<int, RECT>> sortedMons;
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
        }, (LPARAM)&sortedMons);
        std::sort(sortedMons.begin(), sortedMons.end(), [](auto& a, auto& b){return a.first < b.first;});
        int primaryNum=1;
        for(DWORD i=0; ; ++i){ DISPLAY_DEVICEW dd{}; dd.cb=sizeof(dd); if(!EnumDisplayDevicesW(nullptr,i,&dd,0)) break; if(dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE){ const wchar_t* p=wcsstr(dd.DeviceName, L"DISPLAY"); if(p) primaryNum=_wtoi(p+7); break; } }
        std::vector<std::string> monNames; monNames.push_back("Virtual screen (all)");
        for(size_t i=0;i<sortedMons.size();++i){
            int num=sortedMons[i].first;
            RECT rc=sortedMons[i].second;
            bool isPrimary=(num==primaryNum);
            std::string label="Display "+std::to_string(num)+(isPrimary?" (Primary)":"")+" - "+std::to_string(rc.right-rc.left)+"x"+std::to_string(rc.bottom-rc.top)+" at ("+std::to_string(rc.left)+","+std::to_string(rc.top)+")";
            monNames.push_back(label);
        }
        std::vector<const char*> cstr; for(auto& s: monNames) cstr.push_back(s.c_str());
        int sel = m_cfg.monitorIndex +1;
        sel = std::clamp(sel, 0, (int)cstr.size()-1);
        if(ImGui::Combo("Active display", &sel, cstr.data(), (int)cstr.size())){
            m_cfg.monitorIndex = sel -1;
            ConfigManager::Save(m_cfg);
            if(m_overlay) m_overlay->SetConfig(m_cfg);
        }
        ImGui::SameLine();
        if(ImGui::SmallButton("Test Flash (2s)")){ FlashActiveDisplay(); }
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Flashes lime border on selected monitor for 2 seconds to identify it");
        // Show selected monitor resolution for confirmation (sorted Windows numbers)
        {
            RECT trc{0,0,0,0}; bool ok=false;
            if(m_cfg.monitorIndex==-1){
                trc.left=GetSystemMetrics(SM_XVIRTUALSCREEN); trc.top=GetSystemMetrics(SM_YVIRTUALSCREEN);
                trc.right=trc.left+GetSystemMetrics(SM_CXVIRTUALSCREEN); trc.bottom=trc.top+GetSystemMetrics(SM_CYVIRTUALSCREEN); ok=true;
            } else if(m_cfg.monitorIndex>=0 && m_cfg.monitorIndex < (int)sortedMons.size()){
                trc = sortedMons[m_cfg.monitorIndex].second; ok=true;
            }
            if(ok) ImGui::TextColored(kMuted, "Selected: %dx%d at (%d,%d)", trc.right-trc.left, trc.bottom-trc.top, trc.left, trc.top);
        }
        bool admin = m_cfg.IsElevated();
        ImGui::Spacing();
        if(admin){
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f,0.32f,0.18f,0.92f));
            ImGui::BeginChild("adminYes", ImVec2(-1, 52), true);
            ImGui::TextColored(ImVec4(0.75f,1.0f,0.75f,1.0f), "  [OK] Running as Administrator [ADMIN]");
            ImGui::TextColored(ImVec4(0.85f,0.95f,0.85f,1.0f), "  Elevated = Admin. Real Overlay WILL show over exclusive Fullscreen.");
            ImGui::TextColored(kMuted, "  External overlay - no DLL injection, at driver level.");
            ImGui::EndChild();
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.32f,0.12f,0.12f,0.92f));
            ImGui::BeginChild("adminNo", ImVec2(-1, 78), true);
            ImGui::TextColored(ImVec4(1.0f,0.55f,0.55f,1.0f), "  [X] Not elevated - Real Overlay will NOT show over exclusive Fullscreen");
            ImGui::TextColored(ImVec4(1.0f,0.85f,0.55f,1.0f), "  Elevated = Administrator privileges. Click below or right-click exe → Run as admin.");
            if(ImGui::Button("Restart as Admin  [Shield]", ImVec2(-1,26))){
                wchar_t exe[MAX_PATH]{}; GetModuleFileNameW(nullptr,exe,MAX_PATH);
                if(g_hMutex){ ReleaseMutex(g_hMutex); CloseHandle(g_hMutex); g_hMutex=nullptr; }
                HINSTANCE res = ShellExecuteW(nullptr, L"runas", exe, nullptr, nullptr, SW_SHOWNORMAL);
                if((INT_PTR)res > 32) PostQuitMessage(0); else g_hMutex = CreateMutexW(nullptr, TRUE, L"DopesCrosshairTool_SingleInstance");
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
    }
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(kMuted, "PERSISTENCE");
    ImGui::Spacing();
    if(ImGui::Button("SAVE CONFIG", ImVec2(-1,28))) ConfigManager::Save(m_cfg);
    if(ImGui::Button("LOAD CONFIG", ImVec2(-1,28))){
        AppConfig loaded; if(ConfigManager::Load(loaded)){ SetConfig(loaded); if(m_overlay) m_overlay->SetConfig(m_cfg); }
    }
    if(ImGui::Button("RESET DEFAULTS", ImVec2(-1,28))){
        m_cfg.SetDefaults(); SetConfig(m_cfg); if(m_overlay) m_overlay->SetConfig(m_cfg); ConfigManager::Save(m_cfg);
    }
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(kMuted, "SYSTEM");
    ImGui::Spacing();
    if(ImGui::Checkbox("Run in system tray (minimize to tray)", &m_cfg.minimizeToTray)) ConfigManager::Save(m_cfg);
    if(ImGui::Checkbox("Start minimized", &m_cfg.startMinimized)) ConfigManager::Save(m_cfg);
    if(ImGui::Checkbox("Auto start with Windows", &m_cfg.autoStart)){
        ConfigManager::Save(m_cfg);
        wchar_t exePath[MAX_PATH]{}; GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        HKEY hKey;
        if(RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey)==ERROR_SUCCESS){
            if(m_cfg.autoStart){
                std::wstring toSet = std::wstring(exePath) + L" --minimized";
                RegSetValueExW(hKey, L"DopesCrosshairTool", 0, REG_SZ, (const BYTE*)toSet.c_str(), (DWORD)((toSet.size()+1)*sizeof(wchar_t)));
            } else {
                RegDeleteValueW(hKey, L"DopesCrosshairTool");
            }
            RegCloseKey(hKey);
        }
    }
    if(ImGui::Checkbox("Start with high privileges (admin)", &m_cfg.runAsAdmin)) ConfigManager::Save(m_cfg);
    ImGui::TextColored(kMuted, "Requires UAC / task with highest privileges");
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(kMuted, "AUDIO");
    ImGui::Spacing();
    if(ImGui::Checkbox("Play sound on F8 toggle (on/off)", &m_cfg.playToggleSounds)) ConfigManager::Save(m_cfg);
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(kMuted, "THEME - ACCENT COLORS");
    ImGui::Spacing();
    auto colorEdit = [&](const char* label, COLORREF& col){
        float cf[3] = { GetRValue(col)/255.0f, GetGValue(col)/255.0f, GetBValue(col)/255.0f };
        if(ImGui::ColorEdit3(label, cf, ImGuiColorEditFlags_NoInputs)){
            col = RGB((int)(cf[0]*255),(int)(cf[1]*255),(int)(cf[2]*255));
            ConfigManager::Save(m_cfg);
            ApplyTheme();
        }
    };
    colorEdit("Accent Lime (border pulse A)", m_cfg.themeAccentLime);
    colorEdit("Accent Teal (border pulse B)", m_cfg.themeAccentTeal);
    colorEdit("PCB Traces", m_cfg.themePcbTrace);
    colorEdit("PCB Dots (glow)", m_cfg.themePcbDot);
    colorEdit("Border (inner)", m_cfg.themeBorder);
    colorEdit("Button", m_cfg.themeButton);
    colorEdit("Button Hover", m_cfg.themeButtonHover);
    colorEdit("Sidebar Selected", m_cfg.themeSidebar);
    colorEdit("Sidebar Hover", m_cfg.themeSidebarHover);
    colorEdit("Sidebar Active", m_cfg.themeSidebarActive);
    colorEdit("Title Glow", m_cfg.titleGlow);
    if(ImGui::SliderInt("Glow Alpha", &m_cfg.titleGlowAlpha, 0, 255)){ ConfigManager::Save(m_cfg); }
    if(ImGui::Button("Reset Theme", ImVec2(-1,28))){
        m_cfg.themeAccentLime = RGB(140,255,74);
        m_cfg.themeAccentTeal = RGB(0,210,160);
        m_cfg.themePcbTrace = RGB(0,210,148);
        m_cfg.themePcbDot = RGB(0,255,180);
        m_cfg.themeBorder = RGB(0,98,72);
        m_cfg.themeButton = RGB(36,36,40);
        m_cfg.themeButtonHover = RGB(0,82,62);
        m_cfg.themeSidebar = RGB(0,66,50);
        m_cfg.themeSidebarHover = RGB(0,53,38);
        m_cfg.themeSidebarActive = RGB(0,90,66);
        m_cfg.titleGlow = RGB(0,255,128);
        m_cfg.titleGlowAlpha = 85;
        ConfigManager::Save(m_cfg);
        ApplyTheme();
    }
    ImGui::TextColored(kMuted, "Changes apply instantly. Restart not needed.");
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(kMuted, "UPDATES");
    ImGui::Text("Current version: %s", kVersion);
    ImGui::Checkbox("Auto-check on launch", &m_cfg.autoCheckUpdate);
    if(ImGui::IsItemDeactivatedAfterEdit()) ConfigManager::Save(m_cfg);
    ImGui::SameLine();
    if(ImGui::Button(m_checkingUpdate ? "Checking..." : "Check now", ImVec2(110,22))){
        TriggerUpdateCheck(true);
    }
    if(!m_updateError.empty()){
        bool isLatest = m_updateError.find("latest")!=std::string::npos;
        ImGui::TextColored(isLatest?kGood:kDanger, "%s", m_updateError.c_str());
    }
    ImGui::TextColored(kMuted, "Updates from: github.com/Dopemodz420/DopesCrosshairTool");
    ImGui::TextColored(kMuted, "Click Check now -> Update to grab the latest release.");
    CardEnd();

    // Update popup
    RenderUpdatePopup();
    // Also need to handle binding polling outside (already in button)
    if(IsBindingVisibility || IsBindingLeanLeft || IsBindingLeanRight || IsBindingSwitch){
        // If ESC pressed cancel
        if(ImGui::IsKeyPressed(ImGuiKey_Escape)){
            IsBindingVisibility=IsBindingLeanLeft=IsBindingLeanRight=IsBindingSwitch=false;
        }
    }
}
void ConfigWindow::TriggerUpdateCheck(bool manual){
    if(m_checkingUpdate) return;
    m_checkingUpdate=true;
    m_updateError.clear();
    std::wstring feed=DefaultUpdateFeedUrl();
    CheckForUpdateAsync(feed,[this,manual](bool has, RemoteVersion rv, std::string err){
        m_checkingUpdate=false;
        m_updateError=err;
        if(has){
            if(!manual && rv.version==m_cfg.skippedVersion) return;
            m_pendingUpdate=rv;
            m_showUpdatePopup=true;
        } else {
            if(manual){
                m_pendingUpdate=rv;
                if(!err.empty()) m_updateError=err;
                else if(!has) m_updateError="You are on the latest version ("+std::string(kVersion)+")";
            }
        }
    });
}
void ConfigWindow::RenderUpdatePopup(){
    if(m_testFlashHwnd && GetTickCount() > m_testFlashUntil){ DestroyWindow(m_testFlashHwnd); m_testFlashHwnd=nullptr; }
    if(!m_showUpdatePopup) return;
    ImGui::OpenPopup("Update available");
    if(ImGui::BeginPopupModal("Update available", &m_showUpdatePopup, ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::Text("A new version is available!");
        ImGui::Spacing();
        ImGui::Text("Current: %s", kVersion);
        ImGui::Text("Latest:  %s", m_pendingUpdate.version.c_str());
        if(!m_pendingUpdate.notes.empty()){
            ImGui::Separator();
            ImGui::TextWrapped("%s", m_pendingUpdate.notes.c_str());
        }
        if(!m_pendingUpdate.url.empty()){
            ImGui::TextWrapped("Get it: %s", m_pendingUpdate.url.c_str());
        }
        ImGui::Spacing();
        if(ImGui::Button("Update", ImVec2(180,28))){
            std::string dl = m_pendingUpdate.url;
            if(dl.empty()) dl = "https://github.com/Dopemodz420/DopesCrosshairTool/releases";
            ShellExecuteA(nullptr,"open",dl.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
            m_showUpdatePopup=false;
        }
        ImGui::SameLine();
        if(ImGui::Button("Skip this version", ImVec2(140,28))){
            m_cfg.skippedVersion=m_pendingUpdate.version;
            ConfigManager::Save(m_cfg);
            m_showUpdatePopup=false;
        }
        ImGui::SameLine();
        if(ImGui::Button("Later", ImVec2(80,28))){ m_showUpdatePopup=false; }
        ImGui::EndPopup();
    }
}
void ConfigWindow::FlashActiveDisplay(){
    if(m_testFlashHwnd && IsWindow(m_testFlashHwnd)){ DestroyWindow(m_testFlashHwnd); m_testFlashHwnd=nullptr; }
    RECT rc{0,0,0,0}; bool ok=false;
    if(m_cfg.monitorIndex==-1){
        rc.left=GetSystemMetrics(SM_XVIRTUALSCREEN); rc.top=GetSystemMetrics(SM_YVIRTUALSCREEN);
        rc.right=rc.left+GetSystemMetrics(SM_CXVIRTUALSCREEN); rc.bottom=rc.top+GetSystemMetrics(SM_CYVIRTUALSCREEN); ok=true;
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
        if(m_cfg.monitorIndex>=0 && m_cfg.monitorIndex < (int)sms.size()){
            rc = sms[m_cfg.monitorIndex].second; ok=true;
        } else {
            HMONITOR hm=MonitorFromPoint(POINT{0,0}, MONITOR_DEFAULTTOPRIMARY); MONITORINFO mi{}; mi.cbSize=sizeof(mi); if(GetMonitorInfoW(hm,&mi)){ rc=mi.rcMonitor; ok=true; }
        }
    }
    if(!ok) return;
    WNDCLASSEXW wc{}; wc.cbSize=sizeof(wc); wc.lpfnWndProc=FlashWndProc; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"DopesFlashTest"; wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassExW(&wc);
    int w=rc.right-rc.left, h=rc.bottom-rc.top;
    m_testFlashHwnd = CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE, wc.lpszClassName, L"Active Display Test", WS_POPUP, rc.left, rc.top, w, h, nullptr, nullptr, wc.hInstance, nullptr);
    if(!m_testFlashHwnd) return;
    ShowWindow(m_testFlashHwnd, SW_SHOWNA);
    UpdateWindow(m_testFlashHwnd);
    SetTimer(m_testFlashHwnd, 1, 2000, nullptr);
    m_testFlashUntil = GetTickCount() + 2000;
}
void ConfigWindow::EnsureTitleFont(){
    if(m_titleFontLoaded) return;
    m_titleFontLoaded = true;
    ImGuiIO& io = ImGui::GetIO();
    std::wstring exeDir = utils::GetExeDirectory();
    std::vector<std::wstring> tryFonts = {
        exeDir + L"assets\\fonts\\ROG.ttf",
        exeDir + L"assets\\fonts\\ROGFonts-Regular.ttf",
        exeDir + L"..\\assets\\fonts\\ROG.ttf",
        L"C:\\Windows\\Fonts\\Arial Black.ttf",
        L"C:\\Windows\\Fonts\\segoeuib.ttf",
        L"C:\\Windows\\Fonts\\SegoeUI-Bold.ttf"
    };
    for(auto& p: tryFonts){
        if(GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES){
            std::string narrow = utils::WToUtf8(p);
            m_titleFont = io.Fonts->AddFontFromFileTTF(narrow.c_str(), 23.0f);
            if(m_titleFont) break;
        }
    }
    if(!m_titleFont) m_titleFont = io.Fonts->Fonts.Size > 0 ? io.Fonts->Fonts[0] : nullptr;
}
void ConfigWindow::RenderInfo()
{
    // Auto-cleanup flash window after timeout (in case WM_TIMER didn't fire)
    if(m_testFlashHwnd && GetTickCount() > m_testFlashUntil){
        DestroyWindow(m_testFlashHwnd); m_testFlashHwnd=nullptr;
    }
    EnsureDMLogoTexture();
    SectionTitle("INFO", "Dopes Crosshair - Info", "Black / Gray / Green / Teal - Real Overlay - GeoCamo");
    const float avail = ImGui::GetContentRegionAvail().x;
    CardBegin("infoOuter", ImVec2(0, 0));
    // Top logo card
    if (m_dmLogoSRV) {
        float maxW = avail - 28.0f;
        float aspect = (m_dmLogoW > 0 && m_dmLogoH > 0) ? (float)m_dmLogoW / (float)m_dmLogoH : 1.83f;
        float logoH = 148.0f;
        float logoW = logoH * aspect;
        if (logoW > maxW) { logoW = maxW; logoH = logoW / aspect; }
        ImVec2 p = ImGui::GetCursorScreenPos();
        // dark card behind logo for readability on camo
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + logoW + 16, p.y + logoH + 16), IM_COL32(18, 18, 20, 255), 12.0f);
        dl->AddRect(p, ImVec2(p.x + logoW + 16, p.y + logoH + 16), IM_COL32(0, 160, 120, 255), 12.0f, 0, 1.2f);
        ImGui::SetCursorScreenPos(ImVec2(p.x + 8, p.y + 8));
        ImGui::Image((ImTextureID)m_dmLogoSRV, ImVec2(logoW, logoH));
        ImGui::Dummy(ImVec2(0, 14));
    } else {
        ImGui::TextColored(kAccent, "DM Logo (assets/DMlogo.png) not found");
        ImGui::Spacing();
    }
    ImGui::Separator();
    ImGui::Spacing();
    // Info text
    ImGui::TextColored(kAccent, "ABOUT  -  v1.0.2");
    ImGui::TextWrapped("DopesCrosshairTool v1.0.2 - external, non-injecting HUD overlay. Real Overlay mode draws above even exclusive fullscreen via DWM hardware overlay (like Xbox Game Bar), requiring Administrator - invisible to game / anti-cheat.");
    ImGui::Spacing();
    ImGui::TextColored(kGood, "THEME - Black / Gray / Green / Teal");
    ImGui::BulletText("Background: GeoCamoBlack.png tiled with animated Green/Teal marching outer border");
    ImGui::BulletText("Sidebar & controls: Black/Gray base, Green/Teal hover/active, Gray mute text");
    ImGui::BulletText("PCB traces & glows now Teal/Green (was Purple)");
    ImGui::Spacing();
    ImGui::TextColored(kMuted, "REAL OVERLAY - External Overlay PARITY");
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.85f, 0.45f, 1.0f));
    ImGui::TextWrapped("More precisely, there is one small dependency - you need to run DopesCrosshairTool with administrator (elevated) privileges for Real Overlay mode to work correctly. DopesCrosshairTool doesn't inject its code (via DLLs) into the game process, so this will be completely invisible to the game and its anti-cheat. The overlay is drawn on top of everything (even full-screen games), like the Xbox Game Bar widgets, almost at the driver level.");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::TextColored(kMuted, "VERSION & PATHS");
    {
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::string exeA; { int n=WideCharToMultiByte(CP_UTF8,0,exe,-1,nullptr,0,nullptr,nullptr); exeA.resize(n-1); WideCharToMultiByte(CP_UTF8,0,exe,-1,exeA.data(),n,nullptr,nullptr); }
        ImGui::TextWrapped("%s", exeA.c_str());
        ImGui::TextColored(kMuted, "Config: crosshair_config.json (exe dir) + %%APPDATA%%\\DopesCrosshairTool\\config.json");
        ImGui::TextColored(kMuted, "Library: Documents\\DopesCrosshairTool\\custom-crosshairs\\");
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (ImGui::Button("Open Config Folder", ImVec2(220, 32))) {
        std::wstring dir = utils::GetAppDataConfigPath();
        size_t p = dir.find_last_of(L"\\/");
        if (p != std::wstring::npos) dir = dir.substr(0, p);
        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::SameLine();
    if (ImGui::Button("Open Library Folder", ImVec2(220, 32))) {
        std::wstring dir = GetLibraryDir();
        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    CardEnd();
}

void ConfigWindow::RenderUI(HWND hwnd, bool& running)
{
    static bool themeApplied=false;
    if(!themeApplied){ ApplyTheme(); themeApplied=true; }
    static bool didAutoCheck=false;
    if(!didAutoCheck){ didAutoCheck=true; if(m_cfg.autoCheckUpdate) TriggerUpdateCheck(false); }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    constexpr float outerInset = 12.0f;
    const ImVec2 windowPos(viewport->WorkPos.x + outerInset, viewport->WorkPos.y + outerInset);
    const ImVec2 windowSize(viewport->WorkSize.x - outerInset*2.0f, viewport->WorkSize.y - outerInset*2.0f);
    ImGui::SetNextWindowPos(windowPos);
    ImGui::SetNextWindowSize(windowSize);
    BeginPcbWindow(nullptr, kAccent);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 winPos = ImGui::GetWindowPos();
    const ImVec2 winSize = ImGui::GetWindowSize();
    DrawCornerPcbNetwork(drawList, ImVec2(winPos.x + winSize.x, winPos.y), m_pcbGlowPos);
    DrawCornerPcbNetwork(drawList, ImVec2(winPos.x, winPos.y + winSize.y), m_pcbGlowPos, true, true);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8,4));
    {
        EnsureTitleFont();
        bool isAdmin = m_cfg.IsElevated();
        std::string title = std::string("DOPES CROSSHAIR - HUD Overlay v1.0.2") + (isAdmin ? " [ADMIN]" : "");
        ImFont* font = m_titleFont ? m_titleFont : ImGui::GetFont();
        float size = ImGui::GetFontSize() * 1.25f;
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* tdl = ImGui::GetWindowDrawList();
        ImU32 glow = IM_COL32(GetRValue(m_cfg.titleGlow), GetGValue(m_cfg.titleGlow), GetBValue(m_cfg.titleGlow), m_cfg.titleGlowAlpha);
        ImU32 col = isAdmin ? IM_COL32(170, 255, 170, 255) : IM_COL32(150, 255, 150, 255);
        for(int dy=-1; dy<=1; ++dy) for(int dx=-1; dx<=1; ++dx) if(dx||dy) tdl->AddText(font, size, ImVec2(pos.x+dx, pos.y+dy), glow, title.c_str());
        tdl->AddText(font, size, pos, col, title.c_str());
        ImGui::Dummy(ImVec2(ImGui::CalcTextSize(title.c_str()).x, size));
        if(ImGui::IsItemHovered() && isAdmin) ImGui::SetTooltip("Running as Administrator - Real Overlay will show over exclusive Fullscreen");
    }
    // Combined pill for minimize + close, separated by |
    {
        ImVec2 pillPos(winPos.x + winSize.x - 82.0f, winPos.y + 38.0f);
        ImVec2 pillSize(72.0f, 28.0f);
        ImU32 pillBg = IM_COL32(45,45,55,255);
        ImU32 pillBd = IM_COL32(95,95,110,255);
        drawList->AddRectFilled(pillPos, ImVec2(pillPos.x + pillSize.x, pillPos.y + pillSize.y), pillBg, 14.0f);
        drawList->AddRect(pillPos, ImVec2(pillPos.x + pillSize.x, pillPos.y + pillSize.y), pillBd, 14.0f, 0, 1.0f);
        // Vertical separator |
        float midX = pillPos.x + pillSize.x * 0.5f;
        drawList->AddLine(ImVec2(midX, pillPos.y + 6), ImVec2(midX, pillPos.y + pillSize.y - 6), IM_COL32(95,95,110,255), 1.0f);
        ImVec2 minCenter(pillPos.x + pillSize.x * 0.25f, pillPos.y + pillSize.y * 0.5f);
        ImVec2 closeCenter(pillPos.x + pillSize.x * 0.75f, pillPos.y + pillSize.y * 0.5f);
        bool minHovered = ImGui::IsMouseHoveringRect(ImVec2(minCenter.x-12, minCenter.y-12), ImVec2(minCenter.x+12, minCenter.y+12));
        bool closeHovered = ImGui::IsMouseHoveringRect(ImVec2(closeCenter.x-12, closeCenter.y-12), ImVec2(closeCenter.x+12, closeCenter.y+12));
        // Minimize circle hover
        if(minHovered) drawList->AddCircleFilled(minCenter, 11.0f, IM_COL32(60,80,90,255));
        if(closeHovered) drawList->AddCircleFilled(closeCenter, 11.0f, IM_COL32(125,42,52,255));
        // Minimize icon - horizontal line
        drawList->AddLine(ImVec2(minCenter.x - 5, minCenter.y), ImVec2(minCenter.x + 5, minCenter.y), IM_COL32(225,225,232,255), 2.0f);
        // Close X
        ImVec2 xSz = ImGui::CalcTextSize("X");
        drawList->AddText(ImVec2(closeCenter.x - xSz.x*0.5f, closeCenter.y - xSz.y*0.5f), IM_COL32(225,225,232,255), "X");
        // Hit boxes
        ImGui::SetCursorScreenPos(ImVec2(minCenter.x-12, minCenter.y-12));
        if(ImGui::InvisibleButton("##minimize", ImVec2(24,24))){
            // Minimize: delegate to WndProc WM_SIZE which respects minimizeToTray setting.
            // SW_MINIMIZE will hide to tray only if minimizeToTray is enabled, otherwise minimizes to taskbar.
            ShowWindow(hwnd, SW_MINIMIZE);
        }
        ImGui::SetCursorScreenPos(ImVec2(closeCenter.x-12, closeCenter.y-12));
        if(ImGui::InvisibleButton("##closeX", ImVec2(24,24))) running=false;
    }
    ImGui::PopStyleVar();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::SetCursorPosX(18.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10,14));
    ImGui::BeginChild("sidebar", ImVec2(kSidebarW, -72), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    const char* brand="DOPES HUD  v1.0.2";
    float bw = ImGui::CalcTextSize(brand).x;
    ImGui::SetCursorPosX((kSidebarW - bw)*0.5f);
    ImGui::TextColored(ImVec4(0.9f,0.9f,0.95f,1.0f), "%s", brand);
    ImGui::Spacing();
    const char* pages[]={"Overview","Library","Designer","PNG Studio","Allow List","Settings","Info"};
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8,9));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.82f,0.84f,0.90f,1.0f));
    for(int i=0;i<7;++i){
        bool sel = m_page==i;
        ImVec4 sb = ImVec4(GetRValue(m_cfg.themeSidebar)/255.0f, GetGValue(m_cfg.themeSidebar)/255.0f, GetBValue(m_cfg.themeSidebar)/255.0f, 1.0f);
        ImVec4 sbH = ImVec4(GetRValue(m_cfg.themeSidebarHover)/255.0f, GetGValue(m_cfg.themeSidebarHover)/255.0f, GetBValue(m_cfg.themeSidebarHover)/255.0f, 1.0f);
        ImVec4 sbA = ImVec4(GetRValue(m_cfg.themeSidebarActive)/255.0f, GetGValue(m_cfg.themeSidebarActive)/255.0f, GetBValue(m_cfg.themeSidebarActive)/255.0f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, sel ? sb : ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, sbH);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, sbA);
        if(ImGui::Button(pages[i], ImVec2(-1,32))) m_page=i;
        ImGui::PopStyleColor(3);
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 92.0f);
    ImGui::Separator();
    ImGui::SetWindowFontScale(0.85f);
    bool active = m_cfg.overlayEnabled;
    ImGui::TextColored(active?kGood:kMuted, "%s", active?"OVERLAY ON":"OVERLAY OFF");
    ImGui::TextColored(kMuted, "F8 toggle");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::SameLine();
    ImGui::BeginGroup();
    // Leave 90px bottom margin so PCB bottom-left is not covered - Library has its own inner scroll, so outer must NOT scroll (fixes double scrollbar)
    ImGuiWindowFlags contentFlags = (m_page == 4) ? ImGuiWindowFlags_AlwaysVerticalScrollbar : ImGuiWindowFlags_None;
    ImGui::BeginChild("content", ImVec2(0, -90), ImGuiChildFlags_None, contentFlags);
    switch(m_page){
        case 0: RenderDashboard(); break;
        case 1: RenderLibrary(); break;
        case 2: RenderDesigner(); break;
        case 3: RenderPngStudio(); break;
        case 4: RenderAllowList(); break;
        case 5: RenderSettings(); break;
        case 6: RenderInfo(); break;
        default: RenderDashboard(); break;
    }
    ImGui::EndChild();
    ImGui::EndGroup();
    const char* lampLabel = m_cfg.overlayEnabled ? "LIVE" : "PAUSED";
    ImU32 lampCol = ImGui::GetColorU32(m_cfg.overlayEnabled ? kGood : kDanger);
    ImVec2 labelSize = ImGui::CalcTextSize(lampLabel);
    ImVec2 labelPos(winPos.x + winSize.x - 36.0f - labelSize.x, winPos.y + winSize.y - 30.0f);
    drawList->AddText(labelPos, lampCol, lampLabel);
    ImVec2 lampCenter(labelPos.x + labelSize.x + 12.0f, labelPos.y + labelSize.y*0.5f);
    float pulse = 0.5f + 0.5f * sinf((float)ImGui::GetTime()*4.0f);
    float r = 4.0f + 1.5f * pulse;
    ImU32 halo = (lampCol & 0x00FFFFFFu) | ((ImU32)(80*pulse)<<24);
    drawList->AddCircleFilled(lampCenter, r+5.0f+3.0f*pulse, halo);
    drawList->AddCircleFilled(lampCenter, r, lampCol);
    drawList->AddCircle(lampCenter, r+2.5f+2.0f*pulse, lampCol, 0, 1.0f);
    static double lastSave=0;
    if (ImGui::GetTime() - lastSave > 2.0){
        lastSave = ImGui::GetTime();
        ConfigManager::Save(m_cfg);
        if(m_overlay) m_overlay->SetConfig(m_cfg);
        m_mainColorF[0]=GetRValue(m_cfg.crosshair.color)/255.0f;
        m_mainColorF[1]=GetGValue(m_cfg.crosshair.color)/255.0f;
        m_mainColorF[2]=GetBValue(m_cfg.crosshair.color)/255.0f;
        m_outlineColorF[0]=GetRValue(m_cfg.crosshair.outlineColor)/255.0f;
        m_outlineColorF[1]=GetGValue(m_cfg.crosshair.outlineColor)/255.0f;
        m_outlineColorF[2]=GetBValue(m_cfg.crosshair.outlineColor)/255.0f;
    }
    if (ImGui::IsAnyItemActive()){
        if(m_overlay) m_overlay->SetConfig(m_cfg);
    }
    EndPcbWindow();
    // Render editor on top
    RenderPixelEditor();
}

} // namespace dopes



