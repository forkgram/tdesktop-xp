/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_view_swipe_back_session.h"

#include "history/view/history_view_list_widget.h"
#include "ui/chat/chat_style.h"
#include "ui/controls/swipe_handler.h"
#include "ui/controls/swipe_handler_data.h"
#include "window/window_session_controller.h"

namespace Window {

void SetupSwipeBackSection(
		not_null<Ui::RpWidget*> parent,
		not_null<Ui::ScrollArea*> scroll,
		not_null<HistoryView::ListWidget*> list) {
	const auto swipeBackData
		= list->lifetime().make_state<Ui::Controls::SwipeBackResult>();
	auto update = [=](Ui::Controls::SwipeContextData data) {
		if (data.translation > 0) {
			if (!swipeBackData->callback) {
				const auto color = [=]() -> std::pair<QColor, QColor> {
					const auto c = list->delegate()->listPreparePaintContext({
						list->delegate()->listChatTheme(), // theme (XP walk: designated->positional)
					});
					return {
						c.st->msgServiceBg()->c,
						c.st->msgServiceFg()->c,
					};
				};
				(*swipeBackData) = Ui::Controls::SetupSwipeBack(
					parent,
					color);
			}
			swipeBackData->callback(data);
			return;
		} else if (swipeBackData->lifetime) {
			(*swipeBackData) = {};
		}
	};
	auto init = [=](Ui::Controls::SwipeHandlerInitData data) {
		if (data.direction != Qt::RightToLeft) {
			return Ui::Controls::SwipeHandlerFinishData();
		}
		return Ui::Controls::DefaultSwipeBackHandlerFinishData([=] {
			list->controller()->showBackFromStack();
		});
	};
	// XP walk: designated -> positional (C7555). SwipeHandlerArgs:
	// widget, scroll, update, init, dontStart.
	Ui::Controls::SetupSwipeHandler({
		list, // widget
		scroll, // scroll
		std::move(update), // update
		std::move(init), // init
		list->touchMaybeSelectingValue(), // dontStart
	});
}

} // namespace Window
