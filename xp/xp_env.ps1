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
param(
  [string]$Toolchain = $env:XP_TOOLCHAIN_ROOT,
  [string]$QtPrefix = $env:XP_QT_PREFIX,
  [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

if (-not $Toolchain) { $Toolchain = 'C:\xp-toolchain' }
$sdkInclude = if ($env:XP_SDK71A_INCLUDE) { $env:XP_SDK71A_INCLUDE } else { Join-Path $Toolchain 'sdk71a-Include' }
$sdkRoot = if ($env:XP_SDK71A_ROOT) { $env:XP_SDK71A_ROOT } else { "${env:ProgramFiles(x86)}\Microsoft SDKs\Windows\v7.1A" }
$fpcompat = if ($env:XP_FPCOMPAT) { $env:XP_FPCOMPAT } else { Join-Path $Toolchain 'fpcompat' }
$xpCompat = if ($env:XP_COMPAT_LIB) { $env:XP_COMPAT_LIB } else { Join-Path $Toolchain 'xp_compat' }

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

$kits = "${env:ProgramFiles(x86)}\Windows Kits\10"
$ucrtInc = Get-ChildItem "$kits\Include" | Where-Object { Test-Path (Join-Path $_.FullName 'ucrt\stdio.h') } |
  Sort-Object Name -Descending | Select-Object -First 1
$ucrtLib = Get-ChildItem "$kits\Lib" | Where-Object { Test-Path (Join-Path $_.FullName 'ucrt\x86\ucrt.lib') } |
  Sort-Object Name -Descending | Select-Object -First 1
$umLib = Get-ChildItem "$kits\Lib" | Where-Object { Test-Path (Join-Path $_.FullName 'um\x86\ntdll.lib') } |
  Sort-Object Name -Descending | Select-Object -First 1
if (-not $ucrtInc -or -not $ucrtLib -or -not $umLib) { throw 'the Windows 10 kit is incomplete' }

$libParts = @()
if (Test-Path (Join-Path $xpCompat 'xp_compat.lib')) { $libParts += $xpCompat }
if (Test-Path (Join-Path $fpcompat 'fpcompat.lib')) { $libParts += $fpcompat }
$libParts += @(
  (Join-Path $target.FullName 'lib\x86'),
  (Join-Path $sdkRoot 'Lib'),
  (Join-Path $umLib.FullName 'um\x86'),
  (Join-Path $ucrtLib.FullName 'ucrt\x86'))

$env:INCLUDE = @(
  (Join-Path $target.FullName 'include'),
  $sdkInclude,
  (Join-Path $ucrtInc.FullName 'ucrt')) -join ';'
$env:LIB = $libParts -join ';'

$binPath = Join-Path $binary.FullName 'bin\Hostx64\x86'
$hostPath = Join-Path $binary.FullName 'bin\Hostx64\x64'
$env:PATH = "$binPath;$hostPath;$env:PATH"
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

if (-not $Quiet) {
  Write-Host "XP environment:"
  Write-Host "  target  $($target.Name)  ($targetInstance)"
  Write-Host "  binary  $($binary.Name)"
  Write-Host "  msbuild $env:XP_MSBUILD"
  Write-Host "  INCLUDE $env:INCLUDE"
  Write-Host "  LIB     $env:LIB"
}
