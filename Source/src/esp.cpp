/*
 * esp.cpp - ESP rendering with world-to-screen projection
 */

#include "../include/esp.h"
#include "../include/roblox.h"
#include "../include/console.h"
#include "../include/imgui.h"
#include "../include/imgui_internal.h"
#include "../include/aimbot.h"
#include "../include/driver.h"
#include "../Offsets/offsets.hpp"
#include "../include/menu.h"
#include "../include/overlay.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

static ESPSettings g_settings;

ESPSettings &ESP::Settings() { return g_settings; }

/* ── World to Screen using 4x4 ViewProjection matrix (column-major) ───── */

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

  float finalW = (screenW > 0) ? screenW : ImGui::GetIO().DisplaySize.x;
  float finalH = (screenH > 0) ? screenH : ImGui::GetIO().DisplaySize.y;

  out[0] = (finalW * 0.5f) * (1.0f + ndcX);
  out[1] = (finalH * 0.5f) * (1.0f - ndcY);

  static int debugCounter = 0;
  if (debugCounter < 10) {
      Console::Log("[Debug] W2S: W(%.1f,%.1f,%.1f) -> S(%.1f,%.1f) | w:%.2f | NDC(%.2f,%.2f) | res:%.0fx%.0f", 
                   pos[0], pos[1], pos[2], out[0], out[1], w, ndcX, ndcY, finalW, finalH);
      debugCounter++;
  }

  return (out[0] >= 0 && out[0] <= finalW && out[1] >= 0 && out[1] <= finalH);
}

/* ── Distance between two 3D points ──────────────────────────── */

static float Distance3D(const float a[3], const float b[3]) {
  float dx = a[0] - b[0];
  float dy = a[1] - b[1];
  float dz = a[2] - b[2];
  return sqrtf(dx * dx + dy * dy + dz * dz);
}

/* ── Get color from health percentage ────────────────────────── */

static ImU32 HealthColor(float health, float maxHealth) {
  if (maxHealth <= 0.0f)
    return IM_COL32(255, 255, 255, 255);
  float pct = health / maxHealth;
  if (pct > 1.0f)
    pct = 1.0f;
  if (pct < 0.0f)
    pct = 0.0f;

  /* Green to red gradient */
  int r = (int)((1.0f - pct) * 255.0f);
  int g = (int)(pct * 255.0f);
  return IM_COL32(r, g, 0, 255);
}

/* ── Draw Skeleton ───────────────────────────────────────────── */

static void DrawLine(ImDrawList *draw, const float p1[2], const float p2[2],
                     ImU32 col, float thick) {
  draw->AddLine(ImVec2(p1[0], p1[1]), ImVec2(p2[0], p2[1]), col, thick);
}

static void DrawSkeleton(ImDrawList *draw, const PlayerData &p, const float *vm,
                         float sw, float sh, ImU32 col, float thick = 1.0f) {
  auto drawBone = [&](const float a[3], const float b[3]) {
    float sa[2], sb[2];
    if (!WorldToScreen(a, vm, sw, sh, sa) || !WorldToScreen(b, vm, sw, sh, sb))
      return;
    draw->AddLine(ImVec2(sa[0], sa[1]), ImVec2(sb[0], sb[1]), col, thick);
  };

  if (p.isR15) {
    // Head
    float sHeadPos[2], sHeadTop[2];
    float headTopPos[3] = {p.headPos[0], p.headPos[1] + 0.9f, p.headPos[2]};
    if (WorldToScreen(p.headPos, vm, sw, sh, sHeadPos) && WorldToScreen(headTopPos, vm, sw, sh, sHeadTop)) {
        float headRadius = fabsf(sHeadPos[1] - sHeadTop[1]);
        draw->AddCircle(ImVec2(sHeadPos[0], sHeadPos[1]), headRadius, col, 12, thick);
    }

    // Spine
    drawBone(p.headPos, p.upperTorso);
    drawBone(p.upperTorso, p.lowerTorso);

    // Arms
    if (p.isR15) {
      drawBone(p.upperTorso, p.lUpperArm);
      drawBone(p.lUpperArm, p.lLowerArm);
      drawBone(p.lLowerArm, p.lHand);

      drawBone(p.upperTorso, p.rUpperArm);
      drawBone(p.rUpperArm, p.rLowerArm);
      drawBone(p.rLowerArm, p.rHand);
    } else {
      drawBone(p.rootPos, p.lHand);
      drawBone(p.rootPos, p.rHand);
    }

    // Legs
    if (p.isR15) {
      drawBone(p.lowerTorso, p.lUpperLeg);
      drawBone(p.lUpperLeg, p.lLowerLeg);
      drawBone(p.lLowerLeg, p.lFoot);

      drawBone(p.lowerTorso, p.rUpperLeg);
      drawBone(p.rUpperLeg, p.rLowerLeg);
      drawBone(p.rLowerLeg, p.rFoot);
    } else {
      drawBone(p.rootPos, p.lFoot);
      drawBone(p.rootPos, p.rFoot);
    }
  } else {
    /* R6 Skeleton */
    float sHeadPos[2], sHeadTop[2];
    float headTopPos[3] = {p.headPos[0], p.headPos[1] + 0.9f, p.headPos[2]};
    if (WorldToScreen(p.headPos, vm, sw, sh, sHeadPos) && WorldToScreen(headTopPos, vm, sw, sh, sHeadTop)) {
        float headRadius = fabsf(sHeadPos[1] - sHeadTop[1]);
        draw->AddCircle(ImVec2(sHeadPos[0], sHeadPos[1]), headRadius, col, 12, thick);
    }
    drawBone(p.headPos, p.rootPos);
    drawBone(p.rootPos, p.lHand);
    drawBone(p.rootPos, p.rHand);
    drawBone(p.rootPos, p.lFoot);
    drawBone(p.rootPos, p.rFoot);
  }
}

/* ── Render ──────────────────────────────────────────────────── */

void ESP::Render() {
  static float lastTime = 0.0f;
  static int frameCount = 0;
  static float currentFps = 0.0f;

  float now = (float)GetTickCount() * 0.001f;
  frameCount++;
  if (now - lastTime > 1.0f) {
    currentFps = (float)frameCount / (now - lastTime);
    frameCount = 0;
    lastTime = now;
  }

  if (g_settings.showFps) {
    char fpsBuf[32];
    snprintf(fpsBuf, sizeof(fpsBuf), "FPS: %.1f", currentFps);
    ImGui::GetForegroundDrawList()->AddText(
        ImVec2(10, 10), IM_COL32(255, 255, 255, 255), fpsBuf);
  }

  if (!g_settings.enabled)
    return;

  ImDrawList *draw = ImGui::GetBackgroundDrawList();
  if (!draw)
    return;

  ImVec2 screenRes = ImGui::GetIO().DisplaySize;
  float sw = screenRes.x;
  float sh = screenRes.y;

  ImU32 espColor = ImGui::GetColorU32(ImVec4(
      g_settings.espCol[0], g_settings.espCol[1], g_settings.espCol[2], 1.0f));

  if (g_settings.rainbowEsp) {
    float hue = (float)GetTickCount() * 0.0002f;
    hue = fmodf(hue, 1.0f);
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(hue, 0.7f, 0.8f, r, g, b);
    espColor = ImGui::GetColorU32(ImVec4(r, g, b, 1.0f));
  }

  const float *vm = Roblox::GetViewMatrix();
  const auto &players = Roblox::GetPlayers();

  /* ── Debug State ─────────────────────────────────────────── */
  static DWORD lastLog = 0;
  bool shouldLog = (GetTickCount() - lastLog > 5000);
  int passCount = 0;
  int worldFiltered = 0;
  int totalInCache = (int)players.size();

  /* We need local player position for distance calc */
  float localPos[3] = {0, 0, 0};
  uintptr_t localID = 0;

  for (auto &p : players) {
    if (p.isLocalPlayer) {
      memcpy(localPos, p.rootPos, sizeof(localPos));
      localID = p.ptr;
      break;
    }
  }

  /* Robust self-check fallback: if localID is 0, use camera subject's root part
   * distance logic if valid */
  if (localID == 0 && localPos[0] == 0.0f && localPos[1] == 0.0f &&
      localPos[2] == 0.0f) {
    if (!players.empty()) {
      for (auto &p : players) {
        if (p.ptr == Roblox::GetLastCharacter()) {
          memcpy(localPos, p.rootPos, sizeof(localPos));
          localID = p.ptr;
          break;
        }
      }
    }
  }

    for (auto &player : players) {
    if (player.isLocalPlayer || player.ptr == localID ||
        player.ptr == Roblox::GetLastCharacter())
      continue; /* Robust self-check */
    if (!player.valid)
      continue;

    /* Enforce alive and ragdoll checks */
    if (g_settings.checkAlive && !player.isAlive)
      continue;
    if (g_settings.checkRagdoll && player.isRagdoll)
      continue;

    if (g_settings.teamCheck) {
      uintptr_t localTeam = Roblox::GetLocalPlayerTeam();
      uintptr_t playerTeam = Roblox::GetPlayerTeam(player.ptr);
      if (playerTeam != 0 && playerTeam == localTeam)
        continue;
    }

    /* Filter garbage positions (0,0,0 or extreme values) */
    if (player.rootPos[0] == 0.0f && player.rootPos[1] == 0.0f &&
        player.rootPos[2] == 0.0f)
      continue;
    if (abs(player.rootPos[0]) > 500000.0f ||
        abs(player.rootPos[1]) > 500000.0f || abs(player.rootPos[2]) > 500000.0f)
      continue;

    /* Glow (renamed from Charms) */
    if (g_settings.showSkeletonGlow) {
      ImU32 glowCol = ImGui::GetColorU32(ImVec4(
          g_settings.skeletonGlowCol[0], g_settings.skeletonGlowCol[1],
          g_settings.skeletonGlowCol[2], 0.3f));
      if (g_settings.rainbowSkeletonGlow) {
        float hue = (float)GetTickCount() * 0.0002f;
        float r, g, b;
        ImGui::ColorConvertHSVtoRGB(fmodf(hue, 1.0f), 0.7f, 0.8f, r, g, b);
        glowCol = IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), 80);
      }
      DrawSkeleton(draw, player, vm, sw, sh, glowCol, 4.0f);
    }

    float dist = player.dist;
    if (dist > 50000.0f) { // Ignore distance filter for debugging
      // worldFiltered++;
      // continue;
    }

    /* Select Color (Global) */
    ImU32 color = espColor;

    float boxX, boxY, boxW, boxH;
    float headScreen[2], feetScreen[2];

    bool headValid = WorldToScreen(player.headPos, vm, sw, sh, headScreen);
    bool rootValid = WorldToScreen(player.rootPos, vm, sw, sh, feetScreen);

    if (player.hasLimbs) {
      /* ── Dynamic Box from Limbs (Wide Model Support) ─────── */
      float screenPoints[6][2];
      bool validPoints[6];
      int validCount = 0;

      const float *worldPoints[6] = {player.headPos, player.rootPos,
                                     player.lFoot,   player.rFoot,
                                     player.lHand,   player.rHand};

      /* Project all available points */
      for (int i = 0; i < 6; i++) {
        float wp[3] = {worldPoints[i][0], worldPoints[i][1], worldPoints[i][2]};
        if (i == 0)
          wp[1] += 0.8f; // Head top padding
        if (i == 2 || i == 3)
          wp[1] -= 0.5f; // Feet bottom padding

        validPoints[i] = WorldToScreen(wp, vm, sw, sh, screenPoints[i]);
        if (validPoints[i])
          validCount++;
      }

      /* ── Vertical Adjustment Fix ─────────────────────────────── */
      /* 
         The head position in Roblox is often at the center of the Head part.
         Adding an offset to the projected points to ensure the box covers the full height.
      */
      float headTop[3] = {player.headPos[0], player.headPos[1] + 1.2f, player.headPos[2]};
      float feetBottom[3] = {player.rootPos[0], player.rootPos[1] - 3.2f, player.rootPos[2]};
      float sHead[2], sFeet[2];

      bool hV = WorldToScreen(headTop, vm, sw, sh, sHead);
      bool fV = WorldToScreen(feetBottom, vm, sw, sh, sFeet);

      if (hV && fV) {
          boxH = sFeet[1] - sHead[1];
          boxW = boxH * 0.6f;
          boxX = sHead[0] - boxW * 0.5f;
          boxY = sHead[1];
          
          // Update markers for text/snaplines
          headScreen[0] = sHead[0];
          headScreen[1] = sHead[1];
          feetScreen[0] = sFeet[0];
          feetScreen[1] = sFeet[1];
      } else {
          continue;
      }
    } else {
      /* ── Fallback: Static Height-Width ───────────────────── */
      /* For obfuscated PF rigs where head/root may be same part or very close,
         estimate a full character height: ~5.5 studs total */
      float feetPos[3] = {player.rootPos[0], player.rootPos[1] - 3.0f,
                          player.rootPos[2]};
      float headTopPos[3] = {player.rootPos[0], player.rootPos[1] + 2.5f,
                             player.rootPos[2]};

      if (!WorldToScreen(headTopPos, vm, sw, sh, headScreen))
        continue;
      if (!WorldToScreen(feetPos, vm, sw, sh, feetScreen))
        continue;

      boxH = feetScreen[1] - headScreen[1];
      if (abs(boxH) < 1.0f)
        continue; /* Too small or glitch */

      boxW = boxH * 0.55f;
      boxX = headScreen[0] - boxW * 0.5f;
      boxY = headScreen[1];
    }

    /* Sanity check box dimensions to prevent full screen bugs (stretching) */
    if (boxW > sw * 0.9f || boxH > sh * 0.9f || boxW < 0.1f || boxH < 0.1f)
      continue;
    if (boxX < -sw || boxX > sw * 2 || boxY < -sh || boxY > sh * 2)
      continue;

    /* ── Bounding Box ──────────────────────────────────────── */
    if (g_settings.showBox) {
      /* Outline */
      draw->AddRect(
          ImVec2(boxX - 1, boxY - 1), ImVec2(boxX + boxW + 1, boxY + boxH + 1),
          IM_COL32(0, 0, 0, 200), 0.0f, 0, g_settings.boxThickness + 1.0f);
      /* Main box */
      draw->AddRect(ImVec2(boxX, boxY), ImVec2(boxX + boxW, boxY + boxH), color,
                    0.0f, 0, g_settings.boxThickness);
    }

    /* ── Snapline ──────────────────────────────────────────── */
    if (g_settings.showSnapline) {
      draw->AddLine(ImVec2(sw * 0.5f, sh), ImVec2(headScreen[0], feetScreen[1]),
                    color, 1.0f);
    }

    /* ── Player Name ───────────────────────────────────────── */
    if (g_settings.showNames && !player.name.empty()) {
      float scaledFontSize = ImGui::GetFontSize() * g_settings.nameScale;
      ImVec2 textSize = ImGui::CalcTextSize(player.name.c_str());
      textSize.x *= g_settings.nameScale;
      textSize.y *= g_settings.nameScale;

      float textX = headScreen[0] - textSize.x * 0.5f;
      float textY = boxY - textSize.y - 4.0f;

      /* Shadow */
      draw->AddText(ImGui::GetFont(), scaledFontSize,
                    ImVec2(textX + 1, textY + 1), IM_COL32(0, 0, 0, 200),
                    player.name.c_str());
      draw->AddText(ImGui::GetFont(), scaledFontSize, ImVec2(textX, textY),
                    color, player.name.c_str());
    }

    /* ── Health Bar ────────────────────────────────────────── */
    if (g_settings.showHealth) {
      float barW = g_settings.healthWidth;
      float barX = boxX - barW - 3.0f;
      
      // Real-time health update: Re-read health from humanoid if pointer is valid
      float currentHealth = player.health;
      float currentMaxHealth = player.maxHealth;
      
      if (player.humanoid) {
          currentHealth = Driver::Read<float>(Roblox::GetPID(), player.humanoid + Offsets::Humanoid::Health);
          currentMaxHealth = Driver::Read<float>(Roblox::GetPID(), player.humanoid + Offsets::Humanoid::MaxHealth);
      }

      // Ensure we don't divide by zero and clamp the percentage
      float maxH = (currentMaxHealth > 0.1f) ? currentMaxHealth : 100.0f;
      float healthPct = currentHealth / maxH;
      
      if (healthPct > 1.0f) healthPct = 1.0f;
      if (healthPct < 0.0f) healthPct = 0.0f;

      float filledH = boxH * healthPct;

      /* Background */
      draw->AddRectFilled(ImVec2(barX, boxY), ImVec2(barX + barW, boxY + boxH),
                          IM_COL32(0, 0, 0, 180));
      /* Health fill (from bottom up) */
      draw->AddRectFilled(ImVec2(barX, boxY + boxH - filledH),
                          ImVec2(barX + barW, boxY + boxH),
                          HealthColor(currentHealth, maxH));
      /* Outline */
      draw->AddRect(ImVec2(barX, boxY), ImVec2(barX + barW, boxY + boxH),
                    IM_COL32(0, 0, 0, 255));
    }

    /* ── Distance ──────────────────────────────────────────── */
    if (g_settings.showDistance) {
      char distText[32];
      snprintf(distText, sizeof(distText), "[%.0fm]", dist);
      ImVec2 textSize = ImGui::CalcTextSize(distText);
      float textX = headScreen[0] - textSize.x * 0.5f;
      float textY = boxY + boxH + 2.0f;

      draw->AddText(ImVec2(textX + 1, textY + 1), IM_COL32(0, 0, 0, 200),
                    distText);
      draw->AddText(ImVec2(textX, textY), color, distText);
    }

    /* ── Skeleton ──────────────────────────────────────────── */
    if (g_settings.showSkeleton) {
      ImU32 skelCol = ImGui::GetColorU32(
          ImVec4(g_settings.skeletonCol[0], g_settings.skeletonCol[1],
                 g_settings.skeletonCol[2], 1.0f));
      if (g_settings.rainbowSkeleton) {
        float hue = (float)GetTickCount() * 0.0002f;
        hue = fmodf(hue, 1.0f);
        float r, g, b;
        ImGui::ColorConvertHSVtoRGB(hue, 0.7f, 0.8f, r, g, b);
        skelCol = IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), 255);
      }
      
      /* Ensure skeleton head starts from the actual head center, not the adjusted top */
      DrawSkeleton(draw, player, vm, sw, sh, skelCol);
    }

    /* ── Skeleton Glow ─────────────────────────────────────── */
    if (g_settings.showSkeletonGlow) {
      ImU32 glowCol = ImGui::GetColorU32(
          ImVec4(g_settings.skeletonGlowCol[0], g_settings.skeletonGlowCol[1],
                 g_settings.skeletonGlowCol[2], 0.3f));
      if (g_settings.rainbowSkeletonGlow) {
        float hue = (float)GetTickCount() * 0.0002f;
        hue = fmodf(hue, 1.0f);
        float r, g, b;
        ImGui::ColorConvertHSVtoRGB(hue, 0.7f, 0.8f, r, g, b);
        glowCol =
            IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), 80);
      }
      
      DrawSkeleton(draw, player, vm, sw, sh, glowCol, 4.0f);
    }
    passCount++;
  }

  if (shouldLog) {
    int totalValid = 0;
    int isSelf = 0;
    int isDead = 0;
    int isOutOfRange = 0;
    int screenFiltered = 0;

    for (auto& p : players) {
        if (!p.valid) { totalValid++; continue; }
        if (p.isLocalPlayer || p.ptr == localID || p.ptr == Roblox::GetLastCharacter()) { isSelf++; continue; }
        if (!p.isAlive || p.health <= 0.1f) { isDead++; continue; }
        
        float head[2];
        if (!WorldToScreen(p.headPos, vm, sw, sh, head)) { screenFiltered++; continue; }
    }

    Console::Log("[ESP] Render: %d cache | %d pass | %d valid_fail | %d self | %d dead | %d w2s_fail",
                 totalInCache, passCount, totalValid, isSelf, isDead, screenFiltered);
    lastLog = GetTickCount();
  }
  /* ── Aimbot FOV Circle ─────────────────────────────────── */
  if (Aimbot::settings.enabled && Aimbot::settings.drawFov) {
    POINT cursor;
    GetCursorPos(&cursor);
    ScreenToClient(Overlay::GetWindow(), &cursor);

    ImU32 fovColor = ImGui::GetColorU32(
        ImVec4(Aimbot::settings.fovCol[0], Aimbot::settings.fovCol[1],
               Aimbot::settings.fovCol[2], 1.0f));
    if (Aimbot::settings.rainbowFov) {
      float hue = (float)GetTickCount() * 0.0002f;
      hue = fmodf(hue, 1.0f);
      float r, g, b;
      ImGui::ColorConvertHSVtoRGB(hue, 0.7f, 0.8f, r, g, b);
      fovColor = ImGui::GetColorU32(ImVec4(r, g, b, 1.0f));
    }

    draw->AddCircle(ImVec2((float)cursor.x, (float)cursor.y),
                    Aimbot::settings.fov, fovColor, 64, 1.0f);
  }
}
