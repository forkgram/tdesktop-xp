/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_peer_bot_command.h"

namespace Data {

BotCommand BotCommandFromTL(const MTPBotCommand &result) {
	return result.match([](const MTPDbotCommand &data) {
		return BotCommand {
			qs(data.vcommand().v), // command
			qs(data.vdescription().v), // description
			data.is_ephemeral(), // ephemeral
		};
	});
}

} // namespace Data
