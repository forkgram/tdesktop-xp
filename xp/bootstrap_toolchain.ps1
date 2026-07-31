# Build the Windows XP toolchain on a machine that does not have it - a GitHub
# hosted runner in particular. Four pieces, all reproducible from Microsoft's own
# installers plus the sources in this directory; nothing third-party is vendored.
#
#   1. MSVC 14.16 (v141), the CRT the port TARGETS. One vs_installer component on
#      any modern Visual Studio.
#   2. Windows SDK 7.1A, which no Visual Studio since 2019 offers. The VS2019
#      Build Tools bootstrapper still carries Microsoft.VisualStudio.Component.WinXP
#      and installs the SDK to its usual location.
#   3. The PATCHED include tree: a copy of the SDK 7.1A headers with the
#      `_VARIANT_BOOL bool;` members commented out (`bool` is a keyword in C++,
#      so oaidl.h and propidl.h do not parse) and this port's small stub headers
#      from xp/sdk71a dropped in.
#   4. fpcompat.lib: the CRT float helpers the 14.44 compiler emits calls to,
#      extracted from that toolset's own libcmt.lib, plus the thunks in
#      xp/fpcompat that redirect the CRT's Vista+ kernel32 imports (Fls*, NUMA,
#      InitializeCriticalSectionEx) to XP equivalents.
#
# Writes the resulting locations into GITHUB_ENV when running under Actions, so
# the build steps can hand them to xp/cmake_xp.ps1.
#
#   powershell -File xp/bootstrap_toolchain.ps1 [-Root C:\xp-toolchain]
param(
  [string]$Root = 'C:\xp-toolchain'
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Step($text) { Write-Host "=== $text" }
function Export($name, $value) {
  Set-Item -Path "Env:$name" -Value $value
  if ($env:GITHUB_ENV) { "$name=$value" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8 }
  Write-Host "  $name=$value"
}

New-Item -ItemType Directory -Force -Path $Root | Out-Null

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere not found - no Visual Studio on this machine" }
$vs = & $vswhere -latest -products * -property installationPath
Write-Host "Visual Studio: $vs"

# --- 1. the v141 (14.16) target toolset -------------------------------------
Step 'MSVC v141 target toolset'
$toolsRoot = Join-Path $vs 'VC\Tools\MSVC'
$target = Get-ChildItem $toolsRoot -ErrorAction SilentlyContinue |
  Where-Object { $_.Name -like '14.16.*' } | Sort-Object Name -Descending | Select-Object -First 1
if (-not $target) {
  $installer = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vs_installer.exe"
  $p = Start-Process -FilePath $installer -Wait -PassThru -ArgumentList @(
    'modify', '--installPath', "`"$vs`"",
    '--add', 'Microsoft.VisualStudio.Component.VC.v141.x86.x64',
    '--quiet', '--norestart', '--nocache')
  if ($p.ExitCode -ne 0 -and $p.ExitCode -ne 3010) { throw "vs_installer failed with $($p.ExitCode)" }
  $target = Get-ChildItem $toolsRoot | Where-Object { $_.Name -like '14.16.*' } |
    Sort-Object Name -Descending | Select-Object -First 1
}
if (-not $target) { throw 'the 14.16 toolset is still missing' }
Write-Host "  target toolset: $($target.Name)"

# The compiler BINARY stays modern on purpose - see xp/cmake_xp.ps1. Prefer the
# exact 14.44 the workstation builds with: fpcompat below lifts the CRT float
# helpers out of this toolset's libcmt.lib, and those objects are what the
# compiler's own calls expect, so the pair should not drift between machines.
$binary = Get-ChildItem $toolsRoot | Where-Object { $_.Name -like '14.44.*' } |
  Sort-Object Name -Descending | Select-Object -First 1
if (-not $binary) {
  $binary = Get-ChildItem $toolsRoot | Where-Object { $_.Name -like '14.4*' -or $_.Name -like '14.5*' } |
    Sort-Object Name -Descending | Select-Object -First 1
}
if (-not $binary) { throw 'no modern toolset to take the compiler binary from' }
Write-Host "  binary toolset: $($binary.Name)"

# --- 2. the 7.1A SDK ---------------------------------------------------------
Step 'Windows SDK 7.1A'
$sdk = "${env:ProgramFiles(x86)}\Microsoft SDKs\Windows\v7.1A"
if (-not (Test-Path (Join-Path $sdk 'Include\windows.h'))) {
  $exe = Join-Path $env:TEMP 'vs_buildtools2019.exe'
  Write-Host '  fetching the VS2019 Build Tools bootstrapper'
  Invoke-WebRequest -Uri 'https://aka.ms/vs/16/release/vs_buildtools.exe' -OutFile $exe -UseBasicParsing
  $p = Start-Process -FilePath $exe -Wait -PassThru -ArgumentList @(
    '--installPath', (Join-Path $Root 'BuildTools2019'),
    '--add', 'Microsoft.VisualStudio.Workload.VCTools',
    '--add', 'Microsoft.VisualStudio.Component.VC.v141.x86.x64',
    '--add', 'Microsoft.VisualStudio.Component.WinXP',
    '--quiet', '--wait', '--norestart', '--nocache')
  if ($p.ExitCode -ne 0 -and $p.ExitCode -ne 3010) { throw "the VS2019 bootstrapper failed with $($p.ExitCode)" }
}
if (-not (Test-Path (Join-Path $sdk 'Include\windows.h'))) { throw "SDK 7.1A is still missing at $sdk" }
Write-Host "  SDK 7.1A: $sdk"

# --- 3. the patched include tree --------------------------------------------
Step 'Patched SDK 7.1A includes'
$include = Join-Path $Root 'sdk71a-Include'
if (-not (Test-Path (Join-Path $include 'windows.h'))) {
  New-Item -ItemType Directory -Force -Path $include | Out-Null
  Copy-Item -Recurse -Force (Join-Path $sdk 'Include\*') $include
}
# `bool` is a C++ keyword, so these VARIANT/PROPVARIANT members do not parse.
# The port never touches them; upstream's own XP builds did the same.
foreach ($header in @('oaidl.h', 'propidl.h')) {
  $path = Join-Path $include $header
  $text = [IO.File]::ReadAllText($path)
  $patched = $text -replace '(?m)^(\s*)_VARIANT_BOOL(\s+\*?p?bool;)', '$1/* xp-port: _VARIANT_BOOL$2 */'
  if ($patched -ne $text) {
    [IO.File]::WriteAllText($path, $patched)
    Write-Host "  patched $header"
  } else {
    if ($text -notmatch 'xp-port: _VARIANT_BOOL') { throw "could not patch $header" }
    Write-Host "  $header already patched"
  }
}
# The port's own stubs for headers the 7.1A SDK does not have at all.
Copy-Item -Recurse -Force (Join-Path $repo 'xp\sdk71a\*') $include
Write-Host "  include tree: $include"

# --- 4. fpcompat -------------------------------------------------------------
Step 'fpcompat'
$fpdir = Join-Path $Root 'fpcompat'
New-Item -ItemType Directory -Force -Path $fpdir | Out-Null
$fplib = Join-Path $fpdir 'fpcompat.lib'
if (-not (Test-Path $fplib)) {
  $libcmt = Join-Path $binary.FullName 'lib\x86\libcmt.lib'
  if (-not (Test-Path $libcmt)) { throw "no libcmt.lib at $libcmt" }
  $tools = Join-Path $binary.FullName 'bin\Hostx64\x86'
  $lib = Join-Path $tools 'lib.exe'
  $cl = Join-Path $tools 'cl.exe'
  $ml = Join-Path $tools 'ml.exe'

  # The float helpers must come from the toolset that COMPILES the code, whose
  # calls they answer - not from the 14.16 CRT the port links against.
  $members = & $lib /nologo /list $libcmt
  foreach ($obj in @('ftol2.obj', 'ftol3.obj')) {
    $member = $members | Where-Object { $_ -match [regex]::Escape("\$obj") + '$' } | Select-Object -First 1
    if (-not $member) { throw "$obj is not a member of $libcmt" }
    # Compose the switches as whole strings: PowerShell would otherwise split
    # /out:(Join-Path ...) into separate arguments and lib would write the
    # object somewhere else entirely, leaving LNK1181 at the archive step.
    $out = Join-Path $fpdir $obj
    & $lib /nologo "/extract:$member" "/out:$out" $libcmt | Out-Null
    if (-not (Test-Path $out)) { throw "extracting $obj from libcmt.lib produced nothing" }
    Write-Host ("  extracted {0} ({1:N0} bytes)" -f $obj, (Get-Item $out).Length)
  }

  # These sources include <windows.h>, so the compiler needs the whole layering
  # the port builds with: the 14.16 CRT headers, the patched 7.1A Win32 headers
  # and the UCRT from the Windows 10 kit. Only the UCRT may be mixed in - the
  # kit's um/shared trees redefine HKEY__ and friends against 7.1A.
  $kits = "${env:ProgramFiles(x86)}\Windows Kits\10"
  $ucrtInc = Get-ChildItem "$kits\Include" -ErrorAction SilentlyContinue |
    Where-Object { Test-Path (Join-Path $_.FullName 'ucrt\stdio.h') } |
    Sort-Object Name -Descending | Select-Object -First 1
  if (-not $ucrtInc) { throw 'no UCRT headers in the Windows 10 kit' }
  $env:INCLUDE = "$($target.FullName)\include;$include;$($ucrtInc.FullName)\ucrt"
  Write-Host "  INCLUDE=$env:INCLUDE"

  Push-Location $fpdir
  try {
    & $cl /nologo /c /MT /Foxpfls_c.obj (Join-Path $repo 'xp\fpcompat\xpfls.c')
    if ($LASTEXITCODE -ne 0) { throw 'compiling xpfls.c failed' }
    & $cl /nologo /c /MT /Foisacompat.obj (Join-Path $repo 'xp\fpcompat\isacompat.c')
    if ($LASTEXITCODE -ne 0) { throw 'compiling isacompat.c failed' }
    & $ml /nologo /c /coff /Foxpfls_asm.obj (Join-Path $repo 'xp\fpcompat\xpfls.asm')
    if ($LASTEXITCODE -ne 0) { throw 'assembling xpfls.asm failed' }
    & $lib /nologo /OUT:fpcompat.lib ftol2.obj ftol3.obj isacompat.obj xpfls_c.obj xpfls_asm.obj
    if ($LASTEXITCODE -ne 0) { throw 'building fpcompat.lib failed' }

    # Two halves, because they have opposite audiences.
    #
    #   _host  the CRT float helpers plus the __isa_inverted constant ftol3.obj
    #          refers to. The code generators are built with this toolchain but
    #          RUN on the build machine, and they need the helpers because
    #          lib_base does.
    #   _xp    the thunks that redirect the CRT's Vista+ imports (Fls*, NUMA,
    #          InitializeCriticalSectionEx). Exactly
    #          what an XP binary needs - and poison for a process running on a
    #          modern Windows, where the CRT keeps its per-thread data (locale
    #          included) in real FLS: a generator died in the std::cerr
    #          initializer, inside __acrt_add_locale_ref, intermittently.
    & $lib /nologo /OUT:fpcompat_host.lib ftol2.obj ftol3.obj isacompat.obj
    if ($LASTEXITCODE -ne 0) { throw 'building fpcompat_host.lib failed' }
    & $lib /nologo /OUT:fpcompat_xp.lib xpfls_c.obj xpfls_asm.obj
    if ($LASTEXITCODE -ne 0) { throw 'building fpcompat_xp.lib failed' }
  } finally {
    Pop-Location
  }
}
Write-Host "  fpcompat: $fplib"

# --- 5. xp_compat ------------------------------------------------------------
# A tiny DLL exporting the Vista+ kernel32 functions the modern CRT and STL call
# into - SRW locks, condition variables, Fls*, InitOnce* - implemented on XP
# primitives. Putting its import library FIRST on LIB is what makes those
# references resolve here instead of against kernel32, which does not export
# them on XP; without it the executable dies at startup with "the procedure
# entry point ... could not be located".
Step 'xp_compat'
$xpCompatDir = Join-Path $Root 'xp_compat'
New-Item -ItemType Directory -Force -Path $xpCompatDir | Out-Null
if (-not (Test-Path (Join-Path $xpCompatDir 'xp_compat.lib'))) {
  $tools = Join-Path $binary.FullName 'bin\Hostx64\x86'
  $kits = "${env:ProgramFiles(x86)}\Windows Kits\10"
  $ucrtInc = Get-ChildItem "$kits\Include" -ErrorAction SilentlyContinue |
    Where-Object { Test-Path (Join-Path $_.FullName 'ucrt\stdio.h') } |
    Sort-Object Name -Descending | Select-Object -First 1
  $ucrtLib = Get-ChildItem "$kits\Lib" -ErrorAction SilentlyContinue |
    Where-Object { Test-Path (Join-Path $_.FullName 'ucrt\x86\ucrt.lib') } |
    Sort-Object Name -Descending | Select-Object -First 1
  $umLib = Get-ChildItem "$kits\Lib" -ErrorAction SilentlyContinue |
    Where-Object { Test-Path (Join-Path $_.FullName 'um\x86\ntdll.lib') } |
    Sort-Object Name -Descending | Select-Object -First 1

  # It needs the kit's um/shared headers for the SRWLOCK and INIT_ONCE typedefs
  # 7.1A never had - the declarations only, no Vista+ call is made.
  $env:INCLUDE = @(
    (Join-Path $target.FullName 'include'),
    (Join-Path $ucrtInc.FullName 'um'),
    (Join-Path $ucrtInc.FullName 'shared'),
    (Join-Path $ucrtInc.FullName 'ucrt'),
    $include) -join ';'
  $env:LIB = @(
    (Join-Path $target.FullName 'lib\x86'),
    (Join-Path $sdk 'Lib'),
    (Join-Path $umLib.FullName 'um\x86'),
    (Join-Path $ucrtLib.FullName 'ucrt\x86')) -join ';'

  Copy-Item -Force (Join-Path $repo 'xp\xp_compat\xp_compat.c') $xpCompatDir
  Copy-Item -Force (Join-Path $repo 'xp\xp_compat\xp_compat.def') $xpCompatDir
  Push-Location $xpCompatDir
  try {
    & (Join-Path $tools 'cl.exe') /nologo /c /MT /O2 /W3 /GS- /Gs9999999 `
      /D_USING_V110_SDK71_=1 /D_WIN32_WINNT=0x0501 /DWINVER=0x0501 `
      /DNTDDI_VERSION=0x05010300 xp_compat.c
    if ($LASTEXITCODE -ne 0) { throw 'compiling xp_compat.c failed' }
    & (Join-Path $tools 'link.exe') /nologo /DLL /MACHINE:X86 /SUBSYSTEM:WINDOWS,5.01 `
      /DEF:xp_compat.def /NODEFAULTLIB /ENTRY:DllMain `
      /OUT:xp_compat.dll /IMPLIB:xp_compat.lib xp_compat.obj kernel32.lib
    if ($LASTEXITCODE -ne 0) { throw 'linking xp_compat.dll failed' }
  } finally { Pop-Location }
}
if (-not (Test-Path (Join-Path $xpCompatDir 'xp_compat.lib'))) { throw 'xp_compat.lib is missing' }
Write-Host "  xp_compat: $xpCompatDir"

# --- what the build steps need ----------------------------------------------
Step 'Environment'
Export 'XP_SDK71A_INCLUDE' $include
Export 'XP_TOOLSET_TARGET' $target.Name
Export 'XP_TOOLSET_BINARY' $binary.Name
Export 'XP_FPCOMPAT' $fpdir
Export 'XP_COMPAT_LIB' $xpCompatDir
Export 'XP_SDK71A_ROOT' $sdk
Write-Host 'XP toolchain ready.'
