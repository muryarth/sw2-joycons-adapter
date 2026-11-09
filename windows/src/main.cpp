// SW2 Adapter — entry point: Win32 window, DirectX 11, Dear ImGui loop.

#include <Windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <shobjidl_core.h>

#include <winrt/base.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include "app/DeviceManager.h"
#include "app/Version.h"
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
    switch (msg) {
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
        if (HWND other = FindWindowW(WINDOW_CLASS, nullptr)) {
            ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
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
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

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
        if (g_minimized) {
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
        g_swap->Present(interacting ? 1 : 0, 0);
        const DWORD wait = !g_focused ? BACKGROUND_MS : interacting ? INTERACTIVE_MS : IDLE_MS;
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
