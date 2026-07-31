// xp_compat.dll — minimal Vista+ kernel32 API shim for XP target.
// Built without CRT (/NODEFAULTLIB) — provides its own DllMain entry.
//
// Provides fallbacks for SRWLOCK, CONDITION_VARIABLE, FLS, INIT_ONCE
// using CRITICAL_SECTION + semaphore + TLS primitives that XP has.
//
// Built as a side-loaded DLL: Telegram.exe links xp_compat.lib (import lib)
// instead of resolving these symbols from kernel32.lib. At runtime the XP
// loader pulls xp_compat.dll from the .exe directory.
//
// SRWLOCK is a single PVOID. We treat its storage as a pointer to a
// heap-allocated CRITICAL_SECTION, allocated lazily on first acquire.
// Reader/writer distinction is collapsed to plain mutual exclusion — Qt
// and STL paths on XP will be slightly slower but correct.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define XP_API __declspec(dllexport) WINAPI

static CRITICAL_SECTION *srw_cs(PSRWLOCK lock) {
    CRITICAL_SECTION *cs = (CRITICAL_SECTION *)lock->Ptr;
    if (cs) {
        return cs;
    }
    cs = (CRITICAL_SECTION *)HeapAlloc(GetProcessHeap(), 0, sizeof(CRITICAL_SECTION));
    InitializeCriticalSection(cs);
    CRITICAL_SECTION *prev = (CRITICAL_SECTION *)InterlockedCompareExchangePointer(&lock->Ptr, cs, NULL);
    if (prev) {
        DeleteCriticalSection(cs);
        HeapFree(GetProcessHeap(), 0, cs);
        return prev;
    }
    return cs;
}

VOID XP_API InitializeSRWLock(PSRWLOCK lock) {
    lock->Ptr = NULL;
}

VOID XP_API AcquireSRWLockExclusive(PSRWLOCK lock) {
    EnterCriticalSection(srw_cs(lock));
}

VOID XP_API AcquireSRWLockShared(PSRWLOCK lock) {
    EnterCriticalSection(srw_cs(lock));
}

BOOLEAN XP_API TryAcquireSRWLockExclusive(PSRWLOCK lock) {
    return TryEnterCriticalSection(srw_cs(lock)) ? TRUE : FALSE;
}

BOOLEAN XP_API TryAcquireSRWLockShared(PSRWLOCK lock) {
    return TryEnterCriticalSection(srw_cs(lock)) ? TRUE : FALSE;
}

VOID XP_API ReleaseSRWLockExclusive(PSRWLOCK lock) {
    LeaveCriticalSection(srw_cs(lock));
}

VOID XP_API ReleaseSRWLockShared(PSRWLOCK lock) {
    LeaveCriticalSection(srw_cs(lock));
}

// Condition Variable: implemented as a semaphore with a waiter count.
// Each Sleep increments waiters, releases the user lock, waits on the
// semaphore, then re-acquires. Wake* releases N waiters from the semaphore.
typedef struct cv_data {
    LONG waiters;
    HANDLE sema;
} cv_data;

static cv_data *cv_get(PCONDITION_VARIABLE cv) {
    cv_data *p = (cv_data *)cv->Ptr;
    if (p) {
        return p;
    }
    p = (cv_data *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(cv_data));
    p->sema = CreateSemaphoreW(NULL, 0, 0x7fffffff, NULL);
    cv_data *prev = (cv_data *)InterlockedCompareExchangePointer(&cv->Ptr, p, NULL);
    if (prev) {
        CloseHandle(p->sema);
        HeapFree(GetProcessHeap(), 0, p);
        return prev;
    }
    return p;
}

VOID XP_API InitializeConditionVariable(PCONDITION_VARIABLE cv) {
    cv->Ptr = NULL;
}

BOOL XP_API SleepConditionVariableCS(PCONDITION_VARIABLE cv, PCRITICAL_SECTION cs, DWORD ms) {
    cv_data *p = cv_get(cv);
    InterlockedIncrement(&p->waiters);
    LeaveCriticalSection(cs);
    DWORD r = WaitForSingleObject(p->sema, ms);
    EnterCriticalSection(cs);
    if (r != WAIT_OBJECT_0) {
        InterlockedDecrement(&p->waiters);
        SetLastError(r == WAIT_TIMEOUT ? ERROR_TIMEOUT : ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return TRUE;
}

BOOL XP_API SleepConditionVariableSRW(PCONDITION_VARIABLE cv, PSRWLOCK lock, DWORD ms, ULONG flags) {
    (void)flags;
    cv_data *p = cv_get(cv);
    CRITICAL_SECTION *cs = srw_cs(lock);
    InterlockedIncrement(&p->waiters);
    LeaveCriticalSection(cs);
    DWORD r = WaitForSingleObject(p->sema, ms);
    EnterCriticalSection(cs);
    if (r != WAIT_OBJECT_0) {
        InterlockedDecrement(&p->waiters);
        SetLastError(r == WAIT_TIMEOUT ? ERROR_TIMEOUT : ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return TRUE;
}

VOID XP_API WakeConditionVariable(PCONDITION_VARIABLE cv) {
    cv_data *p = (cv_data *)cv->Ptr;
    if (!p) {
        return;
    }
    LONG w = InterlockedDecrement(&p->waiters);
    if (w < 0) {
        InterlockedIncrement(&p->waiters);
        return;
    }
    ReleaseSemaphore(p->sema, 1, NULL);
}

VOID XP_API WakeAllConditionVariable(PCONDITION_VARIABLE cv) {
    cv_data *p = (cv_data *)cv->Ptr;
    if (!p) {
        return;
    }
    LONG w = InterlockedExchange(&p->waiters, 0);
    if (w > 0) {
        ReleaseSemaphore(p->sema, w, NULL);
    } else if (w < 0) {
        InterlockedExchangeAdd(&p->waiters, w);
    }
}

// FLS: we don't run destructor callbacks. Telegram uses Fls only via the CRT
// for stdlib thread-local cleanup; we simply alias to TLS slots. Memory leaks
// on thread/process exit are accepted on XP (process lifetime is short).

DWORD XP_API FlsAlloc(PFLS_CALLBACK_FUNCTION callback) {
    (void)callback;
    return TlsAlloc();
}

BOOL XP_API FlsFree(DWORD index) {
    return TlsFree(index);
}

PVOID XP_API FlsGetValue(DWORD index) {
    return TlsGetValue(index);
}

BOOL XP_API FlsSetValue(DWORD index, PVOID value) {
    return TlsSetValue(index, value);
}

// INIT_ONCE: single-pointer storage; encode state in low bits.
// 0 = uninit, 1 = pending, 2 = done. Anyone arriving in pending state spins.
// This matches Windows' synchronous InitOnce semantics; async (BeginInitialize
// returning to pump) is not exposed via InitOnceBeginInitialize used by MSVC
// STL — STL uses it as a simple "first to start, others wait" pattern.

BOOL XP_API InitOnceBeginInitialize(LPINIT_ONCE once, DWORD flags, PBOOL pending, LPVOID *context) {
    (void)flags;
    if (context) {
        *context = NULL;
    }
    for (;;) {
        PVOID v = once->Ptr;
        ULONG_PTR s = (ULONG_PTR)v & 3;
        if (s == 2) {
            if (pending) {
                *pending = FALSE;
            }
            if (context) {
                *context = (PVOID)((ULONG_PTR)v & ~(ULONG_PTR)3);
            }
            return TRUE;
        }
        if (s == 0) {
            if (InterlockedCompareExchangePointer(&once->Ptr, (PVOID)(ULONG_PTR)1, NULL) == NULL) {
                if (pending) {
                    *pending = TRUE;
                }
                return TRUE;
            }
            continue;
        }
        Sleep(0);
    }
}

BOOL XP_API InitOnceComplete(LPINIT_ONCE once, DWORD flags, LPVOID context) {
    (void)flags;
    ULONG_PTR ctx = (ULONG_PTR)context;
    if (ctx & 3) {
        return FALSE;
    }
    once->Ptr = (PVOID)(ctx | 2);
    return TRUE;
}

// InitializeCriticalSectionEx — Vista+. CRITICAL_SECTION_NO_DEBUG_INFO (0x01000000)
// and CRITICAL_SECTION_DYNAMIC_SPIN (0x02000000) flags are ignored on XP.
BOOL XP_API InitializeCriticalSectionEx(LPCRITICAL_SECTION cs, DWORD spinCount, DWORD flags) {
    (void)flags;
    return InitializeCriticalSectionAndSpinCount(cs, spinCount);
}

// GetModuleHandleExW — XP SP2+, but emulate just in case some app builds
// run on XP RTM/SP1. GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS = 0x4.
BOOL XP_API GetModuleHandleExW(DWORD flags, LPCWSTR name, HMODULE *phModule) {
    if (!phModule) {
        return FALSE;
    }
    if (flags & 0x4) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((LPCVOID)name, &mbi, sizeof(mbi))) {
            *phModule = (HMODULE)mbi.AllocationBase;
            return TRUE;
        }
        return FALSE;
    }
    HMODULE h = GetModuleHandleW(name);
    if (!h) {
        return FALSE;
    }
    *phModule = h;
    return TRUE;
}

// GetUserDefaultUILanguage — Windows 2000+, but provide a shim for safety;
// some XP builds may have stripped this from kernel32. Fallback uses the
// system default UI language (PRIMARYLANGID via GetSystemDefaultLangID).
LANGID XP_API GetUserDefaultUILanguage(void) {
    return GetSystemDefaultLangID();
}

// GetThreadGroupAffinity — Win7+. XP has no processor groups, only single
// processor mask. Synthesize: group 0 with full affinity mask from XP's
// GetProcessAffinityMask.
typedef struct _XP_GROUP_AFFINITY {
    ULONG_PTR Mask;
    WORD Group;
    WORD Reserved[3];
} XP_GROUP_AFFINITY, *PXP_GROUP_AFFINITY;

BOOL XP_API GetThreadGroupAffinity(HANDLE thread, PXP_GROUP_AFFINITY affinity) {
    (void)thread;
    if (!affinity) {
        return FALSE;
    }
    DWORD_PTR processMask = 0, systemMask = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask)) {
        processMask = (ULONG_PTR)-1;
    }
    affinity->Mask = processMask;
    affinity->Group = 0;
    affinity->Reserved[0] = affinity->Reserved[1] = affinity->Reserved[2] = 0;
    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)inst; (void)reason; (void)reserved;
    return TRUE;
}
