/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "base/qt/qt_compare.h"

#include <variant>

namespace tl {
template <typename bare>
class boxed;
} // namespace tl

class MTPreaction;
using MTPReaction = tl::boxed<MTPreaction>;
using DocumentId = uint64;

namespace Data {

struct ReactionId {
	std::variant<QString, DocumentId> data;

	[[nodiscard]] static QChar PaidTag() {
		return '*';
	}
	[[nodiscard]] static ReactionId Paid() {
		return { QString(PaidTag()) };
	}

	[[nodiscard]] bool empty() const {
		const auto emoji = std::get_if<QString>(&data);
		return emoji && emoji->isEmpty();
	}
	[[nodiscard]] bool paid() const {
		const auto emoji = std::get_if<QString>(&data);
		return emoji
			&& emoji->size() == 1
			&& emoji->at(0) == PaidTag();
	}
	[[nodiscard]] QString emoji() const {
		const auto emoji = std::get_if<QString>(&data);
		return (emoji && (emoji->size() != 1 || emoji->at(0) != PaidTag()))
			? *emoji
			: QString();
	}
	[[nodiscard]] DocumentId custom() const {
		const auto custom = std::get_if<DocumentId>(&data);
		return custom ? *custom : DocumentId();
	}

	// XP walk: dropped C++20 defaulted operator<=> / operator== (C7589).
	// Free operator< / == / != below provide C++17 comparison + ordering.
	explicit operator bool() const {
		return !empty();
	}
};

struct MessageReaction {
	ReactionId id;
	int count = 0;
	bool my = false;
};

// XP walk: kept HEAD's free operators; theirs relied on the struct's C++20
// defaulted operator<=> / operator== (removed above for the XP toolchain).
inline bool operator<(const ReactionId &a, const ReactionId &b) {
	return a.data < b.data;
}
inline bool operator==(const ReactionId &a, const ReactionId &b) {
	return a.data == b.data;
}

inline bool operator!=(const ReactionId &a, const ReactionId &b) {
	return !(a == b);
}

[[nodiscard]] QString SearchTagToQuery(const ReactionId &tagId);
[[nodiscard]] ReactionId SearchTagFromQuery(const QString &query);
[[nodiscard]] std::vector<ReactionId> SearchTagsFromQuery(
	const QString &query);

struct HashtagWithUsername {
	QString hashtag;
	QString username;
};
[[nodiscard]] HashtagWithUsername HashtagWithUsernameFromQuery(
	QStringView query);

[[nodiscard]] QString ReactionEntityData(const ReactionId &id);

[[nodiscard]] ReactionId ReactionFromMTP(const MTPReaction &reaction);
[[nodiscard]] MTPReaction ReactionToMTP(ReactionId id);

} // namespace Data

Q_DECLARE_METATYPE(Data::ReactionId);
