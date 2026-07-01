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

base::weak_ptr<Toast::Instance> ShowMultilineToast(
		MultilineToastArgs &&args) {
	// XP walk: positional Toast::Config for cxx_std_17 (skipped durationMs/maxLines
	// spelled out so .multiline lands right); keep the parentOverride branch.
	auto config = Ui::Toast::Config{
		{}, // title
		std::move(args.text),
		&st::defaultMultilineToast,
		(args.duration
			? args.duration
			: Ui::Toast::kDefaultDuration),
		16,
		{}, // adaptive
		true,
	};
	return args.parentOverride
		? Ui::Toast::Show(args.parentOverride, std::move(config))
		: Ui::Toast::Show(std::move(config));
}

} // namespace Ui
