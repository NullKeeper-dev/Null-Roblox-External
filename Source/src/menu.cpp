/*
 * menu.cpp - UPDATED BY ANTIGRAVITY - AIMBOT SETTINGS RE-APPLIED
 */

#define IMGUI_DEFINE_MATH_OPERATORS

#include "../include/menu.h"
#include "../include/imgui.h"
#include "../include/imgui_internal.h"
#include "../include/imgui_impl_dx11.h"
#include "../include/imgui_impl_win32.h"
#include "../include/esp.h"
#include "../include/aimbot.h"
#include "../include/roblox.h"
#include "../include/console.h"
#include <algorithm> // For clamp
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

static bool g_visible = false;
static int g_tab = 0; // 0: Aim, 1: Visuals, 2: Misc, 3: Settings
static int g_prevTab = 0;
static float g_tabAlpha = 1.0f;
int g_toggleKey = VK_INSERT;
int g_selfDestructKey = VK_END;

/* ── Animation System ────────────────────────────────────── */
static float g_menuAlpha = 0.0f;
static std::map<ImGuiID, float> g_animStates;

static float Lerp(float a, float b, float t) { return a + (b - a) * t; }

static float GetAnim(ImGuiID id, bool active, float speed = 0.1f) {
  auto it = g_animStates.find(id);
  if (it == g_animStates.end()) {
    g_animStates[id] = active ? 1.0f : 0.0f;
    return g_animStates[id];
  }
  float target = active ? 1.0f : 0.0f;
  it->second = Lerp(it->second, target, speed);

  // Snapping
  if (fabsf(it->second - target) < 0.001f)
    it->second = target;

  return it->second;
}

/* ── Custom Theme Colors ── */
static ImVec4 g_accentColor = ImVec4(0.70f, 0.40f, 1.00f, 1.00f); // Purple
static ImVec4 g_textColor = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
static ImVec4 g_bgColor = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
static ImVec4 g_childColor = ImVec4(0.09f, 0.09f, 0.11f, 1.00f);

/* ── Rainbow / Chroma State ── */
static bool g_rainbow = false;
static float g_rainbowSpeed = 1.0f;

/* ── Keybind State ── */
static const char *g_activeBindLabel = nullptr; // Track which button is binding

/* ── Configuration System ────────────────────────────────── */
namespace Config {
void Save(const std::string &name) {
  if (!std::filesystem::exists("config"))
    std::filesystem::create_directory("config");

  std::ofstream file("config/" + name + ".cfg");
  if (!file.is_open())
    return;

  // Aim
  file << "aim_enabled " << Aimbot::settings.enabled << "\n";
  file << "aim_drawFov " << Aimbot::settings.drawFov << "\n";
  file << "aim_fov " << Aimbot::settings.fov << "\n";
  file << "aim_smooth " << Aimbot::settings.smooth << "\n";
  file << "aim_key " << Aimbot::settings.key << "\n";
  file << "aim_part " << (int)Aimbot::settings.part << "\n";
  file << "aim_fov_col_r " << Aimbot::settings.fovCol[0] << "\n";
  file << "aim_fov_col_g " << Aimbot::settings.fovCol[1] << "\n";
  file << "aim_fov_col_b " << Aimbot::settings.fovCol[2] << "\n";
  file << "aim_fov_rainbow " << Aimbot::settings.rainbowFov << "\n";
  file << "aim_team_check " << Aimbot::settings.teamCheck << "\n";
  file << "aim_height_offset " << Aimbot::settings.heightOffset << "\n";

  // Visuals
  ESPSettings &esp = ESP::Settings();
  file << "esp_enabled " << esp.enabled << "\n";
  file << "esp_box " << esp.showBox << "\n";
  file << "esp_names " << esp.showNames << "\n";
  file << "esp_health " << esp.showHealth << "\n";
  file << "esp_snap " << esp.showSnapline << "\n";
  file << "esp_col_r " << esp.espCol[0] << "\n";
  file << "esp_col_g " << esp.espCol[1] << "\n";
  file << "esp_col_b " << esp.espCol[2] << "\n";
  file << "esp_show_distance " << esp.showDistance << "\n";
  file << "esp_rainbow " << esp.rainbowEsp << "\n";
  file << "esp_team_check " << esp.teamCheck << "\n";
  file << "esp_refresh_key " << esp.refreshKey << "\n";
  file << "esp_box_thickness " << esp.boxThickness << "\n";
  file << "esp_health_width " << esp.healthWidth << "\n";
  file << "esp_name_scale " << esp.nameScale << "\n";
  file << "esp_skeleton " << esp.showSkeleton << "\n";
  file << "esp_skeleton_rainbow " << esp.rainbowSkeleton << "\n";
  file << "esp_skeleton_col_r " << esp.skeletonCol[0] << "\n";
  file << "esp_skeleton_col_g " << esp.skeletonCol[1] << "\n";
  file << "esp_skeleton_col_b " << esp.skeletonCol[2] << "\n";
  file << "esp_charms " << esp.showSkeletonGlow << "\n";
  file << "esp_charms_rainbow " << esp.rainbowSkeletonGlow << "\n";
  file << "esp_charms_col_r " << esp.skeletonGlowCol[0] << "\n";
  file << "esp_charms_col_g " << esp.skeletonGlowCol[1] << "\n";
  file << "esp_charms_col_b " << esp.skeletonGlowCol[2] << "\n";
  file << "esp_check_ragdoll " << esp.checkRagdoll << "\n";
  file << "esp_check_alive " << esp.checkAlive << "\n";
  file << "perf_show_fps " << esp.showFps << "\n";
  file << "perf_fps_cap " << esp.fpsCap << "\n";

  // Theme
  file << "theme_rainbow " << g_rainbow << "\n";
  file << "theme_rainbow_speed " << g_rainbowSpeed << "\n";
  file << "theme_acc_r " << g_accentColor.x << "\n";
  file << "theme_acc_g " << g_accentColor.y << "\n";
  file << "theme_acc_b " << g_accentColor.z << "\n";
  file << "menu_toggle " << g_toggleKey << "\n";
  file << "self_destruct_key " << g_selfDestructKey << "\n";

  file.close();
}

void Load(const std::string &name) {
  std::ifstream file("config/" + name + ".cfg");
  if (!file.is_open())
    return;

  std::string key;
  while (file >> key) {
    if (key == "aim_enabled")
      file >> Aimbot::settings.enabled;
    else if (key == "aim_drawFov")
      file >> Aimbot::settings.drawFov;
    else if (key == "aim_fov")
      file >> Aimbot::settings.fov;
    else if (key == "aim_smooth")
      file >> Aimbot::settings.smooth;
    else if (key == "aim_key")
      file >> Aimbot::settings.key;
    else if (key == "aim_part") {
      int p;
      file >> p;
      Aimbot::settings.part = (TargetPart)p;
    } else if (key == "aim_fov_col_r")
      file >> Aimbot::settings.fovCol[0];
    else if (key == "aim_fov_col_g")
      file >> Aimbot::settings.fovCol[1];
    else if (key == "aim_fov_col_b")
      file >> Aimbot::settings.fovCol[2];
    else if (key == "aim_fov_rainbow")
      file >> Aimbot::settings.rainbowFov;
    else if (key == "aim_team_check")
      file >> Aimbot::settings.teamCheck;
    else if (key == "aim_height_offset")
      file >> Aimbot::settings.heightOffset;
    else if (key == "esp_enabled")
      file >> ESP::Settings().enabled;
    else if (key == "esp_box")
      file >> ESP::Settings().showBox;
    else if (key == "esp_names")
      file >> ESP::Settings().showNames;
    else if (key == "esp_health")
      file >> ESP::Settings().showHealth;
    else if (key == "esp_snap")
      file >> ESP::Settings().showSnapline;
    else if (key == "esp_col_r")
      file >> ESP::Settings().espCol[0];
    else if (key == "esp_col_g")
      file >> ESP::Settings().espCol[1];
    else if (key == "esp_col_b")
      file >> ESP::Settings().espCol[2];
    else if (key == "esp_show_distance")
      file >> ESP::Settings().showDistance;
    else if (key == "esp_rainbow")
      file >> ESP::Settings().rainbowEsp;
    else if (key == "esp_team_check")
      file >> ESP::Settings().teamCheck;
    else if (key == "esp_refresh_key")
      file >> ESP::Settings().refreshKey;
    else if (key == "esp_box_thickness")
      file >> ESP::Settings().boxThickness;
    else if (key == "esp_health_width")
      file >> ESP::Settings().healthWidth;
    else if (key == "esp_name_scale")
      file >> ESP::Settings().nameScale;
    else if (key == "esp_skeleton")
      file >> ESP::Settings().showSkeleton;
    else if (key == "esp_skeleton_rainbow")
      file >> ESP::Settings().rainbowSkeleton;
    else if (key == "esp_skeleton_col_r")
      file >> ESP::Settings().skeletonCol[0];
    else if (key == "esp_skeleton_col_g")
      file >> ESP::Settings().skeletonCol[1];
    else if (key == "esp_skeleton_col_b")
      file >> ESP::Settings().skeletonCol[2];
    else if (key == "esp_charms")
      file >> ESP::Settings().showSkeletonGlow;
    else if (key == "esp_charms_rainbow")
      file >> ESP::Settings().rainbowSkeletonGlow;
    else if (key == "esp_charms_col_r")
      file >> ESP::Settings().skeletonGlowCol[0];
    else if (key == "esp_charms_col_g")
      file >> ESP::Settings().skeletonGlowCol[1];
    else if (key == "esp_charms_col_b")
      file >> ESP::Settings().skeletonGlowCol[2];
    else if (key == "esp_check_ragdoll")
      file >> ESP::Settings().checkRagdoll;
    else if (key == "esp_check_alive")
      file >> ESP::Settings().checkAlive;
    else if (key == "perf_show_fps")
      file >> ESP::Settings().showFps;
    else if (key == "perf_fps_cap")
      file >> ESP::Settings().fpsCap;
    else if (key == "theme_rainbow")
      file >> g_rainbow;
    else if (key == "theme_rainbow_speed")
      file >> g_rainbowSpeed;
    else if (key == "theme_acc_r")
      file >> g_accentColor.x;
    else if (key == "theme_acc_g")
      file >> g_accentColor.y;
    else if (key == "theme_acc_b")
      file >> g_accentColor.z;
    else if (key == "menu_toggle")
      file >> g_toggleKey;
    else if (key == "self_destruct_key")
      file >> g_selfDestructKey;
    else if (key == "debug_mode") {
      file >> ESP::Settings().debugMode;
      Console::SetVisible(ESP::Settings().debugMode);
    }
  }
  file.close();
}
} // namespace Config

void Menu::Initialize() {
  ImGuiIO &io = ImGui::GetIO();
  io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
  // Fallback if font fails? io.Fonts->AddFontDefault();

  if (std::filesystem::exists("config/default.cfg")) {
    Config::Load("default");
  } else if (std::filesystem::exists("config")) {
    // Try to find ANY config if default doesn't exist
    for (const auto &entry : std::filesystem::directory_iterator("config")) {
      if (entry.path().extension() == ".cfg") {
        Config::Load(entry.path().stem().string());
        break;
      }
    }
  }
}

void Menu::Toggle() { g_visible = !g_visible; }
bool Menu::IsVisible() { return g_visible; }
int Menu::GetToggleKey() { return g_toggleKey; }

void Menu::ApplyTheme() {
  ImGuiStyle &style = ImGui::GetStyle();
  style.WindowRounding = 6.0f;
  style.ChildRounding = 4.0f;
  style.FrameRounding = 3.0f;
  style.GrabRounding = 3.0f;
  style.PopupRounding = 4.0f;
  style.ScrollbarRounding = 12.0f;

  style.WindowPadding =
      ImVec2(0, 0); // No padding for main window to handle sidebar
  style.FramePadding = ImVec2(8, 6);
  style.ItemSpacing = ImVec2(10, 10);
  style.ScrollbarSize = 6.0f;
  style.AntiAliasedLines = true;
  style.AntiAliasedFill = true;

  ImVec4 *colors = style.Colors;
  colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.07f, 1.00f);
  colors[ImGuiCol_ChildBg] = ImVec4(0.08f, 0.08f, 0.09f, 0.50f);
  colors[ImGuiCol_Border] = ImVec4(0.12f, 0.12f, 0.14f, 0.50f);
  colors[ImGuiCol_Text] = g_textColor;
  colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);

  colors[ImGuiCol_Header] =
      ImVec4(g_accentColor.x, g_accentColor.y, g_accentColor.z, 0.20f);
  colors[ImGuiCol_HeaderHovered] =
      ImVec4(g_accentColor.x, g_accentColor.y, g_accentColor.z, 0.30f);
  colors[ImGuiCol_HeaderActive] =
      ImVec4(g_accentColor.x, g_accentColor.y, g_accentColor.z, 0.40f);

  colors[ImGuiCol_Button] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
  colors[ImGuiCol_ButtonHovered] = ImVec4(0.14f, 0.14f, 0.18f, 1.00f);
  colors[ImGuiCol_ButtonActive] = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);

  colors[ImGuiCol_CheckMark] = g_accentColor;
  colors[ImGuiCol_SliderGrab] = g_accentColor;
  colors[ImGuiCol_SliderGrabActive] =
      ImVec4(g_accentColor.x + 0.1f, g_accentColor.y + 0.1f,
             g_accentColor.z + 0.1f, 1.0f);

  colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
  colors[ImGuiCol_FrameBgHovered] = ImVec4(0.15f, 0.15f, 0.18f, 1.00f);
  colors[ImGuiCol_FrameBgActive] = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);

  style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
}

/* ── Custom Widgets ────────────────────────────────────────── */
/* Modern Toggle Switch */
bool CustomSwitch(const char *label, bool *v) {
  ImGuiWindow *window = ImGui::GetCurrentWindow();
  if (window->SkipItems)
    return false;

  ImGuiContext &g = *GImGui;
  const ImGuiStyle &style = g.Style;
  const ImGuiID id = window->GetID(label);
  const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);

  float width = 36.0f;
  float height = 18.0f;
  const ImVec2 pos = window->DC.CursorPos;
  const ImRect total_bb(
      pos, ImVec2(pos.x + ImGui::GetContentRegionAvail().x, pos.y + height));

  ImGui::ItemSize(total_bb, style.FramePadding.y);
  if (!ImGui::ItemAdd(total_bb, id))
    return false;

  bool hovered, held;
  bool pressed = ImGui::ButtonBehavior(total_bb, id, &hovered, &held);
  if (pressed) {
    *v = !(*v);
    ImGui::MarkItemEdited(id);
  }

  // Animation
  float anim = GetAnim(id, *v, 0.15f);

  ImDrawList *draw = window->DrawList;
  ImVec2 switch_pos = ImVec2(total_bb.Max.x - width, total_bb.Min.y);
  ImRect switch_bb(switch_pos,
                   ImVec2(switch_pos.x + width, switch_pos.y + height));

  // Background - Lerp between gray and accent
  ImVec4 bgCol = ImVec4(Lerp(0.15f, g_accentColor.x, anim),
                        Lerp(0.15f, g_accentColor.y, anim),
                        Lerp(0.15f, g_accentColor.z, anim), 1.0f);
  draw->AddRectFilled(switch_bb.Min, switch_bb.Max, ImGui::GetColorU32(bgCol),
                      height * 0.5f);

  // Knob - Lerp position
  float wheel_pos = anim * (width - height);
  draw->AddCircleFilled(ImVec2(switch_bb.Min.x + height * 0.5f + wheel_pos,
                               switch_bb.Min.y + height * 0.5f),
                        height * 0.4f, ImGui::GetColorU32(ImVec4(1, 1, 1, 1)));

  ImGui::PushStyleColor(ImGuiCol_Text,
                        ImVec4(Lerp(0.5f, 1.0f, anim), Lerp(0.5f, 1.0f, anim),
                               Lerp(0.5f, 1.0f, anim), 1.0f));
  ImGui::RenderText(pos, label);
  ImGui::PopStyleColor();
  return pressed;
}

void SectionHeader(const char *label) {
  ImGui::PushStyleColor(ImGuiCol_Text, g_accentColor);
  ImGui::Text(label);
  ImGui::PopStyleColor();

  ImVec2 min = ImGui::GetItemRectMin();
  ImVec2 max = ImGui::GetItemRectMax();
  max.x = min.x + ImGui::GetContentRegionAvail().x;
  min.y = max.y + 2;
  max.y = min.y + 1;

  ImGui::GetWindowDrawList()->AddRectFilled(
      min, max, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.1f)));
  ImGui::Spacing();
}

bool CustomSlider(const char* label, float* v, float v_min, float v_max, const char* format) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);

    const float w = ImGui::GetContentRegionAvail().x;
    const float h = 30.0f;
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect total_bb(pos, ImVec2(pos.x + w, pos.y + h));

    ImGui::ItemSize(total_bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(total_bb, id))
        return false;

    ImGui::RenderText(pos, label);
    char value_buf[64];
    sprintf_s(value_buf, format, *v);
    const ImVec2 value_sz = ImGui::CalcTextSize(value_buf);
    ImGui::RenderText(ImVec2(pos.x + w - value_sz.x, pos.y), value_buf);

    const float slider_y_val = pos.y + 22.0f;
    const ImRect slider_bb_v2(ImVec2(pos.x, slider_y_val - 2.0f), ImVec2(pos.x + w, slider_y_val + 2.0f));

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(slider_bb_v2, id, &hovered, &held);

    if (held || pressed) {
        float normalized = (g.IO.MousePos.x - slider_bb_v2.Min.x) / (slider_bb_v2.Max.x - slider_bb_v2.Min.x);
        normalized = (std::clamp)(normalized, 0.0f, 1.0f);
        *v = v_min + normalized * (v_max - v_min);
        // ImGui::MarkItemEdited(id); // Use internal version if needed, or skip if not strictly required for this custom widget
    }

    float fraction = (*v - v_min) / (v_max - v_min);
    fraction = (std::clamp)(fraction, 0.0f, 1.0f);

    ImDrawList* draw_ptr = window->DrawList;
    const ImU32 bg_col_v2 = ImGui::GetColorU32(ImVec4(0.20f, 0.20f, 0.22f, 1.0f));
    draw_ptr->AddRectFilled(slider_bb_v2.Min, slider_bb_v2.Max, bg_col_v2, 2.0f);

    ImRect active_bb_v2 = slider_bb_v2;
    active_bb_v2.Max.x = slider_bb_v2.Min.x + (slider_bb_v2.Max.x - slider_bb_v2.Min.x) * fraction;
    const ImU32 accent_col_v2 = ImGui::GetColorU32(g_accentColor);
    draw_ptr->AddRectFilled(active_bb_v2.Min, active_bb_v2.Max, accent_col_v2, 2.0f);

    ImVec2 circ_pos_v2;
    circ_pos_v2.x = static_cast<float>(active_bb_v2.Max.x);
    circ_pos_v2.y = static_cast<float>(slider_y_val);

    draw_ptr->AddCircleFilled(circ_pos_v2, 6.0f, IM_COL32(255, 255, 255, 255), 32);
    draw_ptr->AddCircle(circ_pos_v2, 6.0f, accent_col_v2, 32, 1.5f);

    return pressed;
}

bool KeybindButton(const char *label, int *k) {
  char buf[128];
  bool isBinding = (g_activeBindLabel && strcmp(g_activeBindLabel, label) == 0);

  if (isBinding)
    sprintf_s(buf, "Press any key...###%s", label);
  else
    sprintf_s(buf, "%s: [%s]###%s", label, Aimbot::GetKeyName(*k), label);

  ImVec2 size = ImVec2(-1, 30); // Increased height and using negative width for full area
  
  if (ImGui::Button(buf, size)) {
    if (g_activeBindLabel == nullptr)
      g_activeBindLabel = label;
    else if (isBinding)
      g_activeBindLabel = nullptr;
  }

  if (isBinding) {
    for (int i = 1; i < 0xFE; i++) {
      // Skip common toggle keys or handle them specially
      if (GetAsyncKeyState(i) & 0x8000) {
        if (i == VK_ESCAPE) {
          *k = 0; // Set to None
        } else if (i == VK_LBUTTON || i == VK_RBUTTON) {
          continue; // Ignore clicks while binding so we don't bind to mouse 1
                    // immediately
        } else {
          *k = i;
        }
        g_activeBindLabel = nullptr;
        break;
      }
    }
  }
  return true;
}

static void DrawSidebarButton(const char *label, int tabIndex, float width) {
  bool active = (g_tab == tabIndex);
  ImGuiID id = ImGui::GetID(label);

  float anim = GetAnim(id, active, 0.15f);

  // Hover state needs to be fetched AFTER the button or using a persistent
  // state We'll use a slightly different approach: Get the state from the
  // PREVIOUS frame
  bool isHovered = false;
  auto it = g_animStates.find(id + 1);
  float hoverAnim = (it != g_animStates.end()) ? it->second : 0.0f;

  ImVec4 btnCol = ImVec4(g_accentColor.x, g_accentColor.y, g_accentColor.z,
                         (anim * 0.15f) + (hoverAnim * 0.05f));
  ImGui::PushStyleColor(ImGuiCol_Button, btnCol);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, btnCol);
  ImGui::PushStyleColor(
      ImGuiCol_ButtonActive,
      ImVec4(g_accentColor.x, g_accentColor.y, g_accentColor.z, 0.25f));

  ImVec4 txtCol = ImVec4(Lerp(0.6f, g_accentColor.x, anim),
                         Lerp(0.6f, g_accentColor.y, anim),
                         Lerp(0.6f, g_accentColor.z, anim), 1.0f);
  ImGui::PushStyleColor(ImGuiCol_Text, txtCol);

  if (ImGui::Button(label, ImVec2(width - 20, 35))) {
    if (g_tab != tabIndex) {
      g_prevTab = g_tab;
      g_tab = tabIndex;
      g_tabAlpha = 0.0f; // Trigger tab fade
    }
  }

  // Update hover animation state for NEXT frame
  GetAnim(id + 1, ImGui::IsItemHovered(), 0.2f);

  if (anim > 0.01f) {
    ImVec2 p_min = ImGui::GetItemRectMin();
    ImVec2 p_max = ImGui::GetItemRectMax();
    float barHeight = (p_max.y - p_min.y) * anim;
    float barY = p_min.y + (p_max.y - p_min.y) * (1.0f - anim) * 0.5f;

    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(p_min.x - 10, barY), ImVec2(p_min.x - 7, barY + barHeight),
        ImGui::GetColorU32(
            ImVec4(g_accentColor.x, g_accentColor.y, g_accentColor.z, anim)));
  }

  ImGui::PopStyleColor(4);
  ImGui::Spacing();
}

void Menu::Render() {
  if (!g_visible) {
    g_menuAlpha = 0.0f;
    return;
  }

  // Window fade-in
  g_menuAlpha = Lerp(g_menuAlpha, 1.0f, 0.1f);
  if (g_menuAlpha < 0.01f)
    return;

  // Tab fade-in
  g_tabAlpha = Lerp(g_tabAlpha, 1.0f, 0.15f);

  if (g_rainbow) {
    float hue = (float)GetTickCount() * 0.0002f * g_rainbowSpeed;
    hue = fmodf(hue, 1.0f);
    ImGui::ColorConvertHSVtoRGB(hue, 0.7f, 0.8f, g_accentColor.x,
                                g_accentColor.y, g_accentColor.z);
  }

  ApplyTheme();

  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, g_menuAlpha);

  ImVec2 screenSize = ImGui::GetIO().DisplaySize;
  ImGui::SetNextWindowPos(ImVec2(screenSize.x * 0.5f, screenSize.y * 0.5f),
                          ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
  ImGui::Begin("PassatHook Design", &g_visible, ImGuiWindowFlags_NoDecoration);

  ImVec2 p = ImGui::GetWindowPos();
  ImVec2 s = ImGui::GetWindowSize();
  ImDrawList *draw = ImGui::GetWindowDrawList();

  // Sidebar background
  draw->AddRectFilled(p, ImVec2(p.x + 160, p.y + s.y),
                      ImGui::GetColorU32(ImVec4(0.04f, 0.04f, 0.05f, 1.00f)));
  draw->AddLine(ImVec2(p.x + 160, p.y), ImVec2(p.x + 160, p.y + s.y),
                ImGui::GetColorU32(ImVec4(1, 1, 1, 0.05f)));

  // Title
  ImGui::SetCursorPos(ImVec2(20, 20));
  ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
  ImGui::TextColored(g_accentColor, "Null External");
  ImGui::PopFont();

  // Sidebar items
  ImGui::SetCursorPos(ImVec2(10, 70));
  ImGui::BeginGroup();
  DrawSidebarButton("Aimbot", 0, 160);
  DrawSidebarButton("Visuals", 1, 160);
  DrawSidebarButton("Misc", 2, 160);
  DrawSidebarButton("Settings", 3, 160);
  ImGui::EndGroup();

  // Main area
  ImGui::SetCursorPos(ImVec2(170, 20));
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
                      g_menuAlpha * g_tabAlpha); // Combine alphas
  ImGui::BeginChild("MainContent", ImVec2(s.x - 180, s.y - 40), false);

  if (g_tab == 0) { // Aim
    Aimbot::Settings &aim = Aimbot::settings;
    ImGui::Columns(2, NULL, false);

    SectionHeader("Aimbot");
    CustomSwitch("Enabled", &aim.enabled);
    CustomSwitch("Draw FOV", &aim.drawFov);
    ImGui::Spacing();
    ImGui::Text("Target Part");
    const char *parts[] = {"Head", "Torso", "Legs"};
    int currentPart = (int)aim.part;
    ImGui::PushItemWidth(-1);
    if (ImGui::Combo("##Part", &currentPart, parts, IM_ARRAYSIZE(parts)))
      aim.part = (TargetPart)currentPart;
    ImGui::PopItemWidth();

    ImGui::NextColumn();
    SectionHeader("Settings");
    CustomSlider("FOV Radius", &aim.fov, 10.0f, 1000.0f, "%.0f");
    CustomSlider("Smoothness", &aim.smooth, 1.0f, 50.0f, "%.1f");
    CustomSlider("Vertical Offset", &aim.heightOffset, -10.0f, 10.0f, "%.1f");
    CustomSwitch("Check Alive", &aim.checkAlive);
    CustomSwitch("Check Ragdoll", &aim.checkRagdoll);
    CustomSwitch("Team Check", &aim.teamCheck);

    ImGui::Spacing();
    SectionHeader("Binds & Colors");
    KeybindButton("Aim Key", &aim.key);
    ImGui::Spacing();
    ImGui::Text("FOV color");
    ImGui::ColorEdit3("##FOVCol", aim.fovCol, ImGuiColorEditFlags_NoInputs);
    CustomSwitch("Rainbow FOV", &aim.rainbowFov);

    ImGui::Columns(1);
  } else if (g_tab == 1) { // Visuals
    ESPSettings &esp = ESP::Settings();
    ImGui::Columns(2, NULL, false);

    SectionHeader("ESP Main");
    CustomSwitch("Enabled", &esp.enabled);
    CustomSwitch("Boxes", &esp.showBox);
    CustomSwitch("Names", &esp.showNames);
    CustomSwitch("Health", &esp.showHealth);
    CustomSwitch("Snaplines", &esp.showSnapline);
    CustomSwitch("Distance", &esp.showDistance);
    CustomSwitch("Team Check", &esp.teamCheck);

    ImGui::Spacing();
    SectionHeader("Visuals Extra");
    CustomSwitch("Skeleton", &esp.showSkeleton);
    if (esp.showSkeleton) {
      if (esp.rainbowSkeleton) {
        float hue = (float)GetTickCount() * 0.0002f;
        ImGui::ColorConvertHSVtoRGB(fmodf(hue, 1.0f), 0.7f, 0.8f, esp.skeletonCol[0], esp.skeletonCol[1], esp.skeletonCol[2]);
      }
      CustomSwitch("Rainbow Skeleton", &esp.rainbowSkeleton);
      ImGui::ColorEdit3("Skeleton Color", esp.skeletonCol, ImGuiColorEditFlags_NoInputs);
    }
    ImGui::Spacing();
    CustomSwitch("Skeleton Glow", &esp.showSkeletonGlow);
    if (esp.showSkeletonGlow) {
      if (esp.rainbowSkeletonGlow) {
        float hue = (float)GetTickCount() * 0.0002f;
        ImGui::ColorConvertHSVtoRGB(fmodf(hue, 1.0f), 0.7f, 0.8f, esp.skeletonGlowCol[0], esp.skeletonGlowCol[1], esp.skeletonGlowCol[2]);
      }
      CustomSwitch("Rainbow Glow", &esp.rainbowSkeletonGlow);
      ImGui::ColorEdit3("Glow Color", esp.skeletonGlowCol, ImGuiColorEditFlags_NoInputs);
    }
    ImGui::Spacing();
    CustomSwitch("Check Alive", &esp.checkAlive);
    CustomSwitch("Check Ragdoll", &esp.checkRagdoll);

    ImGui::NextColumn();
    SectionHeader("ESP Settings");
    CustomSlider("Box Thick", &esp.boxThickness, 1.0f, 5.0f, "%.1f");
    CustomSlider("Health Width", &esp.healthWidth, 1.0f, 10.0f, "%.1f");
    CustomSlider("Name Scale", &esp.nameScale, 0.5f, 2.0f, "%.1f");

    ImGui::Spacing();
    SectionHeader("Style");
    ImGui::Text("ESP Color");
    if (esp.rainbowEsp) {
      float hue = (float)GetTickCount() * 0.0002f;
      ImGui::ColorConvertHSVtoRGB(fmodf(hue, 1.0f), 0.7f, 0.8f, esp.espCol[0], esp.espCol[1], esp.espCol[2]);
    }
    ImGui::ColorEdit3("##ESPCol", esp.espCol, ImGuiColorEditFlags_NoInputs);
    CustomSwitch("Rainbow ESP", &esp.rainbowEsp);
    ImGui::Spacing();
    KeybindButton("Refresh Cache", &esp.refreshKey);

    ImGui::Columns(1);
  } else if (g_tab == 2) { // Misc
    ESPSettings &esp = ESP::Settings();
    SectionHeader("Performance");
    CustomSwitch("Show FPS Overlay", &esp.showFps);
    
    // Real-time FPS Display
    float currentFps = ImGui::GetIO().Framerate;
    ImGui::Text("Current External FPS: %.1f", currentFps);
    
    ImGui::Spacing();
    float fpsFraction = std::clamp(currentFps / 60.0f, 0.0f, 1.0f);
    ImVec4 fpsColor = ImVec4(1.0f - fpsFraction, fpsFraction, 0.0f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, fpsColor);
    ImGui::ProgressBar(fpsFraction, ImVec2(-1, 15), "");
    ImGui::PopStyleColor();
  } else if (g_tab == 3) { // Settings
    ImGui::Columns(2, NULL, false);

    SectionHeader("Menu Theme");
    ImGui::Text("Accent Color");
    if (g_rainbow) {
      float hue = (float)GetTickCount() * 0.0002f * g_rainbowSpeed;
      ImGui::ColorConvertHSVtoRGB(fmodf(hue, 1.0f), 0.7f, 0.8f, g_accentColor.x, g_accentColor.y, g_accentColor.z);
    }
    ImGui::ColorEdit3("##AccCol", (float *)&g_accentColor,
                      ImGuiColorEditFlags_NoInputs);
    CustomSwitch("Rainbow Mode", &g_rainbow);
    if (g_rainbow)
      CustomSlider("Speed", &g_rainbowSpeed, 0.1f, 5.0f, "%.1f");

    ImGui::Spacing();
    if (CustomSwitch("Debug Mode", &ESP::Settings().debugMode)) {
      Console::SetVisible(ESP::Settings().debugMode);
    }
    CustomSwitch("Hide Launcher", &ESP::Settings().hideLauncher);

    if (ImGui::Button("Self Destruct##button", ImVec2(-1, 30))) {
        Roblox::StopScanner();
        exit(0);
    }

    SectionHeader("Binds");
    KeybindButton("Menu Key", &g_toggleKey);
    KeybindButton("Self Destruct", &g_selfDestructKey);

    ImGui::NextColumn();
    SectionHeader("Configs");
    static char cfg_name[64] = "default";
    ImGui::PushItemWidth(-1);
    if (ImGui::InputText("##cfgname", cfg_name, sizeof(cfg_name), ImGuiInputTextFlags_EnterReturnsTrue)) {
        // Optional: handle enter key if needed
    }
    ImGui::PopItemWidth();
    float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button("Save", ImVec2(buttonWidth, 30)))
      Config::Save(cfg_name);
    ImGui::SameLine();
    if (ImGui::Button("Load", ImVec2(buttonWidth, 30)))
      Config::Load(cfg_name);

    ImGui::Spacing();
    if (std::filesystem::exists("config")) {
      ImGui::Text("Available:");
      ImGui::BeginChild("CfgList", ImVec2(-1, 100), true);
      for (const auto &entry : std::filesystem::directory_iterator("config")) {
        if (entry.path().extension() == ".cfg") {
          if (ImGui::Selectable(entry.path().stem().string().c_str()))
            strcpy_s(cfg_name, entry.path().stem().string().c_str());
        }
      }
      ImGui::EndChild();
    }

    ImGui::Columns(1);
  }

  ImGui::EndChild();
  ImGui::PopStyleVar(2); // Alpha, TabAlpha
  ImGui::End();
}
