/*
 * main.cpp - Entry point for Roblox External
 *
 * Flow:
 *   1. Init debug console
 *   2. Init driver communication (Null Driver)
 *   3. Attach to Roblox process
 *   4. Create transparent DX11 overlay
 *   5. Main render loop (menu + ESP)
 */

#include "../include/imgui.h"
#include "../include/aimbot.h"
#include "../include/console.h"
#include "../include/overlay.h"
#include "../include/menu.h"
#include "../include/esp.h"
#include "../include/aimbot.h"
#include "../include/roblox.h"
#include "../include/console.h"
#include "../include/driver.h"

#include <Windows.h>
#include <algorithm>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
  (void)hInstance;
  (void)hPrevInstance;
  (void)lpCmdLine;
  (void)nCmdShow;

  /* ── 1. Init Debug Console ───────────────────────────────── */
  Console::Init();
  Console::Log("Starting Roblox External...");

  /* ── 2. Init Driver Communication ────────────────────────── */
  Console::Log("Connecting to driver...");
  if (!Driver::Init()) {
    Console::Error(
        "Failed to connect to Null Driver. Make sure the driver is loaded.");
    return 1;
  }

  /* ── 3. Attach to Roblox ─────────────────────────────────── */
  Console::Log("Looking for Roblox process...");

  /* Wait for Roblox to start if not running yet */
  int retries = 0;
  while (!Roblox::Init()) {
    retries++;
    if (retries > 30) {
      Console::Error("Roblox not found after 30 retries. Exiting.");
      return 1;
    }
    Console::Warn("Waiting for Roblox... (attempt %d/30)", retries);
    Sleep(2000);
  }

  /* ── 4. Create Overlay ───────────────────────────────────── */
  Console::Log("Creating overlay...");
  if (!Overlay::Init()) {
    Console::Error("Failed to create overlay window");
    return 1;
  }

  /* Start Background Scanner */
  Roblox::StartScanner();

  /* Initialize Menu (Load default config) */
  Menu::Initialize();

  /* Apply menu theme */
  Menu::ApplyTheme();
  Console::Success("Overlay ready!");
  Console::Log("Press INSERT to toggle menu");
  Console::Log("Press END to exit");

  /* ── 5. Main Loop ────────────────────────────────────────── */
  bool running = true;
  LARGE_INTEGER freq, t1, t2;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&t1);

  while (running) {
    /* Update Roblox data - Called every frame now for smooth ESP/Aimbot */
    Roblox::Update();

    /* FPS Cap Logic - Removed pacing logic, running free */
    t1 = t2;

    /* Telemetry Log (1Hz) */
    static DWORD lastPerfLog = 0;
    DWORD now_perf = GetTickCount();
    if (now_perf - lastPerfLog > 1000) {
        lastPerfLog = now_perf;
        float fps = ImGui::GetIO().Framerate;
        double msPerFrame = (fps > 0.0f) ? (1000.0 / fps) : 0.0;
        Console::Log("[Perf] FPS = %.1f (%.2f ms/frame)",
                     fps, msPerFrame);
    }

    /* Check hotkeys */
    if (GetAsyncKeyState(Menu::GetToggleKey()) & 1) {
      Menu::Toggle();
      Overlay::SetClickThrough(!Menu::IsVisible());
      Console::Log("Menu %s", Menu::IsVisible() ? "shown" : "hidden");
    }

    /* Check Self Destruct hotkey */
    if (g_selfDestructKey != 0 && (GetAsyncKeyState(g_selfDestructKey) & 1)) {
      running = false;
      break;
    }

    /* Check if Roblox is still running */
    HANDLE hCheck =
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, Roblox::GetPID());
    if (!hCheck) {
      Console::Warn("Roblox process closed. Exiting.");
      break;
    }
    CloseHandle(hCheck);

    /* Match overlay to Roblox window and handle window filtering */
    HWND robloxWnd = Roblox::GetWindow();
    HWND overlayWnd = Overlay::GetWindow();
    HWND foregroundWnd = GetForegroundWindow();

    if (robloxWnd) {
      Overlay::MatchWindow(robloxWnd);

      /* Use PID-based focus check for robustness */
      DWORD foregroundPid = 0;
      GetWindowThreadProcessId(foregroundWnd, &foregroundPid);

      DWORD robloxPid = Roblox::GetPID();
      DWORD currentPid = GetCurrentProcessId();

      if (foregroundPid == robloxPid || foregroundPid == currentPid) {
        ShowWindow(overlayWnd, SW_SHOW);
      } else {
        ShowWindow(overlayWnd, SW_HIDE);
        Sleep(100);
        continue;
      }
    }

    /* Update Roblox data */
    // Roblox::Update(); // Already called at start of loop to prevent lag

    /* Begin ImGui frame */
    if (!Overlay::BeginFrame())
      break;

    /* Render menu */
    Menu::Render();

    /* Render ESP */
    ESP::Render();

    /* Update Aimbot */
    Aimbot::Update();

    /* End frame */
    Overlay::EndFrame();
  }

  /* ── Cleanup ─────────────────────────────────────────────── */
  Console::Log("Shutting down...");
  Overlay::Shutdown();
  FreeConsole();

  return 0;
}
