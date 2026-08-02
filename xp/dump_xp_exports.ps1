# Re-dump xp\exports\*.txt -- the export tables xpsafe.ps1 checks every import
# against. Run this only when the gate reports a NORMAL dependency with no table,
# or when the reference XP image changes; the committed tables are the contract.
#
#   powershell <repo>\xp\dump_xp_exports.ps1                 # from the running VM
#   powershell <repo>\xp\dump_xp_exports.ps1 -From C:\xpdll  # from a local copy
#
# The DLLs come off the TARGET image, not off this workstation: the whole point is
# to compare against what Windows XP SP3 really exports, which no SDK or header on
# a modern machine describes. The guest copies them into the `dep` share (it cannot
# be pulled with `guestcontrol copyfrom` for system files), then dumpbin runs here.
param(
  [string]$From,
  [string]$Share = 'C:\TBuild\xp-port\deploy-xp',
  [string]$Out = (Join-Path $PSScriptRoot 'exports'),
  [string]$Dumpbin,
  [string]$Vm = 'WinXP'
)
$ErrorActionPreference = 'Stop'

# Every DLL the port imports, plus the ones a future change is likely to reach for.
# gdiplus lives in WinSxS on XP, not in system32, so it is handled separately.
$dlls = @(
  'kernel32','user32','advapi32','gdi32','shell32','shlwapi','ole32','oleaut32',
  'ws2_32','msvcrt','version','imm32','winmm','crypt32','iphlpapi','netapi32',
  'mpr','userenv','wtsapi32','opengl32','uxtheme','comdlg32','comctl32','ntdll',
  'rpcrt4','psapi','setupapi','secur32','wininet','dnsapi','powrprof','msimg32',
  'shfolder')

if (-not $From) {
  $From = Join-Path $Share 'xpdll'
  $xpexec = Join-Path (Split-Path $PSScriptRoot -Parent) '..\xpvm-tools\xpexec.ps1'
  if (-not (Test-Path $xpexec)) { throw "xpexec.ps1 not found at $xpexec - pass -From instead" }
  $copy = ($dlls | ForEach-Object { "copy /Y C:\WINDOWS\system32\$_.dll xpdll\ >nul 2>&1" }) -join ' & '
  $null = & $xpexec -Cmd ('pushd \\vboxsvr\dep & mkdir xpdll 2>nul & ' + $copy + ' & popd') -TimeoutMs 300000
  $sxs = & $xpexec -Cmd 'dir /s /b C:\WINDOWS\WinSxS\gdiplus.dll' -TimeoutMs 120000 |
    Where-Object { $_ -match 'GdiPlus\.dll$' } | Select-Object -Last 1
  if ($sxs) {
    $null = & $xpexec -Cmd ("pushd \\vboxsvr\dep & copy /Y `"$sxs`" xpdll\gdiplus.dll >nul & popd") -TimeoutMs 120000
  }
}
if (-not (Test-Path $From)) { throw "no DLLs at $From" }

if (-not $Dumpbin -or -not (Test-Path $Dumpbin)) {
  $found = Get-Command dumpbin.exe -ErrorAction SilentlyContinue | Select-Object -First 1
  $Dumpbin = if ($found) { $found.Source } else { '' }
}
if (-not $Dumpbin) {
  $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  foreach ($instance in (& $vswhere -products * -all -prerelease -property installationPath)) {
    $Dumpbin = Get-ChildItem (Join-Path $instance 'VC\Tools\MSVC') -Directory -ErrorAction SilentlyContinue |
      Sort-Object Name -Descending |
      ForEach-Object { Join-Path $_.FullName 'bin\Hostx64\x64\dumpbin.exe' } |
      Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($Dumpbin) { break }
  }
}
if (-not $Dumpbin) { throw 'dumpbin.exe not found' }

New-Item -ItemType Directory -Force -Path $Out | Out-Null
$total = 0
foreach ($dll in Get-ChildItem $From -Filter *.dll) {
  $raw = & $Dumpbin /EXPORTS $dll.FullName
  $names = New-Object System.Collections.Generic.List[string]
  $started = $false
  foreach ($ln in $raw) {
    if ($ln -match '^\s+ordinal\s+hint\s+RVA\s+name') { $started = $true; continue }
    if (-not $started) { continue }
    if ($ln -match '^\s*Summary') { break }
    if ($ln -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]{8}\s+([^\s]+)') { $names.Add($matches[1]) }
    elseif ($ln -match '^\s+\d+\s+[0-9A-Fa-f]+\s+([A-Za-z_][^\s]*)\s+\(forwarded') { $names.Add($matches[1]) }
  }
  $sorted = $names | Sort-Object -Unique
  Set-Content -Path (Join-Path $Out ($dll.BaseName.ToLower() + '.txt')) -Value $sorted -Encoding ascii
  $total += $sorted.Count
  Write-Host ("  {0,-16} {1}" -f $dll.BaseName.ToLower(), $sorted.Count)
}
Write-Host "xp\exports: $total names from $((Get-ChildItem $From -Filter *.dll).Count) DLLs"
