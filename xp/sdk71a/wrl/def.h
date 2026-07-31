#pragma once
// XP walk shim. WRL's def.h hard-#errors when NTDDI_VERSION < NTDDI_VISTA, but the
// XP build targets NTDDI 0x05010300 and only uses WRL::ComPtr/Implements (which are
// header-only and work fine on XP). SDK 7.1A has no wrl/def.h, so <wrl/def.h> would
// fall through to the Win10 SDK copy and fire the #error. Intercept it here, bump
// NTDDI_VERSION across just the real Win10 def.h (reached via the Win10 ucrt INCLUDE
// entry + "..\\winrt"), then restore it so the rest of the TU stays XP-targeted.
#pragma push_macro("NTDDI_VERSION")
#pragma push_macro("_WIN32_WINNT")
#undef NTDDI_VERSION
#undef _WIN32_WINNT
#define NTDDI_VERSION 0x06000000 // NTDDI_VISTA
#define _WIN32_WINNT 0x0600      // _WIN32_WINNT_VISTA
#include <../winrt/wrl/def.h>
#pragma pop_macro("_WIN32_WINNT")
#pragma pop_macro("NTDDI_VERSION")
