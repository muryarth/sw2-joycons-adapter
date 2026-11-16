// SW2 Adapter — entry point: Win32 window, DirectX 11, Dear ImGui loop.

#include <Windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shobjidl_core.h>

#include <cstdio>
#include <string>

#include <winrt/base.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include "app/DeviceManager.h"
#include "app/Version.h"
#include "core/I18n.h"
#include "platform/Autostart.h"
#include "ui/AppUI.h"
#include "ui/Sounds.h"

#include "resource.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

constexpr wchar_t WINDOW_CLASS[] = L"SW2JoyConsAdapter";
constexpr wchar_t WINDOW_TITLE[] = L"SW2 Adapter";

ID3D11Device*           g_device  = nullptr;
ID3D11DeviceContext*    g_context = nullptr;
IDXGISwapChain*         g_swap    = nullptr;
ID3D11RenderTargetView* g_rtv     = nullptr;
bool  g_focused   = true;
bool  g_minimized = false;
float g_dpiScale  = 1.f;
bool  g_dpiChanged = false;

// ---- Notification area (system tray) ----
// The icon is always there while the app runs. Closing the window hides it
// (unless "keep running when closed" is off); the menu has Open and Quit.
constexpr UINT WM_TRAY  = WM_APP + 1;
constexpr UINT WM_SHOW_APP = WM_APP + 2;   // sent by a second launch: bring the window back
constexpr UINT ID_OPEN  = 1001;
constexpr UINT ID_QUIT  = 1002;
UINT  g_taskbarCreated = 0;           // Explorer restarted: add the icon again
NOTIFYICONDATAW g_tray{};
bool  g_trayHintShown = false;        // "still running" balloon, once per session
sw2::DeviceManager* g_devices = nullptr;

std::wstring Wide(const char* utf8) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L' ');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w.data(), n);
    return w;
}

void AddTrayIcon(HWND hwnd) {
    g_tray = {};
    g_tray.cbSize = sizeof(g_tray);
    g_tray.hWnd = hwnd;
    g_tray.uID = 1;
    g_tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    g_tray.uCallbackMessage = WM_TRAY;
    g_tray.hIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                                 GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    wcscpy_s(g_tray.szTip, L"SW2 Adapter");
    Shell_NotifyIconW(NIM_ADD, &g_tray);
    g_tray.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_tray);
}

void SetTrayTip(const std::wstring& tip) {
    if (tip == g_tray.szTip) return;
    wcsncpy_s(g_tray.szTip, tip.c_str(), _TRUNCATE);
    g_tray.uFlags = NIF_TIP | NIF_SHOWTIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_tray);
}

void ShowMainWindow(HWND hwnd) {
    ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(hwnd);
}

void ShowTrayMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_OPEN, Wide(sw2::tr("Open SW2 Adapter", "Abrir o SW2 Adapter")).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_QUIT, Wide(sw2::tr("Quit", "Sair")).c_str());
    SetMenuDefaultItem(menu, ID_OPEN, FALSE);
    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);   // required for the menu to close when clicking elsewhere
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (cmd == ID_OPEN) ShowMainWindow(hwnd);
    else if (cmd == ID_QUIT) DestroyWindow(hwnd);
}

void HideToTray(HWND hwnd) {
    ShowWindow(hwnd, SW_HIDE);
    if (g_trayHintShown) return;
    g_trayHintShown = true;
    NOTIFYICONDATAW n = g_tray;
    n.uFlags = NIF_INFO;
    n.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    wcscpy_s(n.szInfoTitle, L"SW2 Adapter");
    wcsncpy_s(n.szInfo, Wide(sw2::tr("Still running here, ready to connect your controllers. Right-click the icon to quit.",
                                     "Continua rodando aqui, pronto para conectar seus controles. Clique com o botão direito no ícone para sair.")).c_str(),
              _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

void CreateRTV() {
    ID3D11Texture2D* back = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) { g_device->CreateRenderTargetView(back, nullptr, &g_rtv); back->Release(); }
}
void CleanupRTV() { if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; } }

bool CreateDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate = {60, 1};
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                               D3D11_SDK_VERSION, &sd, &g_swap, &g_device, &fl, &g_context);
    if (hr == DXGI_ERROR_UNSUPPORTED)   // no GPU driver: software rasterizer
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                                           D3D11_SDK_VERSION, &sd, &g_swap, &g_device, &fl, &g_context);
    if (FAILED(hr)) return false;
    CreateRTV();
    return true;
}

void CleanupDevice() {
    CleanupRTV();
    if (g_swap)    { g_swap->Release();    g_swap = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device)  { g_device->Release();  g_device = nullptr; }
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    if (g_taskbarCreated && msg == g_taskbarCreated) { AddTrayIcon(hWnd); return 0; }
    switch (msg) {
        case WM_TRAY:
            switch (LOWORD(lParam)) {   // NOTIFYICON_VERSION_4: the event is in the low word
                case NIN_SELECT:
                case NIN_KEYSELECT:
                case WM_LBUTTONDBLCLK: ShowMainWindow(hWnd); break;
                case WM_CONTEXTMENU:   ShowTrayMenu(hWnd); break;
            }
            return 0;
        case WM_SHOW_APP:
            ShowMainWindow(hWnd);
            return 0;
        case WM_CLOSE:
            if (g_devices && g_devices->Settings().closeToTray) { HideToTray(hWnd); return 0; }
            break;   // DefWindowProc destroys the window: the app quits
        case WM_ACTIVATEAPP:
            g_focused = wParam != FALSE;
            return 0;
        case WM_SIZE:
            g_minimized = wParam == SIZE_MINIMIZED;
            if (g_device && !g_minimized) {
                CleanupRTV();
                g_swap->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
                CreateRTV();
            }
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize = {LONG(400 * g_dpiScale), LONG(520 * g_dpiScale)};
            return 0;
        }
        case WM_DPICHANGED: {
            g_dpiScale = HIWORD(wParam) / 96.f;
            g_dpiChanged = true;
            const RECT* r = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hWnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;   // no ALT menu beep
            break;
        case WM_DESTROY:
            Shell_NotifyIconW(NIM_DELETE, &g_tray);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void UseDarkTitleBar(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    COLORREF caption = RGB(0x21, 0x22, 0x2B);
    DwmSetWindowAttribute(hwnd, 35 /*DWMWA_CAPTION_COLOR*/, &caption, sizeof(caption));
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    // One instance only: two apps would fight over the same controllers.
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\SW2JoyConsAdapter.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Ask the running copy to show itself (also brings it back from the tray).
        if (HWND other = FindWindowW(WINDOW_CLASS, nullptr)) {
            DWORD pid = 0;
            GetWindowThreadProcessId(other, &pid);
            AllowSetForegroundWindow(pid);
            PostMessageW(other, WM_SHOW_APP, 0, 0);
        }
        return 0;
    }

    // Same ID as the installer's Start menu shortcut, so taskbar pins and
    // search results group with the running window.
    SetCurrentProcessExplicitAppUserModelID(sw2::APP_USER_MODEL_ID);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = WINDOW_CLASS;
    RegisterClassExW(&wc);

    const UINT sysDpi = GetDpiForSystem();
    g_dpiScale = sysDpi / 96.f;
    HWND hwnd = CreateWindowExW(0, WINDOW_CLASS, WINDOW_TITLE, WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, int(480 * g_dpiScale), int(720 * g_dpiScale),
                                nullptr, nullptr, hInst, nullptr);
    g_dpiScale = GetDpiForWindow(hwnd) / 96.f;
    UseDarkTitleBar(hwnd);

    if (!CreateDevice(hwnd)) {
        MessageBoxW(hwnd, L"Não foi possível iniciar o DirectX 11.", WINDOW_TITLE, MB_ICONERROR);
        DestroyWindow(hwnd);
        return 1;
    }
    // Started with Windows (--tray): stay in the notification area.
    const bool startInTray = wcsstr(GetCommandLineW(), sw2::AUTOSTART_FLAG) != nullptr;
    if (!startInTray) {
        ShowWindow(hwnd, SW_SHOWDEFAULT);
        UpdateWindow(hwnd);
    }
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    AddTrayIcon(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // nothing to persist from ImGui itself
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    sw2::Sounds sounds;
    sw2::DeviceManager devices;
    devices.Init();
    if (wcsstr(GetCommandLineW(), L"--demo")) devices.SetDemo(true);
    if (wcsstr(GetCommandLineW(), L"--trace")) devices.SetTrace(true);
    g_devices = &devices;
    sw2::AppUI ui(devices, sounds);
    ui.SetupFonts(g_dpiScale);
    ui.SetupStyle(g_dpiScale);

    // Frame pacing: the UI is mostly static, so only draw fast while the user
    // interacts. Waiting on the message queue (not a blind Sleep) means any
    // input wakes the loop immediately.
    constexpr DWORD INTERACTIVE_MS = 0;     // vsync (~60 fps) right after input
    constexpr DWORD IDLE_MS        = 33;    // ~30 fps: animations stay smooth
    constexpr DWORD BACKGROUND_MS  = 100;   // ~10 fps when another window has focus
    constexpr DWORD MINIMIZED_MS   = 100;   // no drawing, just connection logic
    constexpr double INTERACTION_WINDOW_S = 1.0;
    double lastInputTime = 0.0;

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if ((msg.message >= WM_MOUSEFIRST && msg.message <= WM_MOUSELAST) ||
                (msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST))
                lastInputTime = ImGui::GetTime();
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (g_dpiChanged) {
            g_dpiChanged = false;
            ui.SetupFonts(g_dpiScale);
            ui.SetupStyle(g_dpiScale);
        }
        // Tray tooltip: how many controllers are connected (checked once a second).
        static double lastTip = -10.0;
        if (const double now = GetTickCount64() / 1000.0; now - lastTip >= 1.0) {
            lastTip = now;
            int connected = 0;
            for (const auto& v : devices.Snapshot()) connected += v.state == sw2::DeviceState::Connected;
            char tip[96];
            if (connected == 0) std::snprintf(tip, sizeof(tip), "SW2 Adapter · %s", sw2::tr("no controller connected", "nenhum controle conectado"));
            else std::snprintf(tip, sizeof(tip), connected == 1 ? sw2::tr("SW2 Adapter · 1 controller connected", "SW2 Adapter · 1 controle conectado")
                                                                : sw2::tr("SW2 Adapter · %d controllers connected", "SW2 Adapter · %d controles conectados"),
                               connected);
            SetTrayTip(Wide(tip));
        }
        if (g_minimized || !IsWindowVisible(hwnd)) {
            // Keep the connection logic alive (background reconnects, sounds) without drawing.
            ui.Background();
            MsgWaitForMultipleObjects(0, nullptr, FALSE, MINIMIZED_MS, QS_ALLINPUT);
            continue;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ui.Frame(g_focused);
        ImGui::Render();
        const float clear[4] = {0x21 / 255.f, 0x22 / 255.f, 0x2B / 255.f, 1.f};
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        const bool interacting = g_focused && ImGui::GetTime() - lastInputTime < INTERACTION_WINDOW_S;
        const bool smooth = interacting || ui.WantsSmoothFrames();   // animations stay fluid, focused or not
        g_swap->Present(smooth ? 1 : 0, 0);
        const DWORD wait = smooth ? INTERACTIVE_MS : !g_focused ? BACKGROUND_MS : IDLE_MS;
        if (wait) MsgWaitForMultipleObjects(0, nullptr, FALSE, wait, QS_ALLINPUT);
    }

    devices.Shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDevice();
    DestroyWindow(hwnd);
    UnregisterClassW(WINDOW_CLASS, hInst);
    if (single) CloseHandle(single);
    return 0;
}
