#pragma once

/* UPDATED BY ANTIGRAVITY - AIMBOT SETTINGS */
#include <Windows.h>

enum class TargetPart {
  Head = 0,
  Torso = 1, /* RootPart */
  Legs = 2   /* Feet/Legs */
};

namespace Aimbot {

struct Settings {
  bool enabled = false;
  int key = VK_RBUTTON; /* Default Right Click */
  float fov = 150.0f;   /* FOV Circle Radius */
  float smooth = 5.0f;  /* Smoothing factor (1.0 = instant, higher = slower) */
  bool teamCheck = true;
  TargetPart part = TargetPart::Head;
  bool drawFov = true;
  float fovCol[3] = {1.0f, 1.0f, 1.0f};
  bool rainbowFov = false;
  float heightOffset = 0.0f; /* Vertical offset Adjustment */
  bool checkAlive = true;
  bool checkRagdoll = false;
};

extern Settings settings;

void Update(); /* Called every frame */
const char *GetKeyName(int vkey);
} // namespace Aimbot
