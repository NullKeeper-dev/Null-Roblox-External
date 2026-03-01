#pragma once
#include "definitions.h"
#include <intrin.h>

#define LOG(fmt, ...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[Null] " fmt "\n", ##__VA_ARGS__)

typedef union _PTE_ENTRY {
    ULONG64 value;
    struct {
        ULONG64 Present        : 1;
        ULONG64 ReadWrite      : 1;
        ULONG64 UserSupervisor : 1;
        ULONG64 WriteThrough   : 1;
        ULONG64 CacheDisable   : 1;
        ULONG64 Accessed       : 1;
        ULONG64 Dirty          : 1;
        ULONG64 LargePage      : 1;
        ULONG64 Global         : 1;
        ULONG64 CopyOnWrite    : 1;
        ULONG64 Prototype      : 1;
        ULONG64 Reserved0      : 1;
        ULONG64 PageFrameNumber: 36;
        ULONG64 Reserved1      : 4;
        ULONG64 SoftwareWsIndex: 11;
        ULONG64 NoExecute      : 1;
    };
} PTE_ENTRY, *PPTE_ENTRY;

typedef struct _PTE_HOOK_STATE {
    PVOID       targetVA;
    PPTE_ENTRY  pteAddress;
    ULONG64     originalPfn;
    ULONG64     newPfn;
    PVOID       newPageVA;
    PHYSICAL_ADDRESS newPagePA;
    BOOLEAN     active;
} PTE_HOOK_STATE;

extern PTE_HOOK_STATE g_PteHook;

typedef PVOID (__fastcall *fn_MiGetPteAddress)(PVOID va);
extern fn_MiGetPteAddress pMiGetPteAddress;

BOOLEAN InstallPteHook(PVOID targetFunction, PVOID handlerAddr);

namespace PTEHook {
    void Shutdown();
    BOOLEAN Initialize();
}
