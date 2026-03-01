#include "../include/aimbot.h"
#include "../include/roblox.h"
#include "../include/driver.h"
#include "../include/console.h"
#include "../Offsets/offsets.hpp"
#include <vector>
#include <algorithm>

/* UPDATED BY ANTIGRAVITY - AIMBOT LOGIC WITH FOV CLAMP & PRIORITIZATION */

namespace Aimbot {

Settings settings;

/* Sticky Target State */
static uintptr_t lockedPlayerPtr = 0;
static float remainderX = 0.0f;
static float remainderY = 0.0f;

/* ── Helper: Get 2D distance ────────────────────────────────── */
static float GetDistance2D(float x1, float y1, float x2, float y2) {
  return sqrtf(powf(x2 - x1, 2) + powf(y2 - y1, 2));
}

/* ── Helper: WorldToScreen (column-major, same as ESP) ─────────── */
static bool WorldToScreen(const float pos[3], const float *vm, float screenW,
                          float screenH, float out[2]) {
  /* Matrix-Transpose * Vector (Column-Major Matrix in Memory) */
  float x = vm[0] * pos[0] + vm[1] * pos[1] + vm[2] * pos[2] + vm[3];
  float y = vm[4] * pos[0] + vm[5] * pos[1] + vm[6] * pos[2] + vm[7];
  float w = vm[12] * pos[0] + vm[13] * pos[1] + vm[14] * pos[2] + vm[15];

  if (w < 0.01f)
    return false;

  float invW = 1.0f / w;
  float ndcX = x * invW;
  float ndcY = y * invW;

  out[0] = (screenW * 0.5f) * (1.0f + ndcX);
  out[1] = (screenH * 0.5f) * (1.0f - ndcY);

  return true;
}

/* ── Helper: Direct Mouse Move (SendInput) ───────────────────── */
static void MoveMouse(float dx, float dy) {
  INPUT input = {0};
  input.type = INPUT_MOUSE;
  input.mi.dwFlags = MOUSEEVENTF_MOVE;
  input.mi.dx = (LONG)dx;
  input.mi.dy = (LONG)dy;
  SendInput(1, &input, sizeof(INPUT));
}

const char *GetKeyName(int vkey) {
  static char name[32];
  if (vkey >= 0x01 && vkey <= 0x06) {
    switch (vkey) {
    case VK_LBUTTON:
      return "Left Click";
    case VK_RBUTTON:
      return "Right Click";
    case VK_MBUTTON:
      return "Middle Click";
    case VK_XBUTTON1:
      return "Mouse 4";
    case VK_XBUTTON2:
      return "Mouse 5";
    }
  }

  UINT scanCode = MapVirtualKeyA(vkey, MAPVK_VK_TO_VSC);
  if (GetKeyNameTextA(scanCode << 16, name, sizeof(name))) {
    return name;
  }

  sprintf_s(name, "Key 0x%X", vkey);
  return name;
}

struct TargetInfo {
  const PlayerData *player;
  float screenX;
  float screenY;
  float crosshairDist;
};

void Update() {
  if (!settings.enabled) {
    lockedPlayerPtr = 0;
    return;
  }

  if (!(GetAsyncKeyState(settings.key) & 0x8000)) {
    lockedPlayerPtr = 0;
    remainderX = 0;
    remainderY = 0;
    return;
  }

  /* Get necessary data */
  const auto &players = Roblox::GetPlayers();
  const float *vm = Roblox::GetViewMatrix();
  float sw = (float)Roblox::GetScreenWidth();
  float sh = (float)Roblox::GetScreenHeight();

  if (sw == 0 || sh == 0)
    return;

  POINT cursor;
  GetCursorPos(&cursor);
  ScreenToClient(Roblox::GetWindow(), &cursor);

  const PlayerData *target = nullptr;
  float targetScreen[2] = {0, 0};

  /* 1. Target Selection / Locking */
  if (lockedPlayerPtr != 0) {
    for (const auto &plr : players) {
      if (plr.ptr == lockedPlayerPtr && plr.valid && plr.health > 0) {
        if (settings.teamCheck) {
          uintptr_t localTeam = Roblox::GetLocalPlayerTeam();
          if (plr.team != 0 && plr.team == localTeam) {
            lockedPlayerPtr = 0;
            break;
          }
        }

        if (plr.isLocalPlayer || (plr.ptr == Roblox::GetLastCharacter() &&
                                  Roblox::GetLastCharacter() != 0)) {
          lockedPlayerPtr = 0;
          break;
        }

        /* Check if still in FOV (FOV Clamp) */
        float targetPos[3] = {0, 0, 0};
        if (settings.part == TargetPart::Head)
          memcpy(targetPos, plr.headPos, 12);
        else if (settings.part == TargetPart::Torso)
          memcpy(targetPos, plr.upperTorso, 12);
        else if (settings.part == TargetPart::Legs)
          memcpy(targetPos, plr.lFoot, 12);
        else
          memcpy(targetPos, plr.rootPos, 12);

        /* Apply manual height offset (vertical adjustment) */
        targetPos[1] += settings.heightOffset;

        float screen[2];
        if (WorldToScreen(targetPos, vm, sw, sh, screen)) {
          float dist = GetDistance2D((float)cursor.x, (float)cursor.y,
                                     screen[0], screen[1]);
          if (dist <= settings.fov) {
            target = &plr;
            targetScreen[0] = screen[0];
            targetScreen[1] = screen[1];
          } else {
            lockedPlayerPtr = 0; /* Target left FOV radius */
          }
        } else {
          lockedPlayerPtr = 0; /* Target went off-screen */
        }
        break;
      }
    }
    if (!target)
      lockedPlayerPtr = 0;
  }

  if (!target) {
    std::vector<TargetInfo> potentialTargets;

    for (const auto &plr : players) {
      if (plr.isLocalPlayer || !plr.valid || plr.health <= 0)
        continue;
      if (plr.ptr == Roblox::GetLastCharacter() &&
          Roblox::GetLastCharacter() != 0)
        continue;

      if (settings.teamCheck) {
        uintptr_t localTeam = Roblox::GetLocalPlayerTeam();
        if (plr.team != 0 && plr.team == localTeam)
          continue;
      }

      float targetPos[3] = {0, 0, 0};
      if (settings.part == TargetPart::Head)
        memcpy(targetPos, plr.headPos, 12);
      else if (settings.part == TargetPart::Torso)
        memcpy(targetPos, plr.upperTorso, 12);
      else if (settings.part == TargetPart::Legs)
        memcpy(targetPos, plr.lFoot, 12);
      else
        memcpy(targetPos, plr.rootPos, 12);

      /* Apply manual height offset (vertical adjustment) */
      targetPos[1] += settings.heightOffset;

      float screen[2];
      if (WorldToScreen(targetPos, vm, sw, sh, screen)) {
        float dist = GetDistance2D((float)cursor.x, (float)cursor.y, screen[0],
                                   screen[1]);
        if (dist <= settings.fov) {
          potentialTargets.push_back({&plr, screen[0], screen[1], dist});
        }
      }
    }

    if (!potentialTargets.empty()) {
      /* Sort by crosshair distance (Prioritization) */
      std::sort(potentialTargets.begin(), potentialTargets.end(),
                [](const TargetInfo &a, const TargetInfo &b) {
                  return a.crosshairDist < b.crosshairDist;
                });

      /* Pick the best (closest to center) */
      const auto &best = potentialTargets[0];
      target = best.player;
      lockedPlayerPtr = best.player->ptr;
      targetScreen[0] = best.screenX;
      targetScreen[1] = best.screenY;

      /* Note: Secondary closest is potentialTargets[1] if size > 1 */
    }
  }

  /* 2. Execute Aim Move */
  if (target) {
    float dx = targetScreen[0] - cursor.x;
    float dy = targetScreen[1] - cursor.y;

    float smooth = (settings.smooth < 1.0f) ? 1.0f : settings.smooth;

    float moveX = (dx / smooth) + remainderX;
    float moveY = (dy / smooth) + remainderY;

    int actualX = (int)moveX;
    int actualY = (int)moveY;

    remainderX = moveX - (float)actualX;
    remainderY = moveY - (float)actualY;

    if (actualX != 0 || actualY != 0) {
      MoveMouse((float)actualX, (float)actualY);
    }
  }
}
} // namespace Aimbot
