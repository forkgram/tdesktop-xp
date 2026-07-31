# XP port — build recipe and safety checks

Everything in this directory describes **how the Windows XP build is produced and
verified**. It lives in the repository for the same reason `patches/` does: a clone
that has the sources and the submodule patches still cannot reproduce the binary
without the FFmpeg configuration and the pre-deploy checks that follow.

| file | what it is |
|---|---|
| `build_ffmpeg_xp.sh` | the FFmpeg (release/3.4) static-lib recipe: which encoders, decoders, demuxers, muxers and filters this port needs, and why |
| `xpsafe.ps1` | pre-deploy check of `Telegram.exe`: no absent-on-XP DLL imported NORMAL, **and** no Vista+ function entry point imported |
| `ffopus_xp.cpp`, `build_ffopus.bat` | harness that links the real FFmpeg libs, prints an inventory and reproduces the voice-message encode path |

Machine-specific locations (`Libraries-walk`, `fpcompat`, the v141_xp environment)
are variables with the current workspace as the default — override them from the
environment rather than editing the scripts.

## Toolchain contract

The build uses the **14.44 compiler binary** with a **v141_xp 14.16 target** via
`INCLUDE`/`LIB`, a patched SDK 7.1A include tree, `/d2FH4-` (FH3 exception
handling, which is what the 14.16 CRT provides) and `/SUBSYSTEM:WINDOWS,5.01`.
The workspace wrapper `cmake_xp.ps1` injects that environment; never build with a
bare `ninja`.

## Three traps this directory exists to prevent

**1. A Vista+ entry point.** `xpsafe.ps1` used to check DLLs only. `kernel32.dll`
exists on XP, so a build importing `ReleaseSRWLockExclusive` passed the check and
then died on the machine with *"The procedure entry point ... could not be
located"* — before `main()`. The culprit was `std::shared_mutex`, which MSVC
implements on the SRW lock API. Prefer `std::mutex` + `std::condition_variable` in
anything that reaches this link, and keep extending `$failFuncs` when a new export
bites.

**2. Stale FFmpeg objects after a configure change.** With the msvc toolchain
ffmpeg's header dependency tracking does not notice `config.h` changing, so
`allcodecs.o` / `allformats.o` keep the previous component list: the libraries
contain the new codecs while `avcodec_register_all()` registers none of them, and
`config.h` looks perfect. `make clean` is therefore part of the recipe. Verify with
the harness, not by reading `config.h`.

**3. Registration is per-library.** FFmpeg 3.4 needs `av_register_all()`,
`avcodec_register_all()` **and** `avfilter_register_all()`; tdesktop targets a
version where all three are gone, so it calls none of them. They live in
`FFmpeg::EnsureRegistered()` (`ffmpeg_utility.cpp`) and must be reached before the
first use of each library — including the cached probe in
`Media::Audio::SupportsSpeedControl()`.

## Order of a deploy

```
build_ffmpeg_xp.sh          # only when the FFmpeg configuration changes
cmake_xp.ps1 ninja ... Telegram
xp/xpsafe.ps1               # must print PASS -- both sections
xpdeploy_keep.ps1           # preserves tdata, so a real chat can be opened
xpalive.ps1                 # a screenshot is NOT proof the build survives
```
