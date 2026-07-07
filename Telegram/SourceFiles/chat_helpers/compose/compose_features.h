/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace ChatHelpers {

struct ComposeFeatures {
	// XP walk: bit-fields dropped (C7582); took theirs field set.
	bool likes = false;
	bool sendAs = true;
	bool ttlInfo = true;
	bool attachments = true;
	bool botCommandSend = true;
	bool silentBroadcastToggle = true;
	bool attachBotsMenu = true;
	bool inlineBots = true;
	bool megagroupSet = true;
	bool collectibleStatus = false;
	bool stickersSettings = true;
	bool openStickerSets = true;
	bool autocompleteHashtags = true;
	bool autocompleteMentions = true;
	bool autocompleteCommands = true;
	bool suggestStickersByEmoji = true;
	bool commonTabbedPanel = true;
	bool recordMediaMessage = true;
	bool editMessageStars = false;
	bool emojiOnlyPanel = false;
	bool videoStream = false;
};

} // namespace ChatHelpers
