/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {

struct BotCommand final {
	QString command;
	QString description;

	// XP walk: defaulted == -> manual (C7589).
	friend inline bool operator==(const BotCommand &a, const BotCommand &b) {
		return (a.command == b.command) && (a.description == b.description);
	}
	friend inline bool operator!=(const BotCommand &a, const BotCommand &b) {
		return !(a == b);
	}
};

[[nodiscard]] BotCommand BotCommandFromTL(const MTPBotCommand &result);

} // namespace Data
