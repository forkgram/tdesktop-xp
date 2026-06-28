/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#if (NTDDI_VERSION < 0x06020000) // NTDDI_WIN8

// XP walk: the real ToastActivator is a WinRT RuntimeClass (Win8+ WRL via
// <wrl/implements.h>/<wrl/module.h>) and its implementation
// (windows_toast_activator.cpp) is excluded from the v141_xp build -- XP uses
// in-app notifications. The class is still referenced on XP via __uuidof() in
// windows_app_user_model_id.cpp (to stamp the toast-activator CLSID onto the
// Start-menu shortcut), so provide a minimal UUID-only declaration here.
#include "base/platform/win/base_windows_h.h"

// {F11932D3-6110-4BBC-9B02-B2EC07A1BD19}
class DECLSPEC_UUID("F11932D3-6110-4BBC-9B02-B2EC07A1BD19") ToastActivator {
};

#else // NTDDI_WIN8+

#include "windows_toastactivator_h.h"
#include "base/platform/win/wrl/wrl_implements_h.h"

// {F11932D3-6110-4BBC-9B02-B2EC07A1BD19}
class DECLSPEC_UUID("F11932D3-6110-4BBC-9B02-B2EC07A1BD19") ToastActivator
	: public ::Microsoft::WRL::RuntimeClass<
		::Microsoft::WRL::RuntimeClassFlags<::Microsoft::WRL::ClassicCom>,
		INotificationActivationCallback,
		::Microsoft::WRL::FtmBase> {
public:
	ToastActivator() = default;
	~ToastActivator() = default;

	HRESULT STDMETHODCALLTYPE Activate(
		_In_ LPCWSTR appUserModelId,
		_In_ LPCWSTR invokedArgs,
		_In_reads_(dataCount) const NOTIFICATION_USER_INPUT_DATA *data,
		ULONG dataCount) override;

	HRESULT STDMETHODCALLTYPE QueryInterface(
		REFIID riid,
		void **ppObj);
	ULONG STDMETHODCALLTYPE AddRef();
	ULONG STDMETHODCALLTYPE Release();

private:
	long _ref = 1;

};

#endif // NTDDI_WIN8+

struct ToastActivation {
	struct UserInput {
		QString key;
		QString value;
	};
	QString args;
	std::vector<UserInput> input;

	[[nodiscard]] static QString String(LPCWSTR value);
};
[[nodiscard]] rpl::producer<ToastActivation> ToastActivations();
