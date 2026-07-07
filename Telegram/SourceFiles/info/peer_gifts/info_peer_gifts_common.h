/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/qt/qt_compare.h"
#include "base/timer.h"
#include "data/data_star_gift.h"
#include "ui/abstract_button.h"
#include "ui/effects/premium_stars_colored.h"
#include "ui/text/custom_emoji_helper.h"
#include "ui/text/text.h"

class StickerPremiumMark;

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Data {
struct UniqueGift;
struct CreditsHistoryEntry;
class SavedStarGiftId;
} // namespace Data

namespace HistoryView {
class StickerPlayer;
} // namespace HistoryView

namespace Main {
class Session;
} // namespace Main

namespace Overview::Layout {
class Checkbox;
} // namespace Overview::Layout

namespace Ui {
class DynamicImage;
} // namespace Ui

namespace Ui::Text {
class CustomEmoji;
} // namespace Ui::Text

namespace Window {
class SessionController;
} // namespace Window

namespace Info::PeerGifts {

struct Tag {
	explicit Tag(not_null<PeerData*> peer, int collectionId = 0)
	: peer(peer)
	, collectionId(collectionId) {
	}

	not_null<PeerData*> peer;
	int collectionId = 0;
};

struct GiftTypePremium {
	int64 cost = 0;
	QString currency;
	int stars = 0;
	int months = 0;
	int discountPercent = 0;

	// XP walk: defaulted operator== (C7589, C++20) -> manual.
	[[nodiscard]] friend inline bool operator==(
			const GiftTypePremium &a,
			const GiftTypePremium &b) {
		return (a.cost == b.cost)
			&& (a.currency == b.currency)
			&& (a.months == b.months)
			&& (a.discountPercent == b.discountPercent);
	}
};

struct GiftTypeStars {
	Data::SavedStarGiftId transferId;
	Data::StarGift info;
	PeerData *from = nullptr;
	TimeId date = 0;
	// XP walk: bit-fields dropped (C7582); took theirs field set.
	bool pinnedSelection = false;
	bool forceTon = false;
	bool userpic = false;
	bool pinned = false;
	bool hidden = false;
	bool resale = false;
	bool mine = false;

	// XP walk: defaulted operator== (C7589, C++20) -> manual.
	[[nodiscard]] friend inline bool operator==(
			const GiftTypeStars &a,
			const GiftTypeStars &b) {
		// XP walk: GiftTypeStars gained nested .info; compare new fields.
		return (a.info == b.info)
			&& (a.from == b.from)
			&& (a.userpic == b.userpic)
			&& (a.hidden == b.hidden)
			&& (a.mine == b.mine);
	}
};

[[nodiscard]] rpl::producer<std::vector<GiftTypeStars>> GiftsStars(
	not_null<Main::Session*> session,
	not_null<PeerData*> peer);

struct GiftDescriptor : std::variant<GiftTypePremium, GiftTypeStars> {
	using variant::variant;

	// XP walk: defaulted operator== (C7589) -> delegate to std::variant's C++17 ==.
	[[nodiscard]] friend inline bool operator==(
			const GiftDescriptor &a,
			const GiftDescriptor &b) {
		return static_cast<const std::variant<GiftTypePremium, GiftTypeStars>&>(a)
			== static_cast<const std::variant<GiftTypePremium, GiftTypeStars>&>(b);
	}
};

struct GiftBadge {
	QString text;
	QColor bg1;
	QColor bg2 = QColor(0, 0, 0, 0);
	QColor border = QColor(0, 0, 0, 0);
	QColor fg;
	bool gradient = false;
	bool small = false;

	explicit operator bool() const {
		return !text.isEmpty();
	}

	// XP walk: C++20 std::strong_ordering operator<=> (needs <compare>) + defaulted ==
	// -> manual ==/!=/< (v5.10.2 order: text, bg1.rgb(), bg2.rgb(), fg.rgb()).
	friend inline bool operator==(const GiftBadge &a, const GiftBadge &b) {
		return (a.text == b.text)
			&& (a.bg1.rgb() == b.bg1.rgb())
			&& (a.bg2.rgb() == b.bg2.rgb())
			&& (a.fg.rgb() == b.fg.rgb());
	}
	friend inline bool operator!=(const GiftBadge &a, const GiftBadge &b) {
		return !(a == b);
	}
	friend inline bool operator<(const GiftBadge &a, const GiftBadge &b) {
		if (a.text != b.text) return a.text < b.text;
		if (a.bg1.rgb() != b.bg1.rgb()) return a.bg1.rgb() < b.bg1.rgb();
		if (a.bg2.rgb() != b.bg2.rgb()) return a.bg2.rgb() < b.bg2.rgb();
		return a.fg.rgb() < b.fg.rgb();
	}
};

enum class GiftButtonMode : uint8 {
	Full,
	Minimal,
	Selection,
};

enum class GiftSelectionMode : uint8 {
	Border,
	Inset,
	Check,
};

class GiftButtonDelegate {
public:
	[[nodiscard]] virtual TextWithEntities star() = 0;
	[[nodiscard]] virtual TextWithEntities monostar() = 0;
	[[nodiscard]] virtual TextWithEntities monoton() = 0;
	[[nodiscard]] virtual TextWithEntities ministar() = 0;
	[[nodiscard]] virtual Ui::Text::MarkedContext textContext() = 0;
	[[nodiscard]] virtual QSize buttonSize() = 0;
	[[nodiscard]] virtual QMargins buttonExtend() const = 0;
	[[nodiscard]] virtual auto buttonPatternEmoji(
		not_null<Data::UniqueGift*> unique,
		Fn<void()> repaint)
	-> std::unique_ptr<Ui::Text::CustomEmoji> = 0;
	[[nodiscard]] virtual QImage background() = 0;
	[[nodiscard]] virtual rpl::producer<not_null<DocumentData*>> sticker(
		const GiftDescriptor &descriptor) = 0;
	[[nodiscard]] virtual not_null<StickerPremiumMark*> hiddenMark() = 0;
	[[nodiscard]] virtual QImage cachedBadge(const GiftBadge &badge) = 0;
	[[nodiscard]] virtual bool amPremium() = 0;
};

class GiftButton final : public Ui::AbstractButton {
public:
	GiftButton(QWidget *parent, not_null<GiftButtonDelegate*> delegate);
	~GiftButton();

	using Mode = GiftButtonMode;
	void setDescriptor(const GiftDescriptor &descriptor, Mode mode);
	void setGeometry(QRect inner, QMargins extend);

	void toggleSelected(
		bool selected,
		GiftSelectionMode selectionMode = GiftSelectionMode::Border,
		anim::type animated = anim::type::normal);

	[[nodiscard]] rpl::producer<QPoint> contextMenuRequests() const {
		return _contextMenuRequests.events();
	}

	[[nodiscard]] rpl::producer<QMouseEvent*> mouseEvents() const {
		return _mouseEvents.events();
	}

private:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

	void paintBackground(QPainter &p, const QImage &background);
	void cacheUniqueBackground(
		not_null<Data::UniqueGift*> unique,
		int width,
		int height);

	void refreshLocked();
	void setDocument(not_null<DocumentData*> document);
	[[nodiscard]] QMargins currentExtend() const;
	[[nodiscard]] bool small() const;

	void onStateChanged(State was, StateChangeSource source) override;
	void unsubscribe();

	const not_null<GiftButtonDelegate*> _delegate;
	rpl::event_stream<QPoint> _contextMenuRequests;
	rpl::event_stream<QMouseEvent*> _mouseEvents;
	QImage _hiddenBgCache;
	GiftDescriptor _descriptor;
	Ui::Text::String _text;
	Ui::Text::String _price;
	Ui::Text::String _byStars;
	std::shared_ptr<Ui::DynamicImage> _userpic;
	QImage _uniqueBackgroundCache;
	QImage _tonIcon;
	std::unique_ptr<Ui::Text::CustomEmoji> _uniquePatternEmoji;
	base::flat_map<float64, QImage> _uniquePatternCache;
	std::optional<Ui::Premium::ColoredMiniStars> _stars;
	Ui::Animations::Simple _selectedAnimation;
	std::unique_ptr<Overview::Layout::Checkbox> _check;
	int _resalePrice = 0;
	GiftButtonMode _mode = GiftButtonMode::Full;
	GiftSelectionMode _selectionMode = GiftSelectionMode::Border;
	bool _subscribed : 1 = false;
	bool _patterned : 1 = false;
	bool _selected : 1 = false;
	bool _locked : 1 = false;

	base::Timer _lockedTimer;
	TimeId _lockedUntilDate = 0;

	QRect _button;
	QMargins _extend;

	DocumentData *_resolvedDocument = nullptr;

	std::unique_ptr<HistoryView::StickerPlayer> _player;
	DocumentData *_playerDocument = nullptr;
	rpl::lifetime _mediaLifetime;
	rpl::lifetime _documentLifetime;

};

class Delegate final : public GiftButtonDelegate {
public:
	Delegate(not_null<Main::Session*> session, GiftButtonMode mode);
	Delegate(Delegate &&other);
	~Delegate();

	TextWithEntities star() override;
	TextWithEntities monostar() override;
	TextWithEntities monoton() override;
	TextWithEntities ministar() override;
	Ui::Text::MarkedContext textContext() override;
	QSize buttonSize() override;
	QMargins buttonExtend() const override;
	auto buttonPatternEmoji(
		not_null<Data::UniqueGift*> unique,
		Fn<void()> repaint)
	-> std::unique_ptr<Ui::Text::CustomEmoji> override;
	QImage background() override;
	rpl::producer<not_null<DocumentData*>> sticker(
		const GiftDescriptor &descriptor) override;
	not_null<StickerPremiumMark*> hiddenMark() override;
	QImage cachedBadge(const GiftBadge &badge) override;
	bool amPremium() override;

private:
	const not_null<Main::Session*> _session;
	std::unique_ptr<StickerPremiumMark> _hiddenMark;
	base::flat_map<GiftBadge, QImage> _badges;
	QSize _single;
	QImage _bg;
	GiftButtonMode _mode = GiftButtonMode::Full;
	Ui::Text::CustomEmojiHelper	_emojiHelper;
	TextWithEntities _ministarEmoji;
	TextWithEntities _starEmoji;

};

[[nodiscard]] DocumentData *LookupGiftSticker(
	not_null<Main::Session*> session,
	const GiftDescriptor &descriptor);

[[nodiscard]] rpl::producer<not_null<DocumentData*>> GiftStickerValue(
	not_null<Main::Session*> session,
	const GiftDescriptor &descriptor);

[[nodiscard]] QImage ValidateRotatedBadge(
	const GiftBadge &badge,
	QMargins padding);

void SelectGiftToUnpin(
	std::shared_ptr<ChatHelpers::Show> show,
	const std::vector<Data::CreditsHistoryEntry> &pinned,
	Fn<void(Data::SavedStarGiftId)> chosen);

} // namespace Info::PeerGifts
