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
anything that reaches this link.

The check is now a **whitelist**: `xp/exports/*.txt` are the real export tables of
the target XP SP3 image (34 DLLs, ~10 700 names, produced by `dump_xp_exports.ps1`
off the VM), and every named import of the finished binary must appear in them. A
blacklist only ever catches what has already broken once; this catches the next one
too. Re-dump only when the gate reports a NORMAL dependency with no table.

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

## The runtime gate: `Telegram.exe -xpselftest [report.txt]`

The checks above prove the binary can *start* on XP. They cannot see the other half
of this port's history: FFmpeg 3.4 registering nothing, libopus refusing `FLTP`,
rlottie deadlocking when a chat opens, an image plugin dropping out. Those only ever
showed up in front of the VM, one human at a time.

`-xpselftest` runs those subsystems and returns an exit code: `0` all probes passed
or were skipped, `1` a probe failed, `3` a probe hung and the watchdog killed the
process (the deadlock case — the reason a watchdog exists at all). Each line of the
report names the stage, the status and the timing; the file is appended as it goes,
so a crash still shows which probe was running.

It runs **before** the launcher, on a bare `QCoreApplication`: no window, no account,
no working directory. That is what lets the CI runner execute it seconds after the
link — and most of what it catches is not XP-specific at all, so it does not have to
wait for the VM. Probes that need the platform (OpenAL) report SKIP elsewhere.

Every runtime bug found from here on belongs in `xp_selftest_win.cpp` as a probe.
That is the whole point: the live run stops being how regressions are found.

## Order of a deploy

```
build_ffmpeg_xp.sh          # only when the FFmpeg configuration changes
cmake_xp.ps1 ninja ... Telegram
xp/xpsafe.ps1               # must print PASS -- both sections
Telegram.exe -xpselftest    # the runtime half; also runs on the CI runner
xpdeploy_keep.ps1           # preserves tdata, so a real chat can be opened
xpalive.ps1                 # a screenshot is NOT proof the build survives
```
