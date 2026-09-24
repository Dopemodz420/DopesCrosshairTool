# DopesCrosshairTool

Crosshair HUD overlay for Windows 10 / 11 — external, non-injecting transparent overlay that draws a custom crosshair at the center of your target program.

**No DLL injection, no hooking** — uses a layered topmost transparent window (`WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST`) with per-pixel alpha via `UpdateLayeredWindow`. Works with most windowed / borderless games and desktop apps without triggering anti-cheat hooks that detect injection.

---

## Features

- **Overlay engine** — virtual-screen sized layered window, follows the foreground / target window center, click-through (mouse passes through), multi-monitor aware, 60 FPS polling.
- **Allow list** — choose which `.exe` files may show the overlay. Modes:
  - `Only when foreground` — overlay only when an allow-listed exe is the foreground window
  - `Hide if not allow-listed` — hide otherwise
  - `Follow target window` — center on target window's client area, otherwise center of current monitor
  - If allow list is empty, overlay shows always (when globally enabled).
- **Crosshair designer** — build your own crosshair without external tools:
  - Types: Cross, Dot, Circle, Cross+Dot, T-Shape
  - Color + outline color (color picker), outline toggle/thickness
  - Size/length, thickness, gap, opacity (0-255)
  - Center dot toggle + dot size
  - Circle radius + thickness
  - Offset X/Y (fine-tune center)
  - Live preview panel (checker background, GDI+ anti-aliased)
- **PNG import** — use your own `.png` (or `.bmp`/`.jpg`) as crosshair:
  - Browse button + drag-and-drop onto main window
  - PNG Scale (0.1× – 5.0×) and PNG opacity sliders
  - Toggle “Use PNG instead” to switch between vector and image mode
  - Per-pixel alpha respected (transparent PNGs work)
- **Hotkey** — `F8` toggles overlay globally (also checkbox in UI). Registered via `RegisterHotKey`.
- **Config persistence** — JSON saved to `crosshair_config.json` next to the exe *and* `%APPDATA%\DopesCrosshairTool\config.json` as backup. Auto-saves periodically and on change.
- **Single-instance** mutex, DPI-aware, runs on Windows 10/11 x64.

## Project Layout

```
DopesCrosshairTool/
├── CMakeLists.txt
├── README.md
├── DopesCrosshairTool.exe          # Release binary (copied from build/bin)
├── crosshair_config.json           # created on first save (example below)
├── resources/
│   └── app.rc
├── src/
│   ├── main.cpp                    # entry, single-instance, hotkey loop
│   ├── Crosshair.h/.cpp            # data model, clamping, defaults
│   ├── CrosshairRenderer.h/.cpp    # GDI+ vector + PNG rendering
│   ├── OverlayWindow.h/.cpp        # layered overlay window, UpdateLayeredWindow
│   ├── ProcessTracker.h/.cpp       # foreground exe detection, allow-list check, enumerating processes
│   ├── ConfigManager.h/.cpp        # JSON load/save (hand-rolled, no external deps)
│   ├── ConfigWindow.h/.cpp         # main Win32 GUI (controls, sliders, preview, drag-drop)
│   └── Utils.h
└── build/                          # out-of-source CMake build (VS 2026)
    └── bin/DopesCrosshairTool.exe
```

## Build

Prerequisites (as on this machine):
- Visual Studio 2026 18.9.1 (MSVC 19.51.36256)
- Windows SDK 10.0.26100.0
- CMake 4.4.2

```powershell
# From Developer PowerShell for VS 2026
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
# exe at build/bin/DopesCrosshairTool.exe
```

Or open `build/DopesCrosshairTool.sln` in Visual Studio and build Release|x64.

Linked libs: `gdiplus`, `gdi32`, `user32`, `dwmapi`, `shlwapi`, `comdlg32`, `comctl32` — all in Windows SDK, no vcpkg needed.

## Usage

1. Launch `DopesCrosshairTool.exe` (no admin required for most apps; some fullscreen exclusives may need admin to stay topmost).
2. In the main window:
   - Pick a **Type** (Cross is default), set **Color**, **Size**, **Thickness**, **Gap**, **Opacity**, **Outline**, etc. Preview updates live.
   - **Allow List**: click `Refresh`, select a running exe from the dropdown, or type `game.exe` manually, then `Add`. Double-click an entry to toggle enabled/disabled; select + `Remove` to delete.
   - Check `Overlay Enabled`, `Only when foreground`, `Hide if not allow-listed` as desired.
   - **PNG**: `Browse PNG` or drag a `.png` file onto the window, check `Use PNG instead`, adjust scale/opacity.
   - `F8` toggles overlay at any time. `Save` writes `crosshair_config.json`.
3. Switch to your game / target exe — if it is in the allow list (and foreground rules match), the crosshair appears centered. Mouse clicks pass through the overlay.
4. Close the main window to quit (overlay is destroyed). Config auto-saves.

### Tips

- For competitive / anti-cheat environments: this is an **external overlay** (no WriteProcessMemory, no hook). Some anti-cheats still flag *any* overlay — use at your own risk and test in single-player / windowed mode first.
- Fullscreen exclusive (FSE) games may hide overlays. Prefer **borderless windowed** or **windowed** mode.
- If overlay appears behind game, try running the tool **as administrator** — elevated windows are higher in Z-order.
- Multi-monitor: overlay spans the virtual screen; center follows the foreground window or the monitor under the cursor.

## Configuration File

Example `crosshair_config.json`:

```json
{
  "type": 0,
  "color": "0,255,0",
  "outlineColor": "0,0,0",
  "outlineEnabled": true,
  "outlineThickness": 2,
  "size": 12,
  "thickness": 2,
  "gap": 4,
  "centerDot": false,
  "dotSize": 4,
  "circleRadius": 10,
  "circleThickness": 2,
  "opacity": 230,
  "offsetX": 0,
  "offsetY": 0,
  "usePng": false,
  "pngPath": "",
  "pngScale": 1,
  "pngOpacity": 255,
  "overlayEnabled": true,
  "onlyWhenForeground": true,
  "hideWhenNotAllowlisted": true,
  "followTargetWindow": true,
  "hotkeyVk": 119,
  "hotkeyCtrl": false,
  "hotkeyAlt": false,
  "hotkeyShift": false,
  "targetFps": 60,
  "allowList": [
    { "exeName": "notepad.exe", "enabled": true },
    { "exeName": "cs2.exe", "enabled": true }
  ]
}
```

Colors are `r,g,b` or `#RRGGBB`.

## Hotkeys

- `F8` — toggle overlay on/off (global).

## Troubleshooting

- **Overlay not visible**: check `Overlay Enabled` is checked, allow list rules, and that target is foreground. Press `F8`.
- **Not centered**: some games have title bars — the tool tracks the *client area* center, which should be correct in borderless. Try adjusting **Offset X/Y**.
- **PNG not showing**: ensure path is valid, file is 32-bit PNG with alpha, and `Use PNG instead` is checked.
- **Blocked in game**: try windowed/borderless; ensure tool is not flagged by vendor — this tool is generic and makes no claims of anti-cheat bypass.

## Tech Notes

- Rendering: GDI+ (`Gdiplus::Graphics`, `Pen`, `SolidBrush`, `ColorMatrix` for PNG opacity), DIBSection + `UpdateLayeredWindow` for per-pixel alpha.
- Overlay window: `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`, `ShowWindow(SW_SHOWNA)` so it never steals focus.
- Process detection: `GetForegroundWindow` + `GetWindowThreadProcessId` + `OpenProcess` + `QueryFullProcessImageNameW` + `Toolhelp32Snapshot` for enumeration.

## License

MIT — do what you want, no warranty.

## Future Ideas

- Per-exe crosshair profiles
- Configurable hotkey UI (Ctrl/Alt/Shift modifiers)
- Direct export of designed crosshair to PNG
- Tray icon + minimize to tray
