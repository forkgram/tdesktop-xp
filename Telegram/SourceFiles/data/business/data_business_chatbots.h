/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/business/data_business_common.h"

class UserData;

template <typename Flags>
struct EditFlagsDescriptor;

namespace Data {

class Session;

struct ChatbotsSettings {
	UserData *bot = nullptr;
	BusinessRecipients recipients;
	ChatbotsPermissions permissions;

	// XP walk: C++17 explicit ==/!= (no defaulted C7589)
	friend inline bool operator==(const ChatbotsSettings &a, const ChatbotsSettings &b) { return (a.bot == b.bot) && (a.recipients == b.recipients) && (a.permissions == b.permissions); }
	friend inline bool operator!=(const ChatbotsSettings &a, const ChatbotsSettings &b) { return !(a == b); }
};

class Chatbots final {
public:
	explicit Chatbots(not_null<Session*> owner);
	~Chatbots();

	void preload();
	[[nodiscard]] bool loaded() const;
	[[nodiscard]] const ChatbotsSettings &current() const;
	[[nodiscard]] rpl::producer<ChatbotsSettings> changes() const;
	[[nodiscard]] rpl::producer<ChatbotsSettings> value() const;

	void save(
		ChatbotsSettings settings,
		Fn<void()> done,
		Fn<void(QString)> fail);

	void togglePaused(not_null<PeerData*> peer, bool paused);
	void removeFrom(not_null<PeerData*> peer);

private:
	enum class SentRequestType {
		Pause,
		Unpause,
		Remove,
	};
	struct SentRequest {
		SentRequestType type = SentRequestType::Pause;
		mtpRequestId requestId = 0;
	};

	void reload();

	const not_null<Session*> _owner;

	rpl::variable<ChatbotsSettings> _settings;
	mtpRequestId _requestId = 0;
	bool _loaded = false;

	base::flat_map<not_null<PeerData*>, SentRequest> _sentRequests;

};

[[nodiscard]] auto ChatbotsPermissionsLabels()
-> EditFlagsDescriptor<ChatbotsPermissions>;

} // namespace Data
