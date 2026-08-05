# Run a command under the XP toolchain environment.
#
# The port compiles with the MODERN compiler BINARY (its c2 backend and PDB DLLs,
# and range-v3 wants _MSC_VER >= 1920) but targets the v141_xp 14.16 CRT through
# INCLUDE/LIB, plus a patched SDK 7.1A include tree. /d2FH4- (FH3 exception
# handling, the only kind the 14.16 CRT provides), the forced xp-compat.h include
# and the subsystem pin (5.01 on x86, 5.02 on x64 - XP x64 is NT 5.2) come from
# options_win.cmake and Telegram/CMakeLists.
# Nothing here is optional: a bare `ninja` produces a binary that cannot start.
#
#   powershell <repo>\xp\cmake_xp.ps1 cmake -GNinja -S . -B out/cmb
#   powershell <repo>\xp\cmake_xp.ps1 ninja -C out/cmb Telegram
#
# Machine specific locations come from the environment, defaulting to this
# workstation's layout. A runner that hosts the toolchain elsewhere sets:
#   XP_VCVARS_BAT      the batch file that enters the v141_xp environment
#   XP_SDK71A_INCLUDE  the PATCHED SDK 7.1A include tree
#   XP_QT_PREFIX       the static XP Qt prefix (host moc/rcc/uic live in \bin)
#   XP_TOOLSET_TARGET  MSVC version whose headers/libs are targeted (14.16.27023)
#   XP_TOOLSET_BINARY  MSVC version whose cl.exe/link.exe are used (14.44.35207)
#   XP_EXTRA_PATH      anything else to prepend to PATH (ninja, python, ...)
#   XP_ARCH            x86 (default, XP SP3) or x64 (XP Professional x64 Edition,
#                      which is NT 5.2 and therefore subsystem 5.02)
$ErrorActionPreference = 'Continue'

$vcvars = if ($env:XP_VCVARS_BAT) { $env:XP_VCVARS_BAT } else { 'C:\TBuild\xp-port\forkgram-xp\_build_xp.bat' }
$sdk71a = if ($env:XP_SDK71A_INCLUDE) { $env:XP_SDK71A_INCLUDE } else { 'C:\TBuild\xp-port\sdk71a-Include' }
$qtPrefix = if ($env:XP_QT_PREFIX) { $env:XP_QT_PREFIX } else { 'C:\TBuild\xp-port\qt-xp-static-prefix' }
$targetToolset = if ($env:XP_TOOLSET_TARGET) { $env:XP_TOOLSET_TARGET } else { '14.16.27023' }
$binaryToolset = if ($env:XP_TOOLSET_BINARY) { $env:XP_TOOLSET_BINARY } else { '14.44.35207' }
$extraPath = if ($env:XP_EXTRA_PATH) { $env:XP_EXTRA_PATH } else { 'C:\Users\h\AppData\Local\Microsoft\WinGet\Links' }

# Two ways to get the same environment. The workstation has a batch file that
# enters it (and that file stays the reference); anywhere else - a CI runner in
# particular - xp_env.ps1 composes INCLUDE/LIB/PATH from the toolchain pieces
# bootstrap_toolchain.ps1 produced. Both end up with the identical layering.
# An x64 build always takes the second path: _build_xp.bat enters the 32-bit
# v141_xp environment and nothing else, and there is no 64-bit counterpart of it
# to defer to - so the environment is composed from the toolchain pieces even on
# the workstation.
$arch = if ($env:XP_ARCH) { $env:XP_ARCH } else { 'x86' }
if ($arch -eq 'x64' -or -not (Test-Path $vcvars)) {
  & (Join-Path $PSScriptRoot 'xp_env.ps1') -QtPrefix $qtPrefix -Arch $arch
  if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne $null) { exit $LASTEXITCODE }
  & $args[0] @($args[1..($args.Count - 1)])
  exit $LASTEXITCODE
}
if (-not (Test-Path $sdk71a)) {
  Write-Output "XP toolchain: patched SDK 7.1A includes not found: $sdk71a"
  Write-Output "  set XP_SDK71A_INCLUDE - the stock SDK will NOT do, it needs the port's fixes"
  Write-Output "  (_VARIANT_BOOL, the ShellScalingApi.h stub, mmsystem, ...)."
  exit 2
}

$envtxt = cmd /c "`"$vcvars`" cmd /c set" 2>$null
foreach ($line in $envtxt) {
  if ($line -match '^([A-Za-z_][A-Za-z0-9_()]*)=(.*)$') {
    $n = $matches[1]; $v = $matches[2]
    if ($n -eq 'CL') { continue }            # flags come from options_win.cmake
    if ($n -eq 'INCLUDE') {
      # Keep the 14.16 headers; redirect the SDK 7.1A include to the patched copy.
      $v = $v -replace [regex]::Escape('C:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A\Include'), $sdk71a
    } elseif ($n -match '^(PATH|Path)$') {
      # Point the tool bin (cl/link/lib and the mspdb DLLs) at the newer toolset,
      # matching the c2 backend, then prepend the Qt host tools and whatever else
      # the caller needs (ninja).
      $v = $v -replace [regex]::Escape($targetToolset), $binaryToolset
      $v = "$qtPrefix\bin;$extraPath;$v"
    }
    Set-Item -Path "Env:$n" -Value $v
  }
}

# The batch file predates the port needing anything from the Windows 10 kit
# beyond the UCRT, so it stops at MSVC + SDK 7.1A + ucrt. Three headers the port
# includes never existed in 7.1A - roapi.h and winstring.h (base_windows_wrl.h)
# and wrl/client.h (integration_win.h) - and on this workstation they only ever
# resolved out of objects built under a wider environment. Append the kit's
# um/shared/winrt AFTER 7.1A, exactly as xp_env.ps1 does for a runner: 7.1A
# still wins every declaration it owns, and both paths now build the same tree.
$kitInclude = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\Include" -Directory -ErrorAction SilentlyContinue |
  Where-Object { Test-Path (Join-Path $_.FullName 'winrt\wrl\client.h') } |
  Sort-Object Name -Descending | Select-Object -First 1
if ($kitInclude) {
  $have = ($env:INCLUDE -split ';')
  foreach ($part in @('um', 'shared', 'winrt')) {
    $dir = Join-Path $kitInclude.FullName $part
    if ($have -notcontains $dir) { $env:INCLUDE = "$env:INCLUDE;$dir" }
  }
}

& $args[0] @($args[1..($args.Count - 1)])
exit $LASTEXITCODE
