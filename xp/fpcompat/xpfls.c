/* XP thunks for the Vista+ kernel32 APIs the static 14.16 CRT imports.
 * Linked as direct objects (before kernel32.lib) so the __imp_ slots in
 * xpfls.asm point here and nothing is imported from kernel32. The app uses no
 * fibers (FLS==TLS) and is not NUMA-aware on XP. No crash; minor: thread_local
 * dtors won't run at thread exit. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
DWORD WINAPI xp_FlsAlloc(void *cb) { (void)cb; return TlsAlloc(); }
BOOL  WINAPI xp_FlsFree(DWORD i) { return TlsFree(i); }
void *WINAPI xp_FlsGetValue(DWORD i) { return TlsGetValue(i); }
BOOL  WINAPI xp_FlsSetValue(DWORD i, void *v) { return TlsSetValue(i, v); }
BOOL  WINAPI xp_GetNumaHighestNodeNumber(PULONG n) { if (n) *n = 0; return TRUE; }
/* InitializeCriticalSectionEx(cs, spin, flags) is Vista+; map to the XP
 * InitializeCriticalSectionAndSpinCount (ignore the flags). */
BOOL WINAPI xp_InitializeCriticalSectionEx(LPCRITICAL_SECTION cs, DWORD spin, DWORD flags) {
    (void)flags;
    return InitializeCriticalSectionAndSpinCount(cs, spin);
}
