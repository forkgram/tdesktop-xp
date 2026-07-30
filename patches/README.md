# XP submodule patches

The walk used to carry **forked submodules** — 14 of them, 1142 commits, all of which had to
be merged against upstream by hand every version. They are gone. Every submodule is now
pinned at the commit **upstream itself pins**, and the XP adaptations live here as one patch
per submodule, applied at CMake configure time by `apply_xp_patches.cmake` (included from the
very top of the root `CMakeLists.txt`, before any `include(cmake/...)`, because the `cmake`
helper submodule is patched too).

So the whole port is one repository again:

```
git clone <this repo> && cd tdesktop-walk
git submodule update --init --recursive     # pristine upstream checkouts
cmake -G Ninja -S . -B out/cmb              # patches get applied here
```

Re-running configure is safe — a patch that already reverse-applies is skipped. Use
`-DXP_SKIP_SUBMODULE_PATCHES=ON` to configure against untouched upstream (useful when
bisecting an upstream regression).

## What is in them

| patch | files | what it carries |
|---|---|---|
| `lib_ui.patch` | 64 | the bulk of the C++17 conversions, text/DirectManipulation/toast adaptations |
| `cmake.patch` | 23 | the XP build system: v141_xp toolchain, SDK 7.1A, `/d2FH4-`, subsystem 5.01 |
| `lib_base.patch` | 26 | WinRT/Win8+ API stubs, C++17 fallbacks |
| `lib_webrtc.patch` | 8 | audio/video pieces the XP build keeps |
| `lib_webview.patch` | 7 | Edge/WebView2 stripped, XP stubs |
| `lib_spellcheck.patch` | 5 | Windows spellcheck API gating |
| `lib_lottie.patch` | 6 | the rlottie deadlock fix (synchronous icon preparation) |
| `tgcalls.patch` | 3 | build-level XP fixes |
| `codegen.patch` | 3 | style/lang codegen fixes |
| `lib_crl.patch` | 2 | C++17 semaphore instead of C++20 atomic wait |
| `libprisma.patch`, `MicroTeX.patch`, `lib_translate.patch` | 1 each | single-line C++17 fixes |

Six further submodules (`QR`, `expected`, `hunspell`, `lz4`, `xxHash`, `lib_tl`) are **not**
patched: they are unmodified upstream code, merely pinned at an older upstream commit than
the current tdesktop tag uses (`lib_tl` deliberately so). Their gitlinks carry that.

## The whole history works this way, not just the tip

All 935 commits of this port were rewritten into the same shape: submodule gitlinks point at
the commit **upstream tdesktop pinned at that version**, and that version's patches sit in this
directory, with a generated `apply_xp_patches.cmake` listing only the submodules that existed
then — `xp-v2.0.0` carries 5 patches, `xp-v5.0.0` 11, the tip 13. Before that, only the tip was
usable by anyone else: every older commit pointed at fork commits that lived on a single
machine, so `git submodule update` could not even check an old version out.

While doing it, five gitlinks turned out to have **no `.gitmodules` entry at all** —
`ThirdParty/Catch`, `ThirdParty/crl`, `ThirdParty/sonnet`, `ThirdParty/variant` and
`lib_rlottie`. Upstream had deleted those submodules together with their records years ago;
our merges took upstream's `.gitmodules` but kept the gitlinks. A clone would stop with
"no submodule mapping found". Nothing in the build referred to any of them, so they were
dropped from the history as upstream did — 3141 stale entries across 669 commits.

Two documented deviations remain:

* **`cmake` nested submodules.** `git apply` cannot create or move a gitlink in a working tree,
  so `external/Implib.so` and `external/glib/cppgir` inside `cmake` keep upstream's values
  instead of the fork's. Both are Linux-only and never checked out by the XP build; all 82
  file-level changes are carried by the patch.
* **One lost state.** The `cmake` fork commit referenced by the v4.6.6 merge no longer exists
  (pruned during an earlier history repair), so commits in that range use pristine upstream
  `cmake`.

`ThirdParty/rlottie` is pinned at a commit that sits on no branch of `john-preston/rlottie`,
but it fetches by SHA (verified) — the same situation upstream tdesktop had with it.

## Regenerating a patch

Patches are `git diff <upstream pin> <our tree>`, so editing a submodule and refreshing its
patch is enough — no fork, no merge:

```
# hack in Telegram/lib_ui, then:
git -C Telegram/lib_ui diff > patches/lib_ui.patch      # careful: this REPLACES the patch
```

That naive form only works if the submodule is at the pin with the old patch applied — which
it is after a configure. In other words: the working tree already contains the old patch, so
`git diff` yields old + new changes together, which is exactly the new patch.

`tools/make_patches.py` (in the workspace, not in this repo) regenerates all of them from the
fork branches that still exist inside each submodule's own history, and **verifies** each one
by checking out the pin in a scratch worktree, applying the patch and comparing the resulting
tree hash against the fork's. All 13 verified byte-identical when they were created.

## Updating to a new upstream version

1. Merge the new tdesktop tag as usual — the submodule gitlinks move to upstream's new pins.
2. `cmake` configure will fail loudly for any patch that no longer applies.
3. Fix those hunks in the submodule working tree and regenerate that one patch.

That replaces the old routine of fork-merging every submodule by hand.
