/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/deep_links/deep_links_router.h"

#include "dialogs/dialogs_key.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "window/window_session_controller.h"

namespace Core::DeepLinks {
namespace {

Result FocusSearch(const Context &ctx) {
	if (!ctx.controller) {
		return Result::NeedsAuth;
	}
	ctx.controller->content()->searchMessages(QString(), Dialogs::Key());
	return Result::Handled;
}

Result ShowEmojiStatus(const Context &ctx) {
	if (!ctx.controller) {
		return Result::NeedsAuth;
	}
	ctx.controller->setHighlightControlId(u"main-menu/emoji-status"_q);
	ctx.controller->widget()->showMainMenu();
	return Result::Handled;
}

} // namespace

void RegisterChatsHandlers(Router &router) {
	router.add(u"chats"_q, {
		u"search"_q, // path
		CodeBlock{ FocusSearch }, // action
		true, // requiresAuth
		true, // skipActivation
	});

	router.add(u"chats"_q, {
		u"emoji-status"_q, // path
		CodeBlock{ ShowEmojiStatus }, // action
	});
}

} // namespace Core::DeepLinks
