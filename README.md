# Null Roblox External

A high-performance, driver-based external overlay for Roblox, designed for educational exploration of game engine internals, memory manipulation, and kernel-mode communication.

**DISCLAIMER: This project is for EDUCATIONAL PURPOSES ONLY. I am not responsible for any misuse, bans, or damages caused by this software. Use at your own risk.**

## Overview

Null Roblox External demonstrates advanced C++ techniques for interacting with protected processes using a kernel-mode driver (Null Driver) to bypass standard user-mode limitations. It features a transparent DirectX 11 overlay with a modern ImGui-based menu for real-time configuration.

## Key Features

- **Kernel-Mode Driver:** Bypasses user-mode memory protections using a custom driver for RPM/WPM.
- **Advanced ESP (Extra Sensory Perception):**
  - Box, Name, and Health displays.
  - Skeleton rendering with customizable colors and "glow" effects.
  - R6 and R15 character model support.
  - Distance-based scaling and team checks.
- **High-Performance Aimbot:**
  - Smooth mouse movement with configurable interpolation.
  - Customizable FOV (Field of View) with visual circle.
  - Target selection (Head, Torso, Legs).
  - Vertical offset adjustment.
- **Modern UI/UX:**
  - Sleek, animated ImGui menu with "PassatHook" design.
  - Real-time performance telemetry (FPS/Frame time).
  - Tabbed interface for easy navigation.
  - Config system (Save/Load settings).
- **Process Robustness:**
  - Automatic process detection and attachment.
  - Background scanner for dynamic DataModel and player list updates.
  - Window-matching logic to keep overlay perfectly synced with the game.

## Tech Stack

- **Language:** C++20
- **Graphics API:** DirectX 11
- **UI Framework:** ImGui (with custom animations)
- **Kernel:** Windows Driver Kit (WDK) for the driver
- **Target OS:** Windows 10/11 (x64)

## Installation & Setup

### Usage
1. Launch Roblox.
2. Run the `launch.bat` file.
3. Press `INSERT` to toggle the menu.
4. Press `END` to exit the application safely.

## Contributing

Please refer to [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines on how to contribute to this project.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Target Audience

- Security researchers interested in kernel-mode development.
- C++ developers learning about game engine internals and memory forensics.
- Educational enthusiasts exploring DirectX overlay techniques.

---
*Developed by [NullKeeper-dev](https://github.com/NullKeeper-dev)*
