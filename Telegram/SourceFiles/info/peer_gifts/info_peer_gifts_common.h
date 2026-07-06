/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/qt/qt_compare.h"
#include "data/data_star_gift.h"
#include "ui/abstract_button.h"
#include "ui/effects/premium_stars_colored.h"
#include "ui/text/text.h"

class StickerPremiumMark;

namespace Data {
struct UniqueGift;
} // namespace Data

namespace HistoryView {
class StickerPlayer;
} // namespace HistoryView

namespace Main {
class Session;
} // namespace Main

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
	Data::StarGift info;
	PeerData *from = nullptr;
	TimeId date = 0;
	bool userpic = false;
	bool pinned = false;
	bool hidden = false;
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

enum class GiftButtonMode {
	Full,
	Minimal,
};

class GiftButtonDelegate {
public:
	[[nodiscard]] virtual TextWithEntities star() = 0;
	[[nodiscard]] virtual TextWithEntities ministar() = 0;
	[[nodiscard]] virtual Ui::Text::MarkedContext textContext() = 0;
	[[nodiscard]] virtual QSize buttonSize() = 0;
	[[nodiscard]] virtual QMargins buttonExtend() = 0;
	[[nodiscard]] virtual auto buttonPatternEmoji(
		not_null<Data::UniqueGift*> unique,
		Fn<void()> repaint)
	-> std::unique_ptr<Ui::Text::CustomEmoji> = 0;
	[[nodiscard]] virtual QImage background() = 0;
	[[nodiscard]] virtual rpl::producer<not_null<DocumentData*>> sticker(
		const GiftDescriptor &descriptor) = 0;
	[[nodiscard]] virtual not_null<StickerPremiumMark*> hiddenMark() = 0;
	[[nodiscard]] virtual QImage cachedBadge(const GiftBadge &badge) = 0;
};

class GiftButton final : public Ui::AbstractButton {
public:
	GiftButton(QWidget *parent, not_null<GiftButtonDelegate*> delegate);
	~GiftButton();

	using Mode = GiftButtonMode;
	void setDescriptor(const GiftDescriptor &descriptor, Mode mode);
	void setGeometry(QRect inner, QMargins extend);

	[[nodiscard]] rpl::producer<QPoint> contextMenuRequests() const {
		return _contextMenuRequests.events();
	}

private:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void contextMenuEvent(QContextMenuEvent *e) override;

	void cacheUniqueBackground(
		not_null<Data::UniqueGift*> unique,
		int width,
		int height);

	void setDocument(not_null<DocumentData*> document);
	[[nodiscard]] bool documentResolved() const;

	void unsubscribe();

	const not_null<GiftButtonDelegate*> _delegate;
	rpl::event_stream<QPoint> _contextMenuRequests;
	QImage _hiddenBgCache;
	GiftDescriptor _descriptor;
	Ui::Text::String _text;
	Ui::Text::String _price;
	Ui::Text::String _byStars;
	std::shared_ptr<Ui::DynamicImage> _userpic;
	QImage _uniqueBackgroundCache;
	std::unique_ptr<Ui::Text::CustomEmoji> _uniquePatternEmoji;
	base::flat_map<float64, QImage> _uniquePatternCache;
	std::optional<Ui::Premium::ColoredMiniStars> _stars;
	bool _subscribed = false;
	bool _patterned = false;
	bool _small = false;

	QRect _button;
	QMargins _extend;

	std::unique_ptr<HistoryView::StickerPlayer> _player;
	rpl::lifetime _mediaLifetime;

};

class Delegate final : public GiftButtonDelegate {
public:
	Delegate(
		not_null<Window::SessionController*> window,
		GiftButtonMode mode);
	Delegate(Delegate &&other);
	~Delegate();

	TextWithEntities star() override;
	TextWithEntities ministar() override;
	Ui::Text::MarkedContext textContext() override;
	QSize buttonSize() override;
	QMargins buttonExtend() override;
	auto buttonPatternEmoji(
		not_null<Data::UniqueGift*> unique,
		Fn<void()> repaint)
	-> std::unique_ptr<Ui::Text::CustomEmoji> override;
	QImage background() override;
	rpl::producer<not_null<DocumentData*>> sticker(
		const GiftDescriptor &descriptor) override;
	not_null<StickerPremiumMark*> hiddenMark() override;
	QImage cachedBadge(const GiftBadge &badge) override;

private:
	const not_null<Window::SessionController*> _window;
	std::unique_ptr<StickerPremiumMark> _hiddenMark;
	base::flat_map<GiftBadge, QImage> _badges;
	QSize _single;
	QImage _bg;
	GiftButtonMode _mode = GiftButtonMode::Full;

};

[[nodiscard]] DocumentData *LookupGiftSticker(
	not_null<Main::Session*> session,
	const GiftDescriptor &descriptor);

[[nodiscard]] rpl::producer<not_null<DocumentData*>> GiftStickerValue(
	not_null<Main::Session*> session,
	const GiftDescriptor &descriptor);

[[nodiscard]] QImage ValidateRotatedBadge(const GiftBadge &badge, int added);

} // namespace Info::PeerGifts
