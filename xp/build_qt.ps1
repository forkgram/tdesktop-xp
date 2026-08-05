# Build the static XP Qt this port links against.
#
# Qt 5.15 is the last branch that can target Windows XP at all, and even that
# needs the-r3dacted/qt5-xp - a fork carrying the XP fixes upstream dropped -
# plus the handful of changes in xp/deps/qt5-xp.patch. The result is a STATIC
# build with a static CRT, which is what lets the finished Telegram.exe run on a
# bare XP install with no runtime to deploy.
#
#   powershell -File xp/build_qt.ps1 -Root C:\xp-toolchain [-Arch x64]
#
# Hours, not minutes. Everything is skipped when its output is already there, so
# a restored cache turns this into a no-op.
#
# -Arch gets its own build tree and prefix; only the checked-out source is
# shared, because the XP patch is idempotent and nothing in the source tree is
# written during a shadow build. win32-msvc is the mkspec for BOTH targets -
# Qt 5 takes the architecture from the compiler it finds on PATH, and there is
# no win64-msvc.
param(
  [string]$Root = 'C:\xp-toolchain',
  [string]$Toolchain = 'C:\xp-toolchain',
  [ValidateSet('x86', 'x64')]
  [string]$Arch = $(if ($env:XP_ARCH) { $env:XP_ARCH } else { 'x86' }),
  [int]$Jobs = 0
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$suffix = if ($Arch -eq 'x64') { '-x64' } else { '' }

$source = Join-Path $Root 'qt5-xp'
$build = Join-Path $Root "qt-xp-static-build$suffix"
$prefix = Join-Path $Root "qt-xp-static-prefix$suffix"
$imageformats = Join-Path $Root "qtimageformats-static-build$suffix"

# Qt's own sources need the modern Windows headers - see xp_env.ps1 -ForQt.
& (Join-Path $PSScriptRoot 'xp_env.ps1') -Toolchain $Toolchain -Arch $Arch -ForQt
Write-Host "Qt for $Arch -> $prefix"

function Step($text) { Write-Host ''; Write-Host "=== $text" }
function RunGit($path, $arguments, $tolerate = $false) {
  $previous = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try {
    $output = & git.exe -C $path @arguments 2>&1
    if ($LASTEXITCODE -ne 0 -and -not $tolerate) {
      $output | ForEach-Object { Write-Host "    $_" }
      throw "git $($arguments -join ' ') failed with $LASTEXITCODE"
    }
    return $output
  } finally { $ErrorActionPreference = $previous }
}
function Run($exe, $arguments, $workdir, $tolerate = $false) {
  Push-Location $workdir
  $previous = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try {
    & $exe @arguments 2>&1 | ForEach-Object { Write-Host "    $_" }
    if ($LASTEXITCODE -ne 0 -and -not $tolerate) { throw "$exe failed with $LASTEXITCODE" }
  } finally {
    $ErrorActionPreference = $previous
    Pop-Location
  }
}

if (Test-Path (Join-Path $prefix 'lib\Qt5Core.lib')) {
  Write-Host "Qt already built at $prefix"
  exit 0
}

# --- source ------------------------------------------------------------------
Step 'qt5-xp source'
$commit = '85b7e0707ca72fc0c9f744c306aa5daede1ffb57'
if (-not (Test-Path (Join-Path $source 'qtbase\configure.bat'))) {
  New-Item -ItemType Directory -Force -Path $source | Out-Null
  $null = RunGit $source @('init', '-q')
  $remotes = RunGit $source @('remote') $true
  if ($remotes -notcontains 'origin') {
    $null = RunGit $source @('remote', 'add', 'origin', 'https://github.com/the-r3dacted/qt5-xp.git')
  }
  # A full Qt history is gigabytes; this pin is all the build needs.
  $null = RunGit $source @('fetch', '--depth', '1', 'origin', $commit)
  $null = RunGit $source @('checkout', '-q', 'FETCH_HEAD')
  if (Test-Path (Join-Path $source '.gitmodules')) {
    $null = RunGit $source @('submodule', 'update', '--init', '--recursive', '--depth', '1')
  }
}
Write-Host "  $source"

Step 'XP patch'
$patch = Join-Path $repo 'xp\deps\qt5-xp.patch'
$null = RunGit $source @('apply', '--reverse', '--check', '--binary', $patch) $true
if ($LASTEXITCODE -ne 0) {
  $null = RunGit $source @('apply', '--binary', $patch)
  Write-Host '  applied'
} else {
  Write-Host '  already in place'
}

# --- configure ---------------------------------------------------------------
Step 'configure'
if (-not (Test-Path (Join-Path $build 'Makefile'))) {
  New-Item -ItemType Directory -Force -Path $build | Out-Null
  # -static with -static-runtime is the whole point: no MSVCP140, no UCRT to
  # deploy next to the executable. qtimageformats is skipped here and built
  # separately below, against the finished prefix.
  $skip = @('qt3d', 'qtactiveqt', 'qtandroidextras', 'qtcharts', 'qtconnectivity',
    'qtdatavis3d', 'qtdeclarative', 'qtdoc', 'qtgamepad', 'qtgraphicaleffects',
    'qtimageformats', 'qtlocation', 'qtlottie', 'qtmacextras', 'qtmultimedia',
    'qtnetworkauth', 'qtpurchasing', 'qtquick3d', 'qtquickcontrols',
    'qtquickcontrols2', 'qtquicktimeline', 'qtremoteobjects', 'qtscript',
    'qtscxml', 'qtsensors', 'qtserialbus', 'qtserialport', 'qtspeech',
    'qttools', 'qttranslations', 'qtvirtualkeyboard', 'qtwayland',
    'qtwebchannel', 'qtwebengine', 'qtwebglplugin', 'qtwebsockets',
    'qtwebview', 'qtwinextras', 'qtx11extras', 'qtxmlpatterns')
  $arguments = @('-opensource', '-confirm-license', '-release', '-static',
    '-static-runtime', '-platform', 'win32-msvc', '-prefix', $prefix,
    '-no-feature-netlistmgr', '-no-angle', '-opengl', 'desktop',
    '-nomake', 'examples', '-nomake', 'tests', '-no-ltcg', '-mp')
  foreach ($module in $skip) { $arguments += @('-skip', $module) }
  Run (Join-Path $source 'configure.bat') $arguments $build
}
Write-Host "  $build"

# --- build -------------------------------------------------------------------
Step 'nmake'
Run 'nmake.exe' @() $build
Step 'nmake install'
Run 'nmake.exe' @('install') $build
if (-not (Test-Path (Join-Path $prefix 'lib\Qt5Core.lib'))) { throw 'Qt did not install' }
Write-Host "  $prefix"

# --- qtimageformats ----------------------------------------------------------
# Only for qwebp: the emoji sets and stickers this port renders are WEBP.
Step 'qtimageformats'
if (-not (Test-Path (Join-Path $prefix 'plugins\imageformats\qwebp.lib'))) {
  New-Item -ItemType Directory -Force -Path $imageformats | Out-Null
  $qmake = Join-Path $prefix 'bin\qmake.exe'
  Run $qmake @((Join-Path $source 'qtimageformats\qtimageformats.pro')) $imageformats
  Run 'nmake.exe' @() $imageformats
  Run 'nmake.exe' @('install') $imageformats
}
if (-not (Test-Path (Join-Path $prefix 'plugins\imageformats\qwebp.lib'))) {
  throw 'qwebp.lib was not produced'
}
Write-Host '  qwebp in place'

Write-Host ''
Write-Host 'Qt ready.'
