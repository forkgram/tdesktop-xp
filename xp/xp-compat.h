// xp-compat.h - SDK 7.1A backfill for Qt 5.15-XP build
// Force-included via /FI to define post-XP constants/structs missing from SDK 7.1A.
//
// IMPORTANT: this header MUST NOT include any Windows SDK headers (windef.h,
// winnt.h, basetsd.h ...). It is force-included before the translation unit's
// own preamble, so pulling Windows headers here freezes WINVER/_WIN32_WINNT to
// the SDK defaults (XP/x86 baseline) and breaks files that later try to lift
// WINVER (e.g. qwindowspointerhandler.cpp sets WINVER=0x0603 itself).
// Anything that needs winnt.h types goes into a separate header that is
// #included explicitly at the *bottom* of the file's own includes.

#pragma once

// Fail the LINK when two separately built pieces disagree about the Windows API
// baseline: the linker compares the values recorded under one name and errors
// with LNK2038. This header is force-included first, so every object records the
// baseline its BUILD was configured with - which is the thing that decides what
// the SDK headers declare. Qt, built by xp/build_qt.ps1, and the app, built by
// CMake, therefore have to agree; if a future Qt bump silently moves to a Vista+
// baseline, the link says so instead of the loader saying it on the VM.
// It cannot see an object built without this header at all, and it cannot see a
// file that lifts the baseline after the forced include. xp/xpsafe.ps1 covers
// both from the other side, by reading the finished binary's import table.
#ifdef _MSC_VER
#ifdef _WIN32_WINNT
#define XP_COMPAT_STRINGIFY_INNER(value) #value
#define XP_COMPAT_STRINGIFY(value) XP_COMPAT_STRINGIFY_INNER(value)
#pragma detect_mismatch("tdesktop_xp_win32_winnt", XP_COMPAT_STRINGIFY(_WIN32_WINNT))
#endif // _WIN32_WINNT
#endif // _MSC_VER

// Version constants pulled in from sdkddkver.h (Win8+). VersionHelpers.h in
// Windows Kit 10 SDK references these without being able to include sdkddkver
// when we're targeting XP (_WIN32_WINNT=0x0501); pre-define them so the
// header parses. Values match sdkddkver.h byte-for-byte.
#ifndef _WIN32_WINNT_WIN8
#define _WIN32_WINNT_WIN8          0x0602
#endif
#ifndef _WIN32_WINNT_WINBLUE
#define _WIN32_WINNT_WINBLUE       0x0603
#endif
#ifndef _WIN32_WINNT_WINTHRESHOLD
#define _WIN32_WINNT_WINTHRESHOLD  0x0A00
#endif
#ifndef _WIN32_WINNT_WIN10
#define _WIN32_WINNT_WIN10         0x0A00
#endif

// Locale identifiers added in Vista (WINVER >= 0x0600). We stay on 0x0502 for
// XP but Telegram requests LOCALE_SNAME at LoadLibrary'd GetLocaleInfo sites
// that are runtime-guarded already; defining the constant lets the source
// parse, with GetLocaleInfo simply returning empty on XP.
#ifndef LOCALE_SNAME
#define LOCALE_SNAME               0x0000005c
#endif

// IMAGE_FILE_MACHINE_ARM64 added in newer winnt.h; XP's PE never sees ARM64
// but the switch arm in base_info_win.cpp needs the constant to compile.
#ifndef IMAGE_FILE_MACHINE_ARM64
#define IMAGE_FILE_MACHINE_ARM64   0xAA64
#endif

// SetDefaultDllDirectories flags (Win7+ via KB2533623); XP loads them via
// GetProcAddress so the runtime path is gated already, but the constants
// must be present for the source to parse.
#ifndef LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
#define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS  0x00001000
#endif
#ifndef LOAD_LIBRARY_SEARCH_APPLICATION_DIR
#define LOAD_LIBRARY_SEARCH_APPLICATION_DIR  0x00000200
#endif
#ifndef LOAD_LIBRARY_SEARCH_USER_DIRS
#define LOAD_LIBRARY_SEARCH_USER_DIRS  0x00000400
#endif
#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32  0x00000800
#endif
#ifndef LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR  0x00000100
#endif

// IFileOperation flag (Win7+)
#ifndef FOFX_RECYCLEONDELETE
#define FOFX_RECYCLEONDELETE 0x00080000
#endif

// GetSystemMetrics extension index (Vista+, returns the additional border
// thickness applied to sizable windows). Used by lib_ui frame math; XP has
// no padded border so the calls will return 0 at runtime via the Vista loader.
#ifndef SM_CXPADDEDBORDER
#define SM_CXPADDEDBORDER 92
#endif

// WM_DPICHANGED is Win 8.1+ DPI notification. XP never sends it but the
// switch arms must parse; the case is unreachable at runtime.
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

// DWM cloak attribute (Win8+). On XP DwmSetWindowAttribute is itself absent
// so the call is a no-op at runtime; this constant lets the source parse.
#ifndef DWMWA_CLOAK
#define DWMWA_CLOAK 13
#endif
#ifndef DWMWA_CLOAKED
#define DWMWA_CLOAKED 14
#endif

// FastFail / Watson constants (Win8+)
#ifndef FAST_FAIL_FATAL_APP_EXIT
#define FAST_FAIL_FATAL_APP_EXIT 7
#endif

#ifndef STATUS_FATAL_APP_EXIT
#define STATUS_FATAL_APP_EXIT 0x40000015UL
#endif

#ifndef PF_FASTFAIL_AVAILABLE
#define PF_FASTFAIL_AVAILABLE 23
#endif

// File ID structs (Win8+). Pure-integer fields, no Windows-SDK types needed.
#if !defined(_FILE_ID_128_DEFINED_)
#define _FILE_ID_128_DEFINED_
typedef struct _FILE_ID_128 {
    unsigned char Identifier[16];
} FILE_ID_128, *PFILE_ID_128;
#endif

#if !defined(_FILE_ID_INFO_DEFINED_)
#define _FILE_ID_INFO_DEFINED_
typedef struct _FILE_ID_INFO {
    unsigned __int64 VolumeSerialNumber;
    FILE_ID_128 FileId;
} FILE_ID_INFO, *PFILE_ID_INFO;
#endif

// NOTE: do NOT add C++ stdlib shims (e.g. std::remove_cvref) here by #including
// <type_traits>/<cmath> -- this header is /FI force-included before every TU's
// preamble, and pulling those transitively drags in CRT/Windows headers that
// FREEZE _WIN32_WINNT at the XP baseline (breaks base_power_save_blocker_win.cpp
// lifting WINVER for the Vista Power APIs) and lock <math.h> before a TU defines
// _USE_MATH_DEFINES (breaks libtgvoip M_PI). Backport C++20 stdlib bits LOCALLY
// in the fork file that uses them instead (see lib_base base/options.cpp).
