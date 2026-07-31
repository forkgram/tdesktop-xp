# Build the XP-pinned third-party libraries this port links against.
#
# These are NOT what Telegram/build/prepare/prepare.py produces: every one of
# them is held at the last version the v141_xp toolchain can build, and several
# need XP-specific switches. The pins live here, in one place, so a clean machine
# (a CI runner in particular) can reproduce the exact set.
#
#   powershell -File xp/build_libraries.ps1 -Root C:\xp-toolchain\Libraries
#
# Skips whatever is already built, so it is safe to re-run and cheap when a cache
# was restored. Pass -Only <name> while iterating on a single library.
param(
  [string]$Root = 'C:\xp-toolchain\Libraries',
  [string]$Toolchain = 'C:\xp-toolchain',
  [string[]]$Only = @()
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

& (Join-Path $PSScriptRoot 'xp_env.ps1') -Toolchain $Toolchain
New-Item -ItemType Directory -Force -Path $Root | Out-Null

function Want($name) { return ($Only.Count -eq 0) -or ($Only -contains $name) }
function Step($name) { Write-Host ''; Write-Host "=== $name" }

# git writes progress to stderr, which a Stop preference would turn into a
# terminating error, so drive it explicitly and judge by the exit code.
function RunGit($path, $arguments, $tolerate = $false) {
  $previous = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try {
    # git.exe, not git: PowerShell resolves the bare name to this very function.
    $output = & git.exe -C $path @arguments 2>&1
    if ($LASTEXITCODE -ne 0 -and -not $tolerate) {
      $output | ForEach-Object { Write-Host "    $_" }
      throw "git $($arguments -join ' ') failed with $LASTEXITCODE"
    }
    return $output
  } finally { $ErrorActionPreference = $previous }
}

# Fetch a pinned commit without dragging the whole history along.
function Fetch($name, $url, $commit) {
  $path = Join-Path $Root $name
  if (Test-Path (Join-Path $path '.git')) {
    $have = (RunGit $path @('rev-parse', 'HEAD') $true | Select-Object -First 1)
    if ("$have".Trim() -eq $commit) { Write-Host "  $name already at $($commit.Substring(0,10))"; return $path }
  }
  if (-not (Test-Path $path)) { New-Item -ItemType Directory -Force -Path $path | Out-Null }
  # Everything a function does not consume lands in its return value, and git is
  # chatty - so swallow each call explicitly or the caller gets "From https://..."
  # instead of the path.
  if (-not (Test-Path (Join-Path $path '.git'))) { $null = RunGit $path @('init', '-q') }
  $remotes = RunGit $path @('remote') $true
  if ($remotes -contains 'origin') { $null = RunGit $path @('remote', 'remove', 'origin') }
  $null = RunGit $path @('remote', 'add', 'origin', $url)
  $null = RunGit $path @('fetch', '--depth', '1', 'origin', $commit)
  $null = RunGit $path @('checkout', '-q', 'FETCH_HEAD')
  Write-Host "  $name at $($commit.Substring(0,10))"
  return $path
}

function Run($exe, $arguments, $workdir, $redirectTo) {
  Push-Location $workdir
  try {
    if ($redirectTo) {
      # Some steps are "script > file" by nature; keep stderr on the console.
      & $exe @arguments 2>&1 | Out-File -FilePath $redirectTo -Encoding ascii
    } else {
      & $exe @arguments 2>&1 | ForEach-Object { Write-Host "    $_" }
    }
    if ($LASTEXITCODE -ne 0) { throw "$exe failed with $LASTEXITCODE" }
  } finally { Pop-Location }
}

# xp_env.ps1 picked the MSBuild belonging to the instance that registers v141.
$msbuild = $env:XP_MSBUILD
if (-not $msbuild) { throw 'no MSBuild in the instance that provides the v141 toolset' }

# --- zlib -------------------------------------------------------------------
# tdesktop's own fork, built through the vc14 solution the way prepare.py does;
# the port links the ReleaseWithoutAsm flavour (the asm sources predate x86-64
# assemblers and buy nothing here).
if (Want 'zlib') {
  Step 'zlib'
  $zlib = Fetch 'zlib' 'https://github.com/telegramdesktop/zlib.git' '06cfb031dae1e30a68f83db9f226661e4d8dfc31'
  $out = Join-Path $zlib 'contrib\vstudio\vc14\x86\ZlibStatReleaseWithoutAsm\zlibstat.lib'
  if (-not (Test-Path $out)) {
    Run $msbuild @('zlibstat.vcxproj', '/p:Configuration=ReleaseWithoutAsm', '/p:Platform=Win32',
      '/p:PlatformToolset=v141', "/p:WindowsTargetPlatformVersion=$env:XP_WIN10_SDK_VERSION", '/m') (Join-Path $zlib 'contrib\vstudio\vc14')
  }
  if (-not (Test-Path $out)) { throw "zlibstat.lib was not produced" }
  Write-Host "  $out"

  # minizip: no project ships for it, and the port only needs the five sources
  # tdesktop uses (miniunz.c and minizip.c are command line tools with a main()).
  $mz = Join-Path $zlib 'contrib\minizip'
  $mzout = Join-Path $mz 'Release\libminizips.lib'
  if (-not (Test-Path $mzout)) {
    New-Item -ItemType Directory -Force -Path (Join-Path $mz 'Release') | Out-Null
    Run 'cl.exe' @('/nologo', '/c', '/TC', '/MT', '/O2', '/DNDEBUG', "/I$zlib",
      'ioapi.c', 'iowin32.c', 'unzip.c', 'zip.c', 'mztools.c') $mz
    Run 'lib.exe' @('/nologo', "/OUT:$mzout", 'ioapi.obj', 'iowin32.obj', 'unzip.obj', 'zip.obj', 'mztools.obj') $mz
  }
  Write-Host "  $mzout"
}

# --- opus -------------------------------------------------------------------
if (Want 'opus') {
  Step 'opus'
  $opus = Fetch 'opus' 'https://github.com/telegramdesktop/opus.git' '9168ae1595b447caebdadc233e06b193aea16fd4'
  $built = Join-Path $opus 'win32\VS2015\Win32\Release\opus.lib'
  if (-not (Test-Path $built)) {
    Run $msbuild @('opus.sln', '/p:Configuration=Release', '/p:Platform=Win32',
      '/p:PlatformToolset=v141', "/p:WindowsTargetPlatformVersion=$env:XP_WIN10_SDK_VERSION", '/m') (Join-Path $opus 'win32\VS2015')
  }
  if (-not (Test-Path $built)) { throw 'opus.lib was not produced' }
  # The build files expect it under out/Release, the way prepare.py lays it out.
  $dest = Join-Path $opus 'out\Release'
  New-Item -ItemType Directory -Force -Path $dest | Out-Null
  Copy-Item -Force $built (Join-Path $dest 'opus.lib')
  Write-Host "  $dest\opus.lib"
}

# --- openssl ----------------------------------------------------------------
# 1.0.2 is the last branch that builds and runs on XP. Native Strawberry Perl
# only: an msys perl breaks mk1mf.pl path handling.
if (Want 'openssl') {
  Step 'openssl'
  $ssl = Fetch 'openssl' 'https://github.com/openssl/openssl.git' '12ad22dd16ffe47f8cde3cddb84a160e8cdb3e30'
  $out = Join-Path $ssl 'out32\ssleay32.lib'
  if (-not (Test-Path $out)) {
    # It has to be a NATIVE perl: an msys one hands mk1mf.pl unix paths (the
    # giveaway is "PERL = /usr/bin/perl" in the Configure output) and the
    # generated makefile then refers to rules that do not exist. ms\do_ms.bat
    # calls `perl` off PATH itself, so put the native one in front there too.
    $candidates = @(
      $env:XP_PERL,
      'C:\Strawberry\perl\bin\perl.exe',
      'C:\TBuild\ThirdParty\StrawberryPerl\perl\bin\perl.exe') | Where-Object { $_ }
    $perl = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $perl) {
      $found = Get-Command perl -ErrorAction SilentlyContinue
      if ($found -and $found.Source -notmatch 'msys|mingw|usr\\bin') { $perl = $found.Source }
    }
    if (-not $perl) { throw 'no native perl found - OpenSSL needs Strawberry Perl' }
    Write-Host "  perl: $perl"
    $previousPath = $env:PATH
    $env:PATH = (Split-Path -Parent $perl) + ';' + $env:PATH
    try {
      Run $perl @('Configure', 'VC-WIN32', 'no-asm', 'no-shared', '--openssldir=/etc/ssl') $ssl
      # ms\do_ms.bat is just these perl scripts plus the shared-library variants
      # this build does not want. Calling them directly gives a real exit code per
      # step instead of one batch file's, and cannot stall on a console prompt.
      Run $perl @('util\mkfiles.pl') $ssl 'MINFO'
      Run $perl @('util\mk1mf.pl', 'no-asm', 'VC-WIN32') $ssl 'ms\nt.mak'
      Run $perl @('util\mkdef.pl', '32', 'libeay') $ssl 'ms\libeay32.def'
      Run $perl @('util\mkdef.pl', '32', 'ssleay') $ssl 'ms\ssleay32.def'
      Run 'nmake.exe' @('-f', 'ms\nt.mak') $ssl
    } finally { $env:PATH = $previousPath }
  }
  if (-not (Test-Path $out)) { throw 'ssleay32.lib was not produced' }
  Write-Host "  $out"
}

# --- openal-soft ------------------------------------------------------------
# tdesktop's fork; WASAPI is Vista+, so XP gets DirectSound and WinMM.
if (Want 'openal') {
  Step 'openal-soft'
  $oal = Fetch 'openal-soft' 'https://github.com/telegramdesktop/openal-soft.git' '8b152542df1343d46913287e8851fc056f0e9039'
  $out = Join-Path $oal 'build\Release\OpenAL32.lib'
  if (-not (Test-Path $out)) {
    $build = Join-Path $oal 'build'
    New-Item -ItemType Directory -Force -Path $build | Out-Null
    $env:CMAKE_POLICY_VERSION_MINIMUM = '3.5'
    Run 'cmake.exe' @('-G', 'Ninja', '-DCMAKE_POLICY_VERSION_MINIMUM=3.5', '-DCMAKE_BUILD_TYPE=Release',
      '-DLIBTYPE=STATIC', '-DFORCE_STATIC_VCRT=ON', '-DALSOFT_UTILS=OFF', '-DALSOFT_EXAMPLES=OFF',
      '-DALSOFT_TESTS=OFF', '-DALSOFT_NO_CONFIG_UTIL=ON', '-DALSOFT_BACKEND_WINMM=ON',
      '-DALSOFT_BACKEND_DSOUND=ON', '-DALSOFT_BACKEND_WASAPI=OFF', '..') $build
    Run 'ninja.exe' @('OpenAL') $build
    New-Item -ItemType Directory -Force -Path (Join-Path $build 'Release') | Out-Null
    Copy-Item -Force (Join-Path $build 'OpenAL32.lib') $out
  }
  if (-not (Test-Path $out)) { throw 'OpenAL32.lib was not produced' }
  Write-Host "  $out"
}

# --- range-v3 ---------------------------------------------------------------
# Header only, but 0.9.1 needs two small fixes for this toolchain - kept as a
# patch next to the rest of the port's patches.
if (Want 'range-v3') {
  Step 'range-v3'
  $rng = Fetch 'range-v3' 'https://github.com/ericniebler/range-v3.git' '8a732ee6736af8af024b5b2032580b85a9be8239'
  $patch = Join-Path $repo 'xp\deps\range-v3.patch'
  if (Test-Path $patch) {
    # The same "is it applied already?" probe apply_xp_patches.cmake uses; it is
    # expected to fail on a fresh checkout, so it must not be fatal.
    $null = RunGit $rng @('apply', '--reverse', '--check', '--binary', $patch) $true
    if ($LASTEXITCODE -ne 0) {
      $null = RunGit $rng @('apply', '--binary', $patch)
      Write-Host '  patch applied'
    } else {
      Write-Host '  patch already in place'
    }
  }
}

# --- ada --------------------------------------------------------------------
# The URL parser, taken as the upstream single header amalgamation. The port
# compiles it itself because ada's own build wants C++20.
if (Want 'ada') {
  Step 'ada'
  $ada = Join-Path $Root 'ada'
  $single = Join-Path $ada 'out\singleheader'
  $out = Join-Path $single 'Release\ada-singleheader-lib.lib'
  if (-not (Test-Path $out)) {
    New-Item -ItemType Directory -Force -Path $single | Out-Null
    if (-not (Test-Path (Join-Path $single 'ada.cpp'))) {
      $zip = Join-Path $env:TEMP 'ada-singleheader.zip'
      Invoke-WebRequest -UseBasicParsing -OutFile $zip `
        -Uri 'https://github.com/ada-url/ada/releases/download/v3.2.4/singleheader.zip'
      Expand-Archive -Force -Path $zip -DestinationPath $single
    }
    # ada.h includes <version> when the COMPILER looks modern enough, but the
    # 14.16 headers this port targets do not ship it. An empty one leaves the
    # C++20 feature macros undefined, so ada takes its C++17 paths.
    $shim = Join-Path $ada 'xp-shim'
    New-Item -ItemType Directory -Force -Path $shim | Out-Null
    Copy-Item -Force (Join-Path $repo 'xp\deps\ada-version-shim') (Join-Path $shim 'version')
    New-Item -ItemType Directory -Force -Path (Join-Path $single 'Release') | Out-Null
    # ada genuinely needs C++20 - std::endian, std::bit_cast - which the 14.16
    # headers this port targets do not have. It is a self-contained library that
    # never touches the Win32 surface, so build just this one against the MODERN
    # standard headers at C++20. Same /MT CRT, so the object links with the rest,
    # and xpsafe.ps1 guards the final binary against anything the newer standard
    # library might drag in.
    $modernInclude = Join-Path $env:XP_TOOLSET_BINARY_DIR 'include'
    $previousInclude = $env:INCLUDE
    $env:INCLUDE = "$modernInclude;$env:INCLUDE"
    try {
      Run 'cl.exe' @('/nologo', '/c', '/MT', '/O2', '/EHsc', '/std:c++20', '/DNDEBUG', '/d2FH4-',
        '/Foada.obj', 'ada.cpp') $single
    } finally { $env:INCLUDE = $previousInclude }
    Run 'lib.exe' @('/nologo', "/OUT:$out", 'ada.obj') $single
  }
  Write-Host "  $out"
}

Write-Host ''
Write-Host 'Libraries ready.'
