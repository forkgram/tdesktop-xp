/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_peer_id.h"

struct MsgId {
	constexpr MsgId() noexcept = default;
	constexpr MsgId(int64 value) noexcept : bare(value) {
	}

	friend inline constexpr bool operator==(MsgId a, MsgId b) { return a.bare == b.bare; }
	friend inline constexpr bool operator!=(MsgId a, MsgId b) { return a.bare != b.bare; }
	friend inline constexpr bool operator<(MsgId a, MsgId b) { return a.bare < b.bare; }
	friend inline constexpr bool operator>(MsgId a, MsgId b) { return a.bare > b.bare; }
	friend inline constexpr bool operator<=(MsgId a, MsgId b) { return a.bare <= b.bare; }
	friend inline constexpr bool operator>=(MsgId a, MsgId b) { return a.bare >= b.bare; }

	[[nodiscard]] constexpr explicit operator bool() const noexcept {
		return (bare != 0);
	}
	[[nodiscard]] constexpr bool operator!() const noexcept {
		return !bare;
	}
	[[nodiscard]] constexpr MsgId operator-() const noexcept {
		return -bare;
	}
	constexpr MsgId operator++() noexcept {
		return ++bare;
	}
	constexpr MsgId operator++(int) noexcept {
		return bare++;
	}
	constexpr MsgId operator--() noexcept {
		return --bare;
	}
	constexpr MsgId operator--(int) noexcept {
		return bare--;
	}

	int64 bare = 0;
};

Q_DECLARE_METATYPE(MsgId);

[[nodiscard]] inline constexpr MsgId operator+(MsgId a, MsgId b) noexcept {
	return MsgId(a.bare + b.bare);
}

[[nodiscard]] inline constexpr MsgId operator-(MsgId a, MsgId b) noexcept {
	return MsgId(a.bare - b.bare);
}

constexpr auto StartClientMsgId = MsgId(0x01 - (1LL << 58));
constexpr auto ClientMsgIds = (1LL << 31);
constexpr auto EndClientMsgId = MsgId(StartClientMsgId.bare + ClientMsgIds);
constexpr auto ServerMaxMsgId = MsgId(1LL << 56);
constexpr auto ScheduledMsgIdsRange = (1LL << 32);
constexpr auto ShowAtUnreadMsgId = MsgId(0);

constexpr auto SpecialMsgIdShift = EndClientMsgId.bare;
constexpr auto ShowAtTheEndMsgId = MsgId(SpecialMsgIdShift + 1);
constexpr auto SwitchAtTopMsgId = MsgId(SpecialMsgIdShift + 2);
constexpr auto ShowAndStartBotMsgId = MsgId(SpecialMsgIdShift + 4);
constexpr auto ShowForChooseMessagesMsgId = MsgId(SpecialMsgIdShift + 6);

static_assert(SpecialMsgIdShift + 0xFF < 0);
static_assert(-(SpecialMsgIdShift + 0xFF) > ServerMaxMsgId);

[[nodiscard]] constexpr inline bool IsClientMsgId(MsgId id) noexcept {
	return (id >= StartClientMsgId && id < EndClientMsgId);
}
[[nodiscard]] constexpr inline int32 ClientMsgIndex(MsgId id) noexcept {
	Expects(IsClientMsgId(id));

	return int(id.bare - StartClientMsgId.bare);
}
[[nodiscard]] constexpr inline MsgId ClientMsgByIndex(int32 index) noexcept {
	Expects(index >= 0);

	return MsgId(StartClientMsgId.bare + index);
}

[[nodiscard]] constexpr inline bool IsServerMsgId(MsgId id) noexcept {
	return (id > 0 && id < ServerMaxMsgId);
}

struct MsgRange {
	constexpr MsgRange() noexcept = default;
	constexpr MsgRange(MsgId from, MsgId till) noexcept
	: from(from)
	, till(till) {
	}

	friend inline constexpr bool operator==(MsgRange a, MsgRange b) { return (a.from == b.from) && (a.till == b.till); }
	friend inline constexpr bool operator!=(MsgRange a, MsgRange b) { return !(a == b); }

	MsgId from = 0;
	MsgId till = 0;
};

struct FullMsgId {
	constexpr FullMsgId() noexcept = default;
	constexpr FullMsgId(PeerId peer, MsgId msg) noexcept
	: peer(peer), msg(msg) {
	}
	FullMsgId(ChannelId channelId, MsgId msgId) = delete;

	friend inline constexpr bool operator==(FullMsgId a, FullMsgId b) { return (a.peer == b.peer) && (a.msg == b.msg); }
	friend inline constexpr bool operator!=(FullMsgId a, FullMsgId b) { return !(a == b); }
	friend inline constexpr bool operator<(FullMsgId a, FullMsgId b) { return (a.peer < b.peer) || ((a.peer == b.peer) && (a.msg < b.msg)); }
	friend inline constexpr bool operator>(FullMsgId a, FullMsgId b) { return b < a; }
	friend inline constexpr bool operator<=(FullMsgId a, FullMsgId b) { return !(b < a); }
	friend inline constexpr bool operator>=(FullMsgId a, FullMsgId b) { return !(a < b); }

	constexpr explicit operator bool() const noexcept {
		return msg != 0;
	}
	constexpr bool operator!() const noexcept {
		return msg == 0;
	}

	PeerId peer = 0;
	MsgId msg = 0;
};

Q_DECLARE_METATYPE(FullMsgId);

struct GlobalMsgId {
	FullMsgId itemId;
	uint64 sessionUniqueId = 0;

	friend inline constexpr bool operator==(GlobalMsgId a, GlobalMsgId b) { return (a.itemId == b.itemId) && (a.sessionUniqueId == b.sessionUniqueId); }
	friend inline constexpr bool operator!=(GlobalMsgId a, GlobalMsgId b) { return !(a == b); }
	friend inline constexpr bool operator<(GlobalMsgId a, GlobalMsgId b) { return (a.itemId < b.itemId) || ((a.itemId == b.itemId) && (a.sessionUniqueId < b.sessionUniqueId)); }

	constexpr explicit operator bool() const noexcept {
		return itemId && sessionUniqueId;
	}
	constexpr bool operator!() const noexcept {
		return !itemId || !sessionUniqueId;
	}
};

namespace std {

template <>
struct hash<MsgId> : private hash<int64> {
	size_t operator()(MsgId value) const noexcept {
		return hash<int64>::operator()(value.bare);
	}
};

} // namespace std
