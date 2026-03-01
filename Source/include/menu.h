#pragma once
/*
 * menu.h - ImGui menu interface
 */

extern int g_toggleKey;
extern int g_selfDestructKey;

namespace Menu {
/* Toggle menu visibility */
void Toggle();

/* Check if menu is visible */
bool IsVisible();

/* Render the menu (call inside ImGui frame) */
void Render();

/* Get the menu toggle key */
int GetToggleKey();

/* Apply dark accent theme to ImGui */
void ApplyTheme();

/* Initialize Menu (Load default config) */
void Initialize();
} // namespace Menu
