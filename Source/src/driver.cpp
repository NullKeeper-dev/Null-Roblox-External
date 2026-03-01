/*
 * driver.cpp - Usermode communication with Null Driver
 *
 * Resolves NtQueryCompositionSurfaceStatistics from win32u.dll and
 * uses it to send commands to the hooked dxgkrnl handler.
 */

#include "../include/driver.h"
#include "../include/console.h"
#include "../driver/shared.h"
#include "../Offsets/offsets.hpp"

/* NTSTATUS values (not available in usermode headers) */
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS          ((NTSTATUS)0x00000000L)
#endif
#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL     ((NTSTATUS)0xC0000001L)
#endif

/* Function pointer type for the hooked dxgkrnl function */
typedef NTSTATUS(NTAPI* fnNtQueryCompositionSurfaceStatistics)(PVOID);

static fnNtQueryCompositionSurfaceStatistics g_CallDriver = nullptr;
static bool g_Connected = false;

/* Send a request to the driver */
static NTSTATUS SendRequest(REQUEST_DATA* req) {
    if (!g_CallDriver) return STATUS_UNSUCCESSFUL;
    return g_CallDriver((PVOID)req);
}

bool Driver::Init() {
    /* Load win32u.dll (should already be loaded in GUI processes) */
    HMODULE hWin32u = LoadLibraryA("win32u.dll");
    if (!hWin32u) {
        Console::Error("Failed to load win32u.dll");
        return false;
    }

    g_CallDriver = (fnNtQueryCompositionSurfaceStatistics)
        GetProcAddress(hWin32u, "NtQueryCompositionSurfaceStatistics");

    if (!g_CallDriver) {
        Console::Error("Failed to resolve NtQueryCompositionSurfaceStatistics");
        return false;
    }

    Console::Log("Resolved hooked function at 0x%p", g_CallDriver);

    /* Ping the driver to verify it's loaded */
    REQUEST_DATA req{};
    req.magic = REQUEST_MAGIC;
    req.command = CMD_PING;
    req.result = 0;

    NTSTATUS status = SendRequest(&req);
    if (status != STATUS_SUCCESS) {
        Console::Error("Driver ping failed (NTSTATUS: 0x%08X)", status);
        return false;
    }

    if (req.result == 0x50544548) {
        Console::Success("Driver connected (PTE hook active)");
    }
    else if (req.result == 0x4B524E4C) {
        Console::Success("Driver connected (kernel hook active)");
    }
    else {
        Console::Error("Driver not responding (result: 0x%llX)", req.result);
        return false;
    }

    g_Connected = true;
    return true;
}

bool Driver::IsConnected() {
    return g_Connected;
}

uintptr_t Driver::GetModuleBase(DWORD pid, const wchar_t* moduleName) {
    REQUEST_DATA req{};
    req.magic = REQUEST_MAGIC;
    req.command = CMD_MODULE_BASE;
    req.pid = (unsigned __int64)pid;
    req.result = 0;

    /* Copy module name into the request */
    wcsncpy_s(req.module_name, 64, moduleName, _TRUNCATE);

    SendRequest(&req);
    return (uintptr_t)req.result;
}

bool Driver::ReadRaw(DWORD pid, uintptr_t address, void* buffer, size_t size) {
    if (!g_Connected || !buffer || !size) return false;

    REQUEST_DATA req{};
    req.magic = REQUEST_MAGIC;
    req.command = CMD_READ;
    req.pid = (unsigned __int64)pid;
    req.address = (unsigned __int64)address;
    req.buffer = (unsigned __int64)buffer;
    req.size = (unsigned __int64)size;
    req.result = 0;

    SendRequest(&req);
    return req.result != 0;
}

bool Driver::WriteRaw(DWORD pid, uintptr_t address, void* buffer, size_t size) {
    if (!g_Connected || !buffer || !size) return false;

    REQUEST_DATA req{};
    req.magic = REQUEST_MAGIC;
    req.command = CMD_WRITE;
    req.pid = (unsigned __int64)pid;
    req.address = (unsigned __int64)address;
    req.buffer = (unsigned __int64)buffer;
    req.size = (unsigned __int64)size;
    req.result = 0;

    SendRequest(&req);
    return req.result != 0;
}

std::string Driver::ReadRobloxString(DWORD pid, uintptr_t instanceAddr, uintptr_t nameOffset) {
    /*
     * Roblox uses Small String Optimization (SSO):
     * - At offset Name (0xB0):     16 bytes of data (inline chars OR pointer)
     * - At offset Name+0x10:       uint32_t length (StringLength)
     *
     * If length <= 15: the string data is inline at the Name offset
     * If length > 15:  the first 8 bytes at Name are a pointer to heap-allocated chars
     */

    uintptr_t nameAddr = instanceAddr + nameOffset;
    uint32_t len = Read<uint32_t>(pid, nameAddr + Offsets::Misc::StringLength);

    if (len == 0 || len > 256) return "";

    char buf[257] = {};

    if (len <= 15) {
        /* Inline string - data is at nameAddr directly */
        ReadRaw(pid, nameAddr, buf, len);
    }
    else {
        /* Heap-allocated - first 8 bytes are a pointer */
        uintptr_t strPtr = Read<uintptr_t>(pid, nameAddr);
        if (!strPtr) return "";
        ReadRaw(pid, strPtr, buf, min(len, (uint32_t)256));
    }

    buf[min(len, (uint32_t)256)] = '\0';
    return std::string(buf);
}
