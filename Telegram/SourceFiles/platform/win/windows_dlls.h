/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/platform/win/base_windows_h.h"

// XP walk: the Windows 7 SDK (7.1A) gates Vista/Win7 shell declarations behind
// NTDDI_VERSION. Those shell APIs are resolved at runtime via GetProcAddress and
// never invoked on XP, but their declarations are needed to compile this TU. Lift
// the target version to Win7 across the Windows-header block, then restore the XP
// baseline (exposes declarations only; no Win7 symbol is imported).
#pragma push_macro("NTDDI_VERSION")
#pragma push_macro("_WIN32_WINNT")
#pragma push_macro("WINVER")
#undef NTDDI_VERSION
#undef _WIN32_WINNT
#undef WINVER
#define NTDDI_VERSION NTDDI_WIN7
#define _WIN32_WINNT _WIN32_WINNT_WIN7
#define WINVER _WIN32_WINNT_WIN7

#include <shlobj.h>
#if defined(__has_include) && __has_include(<roapi.h>)
#include <roapi.h>
#else
// The Windows XP SDK (7.1A) ships no WinRT headers. The activation-factory and
// HSTRING helpers declared below are resolved at runtime via GetProcAddress and
// never invoked on XP, so minimal placeholder types suffice to compile this TU.
typedef struct HSTRING__ { int unused; } HSTRING__;
typedef HSTRING__ *HSTRING;
typedef struct HSTRING_HEADER { void *Reserved; } HSTRING_HEADER;
#endif
#include <dwmapi.h>
#include <RestartManager.h>
#include <psapi.h>

#pragma pop_macro("WINVER")
#pragma pop_macro("_WIN32_WINNT")
#pragma pop_macro("NTDDI_VERSION")

#ifdef __MINGW32__
#define __in
#endif

namespace Platform {
namespace Dlls {

void CheckLoadedModules();

// UXTHEME.DLL
inline HRESULT(__stdcall *SetWindowTheme)(
	HWND hWnd,
	LPCWSTR pszSubAppName,
	LPCWSTR pszSubIdList);

//inline void(__stdcall *RefreshImmersiveColorPolicyState)();
//
//inline BOOL(__stdcall *AllowDarkModeForApp)(BOOL allow);
//
//enum class PreferredAppMode {
//	Default,
//	AllowDark,
//	ForceDark,
//	ForceLight,
//	Max
//};
//
//inline PreferredAppMode(__stdcall *SetPreferredAppMode)(
//	PreferredAppMode appMode);
//inline BOOL(__stdcall *AllowDarkModeForWindow)(HWND hwnd, BOOL allow);
//inline void(__stdcall *FlushMenuThemes)();

// SHELL32.DLL
inline HRESULT(__stdcall *SHAssocEnumHandlers)(
	PCWSTR pszExtra,
	ASSOC_FILTER afFilter,
	IEnumAssocHandlers **ppEnumHandler);
inline HRESULT(__stdcall *SHCreateItemFromParsingName)(
	PCWSTR pszPath,
	IBindCtx *pbc,
	REFIID riid,
	void **ppv);
inline HRESULT(__stdcall *SHOpenWithDialog)(
	HWND hwndParent,
	const OPENASINFO *poainfo);
inline HRESULT(__stdcall *OpenAs_RunDLL)(
	HWND hWnd,
	HINSTANCE hInstance,
	LPCWSTR lpszCmdLine,
	int nCmdShow);
// XP walk: QUERY_USER_NOTIFICATION_STATE is a Vista shell type NTDDI-gated out of
// the XP SDK; this pointer is runtime-loaded, so keep the arg opaque (void*).
inline HRESULT(__stdcall *SHQueryUserNotificationState)(
	void *pquns);
inline void(__stdcall *SHChangeNotify)(
	LONG wEventId,
	UINT uFlags,
	__in_opt LPCVOID dwItem1,
	__in_opt LPCVOID dwItem2);
inline HRESULT(__stdcall *SetCurrentProcessExplicitAppUserModelID)(
	__in PCWSTR AppID);

// WTSAPI32.DLL

inline BOOL(__stdcall *WTSRegisterSessionNotification)(
	HWND hWnd,
	DWORD dwFlags);
inline BOOL(__stdcall *WTSUnRegisterSessionNotification)(
	HWND hWnd);

// PROPSYS.DLL

inline HRESULT(__stdcall *PropVariantToString)(
	_In_ REFPROPVARIANT propvar,
	_Out_writes_(cch) PWSTR psz,
	_In_ UINT cch);
inline HRESULT(__stdcall *PSStringFromPropertyKey)(
	_In_ REFPROPERTYKEY pkey,
	_Out_writes_(cch) LPWSTR psz,
	_In_ UINT cch);

// DWMAPI.DLL

inline HRESULT(__stdcall *DwmIsCompositionEnabled)(
	_Out_ BOOL* pfEnabled);
inline HRESULT(__stdcall *DwmSetWindowAttribute)(
	HWND hwnd,
	DWORD dwAttribute,
	_In_reads_bytes_(cbAttribute) LPCVOID pvAttribute,
	DWORD cbAttribute);

// PSAPI.DLL

inline BOOL(__stdcall *GetProcessMemoryInfo)(
	HANDLE Process,
	PPROCESS_MEMORY_COUNTERS ppsmemCounters,
	DWORD cb);

// USER32.DLL

enum class WINDOWCOMPOSITIONATTRIB {
	WCA_UNDEFINED = 0,
	WCA_NCRENDERING_ENABLED = 1,
	WCA_NCRENDERING_POLICY = 2,
	WCA_TRANSITIONS_FORCEDISABLED = 3,
	WCA_ALLOW_NCPAINT = 4,
	WCA_CAPTION_BUTTON_BOUNDS = 5,
	WCA_NONCLIENT_RTL_LAYOUT = 6,
	WCA_FORCE_ICONIC_REPRESENTATION = 7,
	WCA_EXTENDED_FRAME_BOUNDS = 8,
	WCA_HAS_ICONIC_BITMAP = 9,
	WCA_THEME_ATTRIBUTES = 10,
	WCA_NCRENDERING_EXILED = 11,
	WCA_NCADORNMENTINFO = 12,
	WCA_EXCLUDED_FROM_LIVEPREVIEW = 13,
	WCA_VIDEO_OVERLAY_ACTIVE = 14,
	WCA_FORCE_ACTIVEWINDOW_APPEARANCE = 15,
	WCA_DISALLOW_PEEK = 16,
	WCA_CLOAK = 17,
	WCA_CLOAKED = 18,
	WCA_ACCENT_POLICY = 19,
	WCA_FREEZE_REPRESENTATION = 20,
	WCA_EVER_UNCLOAKED = 21,
	WCA_VISUAL_OWNER = 22,
	WCA_HOLOGRAPHIC = 23,
	WCA_EXCLUDED_FROM_DDA = 24,
	WCA_PASSIVEUPDATEMODE = 25,
	WCA_USEDARKMODECOLORS = 26,
	WCA_LAST = 27
};

struct WINDOWCOMPOSITIONATTRIBDATA {
	WINDOWCOMPOSITIONATTRIB Attrib;
	PVOID pvData;
	SIZE_T cbData;
};

inline BOOL(__stdcall *SetWindowCompositionAttribute)(
	HWND hWnd,
	WINDOWCOMPOSITIONATTRIBDATA*);

} // namespace Dlls
} // namespace Platform
