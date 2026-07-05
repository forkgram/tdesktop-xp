/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Ui::BotWebView {

struct DownloadsProgress {
	// XP walk: bit-fields dropped (C7582); defaulted == (C7589) -> manual.
	uint64 ready = 0;
	uint64 total = 0;
	uint64 loading = 0;

	friend inline bool operator==(
			const DownloadsProgress &a,
			const DownloadsProgress &b) {
		return (a.ready == b.ready)
			&& (a.total == b.total)
			&& (a.loading == b.loading);
	}
};

struct DownloadsEntry {
	uint32 id = 0;
	QString url;
	QString path;
	// XP walk: bit-fields dropped (C7582).
	uint64 ready = 0;
	uint64 loading = 0;
	uint64 total = 0;
	uint64 failed = 0;
};

enum class DownloadsAction {
	Open,
	Retry,
	Cancel,
};

[[nodiscard]] auto FillAttachBotDownloadsSubmenu(
	rpl::producer<std::vector<DownloadsEntry>> content,
	Fn<void(uint32, DownloadsAction)> callback)
-> FnMut<void(not_null<PopupMenu*>)>;

} // namespace Ui::BotWebView
