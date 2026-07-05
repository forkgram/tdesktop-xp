/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/statistics/info_statistics_recent_message.h"

#include "base/unixtime.h"
#include "core/ui_integration.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/data_story.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "history/view/history_view_item_preview.h"
#include "info/statistics/info_statistics_common.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/controls/userpic_button.h"
#include "ui/effects/outline_segments.h" // UnreadStoryOutlineGradient
#include "ui/effects/ripple_animation.h"
#include "ui/effects/spoiler_mess.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/rect.h"
#include "ui/text/format_values.h"
#include "ui/text/text_options.h"
#include "styles/style_boxes.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_statistics.h"

namespace Info::Statistics {
namespace {

[[nodiscard]] QImage PreparePreviewImage(
		QImage original,
		ImageRoundRadius radius,
		int size,
		bool spoiler) {
	if (original.width() * 10 < original.height()
		|| original.height() * 10 < original.width()) {
		return QImage();
	}
	const auto factor = style::DevicePixelRatio();
	size *= factor;
	const auto scaled = original.scaled(
		QSize(size, size),
		Qt::KeepAspectRatioByExpanding,
		Qt::FastTransformation);
	auto square = scaled.copy(
		(scaled.width() - size) / 2,
		(scaled.height() - size) / 2,
		size,
		size
	).convertToFormat(QImage::Format_ARGB32_Premultiplied);
	if (spoiler) {
		square = Images::BlurLargeImage(
			std::move(square),
			style::ConvertScale(3) * factor);
	}
	square = Images::Round(std::move(square), radius);
	square.setDevicePixelRatio(factor);
	return square;
}

} // namespace

MessagePreview::MessagePreview(
	not_null<Ui::RpWidget*> parent,
	not_null<HistoryItem*> item,
	QImage cachedPreview)
: Ui::RpWidget(parent)
, _messageId(item->fullId())
, _date(
	st::statisticsHeaderTitleTextStyle,
	Ui::FormatDateTime(ItemDateTime(item)))
, _preview(std::move(cachedPreview)) {
	_text.setMarkedText(
		st::defaultPeerListItem.nameStyle,
		// XP walk: designated init -> positional (C7555). ToPreviewOptions:
		// existing, hideSender, hideCaption, ignoreMessageText, generateImages,
		// ... (v4.12.0 inserted ignoreMessageText at index 3; generateImages
		// defaults to true so it must be set explicitly). Took theirs (item).
		item->toPreview({
			{}, // existing
			{}, // hideSender
			{}, // hideCaption
			{}, // ignoreMessageText
			false, // generateImages
		}).text,
		Ui::DialogTextOptions(),
		Core::MarkedTextContext{ // XP walk: designated init -> positional (C7555)
			&item->history()->session(), // session
			{}, // type
			[=] { update(); }, // customEmojiRepaint
		});
	if (item->media() && item->media()->hasSpoiler()) {
		_spoiler = std::make_unique<Ui::SpoilerAnimation>([=] { update(); });
	}
	if (_preview.isNull()) {
		if (const auto media = item->media()) {
			if (const auto photo = media->photo()) {
				_photoMedia = photo->createMediaView();
				_photoMedia->wanted(Data::PhotoSize::Large, item->fullId());
			} else if (const auto document = media->document()) {
				_documentMedia = document->createMediaView();
				_documentMedia->thumbnailWanted(item->fullId());
			}
			processPreview();
		}
		if ((!_documentMedia || _documentMedia->thumbnailSize().isNull())
			&& !_photoMedia) {
			const auto userpic = Ui::CreateChild<Ui::UserpicButton>(
				this,
				item->history()->peer,
				st::statisticsRecentPostUserpic);
			userpic->move(st::peerListBoxItem.photoPosition);
			userpic->setAttribute(Qt::WA_TransparentForMouseEvents);
		}
	}
}

MessagePreview::MessagePreview(
	not_null<Ui::RpWidget*> parent,
	not_null<Data::Story*> story,
	QImage cachedPreview)
: Ui::RpWidget(parent)
, _storyId(story->fullId())
, _date(
	st::statisticsHeaderTitleTextStyle,
	Ui::FormatDateTime(base::unixtime::parse(story->date())))
, _preview(std::move(cachedPreview)) {
	_text.setMarkedText(
		st::defaultPeerListItem.nameStyle,
		{ tr::lng_in_dlg_story(tr::now) },
		Ui::DialogTextOptions(),
		Core::MarkedTextContext{
			// XP walk: designated -> positional (C7555). Core::MarkedTextContext:
			// session, type, customEmojiRepaint, customEmojiLoopLimit.
			&story->peer()->session(), // session
			{}, // type
			[=] { update(); }, // customEmojiRepaint
		});
	if (_preview.isNull()) {
		if (const auto photo = story->photo()) {
			_photoMedia = photo->createMediaView();
			_photoMedia->wanted(Data::PhotoSize::Large, story->fullId());
		} else if (const auto document = story->document()) {
			_documentMedia = document->createMediaView();
			_documentMedia->thumbnailWanted(story->fullId());
		}
		processPreview();
	}
}

void MessagePreview::setInfo(int views, int shares, int reactions) {
	_views = Ui::Text::String(
		st::defaultPeerListItem.nameStyle,
		(views >= 0)
			? tr::lng_stats_recent_messages_views(
				tr::now,
				lt_count_decimal,
				views)
			: QString());
	_shares = Ui::Text::String(
		st::statisticsHeaderTitleTextStyle,
		(shares > 0) ? Lang::FormatCountDecimal(shares) : QString());
	_reactions = Ui::Text::String(
		st::statisticsHeaderTitleTextStyle,
		(reactions > 0) ? Lang::FormatCountDecimal(reactions) : QString());
	_viewsWidth = (_views.maxWidth());
	_sharesWidth = (_shares.maxWidth());
	_reactionsWidth = (_reactions.maxWidth());
}

void MessagePreview::processPreview() {
	const auto session = _photoMedia
		? &_photoMedia->owner()->session()
		: _documentMedia
		? &_documentMedia->owner()->session()
		: nullptr;
	if (!session) {
		return;
	}

	struct ThumbInfo final {
		bool loaded = false;
		Image *image = nullptr;
	};

	const auto computeThumbInfo = [=]() -> ThumbInfo {
		using Size = Data::PhotoSize;
		if (_documentMedia) {
			return { true, _documentMedia->thumbnail() };
		} else if (const auto large = _photoMedia->image(Size::Large)) {
			return { true, large };
		} else if (const auto thumb = _photoMedia->image(Size::Thumbnail)) {
			return { false, thumb };
		} else if (const auto small = _photoMedia->image(Size::Small)) {
			return { false, small };
		} else {
			return { false, _photoMedia->thumbnailInline() };
		}
	};

	rpl::single(rpl::empty) | rpl::then(
		session->downloaderTaskFinished()
	) | rpl::start_with_next([=] {
		const auto computed = computeThumbInfo();
		const auto guard = gsl::finally([&] { update(); });
		if (!computed.image) {
			if (_documentMedia && !_documentMedia->owner()->hasThumbnail()) {
				_preview = QImage();
				_lifetimeDownload.destroy();
			}
			return;
		} else if (computed.loaded) {
			_lifetimeDownload.destroy();
		}
		if (_storyId) {
			const auto line = st::dialogsStoriesFull.lineTwice;
			const auto rect = Rect(Size(st::peerListBoxItem.photoSize));
			const auto penWidth = line / 2.;
			const auto offset = 1.5 * penWidth * 2;
			const auto preview = PreparePreviewImage(
				computed.image->original(),
				ImageRoundRadius::Ellipse,
				st::peerListBoxItem.photoSize - offset * 2,
				!!_spoiler);
			auto image = QImage(
				rect.size() * style::DevicePixelRatio(),
				QImage::Format_ARGB32_Premultiplied);
			image.setDevicePixelRatio(style::DevicePixelRatio());
			image.fill(Qt::transparent);
			{
				auto p = QPainter(&image);
				p.drawImage(offset, offset, preview);
				auto hq = PainterHighQualityEnabler(p);
				auto gradient = Ui::UnreadStoryOutlineGradient();
				gradient.setStart(rect.topRight());
				gradient.setFinalStop(rect.bottomLeft());

				p.setPen(QPen(gradient, penWidth));
				p.setBrush(Qt::NoBrush);
				p.drawEllipse(rect - Margins(penWidth));
			}
			_preview = std::move(image);
		} else {
			_preview = PreparePreviewImage(
				computed.image->original(),
				ImageRoundRadius::Large,
				st::peerListBoxItem.photoSize,
				!!_spoiler);
		}
	}, _lifetimeDownload);
}

int MessagePreview::resizeGetHeight(int newWidth) {
	return st::peerListBoxItem.height;
}

void MessagePreview::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto padding = st::boxRowPadding.left() / 2;
	const auto rightSubTextWidth = 0
		+ (_sharesWidth
			? _sharesWidth
				+ st::statisticsRecentPostShareIcon.width()
				+ st::statisticsRecentPostIconSkip
			: 0)
		+ (_reactionsWidth
			? _reactionsWidth
				+ st::statisticsRecentPostReactionIcon.width()
				+ st::statisticsChartRulerCaptionSkip
				+ st::statisticsRecentPostIconSkip
			: 0);
	const auto rightWidth = std::max(_viewsWidth, rightSubTextWidth)
		+ padding;
	const auto left = (false && _preview.isNull())
		? st::peerListBoxItem.photoPosition.x()
		: st::peerListBoxItem.namePosition.x();
	if (left) {
		const auto rect = QRect(
			st::peerListBoxItem.photoPosition,
			Size(st::peerListBoxItem.photoSize));
		p.drawImage(rect.topLeft(), _preview);
		if (_spoiler) {
			const auto paused = On(PowerSaving::kChatSpoiler);
			FillSpoilerRect(
				p,
				rect,
				Images::CornersMaskRef(
					Images::CornersMask(st::roundRadiusLarge)),
				Ui::DefaultImageSpoiler().frame(
					_spoiler->index(crl::now(), paused)),
				_cornerCache);
		}
	}
	const auto topTextTop = st::peerListBoxItem.namePosition.y();
	const auto bottomTextTop = st::peerListBoxItem.statusPosition.y();

	p.setBrush(Qt::NoBrush);
	p.setPen(st::boxTextFg);
	_text.draw(p, { // XP walk: keep HEAD positional (C7555). elisionLines left default
		// 0 == theirs' .elisionLines=1 here: text_renderer uses elisionHeight/font->height
		// (=1) when elisionLines==0, so behaviour is identical.
		{ left, topTextTop }, // position
		width() - left, // outerWidth
		width() - rightWidth - left, // availableWidth
		{}, // geometry
		style::al_left, // align
		{}, // clip
		{}, // palette
		{}, // pre
		{}, // blockquote
		{}, // colors
		Ui::Text::DefaultSpoilerCache(), // spoiler
		crl::now(), // now
		{}, // paused
		{}, // pausedEmoji
		{}, // pausedSpoiler
		true, // fullWidthSelection -- XP walk: v5.4.2 swapped this before selection
		{}, // selection
		{}, // highlight -- XP walk: PaintContext highlight field(18) inserted (C++17 gap)
		st::statisticsDetailsPopupHeaderStyle.font->height, // elisionHeight
	});
	_views.draw(p, { // XP walk: designated init -> positional (C7555)
		{ width() - _viewsWidth, topTextTop }, // position
		_viewsWidth, // outerWidth
		_viewsWidth, // availableWidth
	});

	p.setPen(st::windowSubTextFg);
	_date.draw(p, { // XP walk: designated init -> positional (C7555)
		{ left, bottomTextTop }, // position
		width() - left, // outerWidth
		width() - rightWidth - left, // availableWidth
	});
	// XP walk: v4.12.0 added share/reaction rendering here. Took theirs; the
	// Ui::Text draw options are designated -> positional (C7555): position,
	// outerWidth, availableWidth.
	{
		auto right = width() - _sharesWidth;
		_shares.draw(p, {
			{ right, bottomTextTop }, // position
			_sharesWidth, // outerWidth
			_sharesWidth, // availableWidth
		});
		const auto bottomTextBottom = bottomTextTop
			+ st::statisticsHeaderTitleTextStyle.font->height
			- st::statisticsRecentPostIconSkip;
		if (_sharesWidth) {
			const auto &icon = st::statisticsRecentPostShareIcon;
			const auto iconTop = bottomTextBottom - icon.height();
			right -= st::statisticsRecentPostIconSkip + icon.width();
			icon.paint(p, { right, iconTop }, width());
		}
		right -= _reactionsWidth + st::statisticsChartRulerCaptionSkip;
		_reactions.draw(p, {
			{ right, bottomTextTop }, // position
			_reactionsWidth, // outerWidth
			_reactionsWidth, // availableWidth
		});
		if (_reactionsWidth) {
			const auto &icon = st::statisticsRecentPostReactionIcon;
			const auto iconTop = bottomTextBottom - icon.height();
			right -= st::statisticsRecentPostIconSkip + icon.width();
			icon.paint(p, { right, iconTop }, width());
		}
	}
}

void MessagePreview::saveState(SavedState &state) const {
	if (!_lifetimeDownload) {
		const auto fullId = Data::RecentPostId{ _messageId, _storyId };
		state.recentPostPreviews[fullId] = _preview;
	}
}

} // namespace Info::Statistics
