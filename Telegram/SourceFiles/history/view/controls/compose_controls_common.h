/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Api {
enum class SendProgressType;
struct SendOptions;
struct SendAction;
} // namespace Api

class History;

namespace HistoryView::Controls {

struct MessageToEdit {
	FullMsgId fullId;
	Api::SendOptions options;
	TextWithTags textWithTags;
	std::optional<bool> spoilerMediaOverride;
};
struct VoiceToSend {
	QByteArray bytes;
	VoiceWaveform waveform;
	crl::time duration = 0;
	Api::SendOptions options;
};
struct SendActionUpdate {
	Api::SendProgressType type = Api::SendProgressType();
	int progress = 0;
	bool cancel = false;
};

enum class WriteRestrictionType {
	None,
	Rights,
	PremiumRequired,
};

struct WriteRestriction {
	using Type = WriteRestrictionType;

	QString text;
	QString button;
	Type type = Type::None;

	[[nodiscard]] bool empty() const {
		return (type == Type::None);
	}
	explicit operator bool() const {
		return !empty();
	}

	// XP walk: C++17 has no defaulted ==; explicit body over the members.
	// (rpl::distinct_until_changed needs != too — C++17 won't synth it from ==.)
	friend inline bool operator==(
			const WriteRestriction &a,
			const WriteRestriction &b) {
		return (a.text == b.text)
			&& (a.button == b.button)
			&& (a.type == b.type);
	}
	friend inline bool operator!=(
			const WriteRestriction &a,
			const WriteRestriction &b) {
		return !(a == b);
	}
};

struct SetHistoryArgs {
	required<History*> history;
	MsgId topicRootId = 0;
	Fn<bool()> showSlowmodeError;
	Fn<Api::SendAction()> sendActionFactory;
	rpl::producer<int> slowmodeSecondsLeft;
	rpl::producer<bool> sendDisabledBySlowmode;
	rpl::producer<bool> liked;
	rpl::producer<WriteRestriction> writeRestriction;
};

struct ReplyNextRequest {
	enum class Direction {
		Next,
		Previous,
	};
	const FullMsgId replyId;
	const Direction direction;
};

} // namespace HistoryView::Controls
