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

// XP walk: v4.11.7+ dropped <winrt/base.h> (my base_windows_winrt.h stub omits it),
// but the members below still need a COM smart pointer. NOT winrt::com_ptr: that
// lives in the C++/WinRT projection, which the Windows Kit keeps in its cppwinrt
// tree and this build deliberately does not put on INCLUDE (SDK 7.1A plus the UCRT,
// and from the kit only what 7.1A never had). WRL's ComPtr is the same thing minus
// the WinRT runtime - header only, plain IUnknown - and it comes from the winrt
// tree the port already uses for roapi.h. ShlObj.h arrives via the header above.
#include <wrl/client.h>

namespace Platform {

class TaskbarButtons;

class WindowsIntegration final
	: public Integration
	, public QAbstractNativeEventFilter {
public:
	~WindowsIntegration();

	void init() override;

	[[nodiscard]] ITaskbarList3 *taskbarList() const;

	[[nodiscard]] static WindowsIntegration &Instance();

private:
	bool nativeEventFilter(
		const QByteArray &eventType,
		void *message,
		native_event_filter_result *result) override;
	bool processEvent(
		HWND hWnd,
		UINT msg,
		WPARAM wParam,
		LPARAM lParam,
		LRESULT *result);

	void createCustomJumpList();
	void refreshCustomJumpList();
	void setupTaskbarButtons(HWND window);

	uint32 _taskbarCreatedMsgId = 0;
	Microsoft::WRL::ComPtr<ITaskbarList3> _taskbarList;
	Microsoft::WRL::ComPtr<ICustomDestinationList> _jumpList;
	std::unique_ptr<TaskbarButtons> _taskbarButtons;

};

[[nodiscard]] std::unique_ptr<Integration> CreateIntegration();

} // namespace Platform
