/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/deep_links/deep_links_router.h"

#include "window/window_session_controller.h"

namespace Core::DeepLinks {
namespace {

Result ShowNewGroup(const Context &ctx) {
	if (!ctx.controller) {
		return Result::NeedsAuth;
	}
	ctx.controller->showNewGroup();
	return Result::Handled;
}

Result ShowNewChannel(const Context &ctx) {
	if (!ctx.controller) {
		return Result::NeedsAuth;
	}
	ctx.controller->showNewChannel();
	return Result::Handled;
}

Result ShowAddContact(const Context &ctx) {
	if (!ctx.controller) {
		return Result::NeedsAuth;
	}
	ctx.controller->showAddContact();
	return Result::Handled;
}

} // namespace

void RegisterNewHandlers(Router &router) {
	router.add(u"new"_q, {
		u"group"_q, // path
		CodeBlock{ ShowNewGroup }, // action
	});

	router.add(u"new"_q, {
		u"channel"_q, // path
		CodeBlock{ ShowNewChannel }, // action
	});

	router.add(u"new"_q, {
		u"contact"_q, // path
		CodeBlock{ ShowAddContact }, // action
	});
}

} // namespace Core::DeepLinks
