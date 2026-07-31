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

# The compiler BINARY stays modern on purpose - see xp/cmake_xp.ps1.
$binary = Get-ChildItem $toolsRoot | Where-Object { $_.Name -like '14.4*' -or $_.Name -like '14.5*' } |
  Sort-Object Name -Descending | Select-Object -First 1
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
    & $lib /nologo /extract:$member /out:(Join-Path $fpdir $obj) $libcmt | Out-Null
  }

  Push-Location $fpdir
  try {
    & $cl /nologo /c /MT /I"$include" /Foxpfls_c.obj (Join-Path $repo 'xp\fpcompat\xpfls.c')
    if ($LASTEXITCODE -ne 0) { throw 'compiling xpfls.c failed' }
    & $cl /nologo /c /MT /Foisacompat.obj (Join-Path $repo 'xp\fpcompat\isacompat.c')
    if ($LASTEXITCODE -ne 0) { throw 'compiling isacompat.c failed' }
    & $ml /nologo /c /coff /Foxpfls_asm.obj (Join-Path $repo 'xp\fpcompat\xpfls.asm')
    if ($LASTEXITCODE -ne 0) { throw 'assembling xpfls.asm failed' }
    & $lib /nologo /OUT:fpcompat.lib ftol2.obj ftol3.obj isacompat.obj xpfls_c.obj xpfls_asm.obj
    if ($LASTEXITCODE -ne 0) { throw 'building fpcompat.lib failed' }
  } finally {
    Pop-Location
  }
}
Write-Host "  fpcompat: $fplib"

# --- what the build steps need ----------------------------------------------
Step 'Environment'
Export 'XP_SDK71A_INCLUDE' $include
Export 'XP_TOOLSET_TARGET' $target.Name
Export 'XP_TOOLSET_BINARY' $binary.Name
Export 'XP_FPCOMPAT' $fpdir
Export 'XP_SDK71A_ROOT' $sdk
Write-Host 'XP toolchain ready.'
