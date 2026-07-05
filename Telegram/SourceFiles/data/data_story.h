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
class MediaPreload;
struct SendError;

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
	StoryId repostId = 0;
	MsgId forwardId = 0;
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
	int forwards = 0;
	int views = 0;
	int total = 0;
	bool known = false;
};

struct StoryArea {
	QRectF geometry;
	float64 rotation = 0;
	float64 radius = 0;

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

	// XP walk: defaulted comparisons need C++20; write it out for the C++17 build.
	// (C++17 also does not synthesize != from ==, so provide it explicitly.)
	friend inline bool operator==(
			const SuggestedReaction &a,
			const SuggestedReaction &b) {
		return (a.area == b.area)
			&& (a.reaction == b.reaction)
			&& (a.count == b.count)
			&& (a.flipped == b.flipped)
			&& (a.dark == b.dark);
	}
	friend inline bool operator!=(
			const SuggestedReaction &a,
			const SuggestedReaction &b) {
		return !(a == b);
	}
};

struct ChannelPost {
	StoryArea area;
	FullMsgId itemId;

	// XP walk: defaulted comparison -> explicit body (C7589, C++20-only).
	friend inline bool operator==(
			const ChannelPost &a,
			const ChannelPost &b) {
		return (a.area == b.area) && (a.itemId == b.itemId);
	}
	friend inline bool operator!=(
			const ChannelPost &a,
			const ChannelPost &b) {
		return !(a == b);
	}
};

struct UrlArea {
	StoryArea area;
	QString url;

	// XP walk: C++20 defaulted operator== -> manual ==/!= (C7589).
	friend inline bool operator==(const UrlArea &a, const UrlArea &b) {
		return (a.area == b.area) && (a.url == b.url);
	}
	friend inline bool operator!=(const UrlArea &a, const UrlArea &b) {
		return !(a == b);
	}
};

struct WeatherArea {
	StoryArea area;
	QString emoji;
	QColor color;
	int millicelsius = 0;

	// XP walk: defaulted == -> manual (C7589).
	friend inline bool operator==(const WeatherArea &a, const WeatherArea &b) {
		return (a.area == b.area)
			&& (a.emoji == b.emoji)
			&& (a.color == b.color)
			&& (a.millicelsius == b.millicelsius);
	}
	friend inline bool operator!=(const WeatherArea &a, const WeatherArea &b) {
		return !(a == b);
	}
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

	void setPinnedToTop(bool pinned);
	bool pinnedToTop() const;

	void setInProfile(bool value);
	[[nodiscard]] bool inProfile() const;
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
	[[nodiscard]] Data::SendError errorTextForForward(
		not_null<Thread*> to) const;

	void setCaption(TextWithEntities &&caption);
	[[nodiscard]] const TextWithEntities &caption() const;

	[[nodiscard]] Data::ReactionId sentReactionId() const;
	void setReactionId(Data::ReactionId id);

	[[nodiscard]] auto recentViewers() const
		-> const std::vector<not_null<PeerData*>> &;
	[[nodiscard]] const StoryViews &viewsList() const;
	[[nodiscard]] const StoryViews &channelReactionsList() const;
	[[nodiscard]] int interactions() const;
	[[nodiscard]] int views() const;
	[[nodiscard]] int forwards() const;
	[[nodiscard]] int reactions() const;
	void applyViewsSlice(const QString &offset, const StoryViews &slice);
	void applyChannelReactionsSlice(
		const QString &offset,
		const StoryViews &slice);

	[[nodiscard]] const std::vector<StoryLocation> &locations() const;
	[[nodiscard]] auto suggestedReactions() const
		-> const std::vector<SuggestedReaction> &;
	[[nodiscard]] auto channelPosts() const
		-> const std::vector<ChannelPost> &;
	[[nodiscard]] auto urlAreas() const
		-> const std::vector<UrlArea> &;
	[[nodiscard]] auto weatherAreas() const
		-> const std::vector<WeatherArea> &;

	void applyChanges(
		StoryMedia media,
		const MTPDstoryItem &data,
		TimeId now);
	void applyViewsCounts(const MTPDstoryViews &data);
	[[nodiscard]] TimeId lastUpdateTime() const;

	[[nodiscard]] bool repost() const;
	[[nodiscard]] bool repostModified() const;
	[[nodiscard]] PeerData *repostSourcePeer() const;
	[[nodiscard]] QString repostSourceName() const;
	[[nodiscard]] StoryId repostSourceId() const;

	[[nodiscard]] PeerData *fromPeer() const;

private:
	struct ViewsCounts {
		int views = 0;
		int forwards = 0;
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
	PeerData * const _repostSourcePeer = nullptr;
	const QString _repostSourceName;
	const StoryId _repostSourceId = 0;
	PeerData * const _fromPeer = nullptr;
	Data::ReactionId _sentReactionId;
	StoryMedia _media;
	TextWithEntities _caption;
	std::vector<not_null<PeerData*>> _recentViewers;
	std::vector<StoryLocation> _locations;
	std::vector<SuggestedReaction> _suggestedReactions;
	std::vector<ChannelPost> _channelPosts;
	std::vector<UrlArea> _urlAreas;
	std::vector<WeatherArea> _weatherAreas;
	StoryViews _views;
	StoryViews _channelReactions;
	const TimeId _date = 0;
	const TimeId _expires = 0;
	TimeId _lastUpdateTime = 0;
	// XP walk: v4.16.9 made these bit-fields with default member initializers
	// (`bool _out : 1 = false;`) -- a C++20 feature the v141_xp C++17 build rejects
	// (C7582). Keep them as plain bools. v4.16 renamed _pinned -> _pinnedToTop and
	// added _inProfile; v4.13.0 inserted _repostModified (const, set in the ctor).
	bool _out = false;
	bool _inProfile = false;
	bool _pinnedToTop = false;
	bool _privacyPublic = false;
	bool _privacyCloseFriends = false;
	bool _privacyContacts = false;
	bool _privacySelectedContacts = false;
	const bool _repostModified = false;
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
	const not_null<Story*> _story;

	std::unique_ptr<MediaPreload> _task;

};

} // namespace Data
