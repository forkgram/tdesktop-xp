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
#
# -Arch x64 does the same for Windows XP Professional x64 Edition and writes
# xp\exports-x64, which is what xpsafe.ps1 -Arch x64 wants. Two things differ:
# system32 on that machine holds the 64-BIT DLLs (the 32-bit ones live in
# SysWOW64 and are not what a 64-bit binary imports), and WinSxS carries both
# flavours of gdiplus, so the amd64_ one is picked.
#
# The guest side goes through xpvm-tools\xpexec.ps1, which takes the machine to
# run in; -Arch picks the matching default. Pass -From instead to skip the guest
# half entirely and read DLLs already copied out.
param(
  [string]$From,
  [string]$Share = 'C:\TBuild\xp-port\deploy-xp',
  [ValidateSet('x86', 'x64')]
  [string]$Arch = 'x86',
  [string]$Out,
  [string]$Dumpbin,
  [string]$XpExec,
  [string]$Vm
)
$ErrorActionPreference = 'Stop'

if (-not $Vm) { $Vm = if ($Arch -eq 'x64') { 'WinXP64' } else { 'WinXP' } }

if (-not $Out) {
  $Out = Join-Path $PSScriptRoot $(if ($Arch -eq 'x64') { 'exports-x64' } else { 'exports' })
}

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
  $xpexec = if ($XpExec) { $XpExec } else { Join-Path (Split-Path $PSScriptRoot -Parent) '..\xpvm-tools\xpexec.ps1' }
  if (-not (Test-Path $xpexec)) { throw "xpexec.ps1 not found at $xpexec - pass -From instead" }
  Write-Host "dumping from VM $Vm ($Arch)"
  # system32 holds the NATIVE DLLs on both machines - on the 64-bit one the
  # 32-bit set lives in SysWOW64, and a 64-bit binary never imports from it.
  $copy = ($dlls | ForEach-Object { "copy /Y C:\WINDOWS\system32\$_.dll xpdll\ >nul 2>&1" }) -join ' & '
  $null = & $xpexec -Vm $Vm -Cmd ('pushd \\vboxsvr\dep & mkdir xpdll 2>nul & ' + $copy + ' & popd') -TimeoutMs 300000
  # WinSxS on a 64-bit install carries both builds of gdiplus side by side, and
  # they are told apart only by the assembly directory's architecture prefix.
  $flavour = if ($Arch -eq 'x64') { 'amd64_' } else { 'x86_' }
  $candidates = @(& $xpexec -Vm $Vm -Cmd 'dir /s /b C:\WINDOWS\WinSxS\gdiplus.dll' -TimeoutMs 120000 |
    Where-Object { $_ -match 'GdiPlus\.dll$' })
  $sxs = $candidates | Where-Object { $_ -match [regex]::Escape("\WinSxS\$flavour") } | Select-Object -Last 1
  if (-not $sxs) { $sxs = $candidates | Select-Object -Last 1 }
  if ($sxs) {
    $null = & $xpexec -Vm $Vm -Cmd ("pushd \\vboxsvr\dep & copy /Y `"$sxs`" xpdll\gdiplus.dll >nul & popd") -TimeoutMs 120000
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
Write-Host "$Out : $total names from $((Get-ChildItem $From -Filter *.dll).Count) DLLs ($Arch)"
