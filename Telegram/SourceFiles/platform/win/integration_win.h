/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/platform/win/base_windows_winrt.h"
#include "platform/platform_integration.h"

#include <QAbstractNativeEventFilter>

// XP walk: v4.11.7 dropped <winrt/base.h> (my base_windows_winrt.h stub omits it),
// but _taskbarList still needs winrt::com_ptr (a compile-time COM smart pointer that
// works without the WinRT runtime). Restore it here (single TU; the "widely-included
// header" concern from the stub does not apply). Matches xp-v4.11.6.
#include <winrt/base.h>

#include <ShlObj.h>

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
