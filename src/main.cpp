#include "ConfigWindow.h"
#include "OverlayWindow.h"
#include "ConfigManager.h"
#include "Utils.h"

#include <d3d11.h>
#include <dwmapi.h>
#include <tchar.h>
#include <windows.h>
#include <shellapi.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <mmsystem.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winmm.lib")

using namespace dopes;

static void PlaySoundId(int id){
    switch(id){
        case 0: return;
        case 1: Beep(620, 32); break;
        case 2: Beep(750, 28); break;
        case 3: Beep(900, 35); break;
        case 4: Beep(880, 45); Beep(1200, 40); break;
        case 5: PlaySoundW(TEXT("SystemAsterisk"), NULL, SND_ALIAS | SND_ASYNC); break;
        case 6: PlaySoundW(TEXT("SystemExclamation"), NULL, SND_ALIAS | SND_ASYNC); break;
        default: Beep(620, 32); break;
    }
}
static void PlayToggleSoundWav(bool on){
    wchar_t exe[MAX_PATH]{}; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring exeDir(exe); size_t p=exeDir.find_last_of(L"\\/"); if(p!=std::wstring::npos) exeDir=exeDir.substr(0,p+1);
    std::wstring wav = exeDir + (on ? L"assets\\togglesounds\\toggle-on.wav" : L"assets\\togglesounds\\toggle-off.wav");
    if(GetFileAttributesW(wav.c_str())!=INVALID_FILE_ATTRIBUTES){
        PlaySoundW(wav.c_str(), NULL, SND_FILENAME | SND_ASYNC);
        return;
    }
    // fallback to exeDir alternative (build output)
    wav = exeDir + L"..\\assets\\togglesounds\\" + (on?L"toggle-on.wav":L"toggle-off.wav");
    if(GetFileAttributesW(wav.c_str())!=INVALID_FILE_ATTRIBUTES){
        PlaySoundW(wav.c_str(), NULL, SND_FILENAME | SND_ASYNC);
        return;
    }
    // final fallback to subtle beep
    Beep(on? 720: 500, 36);
}
static void PlayToggleSound(const AppConfig& cfg, bool on){
    if(!cfg.playToggleSounds) return;
    PlayToggleSoundWav(on);
}
static void PlayToggleSoundLegacy(bool on, bool enabled){
    AppConfig tmp; tmp.playToggleSounds=enabled; tmp.toggleOnSound=on?1:1; tmp.toggleOffSound=1;
    PlayToggleSound(tmp, on);
}

static ID3D11Device*           g_device = nullptr;
static ID3D11DeviceContext*    g_context = nullptr;
static IDXGISwapChain*         g_swapChain = nullptr;
static ID3D11RenderTargetView* g_renderTarget = nullptr;

static HWND g_hMainWnd = nullptr;
static NOTIFYICONDATAW g_nid{};
static bool g_trayAdded = false;
static bool g_isMinimizedToTray = false;
static bool g_showTrayPopup = false;
static ImVec2 g_trayPopupPos = ImVec2(0,0);

#define WM_TRAYICON (WM_APP + 1)
#define ID_TRAY_SHOW 1001
#define ID_TRAY_EXIT 1002
#define ID_TRAY_TOGGLE 1003

static void AddTrayIcon(HWND hwnd) {
    if (g_trayAdded) return;
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCE(101));
    if (!g_nid.hIcon) g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"Dopes Crosshair Tool");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_trayAdded = true;
}
static void RemoveTrayIcon() {
    if (!g_trayAdded) return;
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_trayAdded = false;
}
static void ShowMainWindow() {
    if (!g_hMainWnd) return;
    ShowWindow(g_hMainWnd, SW_RESTORE);
    ShowWindow(g_hMainWnd, SW_SHOW);
    SetForegroundWindow(g_hMainWnd);
    g_isMinimizedToTray = false;
}
static void HideToTray() {
    if (!g_hMainWnd) return;
    ShowWindow(g_hMainWnd, SW_HIDE);
    g_isMinimizedToTray = true;
}

static bool IsAutoStartEnabled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
    wchar_t buf[MAX_PATH]{};
    DWORD sz = sizeof(buf);
    DWORD type = 0;
    LONG r = RegQueryValueExW(hKey, L"DopesCrosshairTool", nullptr, &type, (LPBYTE)buf, &sz);
    RegCloseKey(hKey);
    return r == ERROR_SUCCESS;
}
static void SetAutoStart(bool enable, bool asAdmin) {
    // For asAdmin, we create a scheduled task with highest privileges instead of registry
    // For non-admin, use registry Run
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exePath) + L"\" --minimized";
    if (asAdmin) {
        // Create scheduled task
        std::wstring taskName = L"DopesCrosshairTool";
        if (enable) {
            std::wstring sch = L" /Create /F /SC ONLOGON /RL HIGHEST /TN \"" + taskName + L"\" /TR \"" + cmd + L"\"";
            // Use schtasks
            std::wstring full = L"schtasks" + sch;
            // Execute
            STARTUPINFOW si{}; si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            // Use cmd /c
            std::wstring cmdLine = L"cmd.exe /c schtasks " + sch;
            CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
            if (pi.hProcess) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
            // Also remove registry to avoid duplicate
            HKEY hKey;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
                RegDeleteValueW(hKey, L"DopesCrosshairTool");
                RegCloseKey(hKey);
            }
        } else {
            std::wstring del = L" /Delete /F /TN \"" + taskName + L"\"";
            std::wstring cmdLine = L"cmd.exe /c schtasks " + del;
            STARTUPINFOW si{}; si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
            if (pi.hProcess) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
            // Also remove registry
            HKEY hKey;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
                RegDeleteValueW(hKey, L"DopesCrosshairTool");
                RegCloseKey(hKey);
            }
        }
        return;
    }
    // Non-admin: registry
    // Remove task if exists
    {
        std::wstring taskName = L"DopesCrosshairTool";
        std::wstring del = L" /Delete /F /TN \"" + taskName + L"\"";
        std::wstring cmdLine = L"cmd.exe /c schtasks " + del;
        STARTUPINFOW si{}; si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        if (pi.hProcess) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    }
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey) != ERROR_SUCCESS) return;
    if (enable) {
        RegSetValueExW(hKey, L"DopesCrosshairTool", 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.size()+1)*sizeof(wchar_t)));
    } else {
        RegDeleteValueW(hKey, L"DopesCrosshairTool");
    }
    RegCloseKey(hKey);
}

static void CreateRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    g_device->CreateRenderTargetView(backBuffer, nullptr, &g_renderTarget);
    backBuffer->Release();
}
static void CleanupRenderTarget()
{
    if (g_renderTarget) { g_renderTarget->Release(); g_renderTarget = nullptr; }
}
static bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    return D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &g_swapChain, &g_device, nullptr, &g_context) == S_OK;
}
static void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        // Minimize always goes to taskbar (user requested). Only X-close hides to tray when minimizeToTray is enabled.
        if (g_device && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_swapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_CLOSE:
        {
            // If minimize to tray enabled, hide instead of close
            // Check config via user data
            if (auto* cw = (ConfigWindow*)GetWindowLongPtrW(hWnd, GWLP_USERDATA)) {
                AppConfig cfg = cw->GetConfig();
                if (cfg.minimizeToTray && g_trayAdded) {
                    HideToTray();
                    return 0;
                }
            }
        }
        break;
    case WM_TRAYICON:
        {
            // Support both classic WM_* (when no version set) and NIN_SELECT/NIN_KEYSELECT for VERSION_4 compatibility.
            // Left click (any button) restores window.
            if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK || lParam == WM_LBUTTONDOWN
                || lParam == NIN_SELECT || lParam == NIN_KEYSELECT) {
                ShowMainWindow();
                return 0;
            }
            if (lParam == WM_RBUTTONUP || lParam == WM_RBUTTONDBLCLK || lParam == WM_CONTEXTMENU || lParam == WM_RBUTTONDOWN) {
                POINT pt; GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, ID_TRAY_SHOW, L"Open");
                AppendMenuW(hMenu, MF_STRING, ID_TRAY_TOGGLE, L"Toggle Overlay");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Close Program");
                SetForegroundWindow(hWnd);
                TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, nullptr);
                DestroyMenu(hMenu);
                // HMENU requires a dummy message to clear the menu focus
                PostMessageW(hWnd, WM_NULL, 0, 0);
                return 0;
            }
            // Fallback: any other notification still try to show menu on right click coords
            if (lParam == 517 /* WM_RBUTTONUP legacy */) {
                ShowMainWindow();
            }
        }
        return 0;
    case WM_COMMAND:
        {
            int id = LOWORD(wParam);
            if (id == ID_TRAY_SHOW) {
                ShowMainWindow();
            } else if (id == ID_TRAY_EXIT) {
                RemoveTrayIcon();
                PostQuitMessage(0);
            } else if (id == ID_TRAY_TOGGLE) {
                if (auto* cw = (ConfigWindow*)GetWindowLongPtrW(hWnd, GWLP_USERDATA)) {
                    AppConfig cfg = cw->GetConfig();
                    cfg.overlayEnabled = !cfg.overlayEnabled;
                    cw->SetConfig(cfg);
                    ConfigManager::Save(cfg);
                    PlayToggleSound(cfg, cfg.overlayEnabled);
                }
            }
        }
        return 0;
    case WM_HOTKEY:
        if (wParam == 9001) { // F8 - no-lag toggle (RegisterHotKey)
            if (auto* cw = (ConfigWindow*)GetWindowLongPtrW(hWnd, GWLP_USERDATA)) {
                AppConfig cfg = cw->GetConfig();
                cfg.overlayEnabled = !cfg.overlayEnabled;
                cw->SetConfig(cfg);
                ConfigManager::Save(cfg);
                PlayToggleSound(cfg, cfg.overlayEnabled);
                if (IsWindowVisible(hWnd)) { InvalidateRect(hWnd, nullptr, FALSE); }
            }
            return 0;
        }
        break;
    case WM_DESTROY:
        UnregisterHotKey(hWnd, 9001);
        RemoveTrayIcon();
        PostQuitMessage(0);
        return 0;
    case WM_NCHITTEST:
    {
        POINT pt{ (short)LOWORD(lParam), (short)HIWORD(lParam) };
        ScreenToClient(hWnd, &pt);
        if (pt.y >= 0 && pt.y < 28) return HTCAPTION;
        break;
    }
    case WM_NCLBUTTONDBLCLK:
        return 0;
    case WM_DROPFILES:
    {
        if (auto* cw = (ConfigWindow*)GetWindowLongPtrW(hWnd, GWLP_USERDATA))
        {
            HDROP hDrop = (HDROP)wParam;
            cw->HandleDropFiles(hDrop);
        }
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

HANDLE g_hMutex = nullptr;

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE, LPSTR lpCmdLine, int)
{
    // Check for --minimized flag
    bool forceMinimized = false;
    if (lpCmdLine && strstr(lpCmdLine, "--minimized")) forceMinimized = true;

    // Single instance
    g_hMutex = CreateMutexW(nullptr, TRUE, L"DopesCrosshairTool_SingleInstance");
    HANDLE hMutex = g_hMutex; // alias for legacy checks
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        // If this is an elevated restart, the old instance may be exiting — wait a bit and retry
        // Check if command line contains --minimized and we are elevated? Actually just wait 500ms and try again
        Sleep(400);
        // Try again to create mutex after wait
        HANDLE h2 = CreateMutexW(nullptr, TRUE, L"DopesCrosshairTool_SingleInstance2"); // dummy to test if old still exists
        CloseHandle(h2);
        // Re-check: try to find existing window and ask it to close if it's the same exe
        HWND existing = FindWindowW(L"DopesCrosshair", nullptr);
        if (existing) {
            // If we are elevated and existing is not elevated, we want to replace it — try to close old
            // For now, just show it and exit; user can manually close old via tray
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        // If after wait the old instance is still there, show message and exit
        // Check again if mutex still exists by trying to open it
        HANDLE hTest = OpenMutexW(SYNCHRONIZE, FALSE, L"DopesCrosshairTool_SingleInstance");
        if (hTest) {
            CloseHandle(hTest);
            // Still exists, but if we are elevated and old is not, we should force close old via WM_CLOSE
            if (existing) PostMessageW(existing, WM_CLOSE, 0, 0);
            Sleep(600);
            // Try once more: if still exists, then another instance truly running, show message
            HANDLE hTest2 = OpenMutexW(SYNCHRONIZE, FALSE, L"DopesCrosshairTool_SingleInstance");
            if (hTest2) {
                CloseHandle(hTest2);
                MessageBoxW(nullptr, L"Dopes Crosshair Tool is already running.", L"Already running", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
        }
        // If we reached here and hTest2 is null, old instance is gone — allow new elevated instance to continue
        HANDLE hTest2b = OpenMutexW(SYNCHRONIZE, FALSE, L"DopesCrosshairTool_SingleInstance");
        if (hTest2b) {
            CloseHandle(hTest2b);
            MessageBoxW(nullptr, L"Dopes Crosshair Tool is already running.", L"Already running", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        // Old is gone, continue as new instance (mutex from earlier still held by this process? Actually we need to ensure g_hMutex is still valid)
        // g_hMutex was created at start and GetLastError was set, but the handle is still valid for this process, so we can continue
    }

    SetProcessDPIAware();

    // Load config early to check runAsAdmin and startMinimized
    AppConfig earlyCfg;
    if (!ConfigManager::Load(earlyCfg)) earlyCfg.SetDefaults();
    bool isElevated = earlyCfg.IsElevated();
    if (earlyCfg.runAsAdmin && !isElevated) {
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring params;
        if (earlyCfg.startMinimized || forceMinimized) params = L"--minimized";
        ShellExecuteW(nullptr, L"runas", exe, params.empty()?nullptr:params.c_str(), nullptr, SW_SHOWNORMAL);
        if (g_hMutex) { ReleaseMutex(g_hMutex); CloseHandle(g_hMutex); g_hMutex=nullptr; }
        return 0;
    }
    bool startMinimized = earlyCfg.startMinimized || forceMinimized;

    // Register window class
    WNDCLASSEXW wc{ sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, hInst, LoadIconW(hInst, MAKEINTRESOURCE(101)), LoadCursorW(nullptr, IDC_ARROW), nullptr, nullptr, _T("DopesCrosshair"), LoadIconW(hInst, MAKEINTRESOURCE(101)) };
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    if (!wc.hIconSm) wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, _T("Dopes Crosshair Tool  —  Crosshair HUD Overlay v1.0.2"), WS_POPUP | WS_MINIMIZEBOX, 100, 100, 1120, 800, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd)
    {
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        if (g_hMutex) { ReleaseMutex(g_hMutex); CloseHandle(g_hMutex); g_hMutex=nullptr; }
        return 1;
    }
    g_hMainWnd = hwnd;

    const DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
    DragAcceptFiles(hwnd, TRUE);

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        if (g_hMutex) { ReleaseMutex(g_hMutex); CloseHandle(g_hMutex); g_hMutex=nullptr; }
        return 1;
    }

    // Hotkey: F8 global no-lag toggle (WM_HOTKEY path) — fixes polling lag when game has focus / vsync stalls
    RegisterHotKey(hwnd, 9001, 0, VK_F8);

    // Tray
    AddTrayIcon(hwnd);

    if (startMinimized) {
        ShowWindow(hwnd, SW_HIDE);
        g_isMinimizedToTray = true;
    } else {
        ShowWindow(hwnd, SW_SHOWDEFAULT);
        UpdateWindow(hwnd);
    }
    CreateRenderTarget();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_ViewportsEnable;
    io.ConfigViewportsNoTaskBarIcon = true;
    io.IniFilename = nullptr;

    ImFontConfig fontConfig{};
    fontConfig.OversampleH = 3;
    fontConfig.OversampleV = 2;
    ImFontGlyphRangesBuilder glyphBuilder;
    ImVector<ImWchar> glyphRanges;
    glyphBuilder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    glyphBuilder.AddText("\xE2\x97\x8F");
    glyphBuilder.BuildRanges(&glyphRanges);
    if (ImFont* f = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 17.0f, &fontConfig, glyphRanges.Data))
        io.FontDefault = f;

    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    OverlayWindow overlay;
    if (!overlay.Create())
    {
        MessageBoxW(hwnd, L"Failed to create overlay window.", L"Error", MB_OK | MB_ICONERROR);
    }

    AppConfig cfg = earlyCfg;
    overlay.SetConfig(cfg);

    ConfigWindow configWindow(&overlay, g_device, g_context);
    configWindow.SetConfig(cfg);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)&configWindow);

    // Ensure auto-start registry matches config
    // Do not overwrite on startup unless mismatch? We'll sync on UI toggle, but also ensure on startup if autoStart true, set it
    if (cfg.autoStart) {
        SetAutoStart(true, cfg.runAsAdmin);
    }

    bool running = true;
    bool f8PrevDown = false;

    while (running)
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
                running = false;
        }
        if (!running) break;

        // F8 now handled via WM_HOTKEY (RegisterHotKey) for zero-lag global toggle — no polling
        // Fallback polling only if RegisterHotKey was blocked (e.g., already registered by other app)
        {
            bool hotkeyRegistered = (0 != 0); // placeholder, WM_HOTKEY primary
            // Lightweight fallback check without double-toggle: only toggle if WM_HOTKEY not fired recently
            static DWORD lastHotkeyTick = 0;
            // Peek if hotkey message was recently dispatched via time check (last toggle stored in cfg? skip)
            // Keep disabled to avoid double-toggle; WM_HOTKEY is authoritative
            (void)hotkeyRegistered; (void)lastHotkeyTick;
        }
        (void)f8PrevDown; // suppress unused

        // Handle switch hotkey (still polled for custom binding, but not lag-critical)
        if (cfg.switchVk != 0) {
            static bool switchPrev = false;
            bool swDown = (GetAsyncKeyState(cfg.switchVk) & 0x8000) != 0;
            if (swDown && !switchPrev) {
                // Could cycle library — reserved
            }
            switchPrev = swDown;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        configWindow.UpdateAnimations(io.DeltaTime);
        configWindow.RenderUI(hwnd, running);
        // Tray popup small window (PCB style, rounded, smaller)
        if (g_showTrayPopup) {
            ImGui::SetNextWindowPos(g_trayPopupPos, ImGuiCond_Always, ImVec2(1,0)); // pivot bottom-right near cursor
            ImGui::SetNextWindowSize(ImVec2(220, 120), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.07f, 0.96f));
            ImGuiWindowFlags pf = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
            bool keep = true;
            if (ImGui::Begin("TrayPopup", &keep, pf)) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 wp = ImGui::GetWindowPos(); ImVec2 ws = ImGui::GetWindowSize();
                ImU32 bg = ImGui::GetColorU32(ImVec4(0.05f, 0.05f, 0.07f, 0.96f));
                ImU32 bd = ImGui::GetColorU32(ImVec4(0.0039f, 0.733f, 0.858f, 1.0f));
                dl->AddRectFilled(wp, ImVec2(wp.x+ws.x, wp.y+ws.y), bg, 12.0f);
                dl->AddRect(wp, ImVec2(wp.x+ws.x, wp.y+ws.y), bd, 12.0f, 0, 1.2f);
                ImGui::TextColored(ImVec4(0.0039f,0.733f,0.858f,1), "Dopes Crosshair");
                ImGui::Separator();
                if (ImGui::Button("Open", ImVec2(-1,26))) { ShowMainWindow(); g_showTrayPopup=false; }
                if (ImGui::Button("Close Program", ImVec2(-1,26))) { g_showTrayPopup=false; RemoveTrayIcon(); PostQuitMessage(0); }
                if (ImGui::Button("Toggle Overlay", ImVec2(-1,22))) {
                    AppConfig c = configWindow.GetConfig();
                    c.overlayEnabled = !c.overlayEnabled;
                    configWindow.SetConfig(c);
                    ConfigManager::Save(c);
                }
                // Close if clicked outside
                if (!ImGui::IsWindowHovered() && (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1))) {
                    // Check if click was outside popup
                    ImVec2 mp = ImGui::GetMousePos();
                    if (mp.x < wp.x || mp.x > wp.x+ws.x || mp.y < wp.y || mp.y > wp.y+ws.y) g_showTrayPopup=false;
                }
                ImGui::End();
            } else {
                ImGui::End();
                g_showTrayPopup=false;
            }
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            // Also close on Escape
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) g_showTrayPopup=false;
        }

        cfg = configWindow.GetConfig();
        // Handle minimize to tray request from UI? If running set to false via X, but minimizeToTray will hide
        // Check if window was hidden via tray
        if (!IsWindowVisible(hwnd) && !g_isMinimizedToTray && cfg.minimizeToTray) {
            // Window hidden externally? Keep tray
        }
        overlay.SetConfig(cfg);
        overlay.Update();

        ImGui::Render();
        const float clearCol[4] = { 0.055f, 0.06f, 0.085f, 1.0f };
        g_context->OMSetRenderTargets(1, &g_renderTarget, nullptr);
        g_context->ClearRenderTargetView(g_renderTarget, clearCol);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        g_swapChain->Present(1, 0);
    }

    {
        AppConfig finalCfg = configWindow.GetConfig();
        // Update auto-start based on final config
        bool currentlyAuto = IsAutoStartEnabled();
        if (finalCfg.autoStart != currentlyAuto) {
            SetAutoStart(finalCfg.autoStart, finalCfg.runAsAdmin);
        } else if (finalCfg.autoStart && finalCfg.runAsAdmin) {
            // Ensure task is correctly set
            SetAutoStart(true, true);
        }
        ConfigManager::Save(finalCfg);
    }

    RemoveTrayIcon();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    overlay.Destroy();
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    if (g_hMutex) { ReleaseMutex(g_hMutex); CloseHandle(g_hMutex); g_hMutex=nullptr; }
    return 0;
}
