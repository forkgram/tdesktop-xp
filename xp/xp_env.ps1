# Compute the XP build environment - INCLUDE, LIB and PATH - and apply it to the
# current PowerShell process. This is the single definition of what "building for
# XP" means here; cmake_xp.ps1 and build_libraries.ps1 both go through it, and it
# mirrors the workstation's _build_xp.bat exactly.
#
# The layering is not arbitrary:
#   INCLUDE  MSVC 14.16 headers, then the PATCHED SDK 7.1A Win32 headers, then
#            only the UCRT from the Windows 10 kit. The kit's um/shared trees
#            redefine HKEY__, _COAUTHIDENTITY and more against 7.1A, so nothing
#            else from it may be mixed in.
#   LIB      xp_compat first, so the Vista+ imports the modern CRT emits (SRW
#            locks, condition variables, Fls*, InitOnce*) resolve to the shim
#            instead of kernel32, which does not export them on XP. Then fpcompat
#            (the CRT float helpers), the 14.16 CRT, SDK 7.1A for the XP-era
#            imports, the Windows 10 kit's um as a fallback for what 7.1A lacks
#            (ntdll.lib and friends), and the UCRT last.
#   PATH     the compiler BINARY is deliberately a modern toolset - its c2
#            backend and PDB DLLs are what this port compiles with - plus the Qt
#            host tools when a Qt prefix exists.
#
# -ForQt widens INCLUDE with the Windows 10 kit's um/shared/winrt headers. Qt's
# own sources reference modern Windows constants (FILE_ID_INFO, NETIO_STATUS,
# KF_FLAG_DONT_VERIFY) that 7.1A does not declare; they end up behind runtime
# GetProcAddress lookups, so Qt still runs on XP. Telegram itself is built
# WITHOUT this, so a stray Win7+ symbol reference cannot slip into the app.
#
# -Arch picks the TARGET architecture. x86 is Windows XP SP3; x64 is Windows XP
# Professional x64 Edition, which is NT 5.2 (the Server 2003 kernel) and so
# declares subsystem 5.02 - 5.01 is not a legal value for an x64 image and the
# loader refuses it. Everything the two share (the patched 7.1A include tree,
# the MSVC toolset choice) stays common; everything that is a library path
# forks here, and the per-arch pieces bootstrap_toolchain.ps1 builds live in
# sibling directories with a -x64 suffix so an x86 tree keeps working untouched.
param(
  [string]$Toolchain = $env:XP_TOOLCHAIN_ROOT,
  [string]$QtPrefix = $env:XP_QT_PREFIX,
  [ValidateSet('x86', 'x64')]
  [string]$Arch = $(if ($env:XP_ARCH) { $env:XP_ARCH } else { 'x86' }),
  [switch]$ForQt,
  [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

if (-not $Toolchain) { $Toolchain = 'C:\xp-toolchain' }
# The suffix every per-arch toolchain directory carries. Empty for x86 on
# purpose: the existing layout, caches and workstation paths stay valid.
$suffix = if ($Arch -eq 'x64') { '-x64' } else { '' }
$sdkInclude = if ($env:XP_SDK71A_INCLUDE) { $env:XP_SDK71A_INCLUDE } else { Join-Path $Toolchain 'sdk71a-Include' }
$sdkRoot = if ($env:XP_SDK71A_ROOT) { $env:XP_SDK71A_ROOT } else { "${env:ProgramFiles(x86)}\Microsoft SDKs\Windows\v7.1A" }
$fpcompat = if ($env:XP_FPCOMPAT) { $env:XP_FPCOMPAT } else { Join-Path $Toolchain "fpcompat$suffix" }
$xpCompat = if ($env:XP_COMPAT_LIB) { $env:XP_COMPAT_LIB } else { Join-Path $Toolchain "xp_compat$suffix" }
# 7.1A keeps the 32-bit import libraries in Lib\ and the 64-bit ones in Lib\x64.
$sdkLib = if ($Arch -eq 'x64') { Join-Path $sdkRoot 'Lib\x64' } else { Join-Path $sdkRoot 'Lib' }

if (-not (Test-Path (Join-Path $sdkInclude 'windows.h'))) {
  throw "patched SDK 7.1A includes not found at $sdkInclude - run xp/bootstrap_toolchain.ps1"
}

# Look through EVERY Visual Studio instance, not just the newest: this
# workstation keeps the 14.16 toolset in the Build Tools install while the IDE
# install carries the modern one, and a CI runner has them in a single instance.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$instances = & $vswhere -products * -all -prerelease -property installationPath
$toolsets = @()
foreach ($instance in $instances) {
  $root = Join-Path $instance 'VC\Tools\MSVC'
  if (Test-Path $root) { $toolsets += Get-ChildItem $root -Directory }
}
if (-not $toolsets) { throw 'no MSVC toolsets found' }

function Pick($pattern) {
  $toolsets | Where-Object { $_.Name -like $pattern } |
    Sort-Object Name -Descending | Select-Object -First 1
}
$target = if ($env:XP_TOOLSET_TARGET) { Pick $env:XP_TOOLSET_TARGET } else { Pick '14.16.*' }
$binary = if ($env:XP_TOOLSET_BINARY) { Pick $env:XP_TOOLSET_BINARY } else { Pick '14.44.*' }
if (-not $target) { throw 'the 14.16 target toolset is missing - run xp/bootstrap_toolchain.ps1' }
if (-not $binary) { $binary = Pick '14.*' }
if ($ForQt) {
  # Qt is compiled by the 14.16 BINARY as well, not just against its headers.
  # The modern compiler emits calls to CRT float helpers (__ltof3, __ultof3,
  # __dtoul3_legacy) that fpcompat supplies for Telegram - but Qt's own bootstrap
  # links qmake with a fixed library list this environment cannot extend, so
  # qmake fails with three unresolved externals. Telegram still needs the modern
  # binary (its c2 backend, and range-v3 wants _MSC_VER >= 1920); Qt does not.
  $binary = $target
}

$kits = "${env:ProgramFiles(x86)}\Windows Kits\10"
$ucrtInc = Get-ChildItem "$kits\Include" | Where-Object { Test-Path (Join-Path $_.FullName 'ucrt\stdio.h') } |
  Sort-Object Name -Descending | Select-Object -First 1
$ucrtLib = Get-ChildItem "$kits\Lib" | Where-Object { Test-Path (Join-Path $_.FullName "ucrt\$Arch\ucrt.lib") } |
  Sort-Object Name -Descending | Select-Object -First 1
$umLib = Get-ChildItem "$kits\Lib" | Where-Object { Test-Path (Join-Path $_.FullName "um\$Arch\ntdll.lib") } |
  Sort-Object Name -Descending | Select-Object -First 1
if (-not $ucrtInc -or -not $ucrtLib -or -not $umLib) { throw "the Windows 10 kit is incomplete for $Arch" }

$libParts = @()
# Qt is compiled by the 14.16 binary, which never emits the CRT float helpers
# fpcompat carries - and those objects come from the 14.44 CRT, where they rely
# on an __isa_available that a 14.16 process initialises differently. Letting
# them be pulled into Qt produces libraries that link but crash on the first
# conversion: the code generators died with 0xC0000005 before printing a line.
# Telegram itself, compiled by 14.44, does need them - so they stay for everyone
# else. This mirrors the workstation, where the Qt environment omits them too.
# On x64 there are no float helpers to carry at all - ftol2/ftol3 and __ltof3
# are an x86 CRT thing, the 64-bit compiler emits SSE2 conversions inline - so
# fpcompat-x64 holds only the Fls*/NUMA/InitializeCriticalSectionEx thunks. The
# same split is kept anyway, so both architectures behave identically here.
if (-not $ForQt) {
  if (Test-Path (Join-Path $xpCompat 'xp_compat.lib')) { $libParts += $xpCompat }
  if (Test-Path (Join-Path $fpcompat 'fpcompat.lib')) { $libParts += $fpcompat }
}
$libParts += @(
  (Join-Path $target.FullName "lib\$Arch"),
  $sdkLib,
  (Join-Path $umLib.FullName "um\$Arch"),
  (Join-Path $ucrtLib.FullName "ucrt\$Arch"))

if ($ForQt) {
  # The kit's um/shared come FIRST here so the modern declarations win, and 7.1A
  # trails behind to fill in the legacy XP-era headers Qt still includes.
  $env:INCLUDE = @(
    (Join-Path $target.FullName 'include'),
    (Join-Path $ucrtInc.FullName 'um'),
    (Join-Path $ucrtInc.FullName 'shared'),
    (Join-Path $ucrtInc.FullName 'winrt'),
    (Join-Path $ucrtInc.FullName 'ucrt'),
    $sdkInclude) -join ';'
} else {
  # 7.1A comes FIRST so the XP-era declarations win for everything it defines.
  # The Windows 10 kit trails behind only as a fallback for headers 7.1A never
  # had at all - roapi.h and winstring.h, which base_windows_wrl.h includes for
  # the WinRT declarations (the code paths themselves are dead on XP, guarded by
  # SupportsWRL()). On the workstation those includes only ever resolved out of a
  # precompiled header built under a laxer environment; a clean machine needs
  # them for real. xpsafe.ps1 is what guards against a Vista+ symbol sneaking in.
  $env:INCLUDE = @(
    (Join-Path $target.FullName 'include'),
    $sdkInclude,
    (Join-Path $ucrtInc.FullName 'ucrt'),
    (Join-Path $ucrtInc.FullName 'um'),
    (Join-Path $ucrtInc.FullName 'shared'),
    (Join-Path $ucrtInc.FullName 'winrt')) -join ';'
}
$env:LIB = $libParts -join ';'

$binPath = Join-Path $binary.FullName "bin\Hostx64\$Arch"
$hostPath = Join-Path $binary.FullName 'bin\Hostx64\x64'
# rc.exe and mt.exe live in the Windows kit, not in the MSVC toolset. vcvarsall
# adds them locally; building the environment by hand has to as well, or cmake's
# very first compiler check dies at "RC Pass 1 ... no such file or directory".
$kitBin = Join-Path $kits "bin\$($ucrtInc.Name)\x64"
if (-not (Test-Path (Join-Path $kitBin 'rc.exe'))) {
  $kitBin = Get-ChildItem (Join-Path $kits 'bin') -Directory -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending |
    ForEach-Object { Join-Path $_.FullName 'x64' } |
    Where-Object { Test-Path (Join-Path $_ 'rc.exe') } | Select-Object -First 1
}
$env:PATH = "$binPath;$hostPath;$kitBin;$env:PATH"
if ($QtPrefix -and (Test-Path (Join-Path $QtPrefix 'bin'))) {
  $env:PATH = (Join-Path $QtPrefix 'bin') + ';' + $env:PATH
  $env:Qt5_DIR = Join-Path $QtPrefix 'lib\cmake\Qt5'
  $env:CMAKE_PREFIX_PATH = if ($env:CMAKE_PREFIX_PATH) { "$QtPrefix;$env:CMAKE_PREFIX_PATH" } else { $QtPrefix }
}

# MSBuild has to come from the instance that actually REGISTERS the v141
# toolset, not from whichever is newest: a v141 project built by a 2026 MSBuild
# stops at MSB8020 "build tools for Visual Studio 2017 cannot be found".
$targetInstance = (Get-Item $target.FullName).Parent.Parent.Parent.Parent.FullName
$msbuild = Get-ChildItem (Join-Path $targetInstance 'MSBuild') -Recurse -Filter 'MSBuild.exe' -ErrorAction SilentlyContinue |
  Where-Object { $_.FullName -match '\\Bin\\MSBuild\.exe$' } | Select-Object -First 1
$env:XP_MSBUILD = if ($msbuild) { $msbuild.FullName } else { '' }
$env:XP_TOOLSET_TARGET_DIR = $target.FullName
$env:XP_TOOLSET_BINARY_DIR = $binary.FullName
# Old .vcxproj files default to the Windows 8.1 SDK, which no current image has;
# building them needs an explicit WindowsTargetPlatformVersion. It only affects
# those C libraries, never the port's own compilation, which uses SDK 7.1A.
$env:XP_WIN10_SDK_VERSION = $ucrtInc.Name

# Everything downstream reads the architecture from here rather than deciding
# again: the MSBuild platform name for the .vcxproj libraries, the lowest
# subsystem version the image may declare, and the /MACHINE the linker wants.
$env:XP_ARCH = $Arch
$env:XP_MSVC_PLATFORM = if ($Arch -eq 'x64') { 'x64' } else { 'Win32' }
$env:XP_SUBSYSTEM_VERSION = if ($Arch -eq 'x64') { '5.02' } else { '5.01' }
$env:XP_MACHINE = if ($Arch -eq 'x64') { 'X64' } else { 'X86' }

if (-not $Quiet) {
  Write-Host "XP environment:"
  Write-Host "  arch    $Arch  (subsystem $env:XP_SUBSYSTEM_VERSION, platform $env:XP_MSVC_PLATFORM)"
  Write-Host "  target  $($target.Name)  ($targetInstance)"
  Write-Host "  binary  $($binary.Name)"
  Write-Host "  msbuild $env:XP_MSBUILD"
  Write-Host "  INCLUDE $env:INCLUDE"
  Write-Host "  LIB     $env:LIB"
}
