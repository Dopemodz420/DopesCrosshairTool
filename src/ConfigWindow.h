#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <string>
#include <vector>
#include <imgui.h>
#include "Crosshair.h"
#include "CrosshairRenderer.h"
#include "OverlayWindow.h"
#include "Updater.h"
#include "Version.h"

namespace dopes {

class ConfigWindow {
public:
    ConfigWindow(OverlayWindow* overlay, ID3D11Device* device, ID3D11DeviceContext* context);
    ~ConfigWindow();

    void SetConfig(const AppConfig& cfg);
    AppConfig GetConfig() const { return m_cfg; }
    AppConfig& GetConfigRef() { return m_cfg; }

    void UpdateAnimations(float dt);
    void RenderUI(HWND hwnd, bool& running);

    void HandleDropFiles(HDROP hDrop);

    // For main loop to sync
    void RefreshProcessList();

private:
    void RenderDashboard();
    void RenderLibrary();
    void RenderDesigner();
    void RenderPngStudio();
    void RenderAllowList();
    void RenderSettings();
    void RenderInfo();

    void DrawPcbWindowFrame(const ImVec2& pos, const ImVec2& size, ImU32 borderColor, ImU32 bgColor);
    void DrawCamoBackground(ImDrawList* dl, const ImVec2& pos, const ImVec2& size);
    void DrawAnimatedBorder(ImDrawList* dl, const ImVec2& pos, const ImVec2& size, float time);
    void BeginPcbWindow(bool* p_open, ImVec4 accentColor);
    void EndPcbWindow();
    void DrawCornerPcbNetwork(ImDrawList* dl, const ImVec2& corner, float progress, bool mirrorX=false, bool mirrorY=false);
    void DrawGlowHead(ImDrawList* dl, const ImVec2& pos, float coreR=3.0f, float haloR=7.0f);
    ImVec2 PolylinePoint(const ImVec2* pts, int count, float t);
    void DrawPolyline(ImDrawList* dl, const ImVec2* pts, int count, ImU32 col, float thick);

    void ApplyTheme();
    bool ColorPickerWithPreview(const char* label, COLORREF& color);
    void UpdatePreviewTexture();
    void EnsurePreviewTexture(int w, int h);
    void BrowsePng();

    // Helpers
    void SectionTitle(const char* eyebrow, const char* title, const char* body);

    OverlayWindow* m_overlay = nullptr;
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    AppConfig m_cfg;
    CrosshairRenderer m_renderer;

    // UI state
    int m_page = 0;
    float m_pcbGlowPos = 0.0f;
    float m_pcbGlowDir = 1.0f;
    float m_pcbGlowPosSlow = 0.0f;
    float m_letterProgress = 0.0f;

    // Process list
    std::vector<std::wstring> m_processes;
    int m_selectedProcess = 0;
    char m_addExeBuf[128] = {0};

    // Preview texture
    ID3D11ShaderResourceView* m_previewSRV = nullptr;
    ID3D11Texture2D* m_previewTex = nullptr;
    int m_previewW = 320;
    int m_previewH = 220;
    bool m_previewDirty = true;
    AppConfig m_lastPreviewCfg;

    // Color pickers state
    float m_mainColorF[3] = {0.0f, 1.0f, 0.0f};
    float m_outlineColorF[3] = {0.0f, 0.0f, 0.0f};
    bool m_colorInit = false;

    // For allow list selection
    int m_selectedAllow = -1;

    // Pixel editor (External Overlay-like) - now pop-out separate OS window via viewports
    struct PixelEditor {
        bool open = false;
        int imgSize = 64;
        float zoom = 3.0f;
        std::vector<uint32_t> pixels;
        std::vector<std::vector<uint32_t>> undoStack;
        std::vector<std::vector<uint32_t>> redoStack;
        bool useRGBA = true;
        int penR = 255, penG = 0, penB = 0, penA = 255;
        bool dragging = false;
        bool erasing = false;
        int hoverX = -1, hoverY = -1;
        uint32_t hoverValue = 0;
        char saveName[64] = "New_crosshair";
    } m_editor;
    void RenderPixelEditor();
    void EditorNew();
    void EditorUndo();
    void EditorRedo();
    void EditorPushUndo();
    void EditorSave();
    void EditorSaveAs(const std::wstring& name);
    void EditorLoadFromPng(const std::wstring& path);
    void EditorResize(int newSize);
    bool IsBindingVisibility = false;
    bool IsBindingLeanLeft = false;
    bool IsBindingLeanRight = false;
    bool IsBindingSwitch = false;

    // Crosshair library (custom PNGs)
    std::vector<std::wstring> m_libraryFiles; // full path
    std::vector<std::string> m_libraryNames; // display name without ext
    std::vector<ID3D11ShaderResourceView*> m_librarySRVs;
    std::vector<ID3D11Texture2D*> m_libraryTexs;
    int m_selectedLibrary = -1;
    bool m_showOnlyFavorites = false;
    std::vector<std::wstring> m_favorites; // list of exe names? for crosshairs, file names
    char m_libraryFilter[64] = {0};
    int m_libraryCategory = 0; // 0=All
    float m_svgTintF[3] = {0.00f, 1.00f, 0.65f}; // teal default for black SVGs (Kenney)
    double m_hoverStartTime = 0.0;
    int m_hoveredIdx = -1;
    DWORD m_testFlashUntil = 0;
    HWND m_testFlashHwnd = nullptr;
    void FlashActiveDisplay();
    ImFont* m_titleFont = nullptr;
    bool m_titleFontLoaded = false;
    void EnsureTitleFont();
    // Auto update
    RemoteVersion m_pendingUpdate;
    bool m_showUpdatePopup = false;
    std::string m_updateError;
    bool m_checkingUpdate = false;
    void TriggerUpdateCheck(bool manual=false);
    void RenderUpdatePopup();
    void RefreshLibrary();
    void EnsureLibraryThumbnails();
    std::wstring GetLibraryDir();
    std::wstring GetLibraryDirFallbackExe();
    void LoadFavorites();
    void SaveFavorites();
    bool IsFavorite(const std::wstring& file);
    void ToggleFavorite(const std::wstring& file);
    ID3D11ShaderResourceView* GetThumbnailFor(const std::wstring& path, int thumbSize=64);

    // Background camo + DM logo (visual refresh)
    ID3D11ShaderResourceView* m_camoSRV = nullptr;
    ID3D11Texture2D* m_camoTex = nullptr;
    int m_camoW = 0, m_camoH = 0;
    bool m_camoLoaded = false;
    float m_camoOffset = 0.0f;
    ID3D11ShaderResourceView* m_dmLogoSRV = nullptr;
    ID3D11Texture2D* m_dmLogoTex = nullptr;
    int m_dmLogoW = 0, m_dmLogoH = 0;
    bool m_dmLogoLoaded = false;
    bool LoadTextureFromFile(const std::wstring& path, ID3D11ShaderResourceView** outSRV, ID3D11Texture2D** outTex, int* outW=nullptr, int* outH=nullptr);
    void EnsureCamoTexture();
    void EnsureDMLogoTexture();
};

} // namespace dopes

