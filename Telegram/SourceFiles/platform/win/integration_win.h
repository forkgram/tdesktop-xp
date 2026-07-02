/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/platform/win/base_windows_shlobj_h.h"
#include "base/platform/win/base_windows_winrt.h"
#include "platform/platform_integration.h"

#include <QAbstractNativeEventFilter>

// XP walk: v4.11.7+ dropped <winrt/base.h> (my base_windows_winrt.h stub omits it), but
// _taskbarList still needs winrt::com_ptr (a compile-time COM smart pointer, no WinRT
// runtime). Restore it (single TU; the stub's "widely-included header" concern doesn't
// apply). ShlObj.h now comes via base_windows_shlobj_h.h above.
#include <winrt/base.h>

namespace Platform {

class WindowsIntegration final
	: public Integration
	, public QAbstractNativeEventFilter {
public:
	void init() override;

	[[nodiscard]] ITaskbarList3 *taskbarList() const;

	[[nodiscard]] static WindowsIntegration &Instance();

private:
	bool nativeEventFilter(
		const QByteArray &eventType,
		void *message,
		long *result) override;
	bool processEvent(
		HWND hWnd,
		UINT msg,
		WPARAM wParam,
		LPARAM lParam,
		LRESULT *result);

	uint32 _taskbarCreatedMsgId = 0;
	winrt::com_ptr<ITaskbarList3> _taskbarList;

};

[[nodiscard]] std::unique_ptr<Integration> CreateIntegration();

} // namespace Platform
