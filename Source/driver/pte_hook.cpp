#include "pte_hook.h"
#include "hook.h"
#include <intrin.h>

PTE_HOOK_STATE g_PteHook = { 0 };
fn_MiGetPteAddress pMiGetPteAddress = NULL;

static BOOLEAN FindMiGetPteAddress()
{
    ULONG bytes = 0;
    ZwQuerySystemInformation(SystemModuleInformation, NULL, 0, &bytes);
    if (!bytes) return FALSE;

    PRTL_PROCESS_MODULES modules = (PRTL_PROCESS_MODULES)ExAllocatePoolWithTag(NonPagedPool, bytes, 'etPK');
    if (!modules) return FALSE;

    if (!NT_SUCCESS(ZwQuerySystemInformation(SystemModuleInformation, modules, bytes, &bytes))) {
        ExFreePoolWithTag(modules, 'etPK');
        return FALSE;
    }

    PVOID ntBase = modules->Modules[0].ImageBase;
    ExFreePoolWithTag(modules, 'etPK');
    if (!ntBase) return FALSE;

    PIMAGE_NT_HEADERS ntHeaders = RtlImageNtHeader(ntBase);
    if (!ntHeaders) return FALSE;
    ULONG ntSize = ntHeaders->OptionalHeader.SizeOfImage;

    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(ntHeaders);
    for (USHORT i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++, section++) {
        if (section->Misc.VirtualSize == 0) continue;
        PUCHAR start = (PUCHAR)ntBase + section->VirtualAddress;
        ULONG  size  = section->Misc.VirtualSize;
        if (size < 31) continue;
        for (ULONG j = 0; j + 31 <= size; j++) {
            if (start[j + 0]  == 0x48 && start[j + 1]  == 0xC1 && start[j + 2]  == 0xE9 && start[j + 3]  == 0x09 &&
                start[j + 4]  == 0x48 && start[j + 5]  == 0xB8 && start[j + 14] == 0x48 && start[j + 15] == 0x23 &&
                start[j + 16] == 0xC8 && start[j + 17] == 0x48 && start[j + 18] == 0xB8 && start[j + 27] == 0x48 &&
                start[j + 28] == 0x03 && start[j + 29] == 0xC1 && start[j + 30] == 0xC3)
            {
                pMiGetPteAddress = (fn_MiGetPteAddress)(&start[j]);
                return TRUE;
            }
        }
    }
    return FALSE;
}

static PPTE_ENTRY GetPte(PVOID va) { if (!pMiGetPteAddress) return NULL; return (PPTE_ENTRY)pMiGetPteAddress(va); }
static PPTE_ENTRY GetPde(PVOID va) { if (!pMiGetPteAddress) return NULL; return (PPTE_ENTRY)pMiGetPteAddress(pMiGetPteAddress(va)); }

BOOLEAN InstallPteHook(PVOID targetFunction, PVOID handlerAddr)
{
    if (!targetFunction || !handlerAddr) return FALSE;
    if (!pMiGetPteAddress && !FindMiGetPteAddress()) {
        LOG("Failed to find MiGetPteAddress");
        return FALSE;
    }

    ULONG64 targetVA = (ULONG64)targetFunction;
    ULONG64 pageBase = targetVA & ~0xFFFULL;
    ULONG   pageOffset = (ULONG)(targetVA & 0xFFF);

    PPTE_ENTRY pde = GetPde((PVOID)pageBase);
    if (!pde || !MmIsAddressValid(pde)) {
        LOG("PDE lookup failed for 0x%p", (PVOID)pageBase);
        return FALSE;
    }

    PTE_ENTRY pdeEntry;
    pdeEntry.value = pde->value;
    if (!pdeEntry.Present) {
        LOG("PDE not present for 0x%p", (PVOID)pageBase);
        return FALSE;
    }

    if (pdeEntry.LargePage) {
        LOG("Splitting large page at 0x%p", (PVOID)pageBase);
        PHYSICAL_ADDRESS low, high, boundary;
        low.QuadPart = 0; high.QuadPart = 0xFFFFFFFFFFFFULL; boundary.QuadPart = 0;
        PVOID ptPage = MmAllocateContiguousMemorySpecifyCache(PAGE_SIZE, low, high, boundary, MmCached);
        if (!ptPage) return FALSE;
        PHYSICAL_ADDRESS ptPagePA = MmGetPhysicalAddress(ptPage);
        PPTE_ENTRY newPtEntries = (PPTE_ENTRY)ptPage;
        ULONG64 largePfn = pdeEntry.PageFrameNumber;
        for (int i = 0; i < 512; i++) {
            PTE_ENTRY pte; pte.value = 0; pte.Present = 1; pte.ReadWrite = pdeEntry.ReadWrite;
            pte.UserSupervisor = pdeEntry.UserSupervisor; pte.WriteThrough = pdeEntry.WriteThrough;
            pte.CacheDisable = pdeEntry.CacheDisable; pte.Accessed = 1; pte.Dirty = pdeEntry.Dirty;
            pte.Global = pdeEntry.Global; pte.NoExecute = pdeEntry.NoExecute; pte.PageFrameNumber = largePfn + i;
            newPtEntries[i] = pte;
        }
        KIRQL oldIrql = KeRaiseIrqlToDpcLevel();
        PTE_ENTRY newPde; newPde.value = pdeEntry.value; newPde.LargePage = 0; newPde.PageFrameNumber = ptPagePA.QuadPart >> 12;
        InterlockedExchange64((volatile LONG64*)&pde->value, newPde.value);
        ULONG64 largePageBase = targetVA & ~0x1FFFFFULL;
        for (int i = 0; i < 512; i++) __invlpg((PVOID)(largePageBase + (ULONG64)i * 0x1000));
        KeLowerIrql(oldIrql);
    }

    PPTE_ENTRY pte = GetPte((PVOID)pageBase);
    if (!pte || !MmIsAddressValid(pte)) {
        LOG("PTE lookup failed for 0x%p", (PVOID)pageBase);
        return FALSE;
    }
    PTE_ENTRY originalPte; originalPte.value = pte->value;
    if (!originalPte.Present) {
        LOG("PTE not present for 0x%p", (PVOID)pageBase);
        return FALSE;
    }

    PHYSICAL_ADDRESS low, high, boundary;
    low.QuadPart = 0; high.QuadPart = 0xFFFFFFFFFFFFULL; boundary.QuadPart = 0;
    PVOID newPage = MmAllocateContiguousMemorySpecifyCache(PAGE_SIZE, low, high, boundary, MmCached);
    if (!newPage) {
        LOG("Failed to allocate contiguous memory for new page");
        return FALSE;
    }
    PHYSICAL_ADDRESS newPagePA = MmGetPhysicalAddress(newPage);
    ULONG64 newPfn = newPagePA.QuadPart >> 12;

    PHYSICAL_ADDRESS origPA; origPA.QuadPart = (LONGLONG)originalPte.PageFrameNumber << 12;
    PVOID origMapped = MmMapIoSpace(origPA, PAGE_SIZE, MmCached);
    if (!origMapped) {
        LOG("Failed to map original physical page 0x%llX", origPA.QuadPart);
        MmFreeContiguousMemory(newPage);
        return FALSE;
    }
    RtlCopyMemory(newPage, origMapped, PAGE_SIZE);
    MmUnmapIoSpace(origMapped, PAGE_SIZE);

    PUCHAR hookSite = (PUCHAR)newPage + pageOffset;
    hookSite[0] = 0x48; hookSite[1] = 0xB8;
    uintptr_t addr = (uintptr_t)handlerAddr;
    RtlCopyMemory(&hookSite[2], &addr, sizeof(void*));
    hookSite[10] = 0xFF; hookSite[11] = 0xE0;

    KIRQL oldIrql = KeRaiseIrqlToDpcLevel();
    PTE_ENTRY newPte; newPte.value = originalPte.value; newPte.PageFrameNumber = newPfn;
    InterlockedExchange64((volatile LONG64*)&pte->value, newPte.value);
    __invlpg((PVOID)pageBase);
    KeLowerIrql(oldIrql);

    g_PteHook.targetVA = targetFunction; g_PteHook.pteAddress = pte;
    g_PteHook.originalPfn = originalPte.PageFrameNumber; g_PteHook.newPfn = newPfn;
    g_PteHook.newPageVA = newPage; g_PteHook.newPagePA = newPagePA; g_PteHook.active = TRUE;
    return TRUE;
}

namespace PTEHook {
    void Shutdown()
    {
        if (!g_PteHook.active || !g_PteHook.pteAddress) return;
        KIRQL oldIrql = KeRaiseIrqlToDpcLevel();
        PTE_ENTRY restored; restored.value = g_PteHook.pteAddress->value;
        restored.PageFrameNumber = g_PteHook.originalPfn;
        InterlockedExchange64((volatile LONG64*)&g_PteHook.pteAddress->value, restored.value);
        ULONG64 pageBase = (ULONG64)g_PteHook.targetVA & ~0xFFFULL;
        __invlpg((PVOID)pageBase);
        KeLowerIrql(oldIrql);
        if (g_PteHook.newPageVA) MmFreeContiguousMemory(g_PteHook.newPageVA);
        g_PteHook.active = FALSE;
    }

    BOOLEAN Initialize() {
        LOG("PTEHook::Initialize Called");
        if (!pMiGetPteAddress && !FindMiGetPteAddress()) {
            LOG("Failed to find MiGetPteAddress");
            return FALSE;
        }
        LOG("MiGetPteAddress resolved at 0x%p", pMiGetPteAddress);
        return TRUE;
    }
}
