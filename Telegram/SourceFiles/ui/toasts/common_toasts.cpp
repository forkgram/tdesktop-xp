/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/toasts/common_toasts.h"

#include "ui/toast/toast.h"
#include "styles/style_td_common.h"

namespace Ui {

void ShowMultilineToast(MultilineToastArgs &&args) {
	// XP walk: positional for cxx_std_17; Toast::Config is
	// { text, st, durationMs, maxLines, multiline, ... } -- spell out the skipped
	// durationMs/maxLines so .multiline=true lands in the right slot. Keep the
	// v2.6.3 parentOverride branch.
	auto config = Ui::Toast::Config{
		std::move(args.text),
		&st::defaultMultilineToast,
		(args.duration
			? args.duration
			: Ui::Toast::kDefaultDuration),
		16,
		true,
	};
	if (args.parentOverride) {
		Ui::Toast::Show(args.parentOverride, std::move(config));
	} else {
		Ui::Toast::Show(std::move(config));
	}
}

} // namespace Ui
