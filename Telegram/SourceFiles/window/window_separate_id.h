/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Data {
class Thread;
class Folder;
class Forum;
class SavedSublist;
} // namespace Data

namespace Main {
class Account;
class Session;
} // namespace Main

namespace Window {

enum class SeparateType {
	Primary,
	Archive,
	Chat,
	Forum,
	SavedSublist,
	SharedMedia,
};

enum class SeparateSharedMediaType {
	None,
	Photos,
	Videos,
	Files,
	Audio,
	Links,
	Voices,
	GIF,
};

struct SeparateSharedMedia {
	SeparateSharedMediaType type = SeparateSharedMediaType::None;
	not_null<PeerData*> peer;
	MsgId topicRootId = MsgId();
};

struct SeparateId {
	SeparateId(std::nullptr_t);
	SeparateId(not_null<Main::Account*> account);
	SeparateId(SeparateType type, not_null<Main::Session*> session);
	SeparateId(SeparateType type, not_null<Data::Thread*> thread);
	SeparateId(not_null<Data::Thread*> thread);
	SeparateId(not_null<PeerData*> peer);
	SeparateId(SeparateSharedMedia data);

	SeparateType type = SeparateType::Primary;
	SeparateSharedMediaType sharedMedia = SeparateSharedMediaType::None;
	Main::Account *account = nullptr;
	Data::Thread *thread = nullptr; // For types except Main and Archive.
	PeerData *sharedMediaDataPeer = nullptr;
	MsgId sharedMediaDataTopicRootId = MsgId();

	[[nodiscard]] bool valid() const {
		return account != nullptr;
	}
	explicit operator bool() const {
		return valid();
	}

	[[nodiscard]] bool primary() const;
	[[nodiscard]] Data::Thread *chat() const;
	[[nodiscard]] Data::Forum *forum() const;
	[[nodiscard]] Data::Folder *folder() const;
	[[nodiscard]] Data::SavedSublist *sublist() const;
	[[nodiscard]] PeerData *sharedMediaPeer() const;
	[[nodiscard]] MsgId sharedMediaTopicRootId() const;

	[[nodiscard]] bool hasChatsList() const;

	// XP walk: C++20 defaulted operator<=>/== -> manual </==/!= (C7589/C2059).
	friend inline bool operator==(
			const SeparateId &a,
			const SeparateId &b) {
		return (a.type == b.type)
			&& (a.account == b.account)
			&& (a.thread == b.thread);
	}
	friend inline bool operator!=(
			const SeparateId &a,
			const SeparateId &b) {
		return !(a == b);
	}
	friend inline bool operator<(
			const SeparateId &a,
			const SeparateId &b) {
		if (a.type != b.type) return a.type < b.type;
		if (a.account != b.account) return a.account < b.account;
		return a.thread < b.thread;
	}
};

} // namespace Window
