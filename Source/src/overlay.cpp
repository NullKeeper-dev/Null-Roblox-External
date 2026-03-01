/*
 * overlay.cpp - Transparent DX11 overlay with ImGui
 *
 * Creates a borderless topmost window with DWM transparency.
 * Initializes DirectX 11 device/swapchain and ImGui backends.
 */

#include "../include/overlay.h"
#include <dwmapi.h>
#include <windows.h>
#include "../include/imgui.h"
#include "../include/imgui_impl_dx11.h"
#include "../include/imgui_impl_win32.h"
#include "../include/menu.h"
#include "../include/esp.h"
#include "../include/roblox.h"
#include "../include/console.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")

/* Forward declare ImGui WndProc handler */
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

/* ── Globals ─────────────────────────────────────────────────── */

static HWND                     g_hwnd = NULL;
static WNDCLASSEX              g_wc = {};
static ID3D11Device*            g_device = nullptr;
static ID3D11DeviceContext*     g_context = nullptr;
static IDXGISwapChain*          g_swapChain = nullptr;
static ID3D11RenderTargetView*  g_rtv = nullptr;
static char                     g_className[64] = {};

/* ── WndProc ─────────────────────────────────────────────────── */

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
        return true;

    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        if (g_device && wp != SIZE_MINIMIZED) {
            if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
            g_swapChain->ResizeBuffers(0, (UINT)LOWORD(lp), (UINT)HIWORD(lp), DXGI_FORMAT_UNKNOWN, 0);
            ID3D11Texture2D* backBuf = nullptr;
            g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuf));
            if (backBuf) {
                g_device->CreateRenderTargetView(backBuf, nullptr, &g_rtv);
                backBuf->Release();
            }
        }
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

/* ── Generate random class name ──────────────────────────────── */

static void GenerateRandomClassName() {
    srand((unsigned int)GetTickCount64());
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (int i = 0; i < 12; i++) {
        g_className[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    g_className[12] = '\0';
}

/* ── Init ────────────────────────────────────────────────────── */

bool Overlay::Init() {
    /* Generate a random window class name to avoid detection */
    GenerateRandomClassName();

    g_wc.cbSize = sizeof(WNDCLASSEX);
    g_wc.style = CS_HREDRAW | CS_VREDRAW;
    g_wc.lpfnWndProc = WndProc;
    g_wc.hInstance = GetModuleHandleA(NULL);
    g_wc.lpszClassName = g_className;

    if (!RegisterClassExA(&g_wc)) {
        Console::Error("Failed to register window class");
        return false;
    }

    /* Get primary monitor size */
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    /* Create the overlay window */
    g_hwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        g_className,
        "",  /* No title */
        WS_POPUP,
        0, 0, screenW, screenH,
        NULL, NULL, g_wc.hInstance, NULL
    );

    if (!g_hwnd) {
        Console::Error("Failed to create overlay window");
        return false;
    }

    /* Make the window transparent via DWM */
    MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(g_hwnd, &margins);

    /* Set the layered window to use alpha (fully transparent background) */
    SetLayeredWindowAttributes(g_hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    /* ── Create DX11 device and swapchain ────────────────────── */

    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = screenW;
    scd.BufferDesc.Height = screenH;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 0;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = g_hwnd;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    UINT createFlags = 0;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        createFlags, nullptr, 0,
        D3D11_SDK_VERSION, &scd,
        &g_swapChain, &g_device, &featureLevel, &g_context
    );

    if (FAILED(hr)) {
        Console::Error("Failed to create DX11 device (0x%08X)", hr);
        return false;
    }

    /* Create render target view */
    ID3D11Texture2D* backBuffer = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    g_device->CreateRenderTargetView(backBuffer, nullptr, &g_rtv);
    backBuffer->Release();

    Console::Success("DX11 device created (Feature Level: 0x%X)", featureLevel);

    /* ── Init ImGui ──────────────────────────────────────────── */

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;  /* Don't save layout to disk */

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    Console::Success("ImGui initialized");

    return true;
}

void Overlay::Shutdown() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    if (g_rtv) g_rtv->Release();
    if (g_swapChain) g_swapChain->Release();
    if (g_context) g_context->Release();
    if (g_device) g_device->Release();

    if (g_hwnd) DestroyWindow(g_hwnd);
    UnregisterClassA(g_className, g_wc.hInstance);
}

bool Overlay::BeginFrame() {
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
        if (msg.message == WM_QUIT) return false;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    return true;
}

void Overlay::EndFrame() {
    ImGui::Render();

    /* Clear to fully transparent */
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_context->ClearRenderTargetView(g_rtv, clearColor);

    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_swapChain->Present(1, 0);  /* VSync on */
}

HWND Overlay::GetWindow() {
    return g_hwnd;
}

void Overlay::MatchWindow(HWND target) {
    if (!target || !g_hwnd) return;

    RECT r;
    if (GetWindowRect(target, &r)) {
        int w = r.right - r.left;
        int h = r.bottom - r.top;
        SetWindowPos(g_hwnd, HWND_TOPMOST, r.left, r.top, w, h, SWP_NOACTIVATE);
    }
}

void Overlay::SetClickThrough(bool clickThrough) {
    if (!g_hwnd) return;

    LONG_PTR exStyle = GetWindowLongPtrA(g_hwnd, GWL_EXSTYLE);
    if (clickThrough) {
        exStyle |= WS_EX_TRANSPARENT;
    } else {
        exStyle &= ~WS_EX_TRANSPARENT;
    }
    SetWindowLongPtrA(g_hwnd, GWL_EXSTYLE, exStyle);
}
