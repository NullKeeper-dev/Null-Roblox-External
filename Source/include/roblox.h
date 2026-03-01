#pragma once
/*
 * roblox.h - Roblox data model traversal
 */

#include <Windows.h>
#include <cstdint>
#include <string>
#include <vector>

struct PlayerData {
  std::string name;
  float headPos[3]; /* x, y, z world position of head */
  float rootPos[3]; /* x, y, z world position of HumanoidRootPart */
  float health;
  float maxHealth;
  /* Expanded Bone Positions for Skeleton ESP */
  float lUpperArm[3], lLowerArm[3], lHand[3];
  float rUpperArm[3], rLowerArm[3], rHand[3];
  float lUpperLeg[3], lLowerLeg[3], lFoot[3];
  float rUpperLeg[3], rLowerLeg[3], rFoot[3];
  float lowerTorso[3], upperTorso[3];
  bool hasLimbs;
  bool isR15;
  bool isLocalPlayer;
  bool isRagdoll;
  bool isAlive;
  uintptr_t team;     /* Team Pointer */
  float dist;         /* Distance to local player */
  uintptr_t ptr;      /* Raw Player Pointer */
  uintptr_t humanoid; /* Cached Humanoid Pointer */
  bool valid;
};

namespace Roblox {

/* Initialize - find process, get base address */
bool Init();

/* Update all game data (call each frame) */
void Update();

/* ── Multithreaded Scanner ───────────────────────── */
void StartScanner();
void StopScanner();

/* ── Scanner ─────────────────────────────────────── */ void
ForceRefreshCache(); // Manual reset

/* Getters */
DWORD GetPID();
uintptr_t GetBase();
uintptr_t GetLocalPlayerTeam();
const float *GetViewMatrix(); /* 4x4 float matrix */
const std::vector<PlayerData> &GetPlayers();
int GetScreenWidth();
int GetScreenHeight();
HWND GetWindow();

std::string GetLocalPlayerName();
uintptr_t GetLastCharacter();
uintptr_t GetPlayerTeam(uintptr_t playerPtr);
} // namespace Roblox
