/*
 * roblox.cpp - Roblox data model traversal
 *
 * Traverses: DataModel -> Players -> Characters -> HumanoidRootPart/Head
 * Also reads the Camera ViewMatrix for world-to-screen projection.
 */

#include "../include/roblox.h"
#include "../Offsets/offsets.hpp"
#include "../include/console.h"
#include "../include/driver.h"
#include <TlHelp32.h>
#include <atomic>
#include <cctype> /* isdigit */
#include <cmath>
#include <map>
#include <mutex>
#include <thread>

static std::atomic<bool> g_scannerRunning(false);
static std::thread g_scannerThread;
static std::recursive_mutex g_playerListMutex; // Protects g_playerCache

static DWORD g_pid = 0;
static uintptr_t g_base = 0;
static HWND g_hwnd = NULL;
static float g_viewMatrix[16] = {};
static std::vector<PlayerData> g_players;
static int g_screenW = 0;
static int g_screenH = 0;

/* ── Globals for Game State & Caching ──────────────────────────── */
static uintptr_t g_lastLocalPlayer = 0;
static uintptr_t g_localHumanoid = 0;
static uintptr_t g_lastCharacter = 0;
static uintptr_t g_currentDataModel = 0;
static uintptr_t g_playersService = 0;
static uintptr_t g_workspaceService = 0;
static uintptr_t g_currentCamera = 0;
static DWORD g_lastPlayerUpdate = 0;
static DWORD g_lastEntityUpdate = 0;
static std::map<long long, std::string> g_idToName;

/* ── Helper: Find Roblox PID ───────────────────────────────────── */

static DWORD FindRobloxPID() {
  DWORD pid = 0;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE)
    return 0;

  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);

  if (Process32FirstW(snap, &pe)) {
    do {
      if (_wcsicmp(pe.szExeFile, L"RobloxPlayerBeta.exe") == 0) {
        pid = pe.th32ProcessID;
        break;
      }
    } while (Process32NextW(snap, &pe));
  }

  CloseHandle(snap);
  return pid;
}

/* ── Helper: Find Roblox Window ────────────────────────────────── */

struct EnumData {
  DWORD pid;
  HWND result;
};

static BOOL CALLBACK EnumWindowProc(HWND hwnd, LPARAM lp) {
  EnumData *data = (EnumData *)lp;
  DWORD wndPid = 0;
  GetWindowThreadProcessId(hwnd, &wndPid);

  if (wndPid == data->pid && IsWindowVisible(hwnd)) {
    char title[256] = {};
    GetWindowTextA(hwnd, title, 256);
    if (strlen(title) > 0) {
      data->result = hwnd;
      return FALSE;
    }
  }
  return TRUE;
}

static HWND FindRobloxWindow(DWORD pid) {
  EnumData data{pid, NULL};
  EnumWindows(EnumWindowProc, (LPARAM)&data);
  return data.result;
}

/* ── Helper: Get class name ──────────────────────────────────────── */

static std::string GetRobloxClassName(DWORD pid, uintptr_t instance) {
  if (!instance)
    return "";

  uintptr_t classDesc =
      Driver::Read<uintptr_t>(pid, instance + Offsets::Instance::ClassDescriptor);
  if (!classDesc)
    return "";

  /*
     ClassDescriptor + 0x8 contains a POINTER to the string struct.
     We must dereference it first.
  */
  uintptr_t stringPtr = Driver::Read<uintptr_t>(
      pid, classDesc + Offsets::Instance::ClassName);
  if (!stringPtr)
    return "";

  return Driver::ReadRobloxString(pid, stringPtr, 0);
}

/* ── Helper: Get position from a Part (CFrame) ────────────────── */

static bool GetPartPosition(DWORD pid, uintptr_t part, float outPos[3]) {
  if (!part)
    return false;

  uintptr_t primitive = Driver::Read<uintptr_t>(pid, part + Offsets::BasePart::Primitive);
  if (!primitive)
    return false;

  /* CFrame at Primitive + CFrame offset. Position is at CFrame + 0x0C*3 = the
   * translation part */
  /* CFrame layout: 3x3 rotation matrix (9 floats) then 3 position floats */
  /* So position is at CFrame + 36 bytes (9 * 4) = offset 0x24 from CFrame start
   * */
  /* Actually in Roblox, the CFrame stores rotation 3x3 then position xyz */

  return Driver::ReadRaw(pid, primitive + Offsets::Primitive::Position, outPos, sizeof(float) * 3);
}

/* ── Helper: Get children list ─────────────────────────────────── */

static std::vector<uintptr_t> GetChildren(DWORD pid, uintptr_t parent) {
  std::vector<uintptr_t> result;
  if (!parent)
    return result;

  /*
     Roblox Children Vector Structure:
     instance + 0x70 -> Pointer to [begin, end, cap]
     Each entry is 16 bytes (shared_ptr): [instance_ptr, control_block_ptr]
  */
  uintptr_t vectorStruct =
      Driver::Read<uintptr_t>(pid, parent + Offsets::Instance::ChildrenStart);
  if (!vectorStruct)
    return result;

  uintptr_t begin = Driver::Read<uintptr_t>(pid, vectorStruct + 0x0);
  uintptr_t end = Driver::Read<uintptr_t>(pid, vectorStruct + Offsets::Instance::ChildrenEnd);

  if (!begin || !end || end <= begin)
    return result;

  size_t bytes = end - begin;
  size_t count = bytes / 16;

  if (count > 10000)
    count = 10000; // Cap at 10k to prevent crazy memory usage
  if (count == 0)
    return result;

  /* Bulk Read Optimization */
  std::vector<unsigned char> buffer(count * 16);
  if (Driver::ReadRaw(pid, begin, buffer.data(), buffer.size())) {
    for (size_t i = 0; i < count; i++) {
      // Child pointer is at offset 0 of each 16-byte chunk
      uintptr_t child = *reinterpret_cast<uintptr_t *>(&buffer[i * 16]);
      if (child)
        result.push_back(child);
    }
  }

  return result;
}

static uintptr_t FindChildByClass(DWORD pid, uintptr_t parent,
                                  const std::string &className) {
  auto children = GetChildren(pid, parent);
  // Debug log only for rare events or first run
  static int logCount = 0;
  bool debug =
      (logCount < 50 && (className == "Players" || className == "Workspace"));

  if (debug)
    Console::Log("FindChildByClass: Searching for '%s' in %llX (Children: %d)",
                 className.c_str(), parent, (int)children.size());

  for (auto &child : children) {
    std::string cls = GetRobloxClassName(pid, child);
    if (debug && logCount < 50) {
      Console::Log("  > Arg: %llX | Class: '%s' vs '%s'", child, cls.c_str(),
                   className.c_str());
    }
    if (cls == className) {
      if (debug) {
        Console::Success("  > MATCH! Found %llX", child);
        logCount++;
      }
      return child;
    }
  }
  if (debug)
    logCount++;
  return 0;
}

static uintptr_t FindChildByName(DWORD pid, uintptr_t parent,
                                 const std::string &name) {
  auto children = GetChildren(pid, parent);
  for (auto &child : children) {
    // Indirect Name Scan: Ptr -> String
    uintptr_t namePtr = Driver::Read<uintptr_t>(pid, child + Offsets::Instance::Name);
    if (namePtr) {
      std::string s = Driver::ReadRobloxString(pid, namePtr, 0);
      if (s == name)
        return child;
    }
  }
  return 0;
}

/* ── Public API ──────────────────────────────────────────────────── */

/* ── Performance Caching Structs ─────────────────────────────── */

struct CachedPart {
  uintptr_t ptr;
  uintptr_t primitive; /* Optimization: Cache Primitive to skip 1 read level */
};

struct CachedPlayer {
  uintptr_t ptr;
  std::string name;
  uintptr_t character; /* Current ModelInstance */
  uintptr_t humanoid;  /* Cached Humanoid Instance */

  /* Cached Parts (Valid if character != 0) */
  CachedPart rootPart, head;
  CachedPart lUpperArm, lLowerArm, lHand;
  CachedPart rUpperArm, rLowerArm, rHand;
  CachedPart lUpperLeg, lLowerLeg, lFoot;
  CachedPart rUpperLeg, rLowerLeg, rFoot;
  CachedPart lowerTorso, upperTorso;

  bool hasLimbs;
  bool isR15;
  bool isLocalPlayer;
  bool isRagdoll; /* New: Check if character is ragdolled/fallen */
  bool isAlive;   /* New: Improved health-based alive check */
  uintptr_t team; /* Team Pointer */
  bool valid;
  bool isEntity; /* True if found via Workspace scan (not in Players service) */
};

static std::vector<CachedPlayer> g_playerCache;
static DWORD g_lastCacheUpdate = 0;
static DWORD g_lastDebugUpdate = 0;
static bool g_debugEnabled =
    true; // Enable debug by default for troubleshooting

/* ── Helper: Get Position from Cached Part ───────────────────── */

static bool GetPartPositionCached(DWORD pid, const CachedPart &part,
                                  float outPos[3]) {
  if (!part.primitive)
    return false;

  return Driver::ReadRaw(pid, part.primitive + Offsets::Primitive::Position, outPos, sizeof(float) * 3);
}

/* ── Helper: Cache a Part ────────────────────────────────────── */

static CachedPart CachePart(DWORD pid, uintptr_t partPtr) {
  CachedPart cp = {0, 0};
  if (partPtr) {
    cp.ptr = partPtr;
    cp.primitive = Driver::Read<uintptr_t>(pid, partPtr + Offsets::BasePart::Primitive);
  }
  return cp;
}

/* ── Public API ──────────────────────────────────────────────── */

bool Roblox::Init() {
  g_pid = FindRobloxPID();
  if (!g_pid) {
    Console::Error("Roblox process not found");
    return false;
  }
  Console::Success("Found Roblox PID: %d", g_pid);

  g_base = Driver::GetModuleBase(g_pid, L"RobloxPlayerBeta.exe");
  if (!g_base) {
    Console::Error("Failed to get Roblox module base");
    return false;
  }
  Console::Success("Roblox base: 0x%llX", g_base);

  g_hwnd = FindRobloxWindow(g_pid);
  if (!g_hwnd) {
    Console::Warn("Roblox window not found yet");
  } else {
    Console::Success("Roblox window found: 0x%p", g_hwnd);
  }

  return true;
}

/* ── Update Logic ────────────────────────────────────────────── */

static std::string g_localPlayerName = "";

void Roblox::Update() {
  if (!g_pid || !g_base)
    return;

  static DWORD lastUpdateLog = 0;
  DWORD now_log = GetTickCount();
  static int updateCalls = 0;
  updateCalls++;

  if (now_log - lastUpdateLog > 1000) {
      lastUpdateLog = now_log;
      Console::Log("[Perf] Update/sec=%d Players=%zu Cache=%zu",
                   updateCalls,
                   g_players.size(),
                   g_playerCache.size());
      updateCalls = 0;
  }

  DWORD now = GetTickCount();
  uintptr_t localPlayer = 0;

  /* Re-find window if needed */
  if (!g_hwnd || !IsWindow(g_hwnd)) {
    g_hwnd = FindRobloxWindow(g_pid);
  }
  if (g_hwnd) {
    RECT r;
    GetClientRect(g_hwnd, &r);
    g_screenW = r.right - r.left;
    g_screenH = r.bottom - r.top;
  }

  /* ── Standard Update Logic ──────────────────────────────────── */
  static bool loggedPointers = false;
  static float lastUpdateFps = 0.0f;
  static int updateFrames = 0;
  static DWORD lastFpsTime = 0;

  updateFrames++;
  if (now - lastFpsTime > 1000) {
      lastUpdateFps = (float)updateFrames / ((float)(now - lastFpsTime) / 1000.0f);
      updateFrames = 0;
      lastFpsTime = now;
  }
  /* g_screenW/H are already updated above from GetClientRect */

  uintptr_t visualEngine =
      Driver::Read<uintptr_t>(g_pid, g_base + Offsets::VisualEngine::Pointer);
  if (visualEngine) {
    Driver::ReadRaw(g_pid, visualEngine + Offsets::VisualEngine::ViewMatrix, g_viewMatrix,
                    sizeof(g_viewMatrix));

    bool matrixValid = false;
    for (int i = 0; i < 16; i++) {
      if (g_viewMatrix[i] != 0.0f) {
        matrixValid = true;
        break;
      }
    }

    if (!loggedPointers && now % 5000 < 100) {
      Console::Log("[Debug] VisualEngine: 0x%llX | Matrix Valid: %s",
                   visualEngine, matrixValid ? "YES" : "NO");
      Console::Log("[Debug] ViewMatrix[0]: %.2f, [12]: %.2f", g_viewMatrix[0],
                   g_viewMatrix[12]);
    }
  } else if (!loggedPointers && now % 5000 < 100) {
    Console::Warn("[Debug] VisualEngine NOT FOUND at 0x%llX",
                  g_base + Offsets::VisualEngine::Pointer);
  }

  uintptr_t fakeDataModel =
      Driver::Read<uintptr_t>(g_pid, g_base + Offsets::FakeDataModel::Pointer);

  if (!loggedPointers && now % 5000 < 100) { // Log every 5 seconds roughly
    Console::Log("[Debug] Base: 0x%llX | FakeDM: 0x%llX", g_base,
                 fakeDataModel);
  }

  if (fakeDataModel) {
    uintptr_t dataModel = Driver::Read<uintptr_t>(
        g_pid, fakeDataModel + Offsets::FakeDataModel::RealDataModel);

    if (!loggedPointers && now % 5000 < 100) {
      Console::Log("[Debug] DataModel: 0x%llX", dataModel);
      if (dataModel)
        loggedPointers = true; // Stop logging after first success
    }

    if (dataModel) {
      if (dataModel != g_currentDataModel) {
        /* DataModel Changed (Server Switch) - Reset Everything */
        Console::Warn(
            "[Roblox] DataModel mismatch (0x%llX -> 0x%llX). Resetting cache.",
            g_currentDataModel, dataModel);
        g_currentDataModel = dataModel;

        std::lock_guard<std::recursive_mutex> lock(g_playerListMutex);
        g_playersService = 0;
        g_workspaceService = 0;
        g_currentCamera = 0;
        g_localHumanoid = 0;
        g_lastLocalPlayer = 0;
        g_lastCharacter = 0;
        g_playerCache.clear();
        g_idToName.clear();
        g_localPlayerName = "";
      }

      /* ── Find LocalPlayer ── */
      if (!g_playersService)
        g_playersService = FindChildByClass(g_pid, dataModel, "Players");
      if (g_playersService) {
        localPlayer = Driver::Read<uintptr_t>(g_pid, g_playersService +
                                                         Offsets::Player::LocalPlayer);
        if (localPlayer) {
          if (localPlayer != g_lastLocalPlayer) {
            g_lastLocalPlayer = localPlayer;
            g_localHumanoid = 0;
            g_lastCharacter = 0;
            g_localPlayerName = "";

            uintptr_t namePtr =
                Driver::Read<uintptr_t>(g_pid, localPlayer + Offsets::Instance::Name);
            if (namePtr)
              g_localPlayerName = Driver::ReadRobloxString(g_pid, namePtr, 0);
          }

          uintptr_t character = Driver::Read<uintptr_t>(
              g_pid, localPlayer + Offsets::Player::ModelInstance);
          if (character != g_lastCharacter) {
            g_lastCharacter = character;
            g_localHumanoid = 0;
          }

          if (!g_localHumanoid && character) {
            g_localHumanoid = FindChildByClass(g_pid, character, "Humanoid");
          }
        }
      }

      /* ── Fix: Dynamic LocalPlayer Fallback using Camera ── */
      if (!g_workspaceService) {
        g_workspaceService =
            Driver::Read<uintptr_t>(g_pid, dataModel + Offsets::DataModel::Workspace);
      }

      static DWORD lastCameraCheck = 0;
      if (now - lastCameraCheck > 1000) {
        lastCameraCheck = now;
        if (g_workspaceService) {
          g_currentCamera =
              FindChildByClass(g_pid, g_workspaceService, "Camera");
        }
      }

      if (g_currentCamera) {
        uintptr_t cameraSubject = Driver::Read<uintptr_t>(
            g_pid, g_currentCamera + Offsets::Camera::CameraSubject);
        if (cameraSubject &&
            GetRobloxClassName(g_pid, cameraSubject) == "Humanoid") {
          uintptr_t charFromCam =
              Driver::Read<uintptr_t>(g_pid, cameraSubject + Offsets::Instance::Parent);
          if (charFromCam &&
              GetRobloxClassName(g_pid, charFromCam) == "Model") {
            if (charFromCam != g_lastCharacter) {
              g_lastCharacter = charFromCam;
              g_localHumanoid = cameraSubject;
              // Fallback name if localPlayer pointer failed
              if (g_localPlayerName.empty() || g_localPlayerName == "Unknown") {
                uintptr_t namePtr =
                    Driver::Read<uintptr_t>(g_pid, charFromCam + Offsets::Instance::Name);
                if (namePtr) {
                  g_localPlayerName =
                      Driver::ReadRobloxString(g_pid, namePtr, 0);
                }
              }
            }
          }
        }
      }
    }
  }

  /* ── Populate High Frequency List (Fixed) ── */
  {
    float lpPos[3] = {0, 0, 0};
    uintptr_t validLocalId = Roblox::GetLastCharacter();
    uintptr_t realLocalPlayer = 0;

    if (g_playersService) {
        realLocalPlayer = Driver::Read<uintptr_t>(g_pid, g_playersService + Offsets::Player::LocalPlayer);
    }

    if (validLocalId) {
      GetPartPosition(g_pid, validLocalId, lpPos);
    } else if (realLocalPlayer) {
        uintptr_t charPtr = Driver::Read<uintptr_t>(g_pid, realLocalPlayer + Offsets::Player::ModelInstance);
        if (charPtr) GetPartPosition(g_pid, charPtr, lpPos);
    }

    std::vector<PlayerData> tempPlayers;
    {
        std::lock_guard<std::recursive_mutex> lock(g_playerListMutex); 
        for (auto &cp : g_playerCache) {
          if (!cp.ptr) continue;

          PlayerData pd = {};
          pd.ptr = cp.ptr;
          pd.name = cp.name;
          pd.humanoid = cp.humanoid;
          pd.isLocalPlayer = false;
          
          if (cp.ptr == realLocalPlayer && realLocalPlayer != 0) pd.isLocalPlayer = true;
          if (cp.ptr == validLocalId && validLocalId != 0) pd.isLocalPlayer = true;
          if (cp.character == validLocalId && validLocalId != 0) pd.isLocalPlayer = true;

          if (g_currentCamera) {
              uintptr_t cameraSubject = Driver::Read<uintptr_t>(g_pid, g_currentCamera + Offsets::Camera::CameraSubject);
              if (cameraSubject != 0) {
                  if (cp.humanoid == cameraSubject || cp.character == cameraSubject || cp.ptr == cameraSubject) {
                      pd.isLocalPlayer = true;
                  }
              }
          }

          if (!pd.isLocalPlayer && !cp.name.empty() && !g_localPlayerName.empty()) {
            if (cp.name == g_localPlayerName) pd.isLocalPlayer = true;
          }

          pd.valid = false;
          uintptr_t currentCharacter = cp.isEntity ? cp.ptr : Driver::Read<uintptr_t>(g_pid, cp.ptr + Offsets::Player::ModelInstance);

          if (currentCharacter) {
            // Update cache character
            cp.character = currentCharacter;
            
            // Get root part
            bool characterChanged = false;
            if (!cp.rootPart.ptr || !cp.valid) {
                uintptr_t root = FindChildByName(g_pid, currentCharacter, "HumanoidRootPart");
                if (!root) root = FindChildByName(g_pid, currentCharacter, "Torso");
                if (!root) root = FindChildByName(g_pid, currentCharacter, "Head");
                cp.rootPart = CachePart(g_pid, root);
                cp.valid = (root != 0);
                characterChanged = true;
            } else {
            // Verify the root part still belongs to this character to prevent ghosting
            uintptr_t parent = Driver::Read<uintptr_t>(g_pid, cp.rootPart.ptr + Offsets::Instance::Parent);
            if (parent != currentCharacter) {
                cp.valid = false;
                cp.rootPart.ptr = 0;
                cp.rootPart.primitive = 0;
                characterChanged = true;
            } else {
                // Check if any part is missing or name doesn't match expected R15/R6 parts
                // This helps when only a few parts render
                if (!cp.head.ptr || !cp.lHand.ptr || !cp.rHand.ptr) {
                    characterChanged = true; 
                }
                
                // Ensure the root part is still a valid BasePart and has a valid primitive
                std::string cls = GetRobloxClassName(g_pid, cp.rootPart.ptr);
                if (cls == "" || (cls != "Part" && cls != "MeshPart")) {
                    cp.valid = false;
                    cp.rootPart.ptr = 0;
                    cp.rootPart.primitive = 0;
                    characterChanged = true;
                }
            }
            }

            if (cp.valid && GetPartPositionCached(g_pid, cp.rootPart, pd.rootPos)) {
                pd.valid = true;
                
                // Head position
                if (!cp.head.ptr || characterChanged) {
                    uintptr_t head = FindChildByName(g_pid, currentCharacter, "Head");
                    cp.head = CachePart(g_pid, head ? head : cp.rootPart.ptr);
                }
                if (!GetPartPositionCached(g_pid, cp.head, pd.headPos)) {
                    memcpy(pd.headPos, pd.rootPos, 12);
                    pd.headPos[1] += 2.0f;
                }

                /* Scan Limbs - Throttled to prevent RPM spam */
                static DWORD lastLimbScan = 0;
                bool shouldScanLimbs = (now - lastLimbScan > 2000) || characterChanged; 
                if (shouldScanLimbs) {
                    auto GetLimb = [&](const char *r15, const char *r6) {
                      uintptr_t p = FindChildByName(g_pid, currentCharacter, r15);
                      if (!p && r6 && r6[0] != '\0')
                        p = FindChildByName(g_pid, currentCharacter, r6);
                      
                      if (!p) {
                          std::string s15 = r15;
                          if (s15.find("Left") != std::string::npos) p = FindChildByName(g_pid, currentCharacter, "L_Arm");
                          if (!p && s15.find("Right") != std::string::npos) p = FindChildByName(g_pid, currentCharacter, "R_Arm");
                      }
                      
                      return CachePart(g_pid, p);
                    };

                    cp.lFoot = GetLimb("LeftFoot", "Left Leg");
                    if (!cp.lFoot.ptr) cp.lFoot = GetLimb("LeftLowerLeg", "LeftFoot");
                    cp.rFoot = GetLimb("RightFoot", "Right Leg");
                    if (!cp.rFoot.ptr) cp.rFoot = GetLimb("RightLowerLeg", "RightFoot");
                    cp.lHand = GetLimb("LeftHand", "Left Arm");
                    cp.rHand = GetLimb("RightHand", "Right Arm");

                    cp.lUpperArm = GetLimb("LeftUpperArm", "");
                    cp.lLowerArm = GetLimb("LeftLowerArm", "");
                    cp.rUpperArm = GetLimb("RightUpperArm", "");
                    cp.rLowerArm = GetLimb("RightLowerArm", "");
                    cp.lUpperLeg = GetLimb("LeftUpperLeg", "");
                    cp.lLowerLeg = GetLimb("LeftLowerLeg", "");
                    cp.rUpperLeg = GetLimb("RightUpperLeg", "");
                    cp.rLowerLeg = GetLimb("RightLowerLeg", "");
                    cp.lowerTorso = GetLimb("LowerTorso", "");
                    cp.upperTorso = GetLimb("UpperTorso", "Torso");

                    // Robust limb gating: require head, at least one leg, and one arm
                    bool haveHead = (cp.head.ptr != 0);
                    bool haveLeg = (cp.lFoot.ptr != 0 || cp.rFoot.ptr != 0);
                    bool haveArm = (cp.lHand.ptr != 0 || cp.rHand.ptr != 0);
                    
                    cp.isR15 = (cp.lUpperArm.ptr != 0 || cp.lowerTorso.ptr != 0);
                    cp.hasLimbs = (haveHead && haveLeg && haveArm);
                    lastLimbScan = now;
                }

                pd.hasLimbs = cp.hasLimbs;
                pd.isR15 = cp.isR15;

                if (pd.hasLimbs) {
                    GetPartPositionCached(g_pid, cp.lFoot, pd.lFoot);
                    GetPartPositionCached(g_pid, cp.rFoot, pd.rFoot);
                    GetPartPositionCached(g_pid, cp.lHand, pd.lHand);
                    GetPartPositionCached(g_pid, cp.rHand, pd.rHand);

                    if (pd.isR15) {
                        GetPartPositionCached(g_pid, cp.lUpperArm, pd.lUpperArm);
                        GetPartPositionCached(g_pid, cp.lLowerArm, pd.lLowerArm);
                        GetPartPositionCached(g_pid, cp.rUpperArm, pd.rUpperArm);
                        GetPartPositionCached(g_pid, cp.rLowerArm, pd.rLowerArm);
                        GetPartPositionCached(g_pid, cp.lUpperLeg, pd.lUpperLeg);
                        GetPartPositionCached(g_pid, cp.lLowerLeg, pd.lLowerLeg);
                        GetPartPositionCached(g_pid, cp.rUpperLeg, pd.rUpperLeg);
                        GetPartPositionCached(g_pid, cp.rLowerLeg, pd.rLowerLeg);
                        GetPartPositionCached(g_pid, cp.lowerTorso, pd.lowerTorso);
                        GetPartPositionCached(g_pid, cp.upperTorso, pd.upperTorso);
                    } else {
                        memcpy(pd.upperTorso, pd.rootPos, 12);
                        memcpy(pd.lowerTorso, pd.rootPos, 12);
                    }
                }

                // Distance calculation
                if (lpPos[0] != 0 || lpPos[1] != 0 || lpPos[2] != 0) {
                    pd.dist = sqrtf(powf(lpPos[0] - pd.rootPos[0], 2) + 
                                    powf(lpPos[1] - pd.rootPos[1], 2) + 
                                    powf(lpPos[2] - pd.rootPos[2], 2));
                } else {
                    pd.dist = 0.0f;
                }

                // Basic health
                if (cp.humanoid) {
                    pd.health = Driver::Read<float>(g_pid, cp.humanoid + Offsets::Humanoid::Health);
                    pd.maxHealth = Driver::Read<float>(g_pid, cp.humanoid + Offsets::Humanoid::MaxHealth);
                    
                    // DEBUG: Log health values for troubleshooting
                    static DWORD lastHealthLog = 0;
                    if (GetTickCount() - lastHealthLog > 2000) {
                        Console::Log("[Debug] Player: %s | Humanoid: 0x%llX | HP: %.1f / %.1f", 
                                     pd.name.c_str(), cp.humanoid, pd.health, pd.maxHealth);
                        lastHealthLog = GetTickCount();
                    }

                    // Fallback: If health is 0 but we know they are alive, or if reading failed (returns 0 or NaN)
                    if (pd.maxHealth < 1.0f) pd.maxHealth = 100.0f;
                    
                    // Humanoid state check for ragdoll/death
                    int humanoidState = Driver::Read<int>(g_pid, cp.humanoid + Offsets::Humanoid::GetState);
                    pd.isAlive = (pd.health > 0.1f) && (humanoidState != 15);
                    pd.isRagdoll = (humanoidState == 1 || humanoidState == 14);
                } else {
                    pd.health = 100.0f; pd.maxHealth = 100.0f; pd.isAlive = true; pd.isRagdoll = false;
                }
                tempPlayers.push_back(pd);
            }
          }
        }
    }
    {
        std::lock_guard<std::recursive_mutex> lock(g_playerListMutex); 
        g_players = tempPlayers;
    }
  }
}

uintptr_t Roblox::GetLocalPlayerTeam() {
  if (!g_pid || !g_playersService)
    return 0;
  uintptr_t lp =
      Driver::Read<uintptr_t>(g_pid, g_playersService + Offsets::Player::LocalPlayer);
  if (!lp)
    return 0;
  return Driver::Read<uintptr_t>(g_pid, lp + Offsets::Player::Team);
}

/* ── Multithreaded Scanner ───────────────────────────── */

static void ScannerLoop() {
  Console::Log("[Scanner] Thread Started.");
  while (g_scannerRunning) {
    static DWORD lastScannerLog = 0;
    static int scannerIterations = 0;
    scannerIterations++;

    DWORD now_log = GetTickCount();
    if (now_log - lastScannerLog > 1000) {
        lastScannerLog = now_log;
        Console::Log("[Perf] Scanner iters/sec=%d Cache=%zu",
                     scannerIterations,
                     g_playerCache.size());
        scannerIterations = 0;
    }

    if (!g_pid || !g_base) {
      Sleep(500);
      continue;
    }

    DWORD now = GetTickCount();

    /* ── Service Validity Check (1000ms) ────────── */
    static DWORD lastServiceCheck = 0;
    if (now - lastServiceCheck > 1000) {
      lastServiceCheck = now;

      bool servicesInvalid = false;
      if (g_playersService && GetRobloxClassName(g_pid, g_playersService) != "Players")
        servicesInvalid = true;
      if (g_workspaceService && GetRobloxClassName(g_pid, g_workspaceService) != "Workspace")
        servicesInvalid = true;

      if (servicesInvalid || !g_playersService || !g_workspaceService) {
        if (g_currentDataModel) {
            g_playersService = FindChildByClass(g_pid, g_currentDataModel, "Players");
            g_workspaceService = FindChildByClass(g_pid, g_currentDataModel, "Workspace");
            if (servicesInvalid) {
                std::lock_guard<std::recursive_mutex> lock(g_playerListMutex);
                g_playerCache.clear();
                g_idToName.clear();
            }
        }
      }
    }

    /* ── High Frequency: Player Discovery (1000ms) ────────── */
    static DWORD lastPlayerScan = 0;
    if (now - lastPlayerScan > 1000) {
        lastPlayerScan = now;
        if (g_playersService) {
            auto players = GetChildren(g_pid, g_playersService);
            std::lock_guard<std::recursive_mutex> lock(g_playerListMutex);
            for (auto plr : players) {
                bool exists = false;
                for (auto& cp : g_playerCache) {
                    if (cp.ptr == plr) { exists = true; break; }
                }
                if (!exists) {
                    // Only check class for NEW potential players
                    std::string cls = GetRobloxClassName(g_pid, plr);
                    if (cls == "Player") {
                        CachedPlayer newP = {};
                        newP.ptr = plr;
                        uintptr_t namePtr = Driver::Read<uintptr_t>(g_pid, plr + Offsets::Instance::Name);
                        newP.name = namePtr ? Driver::ReadRobloxString(g_pid, namePtr, 0) : "Unknown";
                        g_playerCache.push_back(newP);
                    }
                }
            }
        }
    }

    /* ── Low Frequency: Workspace Rig Discovery (5000ms) ────────── */
    // Only scan workspace if specifically requested or for testing, as it picks up NPCs
    // For now, we only trust the Players service to filter out Dummies
    if (false && now - g_lastEntityUpdate > 5000) {
      g_lastEntityUpdate = now;
      if (g_workspaceService) {
        std::vector<uintptr_t> snapshot;
        {
          std::lock_guard<std::recursive_mutex> lock(g_playerListMutex);
          for (const auto &p : g_playerCache) snapshot.push_back(p.ptr);
        }

        auto ScanRecursive = [&](auto &&self, uintptr_t folder, int depth, bool isTarget) -> void {
          if (depth > 3) return; // Very shallow depth for workspace
          auto children = GetChildren(g_pid, folder);
          if (children.size() > 200) return; // Skip even smaller folders now

          for (auto child : children) {
            // PF Support: skip non-essential objects early
            // We only care about Models and Folders here
            std::string cls = GetRobloxClassName(g_pid, child);
            if (cls == "Model") {
                // Heuristic: check if it has a Humanoid only if depth is shallow
                uintptr_t humanoid = FindChildByClass(g_pid, child, "Humanoid");
                if (humanoid || isTarget) {
                    bool exists = false;
                    for (uintptr_t s : snapshot) if (s == child) { exists = true; break; }
                    if (!exists) {
                        CachedPlayer newP = {};
                        newP.ptr = child; newP.isEntity = true; newP.humanoid = humanoid;
                        uintptr_t nPtr = Driver::Read<uintptr_t>(g_pid, child + Offsets::Instance::Name);
                        newP.name = nPtr ? Driver::ReadRobloxString(g_pid, nPtr, 0) : "NPC";
                        std::lock_guard<std::recursive_mutex> lock(g_playerListMutex);
                        g_playerCache.push_back(newP);
                        snapshot.push_back(child);
                    }
                }
            } else if (cls == "Folder") {
                uintptr_t nPtr = Driver::Read<uintptr_t>(g_pid, child + Offsets::Instance::Name);
                std::string n = nPtr ? Driver::ReadRobloxString(g_pid, nPtr, 0) : "";
                bool target = isTarget || (n == "Players" || n == "Units"); 
                self(self, child, depth + 1, target);
            }
          }
        };
        ScanRecursive(ScanRecursive, g_workspaceService, 0, false);
      }
    }
    Sleep(500); // Massive sleep for scanner thread
  }
}

void Roblox::StartScanner() {
  if (g_scannerRunning)
    return;
  g_scannerRunning = true;
  g_scannerThread = std::thread(ScannerLoop);
}

void Roblox::StopScanner() {
  g_scannerRunning = false;
  if (g_scannerThread.joinable())
    g_scannerThread.join();
}

DWORD Roblox::GetPID() { return g_pid; }
uintptr_t Roblox::GetBase() { return g_base; }
const float *Roblox::GetViewMatrix() { return g_viewMatrix; }
const std::vector<PlayerData> &Roblox::GetPlayers() {
  // Mutex should be here but for read-heavy ESP it might flicker.
  // For now returning the vector is safe-ish if we don't clear/resize during
  // read. But better to return a COPY if possible or use mutex. Let's rely on
  // atomic/fast updates for now to avoid locking render thread.
  return g_players;
}
int Roblox::GetScreenWidth() { return g_screenW; }
int Roblox::GetScreenHeight() { return g_screenH; }
void Roblox::ForceRefreshCache() {
  std::lock_guard<std::recursive_mutex> lock(g_playerListMutex);
  g_playerCache.clear();
  g_players.clear();
  g_lastPlayerUpdate = 0;
  g_lastEntityUpdate = 0;
  Console::Log("Forced player cache refresh.");
}

HWND Roblox::GetWindow() { return g_hwnd; }

std::string Roblox::GetLocalPlayerName() { return g_localPlayerName; }
uintptr_t Roblox::GetLastCharacter() { return g_lastCharacter; }
