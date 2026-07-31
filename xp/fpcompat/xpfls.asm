; Point the CRT's Vista+ import slots at the xp_* implementations above, so
; FlsAlloc/Free/GetValue/SetValue and GetNumaHighestNodeNumber are not imported
; from kernel32 (absent on Windows XP).
    .686
    .model flat
    extern _xp_FlsAlloc@4 : proc
    extern _xp_FlsFree@4 : proc
    extern _xp_FlsGetValue@4 : proc
    extern _xp_FlsSetValue@8 : proc
    extern _xp_GetNumaHighestNodeNumber@4 : proc
    extern _xp_InitializeCriticalSectionEx@12 : proc
    .data
    public __imp__FlsAlloc@4
    public __imp__FlsFree@4
    public __imp__FlsGetValue@4
    public __imp__FlsSetValue@8
    public __imp__GetNumaHighestNodeNumber@4
    public __imp__InitializeCriticalSectionEx@12
__imp__FlsAlloc@4                 dd _xp_FlsAlloc@4
__imp__FlsFree@4                  dd _xp_FlsFree@4
__imp__FlsGetValue@4              dd _xp_FlsGetValue@4
__imp__FlsSetValue@8              dd _xp_FlsSetValue@8
__imp__GetNumaHighestNodeNumber@4 dd _xp_GetNumaHighestNodeNumber@4
__imp__InitializeCriticalSectionEx@12 dd _xp_InitializeCriticalSectionEx@12
    end
