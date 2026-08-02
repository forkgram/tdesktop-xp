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
#
#   powershell <repo>\xp\xpsafe.ps1 [-Exe out\cmb\Telegram.exe] [-Dumpbin ...]
#
# The function check is a WHITELIST, not a list of known-bad names: xp\exports\*.txt
# holds the real export tables of the target XP SP3 image (see xp\dump_xp_exports.ps1),
# so ANY import XP does not have fails -- including the ones nobody has hit yet. The
# previous blacklist only ever caught what had already broken once.
param(
  [string]$Exe = "C:\TBuild\xp-port\tdesktop-walk\out\cmb\Telegram.exe",
  [string]$Dumpbin = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe",
  [string]$Exports = (Join-Path $PSScriptRoot 'exports'),
  [int]$ListLimit = 25
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

# --- the reference: what the target XP image really exports -----------------------
$tables = @{}
if (Test-Path $Exports) {
  foreach ($file in Get-ChildItem $Exports -Filter *.txt) {
    $set = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($name in [IO.File]::ReadAllLines($file.FullName)) {
      if ($name) { $null = $set.Add($name.Trim()) }
    }
    $tables[$file.BaseName.ToLower() + '.dll'] = $set
  }
}
if (-not $tables.Count) {
  Write-Output "XPSAFE: FAIL no export tables in $Exports - run xp\dump_xp_exports.ps1"
  exit 2
}
Write-Output ("XPSAFE exports: {0} DLL tables, {1} names" -f $tables.Count,
  ($tables.Values | ForEach-Object { $_.Count } | Measure-Object -Sum).Sum)

# Absent on XP, so a NORMAL import is a loader crash; they are legitimate as DELAY
# imports behind a runtime check. Anything else missing a table is reported too --
# an unknown NORMAL dependency is exactly what this gate must not wave through.
$knownAbsent = @(
  'dwmapi.dll','combase.dll','d3d11.dll','d3d10.dll','d3d12.dll','dcomp.dll',
  'dxgi.dll','dwrite.dll','propsys.dll','shcore.dll','bcrypt.dll','ncrypt.dll',
  'windows.storage.dll','wlanapi.dll','mfplat.dll','mf.dll','mfreadwrite.dll',
  'd2d1.dll','dwmredir.dll','api-ms-win-core-synch-l1-2-0.dll'
)

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

Write-Output "XPSAFE check: $Exe"
Write-Output ("  NORMAL deps ({0}): {1}" -f $normal.Count, ($normal -join ' '))
Write-Output ("  DELAY  deps ({0}): {1}" -f $delay.Count, ($delay -join ' '))

$problems = @()
$unknown = @()
foreach ($d in $normal) {
  if ($knownAbsent -contains $d -or $d -like 'api-ms-win-*') {
    $problems += $d
  } elseif (-not $tables.ContainsKey($d)) {
    $unknown += $d
  }
}

# --- ENTRY POINT check ------------------------------------------------------------
# The DLL-level check above is not enough: kernel32.dll and user32.dll DO exist on
# XP, but importing a function they only gained in Vista/7/8/10 fails the loader
# just as hard -- "The procedure entry point X could not be located in the dynamic
# link library KERNEL32.dll", before main() runs. This is how a 2026-07-31 build
# died on the VM after xpsafe had said PASS: linking a modern-MSVC static lib
# pulled libcpmt objects that import the SRW lock API (Vista+).
# Delay imports are checked too: they fail on first CALL rather than at startup,
# which is worse to diagnose, and a guarded call site is supposed to GetProcAddress.
$impRaw = & $dumpbin /IMPORTS $Exe 2>&1 | Out-String
$impLines = $impRaw -split "`r?`n"
$currentDll = ''
$badFuncs = New-Object System.Collections.Generic.List[string]
$byOrdinal = New-Object System.Collections.Generic.List[string]
$checked = 0
foreach ($ln in $impLines) {
  $t = $ln.Trim()
  # The trailing Summary block lists section sizes in the same two-column shape as
  # imports ("2000 _RDATA"), so stop before it rather than read it as one.
  if ($t -eq 'Summary') { break }
  if ($t -match '^([\w\.\-]+\.dll)$') { $currentDll = $t.ToLower(); continue }
  if (-not $tables.ContainsKey($currentDll)) { continue }
  # dumpbin prints "  <hint> <name>" for named imports and "<hex>  Ordinal   <n>"
  # for the ordinal ones, which carry no name to compare. A hint is at most 4 hex
  # digits; the delay-load block's own header lines ("00000000 Characteristics")
  # carry an 8-digit ADDRESS in the same column and would otherwise read as
  # imports named after the header field.
  if ($t -match '^(?:[0-9A-Fa-f]+\s+)?Ordinal\s+(\d+)$') {
    $byOrdinal.Add("$currentDll#$($matches[1])")
  } elseif ($t -match '^[0-9A-Fa-f]{1,4}\s+([A-Za-z_][\w@\?\$]*)$') {
    $fn = $matches[1]
    $checked++
    if (-not $tables[$currentDll].Contains($fn)) { $badFuncs.Add("$currentDll!$fn") }
  }
}
$badFuncs = @($badFuncs | Sort-Object -Unique)
$byOrdinal = @($byOrdinal | Sort-Object -Unique)

Write-Output ("  ENTRY POINTS: {0} imports checked against the XP tables" -f $checked)
if ($byOrdinal.Count -gt 0) {
  # Nothing to verify by name; kept visible because an ordinal can shift between
  # Windows versions and no table can catch that.
  Write-Output ("  by ordinal ({0}, unverifiable): {1}" -f $byOrdinal.Count, ($byOrdinal -join ' '))
}
if ($unknown.Count -gt 0) {
  Write-Output ("  NO TABLE for NORMAL dep(s): {0}" -f ($unknown -join ' '))
  Write-Output "  -> add the DLL to xp\dump_xp_exports.ps1 and re-dump, or delay-load it."
}
if ($badFuncs.Count -gt 0) {
  $show = if ($badFuncs.Count -gt $ListLimit) { $badFuncs[0..($ListLimit - 1)] } else { $badFuncs }
  Write-Output ("  ABSENT on XP ({0}): {1}{2}" -f $badFuncs.Count, ($show -join ' '),
    $(if ($badFuncs.Count -gt $ListLimit) { " ... +$($badFuncs.Count - $ListLimit) more" } else { '' }))
} else {
  Write-Output "  ENTRY POINTS: every named import exists in the XP export tables."
}

if ($problems.Count -gt 0) {
  Write-Output ("XPSAFE: FAIL -- Vista+ DLL(s) NORMAL-imported (loader-crash on XP): {0}" -f ($problems -join ' '))
  Write-Output "  -> move to DELAYLOAD or GetProcAddress, or the build will not launch on XP."
  exit 1
}
if ($unknown.Count -gt 0) {
  Write-Output "XPSAFE: FAIL -- a NORMAL dependency has no XP export table, so it cannot be verified."
  exit 1
}
if ($badFuncs.Count -gt 0) {
  Write-Output "XPSAFE: FAIL -- import(s) absent from the XP export tables; XP shows"
  Write-Output "  'The procedure entry point ... could not be located' and never reaches main()."
  Write-Output "  -> find the object that pulls it (usually a third-party lib built with a"
  Write-Output "     modern toolset dragging libcpmt), rebuild it with v141_xp, or drop it."
  exit 1
}
Write-Output "XPSAFE: PASS -- no absent-on-XP DLL is NORMAL-imported, no absent-on-XP entry point."
exit 0
