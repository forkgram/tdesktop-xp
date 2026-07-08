/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_view_top_toast.h"

#include "ui/toast/toast.h"
#include "core/ui_integration.h"
#include "styles/style_chat.h"
#include "styles/style_widgets.h"

namespace HistoryView {

namespace {

[[nodiscard]] crl::time CountToastDuration(const TextWithEntities &text) {
	return std::clamp(
		crl::time(1000) * int(text.text.size()) / 14,
		crl::time(1000) * 5,
		crl::time(1000) * 8);
}

} // namespace

InfoTooltip::InfoTooltip() = default;

void InfoTooltip::show(
		not_null<QWidget*> parent,
		not_null<Main::Session*> session,
		const TextWithEntities &text,
		Fn<void()> hiddenCallback) {
	hide(anim::type::normal);
	// XP walk: take theirs (MarkedTextContext -> TextContext). Config named-local
	// (move-only member); TextContextArgs designated -> positional (session@0).
	// Took theirs' icon.
	auto config = Ui::Toast::Config();
	config.text = text;
	config.textContext = Core::TextContext({ session });
	config.icon = &st::historyInfoToastIcon;
	config.st = &st::historyInfoToast;
	config.attach = RectPart::Top;
	config.duration = CountToastDuration(text);
	_topToast = Ui::Toast::Show(parent, std::move(config));
	if (const auto strong = _topToast.get()) {
		if (hiddenCallback) {
			QObject::connect(
				strong->widget(),
				&QObject::destroyed,
				hiddenCallback);
		}
	} else if (hiddenCallback) {
		hiddenCallback();
	}
}

void InfoTooltip::hide(anim::type animated) {
	if (const auto strong = _topToast.get()) {
		if (animated == anim::type::normal) {
			strong->hideAnimated();
		} else {
			strong->hide();
		}
	}
}

} // namespace HistoryView
