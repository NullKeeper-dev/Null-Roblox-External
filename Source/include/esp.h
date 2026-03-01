#pragma once
/*
 * esp.h - ESP rendering (boxes, names, health bars)
 */

#include <Windows.h>

/* Settings exposed to the menu */
struct ESPSettings {
  bool enabled = false;
  bool showNames = true;
  bool showHealth = true;
  bool showDistance = true;
  bool showBox = true;
  bool showSnapline = false;

  float espCol[3] = {0.70f, 0.40f, 1.00f}; /* Purple */

  /* Visuals - Skeleton */
  bool showSkeleton = false;
  bool rainbowSkeleton = false;
  float skeletonCol[3] = {1.0f, 1.0f, 1.0f};

  // Visuals - Skeleton Glow
  bool showSkeletonGlow = false;
  bool rainbowSkeletonGlow = false;
  float skeletonGlowCol[3] = {1.0f, 0.40f, 0.40f};

  bool checkRagdoll = true;
  bool checkAlive = true;
  bool showDead = false;
  bool debugMode = false; // New: Debug mode toggle
  bool hideLauncher = true; // New: Option to hide launcher window

  /* Performance */
  bool showFps = true;
  int fpsCap = 144;

  bool rainbowEsp = false;
  bool teamCheck = true;
  int refreshKey = 0; // Manual cache refresh key
  float boxThickness = 1.0f;
  float healthWidth = 3.0f;
  float nameScale = 1.0f;
};

namespace ESP {
/* Get mutable settings reference */
ESPSettings &Settings();

/* Draw ESP for all players (call inside ImGui render) */
void Render();
} // namespace ESP
