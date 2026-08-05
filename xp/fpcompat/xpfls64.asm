; The x64 half of xpfls.asm: point the CRT's Vista+ import slots at the xp_*
; implementations in xpfls.c, so FlsAlloc/Free/GetValue/SetValue,
; GetNumaHighestNodeNumber and InitializeCriticalSectionEx are not imported from
; kernel32.
;
; Two things differ from the 32-bit file and only two:
;   * no stdcall decoration. On x64 there is one calling convention, so the
;     names are plain - _xp_FlsAlloc@4 here is simply xp_FlsAlloc, and the
;     import slot is __imp_FlsAlloc rather than __imp__FlsAlloc@4.
;   * an import slot holds a 64-bit address, so dq instead of dd.
;
; Windows XP Professional x64 Edition is NT 5.2 - the Server 2003 kernel - and
; its kernel32 DOES export the Fls* family and GetNumaHighestNodeNumber, unlike
; the 32-bit XP SP3 one. InitializeCriticalSectionEx is Vista+ on both, so at
; least one slot here is load-bearing; the rest are kept because the 14.16 CRT
; asks for them, resolving them locally costs nothing, and a build that does not
; depend on which service pack the target carries is the safer build.
;
; Assembled by xp/bootstrap_toolchain.ps1 with ml64.exe into xpfls_asm.obj - the
; same object name the 32-bit build produces, so Telegram/CMakeLists.txt names
; one file for both architectures.

option casemap:none

extern xp_FlsAlloc : proc
extern xp_FlsFree : proc
extern xp_FlsGetValue : proc
extern xp_FlsSetValue : proc
extern xp_GetNumaHighestNodeNumber : proc
extern xp_InitializeCriticalSectionEx : proc

.data

public __imp_FlsAlloc
public __imp_FlsFree
public __imp_FlsGetValue
public __imp_FlsSetValue
public __imp_GetNumaHighestNodeNumber
public __imp_InitializeCriticalSectionEx

__imp_FlsAlloc                    dq xp_FlsAlloc
__imp_FlsFree                     dq xp_FlsFree
__imp_FlsGetValue                 dq xp_FlsGetValue
__imp_FlsSetValue                 dq xp_FlsSetValue
__imp_GetNumaHighestNodeNumber    dq xp_GetNumaHighestNodeNumber
__imp_InitializeCriticalSectionEx dq xp_InitializeCriticalSectionEx

end
