#pragma once
#include <cstdint>
#include <string>
namespace Offsets {
    inline std::string ClientVersion = "version-760d064d05424689";
    namespace Instance {
         inline constexpr uintptr_t ClassDescriptor = 0x18;
         inline constexpr uintptr_t ClassName = 0x8;
         inline constexpr uintptr_t Name = 0xb0;
         inline constexpr uintptr_t Parent = 0x68;
         inline constexpr uintptr_t ChildrenStart = 0x70;
         inline constexpr uintptr_t ChildrenEnd = 0x8;
    }
    namespace Humanoid {
         inline constexpr uintptr_t Health = 0x194;
         inline constexpr uintptr_t MaxHealth = 0x1b4;
         inline constexpr uintptr_t HumanoidRootPart = 0x4c0;
         inline constexpr uintptr_t Walkspeed = 0x1d4;
         inline constexpr uintptr_t GetState = 0x21c;
    }
    namespace Primitive {
         inline constexpr uintptr_t Position = 0xe4;
    }
    namespace Player {
         inline constexpr uintptr_t LocalPlayer = 0x130;
         inline constexpr uintptr_t ModelInstance = 0x380;
         inline constexpr uintptr_t Team = 0x290;
    }
    namespace Misc {
         inline constexpr uintptr_t StringLength = 0x10;
    }
    namespace VisualEngine {
         inline constexpr uintptr_t Dimensions = 0x720;
         inline constexpr uintptr_t FakeDataModel = 0x700;
         inline constexpr uintptr_t Pointer = 0x7a36cd8;
         inline constexpr uintptr_t RenderView = 0x800;
         inline constexpr uintptr_t ViewMatrix = 0x120;
    }
    namespace DataModel {
         inline constexpr uintptr_t Workspace = 0x178;
    }
    namespace Camera {
         inline constexpr uintptr_t CameraSubject = 0xe8;
    }
    namespace Workspace {
         inline constexpr uintptr_t CurrentCamera = 0x460;
    }
    namespace FakeDataModel {
         inline constexpr uintptr_t Pointer = 0x7e83168;
         inline constexpr uintptr_t RealDataModel = 0x1c0;
    }
    namespace BasePart {
         inline constexpr uintptr_t Primitive = 0x148;
    }
}
