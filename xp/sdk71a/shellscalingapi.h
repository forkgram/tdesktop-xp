#ifndef _INC_SHELLSCALINGAPI
#define _INC_SHELLSCALINGAPI
#pragma once

// XP walk: stub for the Win8.1+ Shell Scaling API.
//
// The patched SDK 7.1A ships no <ShellScalingApi.h>, so an unqualified
// `#include <ShellScalingApi.h>` falls through to the Windows 10 SDK copy, whose
// content does not parse under our v141_xp / -std:c++17 toolchain (it references
// Win8.1 DPI infrastructure guarded out at WINVER=Win7) -> C2143/C2061.
//
// Telegram's XP build only needs MONITOR_DPI_TYPE: GetDpiForMonitor is loaded
// dynamically via Dlls (a function pointer) and wrapped in specific_win.cpp, and
// per-monitor DPI awareness is simply absent on Windows XP. Provide just the enum.

typedef enum MONITOR_DPI_TYPE {
	MDT_EFFECTIVE_DPI = 0,
	MDT_ANGULAR_DPI = 1,
	MDT_RAW_DPI = 2,
	MDT_DEFAULT = MDT_EFFECTIVE_DPI
} MONITOR_DPI_TYPE;

#endif // _INC_SHELLSCALINGAPI
