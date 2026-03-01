#pragma once
/*
 * driver.h - Usermode communication with Null Driver
 *
 * Calls the hooked NtQueryCompositionSurfaceStatistics in dxgkrnl.sys
 * to dispatch R/W commands to the kernel driver.
 */

#include <Windows.h>
#include <cstdint>
#include <string>

/* Import shared definitions (REQUEST_DATA, CMD_*, REQUEST_MAGIC) */
#include "../driver/shared.h"

namespace Driver {

    /* Resolve the hooked function from win32u.dll and ping the driver */
    bool Init();

    /* Check if the driver is connected */
    bool IsConnected();

    /* Get module base address in target process */
    uintptr_t GetModuleBase(DWORD pid, const wchar_t* moduleName);

    /* Read memory from target process */
    bool ReadRaw(DWORD pid, uintptr_t address, void* buffer, size_t size);

    /* Write memory to target process */
    bool WriteRaw(DWORD pid, uintptr_t address, void* buffer, size_t size);

    /* Templated read */
    template <typename T>
    T Read(DWORD pid, uintptr_t address) {
        T result{};
        ReadRaw(pid, address, &result, sizeof(T));
        return result;
    }

    /* Templated write */
    template <typename T>
    bool Write(DWORD pid, uintptr_t address, const T& value) {
        return WriteRaw(pid, address, (void*)&value, sizeof(T));
    }

    /* Read a Roblox string (SSO-aware) */
    std::string ReadRobloxString(DWORD pid, uintptr_t instanceAddr, uintptr_t nameOffset);
}
