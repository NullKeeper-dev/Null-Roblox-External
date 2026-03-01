#include "driver.h"
#include "hook.h"
#include "cleaner.h"
#include "pte_hook.h"

// Simple logging macro for the loader console to see
#define LOG(fmt, ...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[Null] " fmt "\n", ##__VA_ARGS__)

void volatile __stdcall JunkFunction_0x7A() {
    volatile int x = 0xDEADBEEF;
    x ^= 0x1337;
    x = (x << 2) | (x >> 30);
}

static NTSTATUS RealEntry(PDRIVER_OBJECT DriverObject)
{
    LOG("Entering RealEntry");
    
    /* 1. Install dxgkrnl hook */
    if (!Hook::Install((void*)Hook::Handler)) {
        LOG("Hook::Install FAILED");
        return STATUS_UNSUCCESSFUL;
    }
    LOG("Hook::Install SUCCESS");

    /* 2. Clean all traces and hide driver */
    LOG("Cleaning traces...");
    CleanAllTraces(DriverObject);
    LOG("Traces cleaned and driver hidden");

    return STATUS_SUCCESS;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT driver_obj, PUNICODE_STRING registry_path) {
    UNREFERENCED_PARAMETER(driver_obj);
    UNREFERENCED_PARAMETER(registry_path);
    
    LOG("DriverEntry Called");
    JunkFunction_0x7A();

    // PTE Hook Initialization
    LOG("Initializing PTE Hook...");
    if (!PTEHook::Initialize()) {
        LOG("PTEHook::Initialize FAILED");
        return STATUS_UNSUCCESSFUL;
    }
    LOG("PTEHook::Initialize SUCCESS");

    NTSTATUS status = RealEntry(driver_obj);
    LOG("RealEntry returned status: 0x%X", status);
    
    return status;
}
