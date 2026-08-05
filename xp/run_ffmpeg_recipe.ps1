# Run xp/build_ffmpeg_xp.sh under the XP toolchain environment.
#
# The recipe itself is a shell script because ffmpeg's configure is: it needs
# msys2 for make, nasm and pkg-config while the compiler stays MSVC. This wrapper
# is what the workstation's build_ffmpeg_xp.bat does, expressed so a CI runner
# with actions' msys2 can use the very same recipe.
#
#   powershell -File xp/run_ffmpeg_recipe.ps1 -Root C:\xp-toolchain\Libraries
#
# The recipe itself needs no architecture flag: ffmpeg's configure decides the
# subarch by compiling a _M_X64 probe with the cl.exe it finds, so it follows
# whatever environment is set up here. -Arch only selects that environment (and
# -Root, which must be the matching per-architecture library tree).
param(
  [string]$Root = 'C:\xp-toolchain\Libraries',
  [string]$Toolchain = 'C:\xp-toolchain',
  [ValidateSet('x86', 'x64')]
  [string]$Arch = $(if ($env:XP_ARCH) { $env:XP_ARCH } else { 'x86' }),
  [string]$Bash
)

$ErrorActionPreference = 'Stop'

& (Join-Path $PSScriptRoot 'xp_env.ps1') -Toolchain $Toolchain -Arch $Arch

if (-not $Bash) {
  $candidates = @(
    $env:XP_MSYS2_BASH,
    'C:\msys64\usr\bin\bash.exe',
    'D:\a\_temp\msys64\usr\bin\bash.exe',
    'C:\TBuild\ThirdParty\msys64\usr\bin\bash.exe') | Where-Object { $_ }
  $Bash = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $Bash) { throw 'msys2 bash not found - set XP_MSYS2_BASH' }
Write-Host "bash: $Bash"

function ToMsys([string]$path) {
  $full = (Resolve-Path $path).Path
  return '/' + $full.Substring(0, 1).ToLower() + $full.Substring(2).Replace('\', '/')
}

# The recipe reads these; everything else it derives.
$env:XP_LIBS_DIR = ToMsys $Root
$env:XP_OPUS_LIB = $env:XP_LIBS_DIR + '/opus/out/Release'
# Hand the Windows PATH (cl, link, lib) through to the msys shell.
$env:MSYS2_PATH_TYPE = 'inherit'
$env:MSYSTEM = 'MINGW64'

$script = ToMsys (Join-Path $PSScriptRoot 'build_ffmpeg_xp.sh')
Write-Host "recipe: $script"
Write-Host "libs:   $env:XP_LIBS_DIR"

& $Bash -lc "bash $script"
if ($LASTEXITCODE -ne 0) { throw "the FFmpeg recipe failed with $LASTEXITCODE" }
Write-Host 'FFmpeg libraries built.'
