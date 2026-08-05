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
| `xp_env.ps1` | the single definition of what "building for XP" means — INCLUDE/LIB/PATH, and the `-Arch` switch every other script defers to |
| `bootstrap_toolchain.ps1` | builds the toolchain on a machine that has none (a CI runner): the v141 target, SDK 7.1A, the patched headers, `fpcompat`, `xp_compat` |
| `build_libraries.ps1`, `build_qt.ps1` | the pinned third-party set and the static Qt, per architecture |
| `fpcompat/xpfls.asm`, `fpcompat/xpfls64.asm` | the CRT's Vista+ import slots pointed at XP equivalents — 32- and 64-bit dialects of the same six thunks |
| `exports/`, `exports-x64-extra.txt` | the export whitelist `xpsafe.ps1` checks against, and the provisional 64-bit overlay |

Machine-specific locations (`Libraries-walk`, `fpcompat`, the v141_xp environment)
are variables with the current workspace as the default — override them from the
environment rather than editing the scripts.

## Toolchain contract

The build uses the **14.44 compiler binary** with a **v141_xp 14.16 target** via
`INCLUDE`/`LIB`, a patched SDK 7.1A include tree, `/d2FH4-` (FH3 exception
handling, which is what the 14.16 CRT provides) and `/SUBSYSTEM:WINDOWS,5.01`
(5.02 on x64 — see below). The workspace wrapper `cmake_xp.ps1` injects that
environment; never build with a bare `ninja`.

## Two targets

| | x86 | x64 |
|---|---|---|
| Windows | XP SP3 | XP Professional x64 Edition |
| NT version | 5.1 | **5.2** — the Server 2003 kernel |
| subsystem | 5.01 | **5.02** (5.01 is not a legal value for an x64 image and the linker refuses it) |
| package | `txpupd<version>` | `txp64upd<version>` |
| feed key | `winxp` | `winxp64` |

Everything is chosen by one switch: `-Arch x86|x64` on each script, defaulting to
`$env:XP_ARCH`, which `xp_env.ps1` also *exports* along with `XP_SUBSYSTEM_VERSION`,
`XP_MSVC_PLATFORM` and `XP_MACHINE`. Set `XP_ARCH` once and the rest follows.

```
powershell xp\bootstrap_toolchain.ps1 -Root C:\xp-toolchain -Arch x64
powershell xp\build_libraries.ps1 -Root C:\xp-toolchain\Libraries-x64 -Arch x64
powershell xp\build_qt.ps1 -Root C:\xp-toolchain -Arch x64
```

Shared between them: the toolset install (one component carries both targets), the
patched 7.1A include tree, and the `qt5-xp` source checkout. Per-architecture, in
sibling directories with a `-x64` suffix so nothing already built moves:
`fpcompat`, `xp_compat`, `Libraries`, `qt-xp-static-prefix`, the build tree, and
every CI cache key.

One thing does **not** move: `cmake/variables.cmake` resolves the dependencies as
`../Libraries-walk`, a sibling of the source tree, and that rule is the same for
both. The release workflow satisfies it with a junction whose *target* changes
with the architecture — the name stays put. A workstation building x64 has to do
the same (repoint `..\Libraries-walk` at the x64 set) or keep a second checkout;
the two cannot be configured side by side out of one tree.

**What actually differs in the code**, beyond library paths:

* **fpcompat has no float helpers on x64.** `ftol2`/`ftol3` and `__ltof3` /
  `__ultof3` / `__dtoul3_legacy` answer calls the *32-bit* compiler emits for
  double→integer conversion; the 64-bit one does it inline with SSE2 and emits
  none of them, so `lib\x64\libcmt.lib` has no such members and there is nothing
  to lift out. `fpcompat_host.lib` would then be an empty archive, which `lib.exe`
  refuses to write — `fpcompat/hoststub.c` is the one object that fills it.
* **The FLS thunks are assembled from `fpcompat/xpfls64.asm`** by `ml64`, because
  x64 has no stdcall decoration (`__imp_FlsAlloc`, not `__imp__FlsAlloc@4`) and an
  import slot is 8 bytes. The C half (`xpfls.c`) is shared. Note that NT 5.2 *does*
  export `Fls*` and `GetNumaHighestNodeNumber` — `InitializeCriticalSectionEx` is
  the one that is genuinely Vista+ on both — but they are all thunked anyway, so
  the binary does not depend on which service pack the target carries.
* **OpenSSL configures as `VC-WIN64A`.** The output directories do *not* change
  with it: mk1mf still writes `out32`/`inc32`, which is what `cmake/external/openssl`
  points at for both.
* **FFmpeg needs no flag at all.** Its configure decides the subarch by compiling a
  `_M_X64` probe with whatever `cl.exe` is on PATH, so it follows the environment.
* **Qt uses the `win32-msvc` mkspec on both.** There is no `win64-msvc`; Qt 5 takes
  the architecture from the compiler.

### The 64-bit export tables

`xp/exports-x64` holds the real export tables of a Windows XP Professional x64
Edition install — 34 DLLs, 10 792 names, against which `xpsafe.ps1 -Arch x64`
checks every named import. Same contract as the 32-bit `xp/exports`.

They come off **XP x64 RTM, 5.2.3790.1830** (`srv03_sp1_rtm`). If the machines
this ships to run SP2, the tables are a *subset* of what they have, which errs the
safe way — the gate can report a name that exists there, never wave through one
that does not.

Why this matters is not theoretical. Every 64-bit binary imports the x64 SEH
unwinder — `RtlLookupFunctionEntry`, `RtlPcToFileHeader`, `RtlUnwindEx`,
`RtlVirtualUnwind` — and 32-bit Windows has no counterpart for any of them, so
without these tables the gate fails a perfectly good build on its very first probe.

A tree without `xp/exports-x64` falls back to the x86 tables and prints
**PROVISIONAL** rather than refusing to run; see the header of `xpsafe.ps1`.

To re-dump, the guest half needs Guest Additions, which **7.2.6 does not deliver on
NT 5.2 x64** — the services start and the VBoxGuest driver does not, so
`guestcontrol` and shared folders are both dead. Read the DLLs off the disk instead,
with no guest running and no elevation:

```powershell
VBoxManage clonemedium disk WinXP64.vdi xp64.img --format RAW   # XP puts NTFS at LBA 63
$T = 'C:\Users\h\xp-iso\win81-deploy\tools\ntfs-reader.ps1'
& $T -Image xp64.img -PartLBA 63 -Paths 'WINDOWS\system32\kernel32.dll',... -MaxBytes 20MB -To dlls
& $T -Image xp64.img -PartLBA 63 -Find 'gdiplus.dll'   # take the amd64_ WinSxS one
powershell xp\dump_xp_exports.ps1 -Arch x64 -From dlls
```

`system32` on that machine holds the 64-bit DLLs; `SysWOW64` holds the 32-bit set,
which a 64-bit binary never imports. When the guest can run commands, the shorter
route is `xp\dump_xp_exports.ps1 -Arch x64`, which drives the VM through
`xpvm-tools\xpexec.ps1 -Vm WinXP64` by itself.

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

## Auto-update

The XP build updates itself through the same MTProto feed as the fork's Windows 7+
and Linux releases, and stays separate inside it:

* the client asks the feed for the **`winxp`** key - **`winxp64`** on the 64-bit
  build (`update_checker.cpp`, not `Platform::AutoUpdateKey()`, which says `win`
  for any x86 build) - so a Windows 7+ package, which cannot even start here, is
  never offered. The two XP builds are kept apart for a reason of their own: the
  x86 one runs on XP x64 through WOW64, so a shared key would silently move a
  64-bit machine onto the 32-bit line and keep it there;
* packages are named **`txpupd<version>`** (`Packer -target winxp`) and
  **`txp64upd<version>`** (`-target winxp64`) for the same reason, and
  `FindUpdateFile()` accepts both prefixes;
* the signature key pair is the fork's. It lives in TWO places that must agree:
  `config.h` (client verifies) and `_other/packer.cpp` (Packer verifies its own
  output). A mismatch fails at packing time, which is the good outcome.

Publishing is `xp/publish_telegram.py` from the release workflow: it uploads the
package to the files channel and MERGES a `winxp` / `winxp64` entry into the feed
message, leaving every other platform's entry untouched. It publishes whichever of
the two packages the run produced — both, when the matrix built both — in one
message. `publish_update: rehearsal` sends
everything 360 days into the future - the whole path runs, nothing appears yet.

Enabling this needs `DESKTOP_APP_DISABLE_AUTOUPDATE=OFF` (the port has no
DESKTOP_APP_SPECIAL_TARGET, which is what upstream keys autoupdate off) plus
`DESKTOP_APP_BUILD_PACKER=ON`, and `Updater.exe` must ship next to the app - it is
what replaces the running binary, so xpsafe gates it exactly like `Telegram.exe`.

## Order of a deploy

```
build_ffmpeg_xp.sh          # only when the FFmpeg configuration changes
cmake_xp.ps1 ninja ... Telegram
xp/xpsafe.ps1               # must print PASS -- both sections
Telegram.exe -xpselftest    # the runtime half; also runs on the CI runner
xpdeploy_keep.ps1           # preserves tdata, so a real chat can be opened
xpalive.ps1                 # a screenshot is NOT proof the build survives
```
