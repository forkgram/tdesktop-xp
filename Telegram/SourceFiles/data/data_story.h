/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "data/data_location.h"
#include "data/data_message_reaction_id.h"

class Image;
class PhotoData;
class DocumentData;

namespace Main {
class Session;
} // namespace Main

namespace Data {

class Session;
class Thread;
class PhotoMedia;

enum class StoryPrivacy : uchar {
	Public,
	CloseFriends,
	Contacts,
	SelectedContacts,
	Other,
};

struct StoryIdDates {
	StoryId id = 0;
	TimeId date = 0;
	TimeId expires = 0;

	[[nodiscard]] bool valid() const {
		return id != 0;
	}
	explicit operator bool() const {
		return valid();
	}

	friend inline bool operator==(StoryIdDates a, StoryIdDates b) {
		return (a.id == b.id)
			&& (a.date == b.date)
			&& (a.expires == b.expires);
	}
	friend inline bool operator!=(StoryIdDates a, StoryIdDates b) {
		return !(a == b);
	}
	friend inline bool operator<(StoryIdDates a, StoryIdDates b) {
		return (a.id < b.id)
			|| ((a.id == b.id) && ((a.date < b.date)
			|| ((a.date == b.date) && (a.expires < b.expires))));
	}
};

struct StoryMedia {
	std::variant<
		v::null_t,
		not_null<PhotoData*>,
		not_null<DocumentData*>> data;

	friend inline bool operator==(StoryMedia a, StoryMedia b) {
		return (a.data == b.data);
	}
	friend inline bool operator!=(StoryMedia a, StoryMedia b) {
		return !(a == b);
	}
};

struct StoryView {
	not_null<PeerData*> peer;
	Data::ReactionId reaction;
	TimeId date = 0;

	friend inline bool operator==(StoryView a, StoryView b) {
		return (a.peer == b.peer)
			&& (a.date == b.date);
	}
	friend inline bool operator!=(StoryView a, StoryView b) {
		return !(a == b);
	}
};

struct StoryViews {
	std::vector<StoryView> list;
	QString nextOffset;
	int reactions = 0;
	int total = 0;
	bool known = false;
};

struct StoryArea {
	QRectF geometry;
	float64 rotation = 0;

	friend inline bool operator==(
			const StoryArea &a,
			const StoryArea &b) {
		return (a.geometry == b.geometry)
			&& (a.rotation == b.rotation);
	}
	friend inline bool operator!=(
			const StoryArea &a,
			const StoryArea &b) {
		return !(a == b);
	}
};

struct StoryLocation {
	StoryArea area;
	Data::LocationPoint point;
	QString title;
	QString address;
	QString provider;
	QString venueId;
	QString venueType;

	friend inline bool operator==(
			const StoryLocation &a,
			const StoryLocation &b) {
		return (a.area == b.area)
			&& (a.point == b.point)
			&& (a.title == b.title)
			&& (a.address == b.address)
			&& (a.provider == b.provider)
			&& (a.venueId == b.venueId)
			&& (a.venueType == b.venueType);
	}
	friend inline bool operator!=(
			const StoryLocation &a,
			const StoryLocation &b) {
		return !(a == b);
	}
};

struct SuggestedReaction {
	StoryArea area;
	Data::ReactionId reaction;
	int count = 0;
	bool flipped = false;
	bool dark = false;

	friend inline bool operator==(
		const SuggestedReaction &,
		const SuggestedReaction &) = default;
};

class Story final {
public:
	Story(
		StoryId id,
		not_null<PeerData*> peer,
		StoryMedia media,
		const MTPDstoryItem &data,
		TimeId now);

	static constexpr int kRecentViewersMax = 3;

	[[nodiscard]] Session &owner() const;
	[[nodiscard]] Main::Session &session() const;
	[[nodiscard]] not_null<PeerData*> peer() const;

	[[nodiscard]] StoryId id() const;
	[[nodiscard]] bool mine() const;
	[[nodiscard]] StoryIdDates idDates() const;
	[[nodiscard]] FullStoryId fullId() const;
	[[nodiscard]] TimeId date() const;
	[[nodiscard]] TimeId expires() const;
	[[nodiscard]] bool unsupported() const;
	[[nodiscard]] bool expired(TimeId now = 0) const;
	[[nodiscard]] const StoryMedia &media() const;
	[[nodiscard]] PhotoData *photo() const;
	[[nodiscard]] DocumentData *document() const;

	[[nodiscard]] bool hasReplyPreview() const;
	[[nodiscard]] Image *replyPreview() const;
	[[nodiscard]] TextWithEntities inReplyText() const;

	void setPinned(bool pinned);
	[[nodiscard]] bool pinned() const;
	[[nodiscard]] StoryPrivacy privacy() const;
	[[nodiscard]] bool forbidsForward() const;
	[[nodiscard]] bool edited() const;
	[[nodiscard]] bool out() const;

	[[nodiscard]] bool canDownloadIfPremium() const;
	[[nodiscard]] bool canDownloadChecked() const;
	[[nodiscard]] bool canShare() const;
	[[nodiscard]] bool canDelete() const;
	[[nodiscard]] bool canReport() const;

	[[nodiscard]] bool hasDirectLink() const;
	[[nodiscard]] std::optional<QString> errorTextForForward(
		not_null<Thread*> to) const;

	void setCaption(TextWithEntities &&caption);
	[[nodiscard]] const TextWithEntities &caption() const;

	[[nodiscard]] Data::ReactionId sentReactionId() const;
	void setReactionId(Data::ReactionId id);

	[[nodiscard]] auto recentViewers() const
		-> const std::vector<not_null<PeerData*>> &;
	[[nodiscard]] const StoryViews &viewsList() const;
	[[nodiscard]] int views() const;
	[[nodiscard]] int reactions() const;
	void applyViewsSlice(const QString &offset, const StoryViews &slice);

	[[nodiscard]] const std::vector<StoryLocation> &locations() const;
	[[nodiscard]] auto suggestedReactions() const
		-> const std::vector<SuggestedReaction> &;

	void applyChanges(
		StoryMedia media,
		const MTPDstoryItem &data,
		TimeId now);
	void applyViewsCounts(const MTPDstoryViews &data);
	[[nodiscard]] TimeId lastUpdateTime() const;

private:
	struct ViewsCounts {
		int views = 0;
		int reactions = 0;
		base::flat_map<Data::ReactionId, int> reactionsCounts;
		std::vector<not_null<PeerData*>> viewers;
	};

	void changeSuggestedReactionCount(Data::ReactionId id, int delta);
	void applyFields(
		StoryMedia media,
		const MTPDstoryItem &data,
		TimeId now,
		bool initial);

	void updateViewsCounts(ViewsCounts &&counts, bool known, bool initial);
	[[nodiscard]] ViewsCounts parseViewsCounts(
		const MTPDstoryViews &data,
		const Data::ReactionId &mine);

	const StoryId _id = 0;
	const not_null<PeerData*> _peer;
	Data::ReactionId _sentReactionId;
	StoryMedia _media;
	TextWithEntities _caption;
	std::vector<not_null<PeerData*>> _recentViewers;
	std::vector<StoryLocation> _locations;
	std::vector<SuggestedReaction> _suggestedReactions;
	StoryViews _views;
	const TimeId _date = 0;
	const TimeId _expires = 0;
	TimeId _lastUpdateTime = 0;
	// XP walk: v4.10.0 made these bit-fields with default member initializers
	// (`bool _out : 1 = false;`) -- a C++20 feature the v141_xp C++17 build rejects.
	// Keep them as plain bools (added _out to match the new upstream field).
	bool _out = false;
	bool _pinned = false;
	bool _privacyPublic = false;
	bool _privacyCloseFriends = false;
	bool _privacyContacts = false;
	bool _privacySelectedContacts = false;
	bool _noForwards = false;
	bool _edited = false;

};

class StoryPreload final : public base::has_weak_ptr {
public:
	StoryPreload(not_null<Story*> story, Fn<void()> done);
	~StoryPreload();

	[[nodiscard]] FullStoryId id() const;
	[[nodiscard]] not_null<Story*> story() const;

private:
	class LoadTask;

	void start();
	void load();
	void callDone();

	const not_null<Story*> _story;
	Fn<void()> _done;

	std::shared_ptr<Data::PhotoMedia> _photo;
	std::unique_ptr<LoadTask> _task;
	rpl::lifetime _lifetime;

};

} // namespace Data
