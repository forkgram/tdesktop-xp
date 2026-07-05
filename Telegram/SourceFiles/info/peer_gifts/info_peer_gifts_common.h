/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/abstract_button.h"
#include "ui/text/text.h"

class StickerPremiumMark;

namespace HistoryView {
class StickerPlayer;
} // namespace HistoryView

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class DynamicImage;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Info::PeerGifts {

struct GiftTypePremium {
	int64 cost = 0;
	QString currency;
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
	uint64 id = 0;
	int64 stars = 0;
	int64 convertStars = 0;
	DocumentData *document = nullptr;
	PeerData *from = nullptr;
	int limitedCount = 0;
	bool userpic = false;
	bool hidden = false;
	bool mine = false;

	// XP walk: defaulted operator== (C7589, C++20) -> manual.
	[[nodiscard]] friend inline bool operator==(
			const GiftTypeStars &a,
			const GiftTypeStars &b) {
		return (a.id == b.id)
			&& (a.stars == b.stars)
			&& (a.convertStars == b.convertStars)
			&& (a.document == b.document)
			&& (a.from == b.from)
			&& (a.limitedCount == b.limitedCount)
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

class GiftButtonDelegate {
public:
	[[nodiscard]] virtual TextWithEntities star() = 0;
	[[nodiscard]] virtual std::any textContext() = 0;
	[[nodiscard]] virtual QSize buttonSize() = 0;
	[[nodiscard]] virtual QMargins buttonExtend() = 0;
	[[nodiscard]] virtual QImage background() = 0;
	[[nodiscard]] virtual DocumentData *lookupSticker(
		const GiftDescriptor &descriptor) = 0;
	[[nodiscard]] virtual not_null<StickerPremiumMark*> hiddenMark() = 0;
};

class GiftButton final : public Ui::AbstractButton {
public:
	GiftButton(QWidget *parent, not_null<GiftButtonDelegate*> delegate);
	~GiftButton();

	void setDescriptor(const GiftDescriptor &descriptor);
	void setGeometry(QRect inner, QMargins extend);

private:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void setDocument(not_null<DocumentData*> document);
	[[nodiscard]] bool documentResolved() const;

	void unsubscribe();

	const not_null<GiftButtonDelegate*> _delegate;
	QImage _hiddenBgCache;
	GiftDescriptor _descriptor;
	Ui::Text::String _text;
	Ui::Text::String _price;
	std::shared_ptr<Ui::DynamicImage> _userpic;
	bool _subscribed = false;

	QRect _button;
	QMargins _extend;

	std::unique_ptr<HistoryView::StickerPlayer> _player;
	rpl::lifetime _mediaLifetime;

};

class Delegate final : public GiftButtonDelegate {
public:
	explicit Delegate(not_null<Window::SessionController*> window);
	Delegate(Delegate &&other);
	~Delegate();

	TextWithEntities star() override;
	std::any textContext() override;
	QSize buttonSize() override;
	QMargins buttonExtend() override;
	QImage background() override;
	DocumentData *lookupSticker(const GiftDescriptor &descriptor) override;
	not_null<StickerPremiumMark*> hiddenMark() override;

private:
	const not_null<Window::SessionController*> _window;
	std::unique_ptr<StickerPremiumMark> _hiddenMark;
	QSize _single;
	QImage _bg;

};

[[nodiscard]] DocumentData *LookupGiftSticker(
	not_null<Main::Session*> session,
	const GiftDescriptor &descriptor);

} // namespace Info::PeerGifts
