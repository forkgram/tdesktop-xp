# XP-safety check for the port's Telegram.exe -- run it before EVERY deploy.
#
# Two independent ways a build that links fine still cannot start on XP:
#   1. a DLL that does not exist on XP imported as NORMAL (not DELAYLOAD) -- the
#      loader fails before main();
#   2. a FUNCTION that XP's copy of an existing DLL does not export -- "The
#      procedure entry point X could not be located in KERNEL32.dll", also before
#      main(). Checking DLLs alone cannot see this.
# Both are checked here. Neither is visible to a smoke screenshot: the process
# exists (a modal error box) and can even leave an old window on screen.
#   powershell <repo>\xp\xpsafe.ps1 [-Exe out\cmb\Telegram.exe] [-Dumpbin ...]
param(
  [string]$Exe = "C:\TBuild\xp-port\tdesktop-walk\out\cmb\Telegram.exe",
  [string]$Dumpbin = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe"
)
if (-not (Test-Path $Exe)) { Write-Output "XPSAFE: FAIL exe not found: $Exe"; exit 2 }

# dumpbin ships with MSVC, not with Windows, and this workstation's copy is only
# a default. Resolve it here rather than making every caller know where a toolset
# lives: a runner has its own layout, and a caller that guesses wrong silently
# hands over an empty path - which is how the release job skipped this gate.
# Order: an explicit -Dumpbin, then PATH (a vcvars'd shell), then every installed
# Visual Studio instance, newest toolset first.
$dumpbin = if ($Dumpbin -and (Test-Path $Dumpbin)) { $Dumpbin } else { '' }
if (-not $dumpbin) {
  $onPath = Get-Command dumpbin.exe -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($onPath) { $dumpbin = $onPath.Source }
}
if (-not $dumpbin) {
  $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  if (Test-Path $vswhere) {
    foreach ($instance in (& $vswhere -products * -all -prerelease -property installationPath)) {
      $found = Get-ChildItem (Join-Path $instance 'VC\Tools\MSVC') -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        ForEach-Object { Join-Path $_.FullName 'bin\Hostx64\x64\dumpbin.exe' } |
        Where-Object { Test-Path $_ } | Select-Object -First 1
      if ($found) { $dumpbin = $found; break }
    }
  }
}
if (-not $dumpbin) { Write-Output 'XPSAFE: FAIL dumpbin.exe not found - pass -Dumpbin'; exit 2 }
Write-Output "XPSAFE dumpbin: $dumpbin"

# DLLs that DO NOT EXIST on Windows XP -> a NORMAL import loader-crashes at startup.
$failDlls = @(
  'dwmapi','combase','d3d11','d3d10','d3d12','dcomp','dxgi','dwrite',
  'propsys','shcore','bcrypt','ncrypt','windows.storage','wlanapi',
  'mfplat','mf','mfreadwrite','d2d1','dwmredir'
)
# Present on XP but expose Vista+ functions -> NORMAL import is usually fine, but
# verify only XP-era functions are used (regression watch).
$warnDlls = @('uxtheme')

$raw = & $dumpbin /DEPENDENTS $Exe 2>&1 | Out-String
$lines = $raw -split "`r?`n"

$normal = New-Object System.Collections.Generic.List[string]
$delay  = New-Object System.Collections.Generic.List[string]
$mode = 0   # 1=normal deps, 2=delay deps
foreach ($ln in $lines) {
  if ($ln -match 'following dependencies:') { $mode = 1; continue }
  if ($ln -match 'following delay load dependencies:') { $mode = 2; continue }
  if ($ln -match 'Summary') { $mode = 0; continue }
  $t = $ln.Trim()
  if ($t -match '^[\w\.\-]+\.dll$') {
    if ($mode -eq 1) { $normal.Add($t.ToLower()) }
    elseif ($mode -eq 2) { $delay.Add($t.ToLower()) }
  }
}

$problems = @()
$warnings = @()
foreach ($d in $normal) {
  $base = $d -replace '\.dll$',''
  if ($failDlls -contains $base -or $base -like 'api-ms-win-*') {
    $problems += $d
  } elseif ($warnDlls -contains $base) {
    $warnings += $d
  }
}

Write-Output "XPSAFE check: $Exe"
Write-Output ("  NORMAL deps ({0}): {1}" -f $normal.Count, ($normal -join ' '))
Write-Output ("  DELAY  deps ({0}): {1}" -f $delay.Count, ($delay -join ' '))
foreach ($w in $warnings) { Write-Output "  WARN: $w is a NORMAL import (exists on XP; verify no Vista+ functions are used)" }

# --- ENTRY POINT check ---------------------------------------------------------
# The DLL-level check above is not enough: kernel32.dll and user32.dll DO exist on
# XP, but importing a function they only gained in Vista/7/8/10 fails the loader
# just as hard -- "The procedure entry point X could not be located in the dynamic
# link library KERNEL32.dll", before main() runs. This is how a 2026-07-31 build
# died on the VM after xpsafe had said PASS: linking a modern-MSVC static lib
# pulled libcpmt objects that import the SRW lock API (Vista+).
# Everything listed here is absent from Windows XP SP3's export tables.
$failFuncs = @(
  # Slim reader/writer locks, condition variables, one-time init -- all Vista+
  'InitializeSRWLock','AcquireSRWLockExclusive','AcquireSRWLockShared',
  'ReleaseSRWLockExclusive','ReleaseSRWLockShared','TryAcquireSRWLockExclusive',
  'TryAcquireSRWLockShared',
  'InitializeConditionVariable','SleepConditionVariableCS',
  'SleepConditionVariableSRW','WakeConditionVariable','WakeAllConditionVariable',
  'InitOnceExecuteOnce','InitOnceBeginInitialize','InitOnceComplete',
  # Misc Vista+/Win7+/Win8+ kernel32-user32 exports we have hit before
  'GetTickCount64','QueryUnbiasedInterruptTime','GetLogicalProcessorInformationEx',
  'SetThreadGroupAffinity','GetCurrentProcessorNumberEx','CreateThreadpoolWork',
  'SubmitThreadpoolWork','CloseThreadpoolWork','CreateThreadpoolTimer',
  'SetThreadpoolTimer','WaitForThreadpoolTimerCallbacks','CloseThreadpoolTimer',
  'GetFinalPathNameByHandleW','SetFileInformationByHandle','GetFileInformationByHandleEx',
  'CompareStringOrdinal','GetUserDefaultLocaleName','LocaleNameToLCID',
  'LCIDToLocaleName','GetLocaleInfoEx','GetDpiForWindow','GetSystemMetricsForDpi',
  'AdjustWindowRectExForDpi','GetDpiForSystem','AreDpiAwarenessContextsEqual',
  'SetProcessDpiAwarenessContext','GetThreadDpiAwarenessContext',
  'PowerCreateRequest','PowerSetRequest','PowerClearRequest',
  'RegisterPowerSettingNotification','UnregisterPowerSettingNotification',
  'CancelIoEx','GetQueuedCompletionStatusEx','RegGetValueW','RegSetKeyValueW',
  'IsWow64Process2','SetDefaultDllDirectories','AddDllDirectory'
)
# Only DLLs that exist on XP are worth scanning -- a missing DLL is already fatal
# above, and Vista+ DLLs are delay-loaded on purpose.
$scanDlls = @('kernel32.dll','user32.dll','advapi32.dll','gdi32.dll','shell32.dll',
  'shlwapi.dll','ole32.dll','oleaut32.dll','ws2_32.dll','msvcrt.dll','version.dll',
  'imm32.dll','winmm.dll','crypt32.dll','iphlpapi.dll','netapi32.dll','mpr.dll',
  'userenv.dll','wtsapi32.dll')

$impRaw = & $dumpbin /IMPORTS $Exe 2>&1 | Out-String
$impLines = $impRaw -split "`r?`n"
$currentDll = ''
$badFuncs = New-Object System.Collections.Generic.List[string]
foreach ($ln in $impLines) {
  $t = $ln.Trim()
  if ($t -match '^([\w\.\-]+\.dll)$') { $currentDll = $t.ToLower(); continue }
  if (-not ($scanDlls -contains $currentDll)) { continue }
  # dumpbin prints "  <hint> <name>" for named imports.
  if ($t -match '^[0-9A-Fa-f]+\s+(\w+)$') {
    $fn = $matches[1]
    if ($failFuncs -contains $fn) { $badFuncs.Add("$currentDll!$fn") }
  }
}
$badFuncs = $badFuncs | Sort-Object -Unique
if ($badFuncs.Count -gt 0) {
  Write-Output ("  ENTRY POINTS absent on XP ({0}): {1}" -f $badFuncs.Count, ($badFuncs -join ' '))
} else {
  Write-Output "  ENTRY POINTS: none of the known Vista+ exports are imported."
}

if ($problems.Count -gt 0) {
  Write-Output ("XPSAFE: FAIL -- Vista+ DLL(s) NORMAL-imported (loader-crash on XP): {0}" -f ($problems -join ' '))
  Write-Output "  -> move to DELAYLOAD or GetProcAddress, or the build will not launch on XP."
  exit 1
}
if ($badFuncs.Count -gt 0) {
  Write-Output "XPSAFE: FAIL -- Vista+ ENTRY POINT(s) statically imported; XP shows"
  Write-Output "  'The procedure entry point ... could not be located' and never reaches main()."
  Write-Output "  -> find the object that pulls it (usually a third-party lib built with a"
  Write-Output "     modern toolset dragging libcpmt), rebuild it with v141_xp, or drop it."
  exit 1
}
Write-Output "XPSAFE: PASS -- no absent-on-XP DLL is NORMAL-imported, no Vista+ entry point."
exit 0
