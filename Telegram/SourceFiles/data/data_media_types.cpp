/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_media_types.h"

#include "history/history.h"
#include "history/history_item.h" // CreateMedia.
#include "history/history_location_manager.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_item_preview.h"
#include "history/view/media/history_view_extended_preview.h"
#include "history/view/media/history_view_photo.h"
#include "history/view/media/history_view_sticker.h"
#include "history/view/media/history_view_gif.h"
#include "history/view/media/history_view_document.h"
#include "history/view/media/history_view_contact.h"
#include "history/view/media/history_view_location.h"
#include "history/view/media/history_view_game.h"
#include "history/view/media/history_view_giveaway.h"
#include "history/view/media/history_view_invoice.h"
#include "history/view/media/history_view_call.h"
#include "history/view/media/history_view_web_page.h"
#include "history/view/media/history_view_poll.h"
#include "history/view/media/history_view_theme_document.h"
#include "history/view/media/history_view_slot_machine.h"
#include "history/view/media/history_view_dice.h"
#include "history/view/media/history_view_service_box.h"
#include "history/view/media/history_view_story_mention.h"
#include "history/view/media/history_view_premium_gift.h"
#include "history/view/media/history_view_userpic_suggestion.h"
#include "dialogs/ui/dialogs_message_view.h"
#include "ui/image/image.h"
#include "ui/effects/spoiler_mess.h"
#include "ui/text/format_song_document_name.h"
#include "ui/text/format_values.h"
#include "ui/text/text_entity.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/emoji_config.h"
#include "api/api_sending.h"
#include "api/api_transcribes.h"
#include "storage/storage_shared_media.h"
#include "storage/localstorage.h"
#include "chat_helpers/stickers_dice_pack.h" // Stickers::DicePacks::IsSlot.
#include "chat_helpers/stickers_gift_box_pack.h"
#include "data/data_session.h"
#include "data/data_auto_download.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_game.h"
#include "data/data_web_page.h"
#include "data/data_poll.h"
#include "data/data_channel.h"
#include "data/data_file_origin.h"
#include "data/data_stories.h"
#include "data/data_story.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "core/application.h"
#include "core/click_handler_types.h" // ClickHandlerContext
#include "lang/lang_keys.h"
#include "storage/file_upload.h"
#include "window/window_session_controller.h" // SessionController::uiShow.
#include "apiwrap.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"

namespace Data {
namespace {

constexpr auto kFastRevokeRestriction = 24 * 60 * TimeId(60);
constexpr auto kMaxPreviewImages = 3;
constexpr auto kLoadingStoryPhotoId = PhotoId(0x7FFF'DEAD'FFFF'FFFFULL);

using ItemPreview = HistoryView::ItemPreview;
using ItemPreviewImage = HistoryView::ItemPreviewImage;

[[nodiscard]] TextWithEntities WithCaptionNotificationText(
		const QString &attachType,
		const TextWithEntities &caption,
		bool hasMiniImages = false) {
	if (caption.text.isEmpty()) {
		return Ui::Text::Colorized(attachType);
	}

	return hasMiniImages
		? caption
		: tr::lng_dialogs_text_media(
			tr::now,
			lt_media_part,
			tr::lng_dialogs_text_media_wrapped(
				tr::now,
				lt_media,
				Ui::Text::Colorized(attachType),
				Ui::Text::WithEntities),
			lt_caption,
			caption,
			Ui::Text::WithEntities);
}

[[nodiscard]] QImage PreparePreviewImage(
		not_null<const Image*> image,
		ImageRoundRadius radius,
		bool spoiler) {
	const auto original = image->original();
	if (original.width() * 10 < original.height()
		|| original.height() * 10 < original.width()) {
		return QImage();
	}
	const auto factor = style::DevicePixelRatio();
	const auto size = st::dialogsMiniPreview * factor;
	const auto scaled = original.scaled(
		QSize(size, size),
		Qt::KeepAspectRatioByExpanding,
		Qt::SmoothTransformation);
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
	if (radius == ImageRoundRadius::Small) {
		struct Cache {
			base::flat_map<int, std::array<QImage, 4>> all;
			std::array<QImage, 4> *lastUsed = nullptr;
			int lastUsedRadius = 0;
		};
		static auto cache = Cache();
		const auto pxRadius = st::dialogsMiniPreviewRadius;
		if (!cache.lastUsed || cache.lastUsedRadius != pxRadius) {
			cache.lastUsedRadius = pxRadius;
			const auto i = cache.all.find(pxRadius);
			if (i != end(cache.all)) {
				cache.lastUsed = &i->second;
			} else {
				cache.lastUsed = &cache.all.emplace(
					pxRadius,
					Images::CornersMask(pxRadius)).first->second;
			}
		}
		square = Images::Round(std::move(square), *cache.lastUsed);
	} else {
		square = Images::Round(std::move(square), radius);
	}
	square.setDevicePixelRatio(factor);
	return square;
}

template <typename MediaType>
[[nodiscard]] uint64 CountCacheKey(not_null<MediaType*> data, bool spoiler) {
	return (reinterpret_cast<uint64>(data.get()) & ~1) | (spoiler ? 1 : 0);
}

[[nodiscard]] ItemPreviewImage PreparePhotoPreview(
		not_null<const HistoryItem*> item,
		const std::shared_ptr<PhotoMedia> &media,
		ImageRoundRadius radius,
		bool spoiler) {
	const auto photo = media->owner();
	const auto counted = CountCacheKey(photo, spoiler);
	if (const auto small = media->image(PhotoSize::Small)) {
		return { PreparePreviewImage(small, radius, spoiler), counted };
	} else if (const auto thumbnail = media->image(PhotoSize::Thumbnail)) {
		return { PreparePreviewImage(thumbnail, radius, spoiler), counted };
	} else if (const auto large = media->image(PhotoSize::Large)) {
		return { PreparePreviewImage(large, radius, spoiler), counted };
	}
	const auto allowedToDownload = media->autoLoadThumbnailAllowed(
		item->history()->peer);
	const auto cacheKey = allowedToDownload ? 0 : counted;
	if (allowedToDownload) {
		media->owner()->load(PhotoSize::Small, item->fullId());
	}
	if (const auto blurred = media->thumbnailInline()) {
		return { PreparePreviewImage(blurred, radius, spoiler), cacheKey };
	}
	return { QImage(), allowedToDownload ? 0 : cacheKey };
}

[[nodiscard]] ItemPreviewImage PrepareFilePreviewImage(
		not_null<const HistoryItem*> item,
		const std::shared_ptr<DocumentMedia> &media,
		ImageRoundRadius radius,
		bool spoiler) {
	Expects(media->owner()->hasThumbnail());

	const auto document = media->owner();
	const auto readyCacheKey = CountCacheKey(document, spoiler);
	if (const auto thumbnail = media->thumbnail()) {
		return {
			PreparePreviewImage(thumbnail, radius, spoiler),
			readyCacheKey,
		};
	}
	document->loadThumbnail(item->fullId());
	if (const auto blurred = media->thumbnailInline()) {
		return { PreparePreviewImage(blurred, radius, spoiler), 0 };
	}
	return { QImage(), 0 };
}

[[nodiscard]] QImage PutPlayIcon(QImage preview) {
	Expects(!preview.isNull());

	{
		QPainter p(&preview);
		st::dialogsMiniPlay.paintInCenter(
			p,
			QRect(QPoint(), preview.size() / preview.devicePixelRatio()));
	}
	return preview;
}

[[nodiscard]] ItemPreviewImage PrepareFilePreview(
		not_null<const HistoryItem*> item,
		const std::shared_ptr<DocumentMedia> &media,
		ImageRoundRadius radius,
		bool spoiler) {
	auto result = PrepareFilePreviewImage(item, media, radius, spoiler);
	const auto document = media->owner();
	if (!result.data.isNull()
		&& (document->isVideoFile() || document->isVideoMessage())) {
		result.data = PutPlayIcon(std::move(result.data));
	}
	return result;
}

[[nodiscard]] bool TryFilePreview(not_null<DocumentData*> document) {
	return document->hasThumbnail()
		&& !document->sticker()
		&& !document->isAudioFile();
}

template <typename MediaType>
[[nodiscard]] ItemPreviewImage FindCachedPreview(
		const std::vector<ItemPreviewImage> *existing,
		not_null<MediaType*> data,
		bool spoiler) {
	if (!existing) {
		return {};
	}
	const auto i = ranges::find(
		*existing,
		CountCacheKey(data, spoiler),
		&ItemPreviewImage::cacheKey);
	return (i != end(*existing)) ? *i : ItemPreviewImage();
}

bool UpdateExtendedMedia(
		Invoice &invoice,
		not_null<HistoryItem*> item,
		const MTPMessageExtendedMedia &media) {
	return media.match([&](const MTPDmessageExtendedMediaPreview &data) {
		if (invoice.extendedMedia) {
			return false;
		}
		auto changed = false;
		auto &preview = invoice.extendedPreview;
		if (const auto &w = data.vw()) {
			const auto &h = data.vh();
			Assert(h != nullptr);
			const auto dimensions = QSize(w->v, h->v);
			if (preview.dimensions != dimensions) {
				preview.dimensions = dimensions;
				changed = true;
			}
		}
		if (const auto &thumb = data.vthumb()) {
			if (thumb->type() == mtpc_photoStrippedSize) {
				const auto bytes = thumb->c_photoStrippedSize().vbytes().v;
				if (preview.inlineThumbnailBytes != bytes) {
					preview.inlineThumbnailBytes = bytes;
					changed = true;
				}
			}
		}
		if (const auto &duration = data.vvideo_duration()) {
			if (preview.videoDuration != duration->v) {
				preview.videoDuration = duration->v;
				changed = true;
			}
		}
		return changed;
	}, [&](const MTPDmessageExtendedMedia &data) {
		invoice.extendedMedia = HistoryItem::CreateMedia(
			item,
			data.vmedia());
		return true;
	});
}

TextForMimeData WithCaptionClipboardText(
		const QString &attachType,
		TextForMimeData &&caption) {
	auto result = TextForMimeData();
	if (attachType.isEmpty()) {
		result.reserve(1 + caption.expanded.size());
		if (!caption.empty()) {
			result.append(std::move(caption));
		}
	} else {
		result.reserve(5 + attachType.size() + caption.expanded.size());
		result.append(u"[ "_q).append(attachType).append(u" ]"_q);
		if (!caption.empty()) {
			result.append('\n').append(std::move(caption));
		}
	}
	return result;
}

} // namespace

Invoice ComputeInvoiceData(
		not_null<HistoryItem*> item,
		const MTPDmessageMediaInvoice &data) {
	auto description = qs(data.vdescription());
	auto result = Invoice{
		data.vreceipt_msg_id().value_or_empty(),
		data.vtotal_amount().v,
		qs(data.vcurrency()),
		TextUtilities::SingleLine(qs(data.vtitle())),
		TextUtilities::ParseEntities(
			description,
			TextParseLinks | TextParseMultiline),
		{}, // extendedPreview
		{}, // extendedMedia
		(data.vphoto()
			? item->history()->owner().photoFromWeb(
				*data.vphoto(),
				ImageLocation())
			: nullptr),
		data.is_test(),
	};
	if (const auto &media = data.vextended_media()) {
		UpdateExtendedMedia(result, item, *media);
	}
	return result;
}

Call ComputeCallData(const MTPDmessageActionPhoneCall &call) {
	auto result = Call();
	result.finishReason = [&] {
		if (const auto reason = call.vreason()) {
			switch (reason->type()) {
			case mtpc_phoneCallDiscardReasonBusy:
				return CallFinishReason::Busy;
			case mtpc_phoneCallDiscardReasonDisconnect:
				return CallFinishReason::Disconnected;
			case mtpc_phoneCallDiscardReasonHangup:
				return CallFinishReason::Hangup;
			case mtpc_phoneCallDiscardReasonMissed:
				return CallFinishReason::Missed;
			}
			Unexpected("Call reason type.");
		}
		return CallFinishReason::Hangup;
	}();
	result.duration = call.vduration().value_or_empty();
	result.video = call.is_video();
	return result;
}

GiveawayStart ComputeGiveawayStartData(
		not_null<HistoryItem*> item,
		const MTPDmessageMediaGiveaway &data) {
	auto result = GiveawayStart{
		// XP walk: designated -> positional (C7555). v4.13.0 renamed Giveaway ->
		// GiveawayStart and inserted additionalPrize at field 2. Declaration order:
		// channels, countries, additionalPrize, untilDate, quantity, months, all.
		{}, // channels
		{}, // countries
		{}, // additionalPrize
		data.vuntil_date().v, // untilDate
		data.vquantity().v, // quantity
		data.vmonths().v, // months
		!data.is_only_new_subscribers(), // all
	};
	result.channels.reserve(data.vchannels().v.size());
	const auto owner = &item->history()->owner();
	for (const auto &id : data.vchannels().v) {
		result.channels.push_back(owner->channel(ChannelId(id)));
	}
	if (const auto countries = data.vcountries_iso2()) {
		result.countries.reserve(countries->v.size());
		for (const auto &country : countries->v) {
			result.countries.push_back(qs(country));
		}
	}
	if (const auto additional = data.vprize_description()) {
		result.additionalPrize = qs(*additional);
	}
	return result;
}

GiveawayResults ComputeGiveawayResultsData(
		not_null<HistoryItem*> item,
		const MTPDmessageMediaGiveawayResults &data) {
	const auto additional = data.vadditional_peers_count();
	auto result = GiveawayResults{
		// XP walk: designated -> positional (C7555). GiveawayResults order: channel,
		// winners, additionalPrize, untilDate, launchId, additionalPeersCount,
		// winnersCount, unclaimedCount, months, refunded, all (v4.13.0 new struct).
		item->history()->owner().channel(data.vchannel_id()), // channel
		{}, // winners
		{}, // additionalPrize
		data.vuntil_date().v, // untilDate
		data.vlaunch_msg_id().v, // launchId
		additional.value_or_empty(), // additionalPeersCount
		data.vwinners_count().v, // winnersCount
		data.vunclaimed_count().v, // unclaimedCount
		data.vmonths().v, // months
		data.is_refunded(), // refunded
		!data.is_only_new_subscribers(), // all
	};
	result.winners.reserve(data.vwinners().v.size());
	const auto owner = &item->history()->owner();
	for (const auto &id : data.vwinners().v) {
		result.winners.push_back(owner->user(UserId(id)));
	}
	if (const auto additional = data.vprize_description()) {
		result.additionalPrize = qs(*additional);
	}
	return result;
}

Media::Media(not_null<HistoryItem*> parent) : _parent(parent) {
}

not_null<HistoryItem*> Media::parent() const {
	return _parent;
}

DocumentData *Media::document() const {
	return nullptr;
}

PhotoData *Media::photo() const {
	return nullptr;
}

WebPageData *Media::webpage() const {
	return nullptr;
}

MediaWebPageFlags Media::webpageFlags() const {
	return {};
}

const SharedContact *Media::sharedContact() const {
	return nullptr;
}

const Call *Media::call() const {
	return nullptr;
}

GameData *Media::game() const {
	return nullptr;
}

const Invoice *Media::invoice() const {
	return nullptr;
}

CloudImage *Media::location() const {
	return nullptr;
}

PollData *Media::poll() const {
	return nullptr;
}

const WallPaper *Media::paper() const {
	return nullptr;
}

bool Media::paperForBoth() const {
	return false;
}

FullStoryId Media::storyId() const {
	return {};
}

bool Media::storyExpired(bool revalidate) {
	return false;
}

bool Media::storyMention() const {
	return false;
}

const GiveawayStart *Media::giveawayStart() const {
	return nullptr;
}

const GiveawayResults *Media::giveawayResults() const {
	return nullptr;
}

bool Media::uploading() const {
	return false;
}

Storage::SharedMediaTypesMask Media::sharedMediaTypes() const {
	return {};
}

bool Media::canBeGrouped() const {
	return false;
}

ItemPreview Media::toPreview(ToPreviewOptions options) const {
	return { notificationText() };
}

bool Media::hasReplyPreview() const {
	return false;
}

Image *Media::replyPreview() const {
	return nullptr;
}

bool Media::replyPreviewLoaded() const {
	return true;
}

bool Media::allowsForward() const {
	return true;
}

bool Media::allowsEdit() const {
	return allowsEditCaption();
}

bool Media::allowsEditCaption() const {
	return false;
}

bool Media::allowsEditMedia() const {
	return false;
}

bool Media::allowsRevoke(TimeId now) const {
	return true;
}

bool Media::forwardedBecomesUnread() const {
	return false;
}

bool Media::dropForwardedInfo() const {
	return false;
}

bool Media::forceForwardedInfo() const {
	return false;
}

bool Media::hasSpoiler() const {
	return false;
}

crl::time Media::ttlSeconds() const {
	return 0;
}

bool Media::consumeMessageText(const TextWithEntities &text) {
	return false;
}

TextWithEntities Media::consumedMessageText() const {
	return {};
}

std::unique_ptr<HistoryView::Media> Media::createView(
		not_null<HistoryView::Element*> message,
		HistoryView::Element *replacing) {
	return createView(message, message->data(), replacing);
}

ItemPreview Media::toGroupPreview(
		const HistoryItemsList &items,
		ToPreviewOptions options) const {
	auto result = ItemPreview();
	auto loadingContext = std::vector<std::any>();
	auto photoCount = 0;
	auto videoCount = 0;
	auto audioCount = 0;
	auto fileCount = 0;
	auto manyCaptions = false;
	for (const auto &item : items) {
		if (const auto media = item->media()) {
			if (media->photo()) {
				photoCount++;
			} else if (const auto document = media->document()) {
				(document->isVideoFile()
					? videoCount
					: document->isAudioFile()
					? audioCount
					: fileCount)++;
			}
			auto copy = options;
			copy.ignoreGroup = true;
			const auto already = int(result.images.size());
			const auto left = kMaxPreviewImages - already;
			auto single = left ? media->toPreview(copy) : ItemPreview();
			if (!single.images.empty()) {
				while (single.images.size() > left) {
					single.images.pop_back();
				}
				result.images.insert(
					end(result.images),
					std::make_move_iterator(begin(single.images)),
					std::make_move_iterator(end(single.images)));
			}
			if (single.loadingContext.has_value()) {
				loadingContext.push_back(std::move(single.loadingContext));
			}
			const auto original = item->originalText();
			if (!original.text.isEmpty()) {
				if (result.text.text.isEmpty()) {
					result.text = original;
				} else {
					manyCaptions = true;
				}
			}
		}
	}
	if (manyCaptions || result.text.text.isEmpty()) {
		const auto mediaCount = photoCount + videoCount;
		auto genericText = (photoCount && videoCount)
			? tr::lng_in_dlg_media_count(tr::now, lt_count, mediaCount)
			: photoCount
			? tr::lng_in_dlg_photo_count(tr::now, lt_count, photoCount)
			: videoCount
			? tr::lng_in_dlg_video_count(tr::now, lt_count, videoCount)
			: audioCount
			? tr::lng_in_dlg_audio_count(tr::now, lt_count, audioCount)
			: fileCount
			? tr::lng_in_dlg_file_count(tr::now, lt_count, fileCount)
			: tr::lng_in_dlg_album(tr::now);
		result.text = Ui::Text::Colorized(genericText);
	}
	if (!loadingContext.empty()) {
		result.loadingContext = std::move(loadingContext);
	}
	return result;
}

MediaPhoto::MediaPhoto(
	not_null<HistoryItem*> parent,
	not_null<PhotoData*> photo,
	bool spoiler)
: Media(parent)
, _photo(photo)
, _spoiler(spoiler) {
	parent->history()->owner().registerPhotoItem(_photo, parent);

	if (_spoiler) {
		Ui::PreloadImageSpoiler();
	}
}

MediaPhoto::MediaPhoto(
	not_null<HistoryItem*> parent,
	not_null<PeerData*> chat,
	not_null<PhotoData*> photo)
: Media(parent)
, _photo(photo)
, _chat(chat) {
	parent->history()->owner().registerPhotoItem(_photo, parent);
}

MediaPhoto::~MediaPhoto() {
	if (uploading() && !Core::Quitting()) {
		parent()->history()->session().uploader().cancel(parent()->fullId());
	}
	parent()->history()->owner().unregisterPhotoItem(_photo, parent());
}

std::unique_ptr<Media> MediaPhoto::clone(not_null<HistoryItem*> parent) {
	return _chat
		? std::make_unique<MediaPhoto>(parent, _chat, _photo)
		: std::make_unique<MediaPhoto>(parent, _photo, _spoiler);
}

PhotoData *MediaPhoto::photo() const {
	return _photo;
}

bool MediaPhoto::uploading() const {
	return _photo->uploading();
}

Storage::SharedMediaTypesMask MediaPhoto::sharedMediaTypes() const {
	using Type = Storage::SharedMediaType;
	if (_chat) {
		return Type::ChatPhoto;
	}
	return Storage::SharedMediaTypesMask{}
		.added(Type::Photo)
		.added(Type::PhotoVideo);
}

bool MediaPhoto::canBeGrouped() const {
	return true;
}

bool MediaPhoto::hasReplyPreview() const {
	return !_photo->isNull();
}

Image *MediaPhoto::replyPreview() const {
	return _photo->getReplyPreview(parent());
}

bool MediaPhoto::replyPreviewLoaded() const {
	return _photo->replyPreviewLoaded(_spoiler);
}

TextWithEntities MediaPhoto::notificationText() const {
	return WithCaptionNotificationText(
		tr::lng_in_dlg_photo(tr::now),
		parent()->originalText());
}

ItemPreview MediaPhoto::toPreview(ToPreviewOptions options) const {
	const auto item = parent();
	if (!options.ignoreGroup && item->groupId()) {
		if (const auto group = item->history()->owner().groups().find(item)
			; group && group->items.size() > 1) {
			return toGroupPreview(group->items, options);
		}
	}
	auto images = std::vector<ItemPreviewImage>();
	auto context = std::any();
	if (auto found = FindCachedPreview(options.existing, _photo, _spoiler)) {
		images.push_back(std::move(found));
	} else {
		const auto media = _photo->createMediaView();
		const auto radius = _chat
			? ImageRoundRadius::Ellipse
			: ImageRoundRadius::Small;
		if (auto prepared = PreparePhotoPreview(
				parent(),
				media,
				radius,
				_spoiler)
			; prepared || !prepared.cacheKey) {
			images.push_back(std::move(prepared));
			if (!prepared.cacheKey) {
				context = media;
			}
		}
	}
	const auto type = tr::lng_in_dlg_photo(tr::now);
	const auto caption = (options.hideCaption || options.ignoreMessageText)
		? TextWithEntities()
		: options.translated
		? parent()->translatedText()
		: parent()->originalText();
	const auto hasMiniImages = !images.empty();
	return {
		WithCaptionNotificationText(type, caption, hasMiniImages),
		std::move(images),
		-1, // arrowInTextPosition (in-class default is -1, not {})
		{}, // imagesInTextPosition
		std::move(context),
	};
}

QString MediaPhoto::pinnedTextSubstring() const {
	return tr::lng_action_pinned_media_photo(tr::now);
}

TextForMimeData MediaPhoto::clipboardText() const {
	return TextForMimeData();
}

bool MediaPhoto::allowsEditCaption() const {
	return true;
}

bool MediaPhoto::allowsEditMedia() const {
	return true;
}

bool MediaPhoto::hasSpoiler() const {
	return _spoiler;
}

bool MediaPhoto::updateInlineResultMedia(const MTPMessageMedia &media) {
	if (media.type() != mtpc_messageMediaPhoto) {
		return false;
	}
	const auto &data = media.c_messageMediaPhoto();
	const auto content = data.vphoto();
	if (content && !data.vttl_seconds()) {
		const auto photo = parent()->history()->owner().processPhoto(
			*content);
		if (photo == _photo) {
			return true;
		} else {
			photo->collectLocalData(_photo);
		}
	} else {
		LOG(("API Error: "
			"Got MTPMessageMediaPhoto without photo "
			"or with ttl_seconds in updateInlineResultMedia()"));
	}
	return false;
}

bool MediaPhoto::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() != mtpc_messageMediaPhoto) {
		return false;
	}
	const auto &mediaPhoto = media.c_messageMediaPhoto();
	const auto content = mediaPhoto.vphoto();
	if (!content || mediaPhoto.vttl_seconds()) {
		LOG(("Api Error: "
			"Got MTPMessageMediaPhoto without photo "
			"or with ttl_seconds in updateSentMedia()"));
		return false;
	}
	parent()->history()->owner().photoConvert(_photo, *content);
	return true;
}

std::unique_ptr<HistoryView::Media> MediaPhoto::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing) {
	if (_chat) {
		if (realParent->isUserpicSuggestion()) {
			return std::make_unique<HistoryView::ServiceBox>(
				message,
				std::make_unique<HistoryView::UserpicSuggestion>(
					message,
					_chat,
					_photo,
					st::msgServicePhotoWidth));
		}
		return std::make_unique<HistoryView::Photo>(
			message,
			_chat,
			_photo,
			st::msgServicePhotoWidth);
	}
	return std::make_unique<HistoryView::Photo>(
		message,
		realParent,
		_photo,
		_spoiler);
}

MediaFile::MediaFile(
	not_null<HistoryItem*> parent,
	not_null<DocumentData*> document,
	bool skipPremiumEffect,
	bool spoiler,
	crl::time ttlSeconds)
: Media(parent)
, _document(document)
, _emoji(document->sticker() ? document->sticker()->alt : QString())
, _skipPremiumEffect(skipPremiumEffect)
, _spoiler(spoiler)
, _ttlSeconds(ttlSeconds) {
	parent->history()->owner().registerDocumentItem(_document, parent);

	if (!_emoji.isEmpty()) {
		if (const auto emoji = Ui::Emoji::Find(_emoji)) {
			_emoji = emoji->text();
		}
	}

	if (_spoiler) {
		Ui::PreloadImageSpoiler();
	}
}

MediaFile::~MediaFile() {
	if (uploading() && !Core::Quitting()) {
		parent()->history()->session().uploader().cancel(parent()->fullId());
	}
	parent()->history()->owner().unregisterDocumentItem(
		_document,
		parent());
}

std::unique_ptr<Media> MediaFile::clone(not_null<HistoryItem*> parent) {
	return std::make_unique<MediaFile>(
		parent,
		_document,
		!_document->session().premium(),
		_spoiler,
		_ttlSeconds);
}

DocumentData *MediaFile::document() const {
	return _document;
}

bool MediaFile::uploading() const {
	return _document->uploading();
}

Storage::SharedMediaTypesMask MediaFile::sharedMediaTypes() const {
	using Type = Storage::SharedMediaType;
	if (_document->sticker() || ttlSeconds()) {
		return {};
	} else if (_document->isVideoMessage()) {
		return Storage::SharedMediaTypesMask{}
			.added(Type::RoundFile)
			.added(Type::RoundVoiceFile);
	} else if (_document->isGifv()) {
		return Type::GIF;
	} else if (_document->isVideoFile()) {
		return Storage::SharedMediaTypesMask{}
			.added(Type::Video)
			.added(Type::PhotoVideo);
	} else if (_document->isVoiceMessage()) {
		return Storage::SharedMediaTypesMask{}
			.added(Type::VoiceFile)
			.added(Type::RoundVoiceFile);
	} else if (_document->isSharedMediaMusic()) {
		return Type::MusicFile;
	}
	return Type::File;
}

bool MediaFile::canBeGrouped() const {
	if (_document->sticker() || _document->isAnimation()) {
		return false;
	} else if (_document->isVideoFile()) {
		return true;
	} else if (_document->isTheme() && _document->hasThumbnail()) {
		return false;
	}
	return true;
}

bool MediaFile::hasReplyPreview() const {
	return _document->hasThumbnail();
}

Image *MediaFile::replyPreview() const {
	return _document->getReplyPreview(parent());
}

bool MediaFile::replyPreviewLoaded() const {
	return _document->replyPreviewLoaded(_spoiler);
}

ItemPreview MediaFile::toPreview(ToPreviewOptions options) const {
	const auto item = parent();
	if (!options.ignoreGroup && item->groupId()) {
		if (const auto group = item->history()->owner().groups().find(item)
			; group && group->items.size() > 1) {
			return toGroupPreview(group->items, options);
		}
	}
	if (const auto sticker = _document->sticker()) {
		return Media::toPreview(options);
	}
	auto images = std::vector<ItemPreviewImage>();
	auto context = std::any();
	const auto existing = options.existing;
	if (auto found = FindCachedPreview(existing, _document, _spoiler)) {
		images.push_back(std::move(found));
	} else if (TryFilePreview(_document)) {
		const auto media = _document->createMediaView();
		const auto radius = _document->isVideoMessage()
			? ImageRoundRadius::Ellipse
			: ImageRoundRadius::Small;
		if (auto prepared = PrepareFilePreview(
				parent(),
				media,
				radius,
				_spoiler)
			; prepared || !prepared.cacheKey) {
			images.push_back(std::move(prepared));
			if (!prepared.cacheKey) {
				context = media;
			}
		}
	}
	const auto type = [&] {
		using namespace Ui::Text;
		if (_document->isVideoMessage()) {
			return (item->media() && item->media()->ttlSeconds())
				? tr::lng_in_dlg_video_message_ttl(tr::now)
				: tr::lng_in_dlg_video_message(tr::now);
		} else if (_document->isAnimation()) {
			return u"GIF"_q;
		} else if (_document->isVideoFile()) {
			return tr::lng_in_dlg_video(tr::now);
		} else if (_document->isVoiceMessage()) {
			return (item->media() && item->media()->ttlSeconds())
				? tr::lng_in_dlg_voice_message_ttl(tr::now)
				: tr::lng_in_dlg_audio(tr::now);
		} else if (const auto name = FormatSongNameFor(_document).string();
				!name.isEmpty()) {
			return name;
		} else if (_document->isAudioFile()) {
			return tr::lng_in_dlg_audio_file(tr::now);
		}
		return tr::lng_in_dlg_file(tr::now);
	}();
	const auto caption = (options.hideCaption || options.ignoreMessageText)
		? TextWithEntities()
		: options.translated
		? parent()->translatedText()
		: parent()->originalText();
	const auto hasMiniImages = !images.empty();
	return {
		WithCaptionNotificationText(type, caption, hasMiniImages),
		std::move(images),
		-1, // arrowInTextPosition (in-class default is -1, not {})
		{}, // imagesInTextPosition
		std::move(context),
	};
}

TextWithEntities MediaFile::notificationText() const {
	if (const auto sticker = _document->sticker()) {
		const auto text = _emoji.isEmpty()
			? tr::lng_in_dlg_sticker(tr::now)
			: tr::lng_in_dlg_sticker_emoji(tr::now, lt_emoji, _emoji);
		return Ui::Text::Colorized(text);
	}
	const auto type = [&] {
		if (_document->isVideoMessage()) {
			const auto media = parent()->media();
			return (media && media->ttlSeconds())
				? tr::lng_in_dlg_video_message_ttl(tr::now)
				: tr::lng_in_dlg_video_message(tr::now);
		} else if (_document->isAnimation()) {
			return u"GIF"_q;
		} else if (_document->isVideoFile()) {
			return tr::lng_in_dlg_video(tr::now);
		} else if (_document->isVoiceMessage()) {
			const auto media = parent()->media();
			return (media && media->ttlSeconds())
				? tr::lng_in_dlg_voice_message_ttl(tr::now)
				: tr::lng_in_dlg_audio(tr::now);
		} else if (!_document->filename().isEmpty()) {
			return _document->filename();
		} else if (_document->isAudioFile()) {
			return tr::lng_in_dlg_audio_file(tr::now);
		}
		return tr::lng_in_dlg_file(tr::now);
	}();
	return WithCaptionNotificationText(type, parent()->originalText());
}

QString MediaFile::pinnedTextSubstring() const {
	if (const auto sticker = _document->sticker()) {
		if (!_emoji.isEmpty()) {
			return tr::lng_action_pinned_media_emoji_sticker(
				tr::now,
				lt_emoji,
				_emoji);
		}
		return tr::lng_action_pinned_media_sticker(tr::now);
	} else if (_document->isAnimation()) {
		if (_document->isVideoMessage()) {
			return tr::lng_action_pinned_media_video_message(tr::now);
		}
		return tr::lng_action_pinned_media_gif(tr::now);
	} else if (_document->isVideoFile()) {
		return tr::lng_action_pinned_media_video(tr::now);
	} else if (_document->isVoiceMessage()) {
		return tr::lng_action_pinned_media_voice(tr::now);
	} else if (_document->isSong()) {
		return tr::lng_action_pinned_media_audio(tr::now);
	}
	return tr::lng_action_pinned_media_file(tr::now);
}

TextForMimeData MediaFile::clipboardText() const {
	auto caption = parent()->clipboardText();

	if (_document->isVoiceMessage() || _document->isVideoMessage()) {
		const auto &entry = _document->session().api().transcribes().entry(
			parent());
		if (!entry.requestId
			&& entry.shown
			&& !entry.toolong
			&& !entry.failed
			&& (entry.pending || !entry.result.isEmpty())) {
			const auto hasCaption = !caption.rich.text.isEmpty();
			const auto text = (hasCaption ? "{{\n" : "")
				+ entry.result
				+ (entry.result.isEmpty() ? "" : " ")
				+ (entry.pending ? "[...]" : "")
				+ (hasCaption ? "\n}}\n" : "");
			caption = TextForMimeData{ text, { text } }.append(
				std::move(caption));
		}
	}

	return caption;
}

bool MediaFile::allowsEditCaption() const {
	return !_document->isVideoMessage() && !_document->sticker();
}

bool MediaFile::allowsEditMedia() const {
	return !_document->isVideoMessage()
		&& !_document->sticker()
		&& !_document->isVoiceMessage();
}

bool MediaFile::forwardedBecomesUnread() const {
	return _document->isVoiceMessage()
		//|| _document->isVideoFile()
		|| _document->isVideoMessage();
}

bool MediaFile::dropForwardedInfo() const {
	return _document->isSong();
}

bool MediaFile::hasSpoiler() const {
	return _spoiler;
}

crl::time MediaFile::ttlSeconds() const {
	return _ttlSeconds;
}

bool MediaFile::allowsForward() const {
	return !ttlSeconds();
}

bool MediaFile::updateInlineResultMedia(const MTPMessageMedia &media) {
	if (media.type() != mtpc_messageMediaDocument) {
		return false;
	}
	const auto &data = media.c_messageMediaDocument();
	const auto content = data.vdocument();
	if (content && !data.vttl_seconds()) {
		const auto document = parent()->history()->owner().processDocument(
			*content);
		if (document == _document) {
			return false;
		} else {
			document->collectLocalData(_document);
		}
	} else {
		LOG(("API Error: "
			"Got MTPMessageMediaDocument without document "
			"or with ttl_seconds in updateInlineResultMedia()"));
	}
	return false;
}

bool MediaFile::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() != mtpc_messageMediaDocument) {
		return false;
	}
	const auto &data = media.c_messageMediaDocument();
	const auto content = data.vdocument();
	if (!content || data.vttl_seconds()) {
		LOG(("Api Error: "
			"Got MTPMessageMediaDocument without document "
			"or with ttl_seconds in updateSentMedia()"));
		return false;
	}
	parent()->history()->owner().documentConvert(_document, *content);
	return true;
}

std::unique_ptr<HistoryView::Media> MediaFile::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing) {
	if (_document->sticker()) {
		return std::make_unique<HistoryView::UnwrappedMedia>(
			message,
			std::make_unique<HistoryView::Sticker>(
				message,
				_document,
				_skipPremiumEffect,
				replacing));
	} else if (_document->isVideoMessage()) {
		const auto &entry = _document->session().api().transcribes().entry(
			parent());
		if (!entry.requestId
			&& entry.shown
			&& entry.roundview
			&& !entry.pending) {
			return std::make_unique<HistoryView::Document>(
				message,
				realParent,
				_document);
		} else {
			return std::make_unique<HistoryView::Gif>(
				message,
				realParent,
				_document,
				_spoiler);
		}
	} else if (_document->isAnimation() || _document->isVideoFile()) {
		return std::make_unique<HistoryView::Gif>(
			message,
			realParent,
			_document,
			_spoiler);
	} else if (_document->isTheme() && _document->hasThumbnail()) {
		return std::make_unique<HistoryView::ThemeDocument>(
			message,
			_document);
	}
	return std::make_unique<HistoryView::Document>(
		message,
		realParent,
		_document);
}

MediaContact::MediaContact(
	not_null<HistoryItem*> parent,
	UserId userId,
	const QString &firstName,
	const QString &lastName,
	const QString &phoneNumber)
: Media(parent) {
	parent->history()->owner().registerContactItem(userId, parent);

	_contact.userId = userId;
	_contact.firstName = firstName;
	_contact.lastName = lastName;
	_contact.phoneNumber = phoneNumber;
}

MediaContact::~MediaContact() {
	parent()->history()->owner().unregisterContactItem(
		_contact.userId,
		parent());
}

std::unique_ptr<Media> MediaContact::clone(not_null<HistoryItem*> parent) {
	return std::make_unique<MediaContact>(
		parent,
		_contact.userId,
		_contact.firstName,
		_contact.lastName,
		_contact.phoneNumber);
}

const SharedContact *MediaContact::sharedContact() const {
	return &_contact;
}

TextWithEntities MediaContact::notificationText() const {
	return tr::lng_in_dlg_contact(tr::now, Ui::Text::WithEntities);
}

QString MediaContact::pinnedTextSubstring() const {
	return tr::lng_action_pinned_media_contact(tr::now);
}

TextForMimeData MediaContact::clipboardText() const {
	const auto text = u"[ "_q
		+ tr::lng_in_dlg_contact(tr::now)
		+ u" ]\n"_q
		+ tr::lng_full_name(
			tr::now,
			lt_first_name,
			_contact.firstName,
			lt_last_name,
			_contact.lastName).trimmed()
		+ '\n'
		+ _contact.phoneNumber;
	return TextForMimeData::Simple(text);
}

bool MediaContact::updateInlineResultMedia(const MTPMessageMedia &media) {
	return false;
}

bool MediaContact::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() != mtpc_messageMediaContact) {
		return false;
	}
	const auto userId = UserId(media.c_messageMediaContact().vuser_id());
	if (_contact.userId != userId) {
		parent()->history()->owner().unregisterContactItem(
			_contact.userId,
			parent());
		_contact.userId = userId;
		parent()->history()->owner().registerContactItem(
			_contact.userId,
			parent());
	}
	return true;
}

std::unique_ptr<HistoryView::Media> MediaContact::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing) {
	return std::make_unique<HistoryView::Contact>(
		message,
		_contact.userId,
		_contact.firstName,
		_contact.lastName,
		_contact.phoneNumber);
}

MediaLocation::MediaLocation(
	not_null<HistoryItem*> parent,
	const LocationPoint &point)
: MediaLocation(parent, point, QString(), QString()) {
}

MediaLocation::MediaLocation(
	not_null<HistoryItem*> parent,
	const LocationPoint &point,
	const QString &title,
	const QString &description)
: Media(parent)
, _point(point)
, _location(parent->history()->owner().location(point))
, _title(title)
, _description(description) {
}

std::unique_ptr<Media> MediaLocation::clone(not_null<HistoryItem*> parent) {
	return std::make_unique<MediaLocation>(
		parent,
		_point,
		_title,
		_description);
}

CloudImage *MediaLocation::location() const {
	return _location;
}

ItemPreview MediaLocation::toPreview(ToPreviewOptions options) const {
	const auto type = tr::lng_maps_point(tr::now);
	const auto hasMiniImages = false;
	const auto text = TextWithEntities{ _title };
	return {
		WithCaptionNotificationText(type, text, hasMiniImages),
	};
}

TextWithEntities MediaLocation::notificationText() const {
	return WithCaptionNotificationText(
		tr::lng_maps_point(tr::now),
		{ _title });
}

QString MediaLocation::pinnedTextSubstring() const {
	return tr::lng_action_pinned_media_location(tr::now);
}

TextForMimeData MediaLocation::clipboardText() const {
	auto result = TextForMimeData::Simple(
		u"[ "_q + tr::lng_maps_point(tr::now) + u" ]\n"_q);
	auto titleResult = TextUtilities::ParseEntities(
		_title,
		Ui::WebpageTextTitleOptions().flags);
	auto descriptionResult = TextUtilities::ParseEntities(
		_description,
		TextParseLinks | TextParseMultiline);
	if (!titleResult.empty()) {
		result.append(std::move(titleResult));
	}
	if (!descriptionResult.text.isEmpty()) {
		result.append(std::move(descriptionResult));
	}
	result.append(LocationClickHandler(_point).url());
	return result;
}

bool MediaLocation::updateInlineResultMedia(const MTPMessageMedia &media) {
	return false;
}

bool MediaLocation::updateSentMedia(const MTPMessageMedia &media) {
	return false;
}

std::unique_ptr<HistoryView::Media> MediaLocation::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing) {
	return std::make_unique<HistoryView::Location>(
		message,
		_location,
		_point,
		_title,
		_description);
}

MediaCall::MediaCall(not_null<HistoryItem*> parent, const Call &call)
: Media(parent)
, _call(call) {
	parent->history()->owner().registerCallItem(parent);
}

MediaCall::~MediaCall() {
	parent()->history()->owner().unregisterCallItem(parent());
}

std::unique_ptr<Media> MediaCall::clone(not_null<HistoryItem*> parent) {
	Unexpected("Clone of call media.");
}

const Call *MediaCall::call() const {
	return &_call;
}

TextWithEntities MediaCall::notificationText() const {
	auto result = Text(parent(), _call.finishReason, _call.video);
	if (_call.duration > 0) {
		result = tr::lng_call_type_and_duration(
			tr::now,
			lt_type,
			result,
			lt_duration,
			Ui::FormatDurationWords(_call.duration));
	}
	return { result };
}

QString MediaCall::pinnedTextSubstring() const {
	return QString();
}

TextForMimeData MediaCall::clipboardText() const {
	return { {}, notificationText() };
}

bool MediaCall::allowsForward() const {
	return false;
}

bool MediaCall::updateInlineResultMedia(const MTPMessageMedia &media) {
	return false;
}

bool MediaCall::updateSentMedia(const MTPMessageMedia &media) {
	return false;
}

std::unique_ptr<HistoryView::Media> MediaCall::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing) {
	return std::make_unique<HistoryView::Call>(message, &_call);
}

QString MediaCall::Text(
		not_null<HistoryItem*> item,
		CallFinishReason reason,
		bool video) {
	if (item->out()) {
		return ((reason == CallFinishReason::Missed)
			? (video
				? tr::lng_call_video_cancelled
				: tr::lng_call_cancelled)
			: (video
				? tr::lng_call_video_outgoing
				: tr::lng_call_outgoing))(tr::now);
	} else if (reason == CallFinishReason::Missed) {
		return (video
			? tr::lng_call_video_missed
			: tr::lng_call_missed)(tr::now);
	} else if (reason == CallFinishReason::Busy) {
		return (video
			? tr::lng_call_video_declined
			: tr::lng_call_declined)(tr::now);
	}
	return (video
		? tr::lng_call_video_incoming
		: tr::lng_call_incoming)(tr::now);
}

MediaWebPage::MediaWebPage(
	not_null<HistoryItem*> parent,
	not_null<WebPageData*> page,
	MediaWebPageFlags flags)
: Media(parent)
, _page(page)
, _flags(flags) {
	parent->history()->owner().registerWebPageItem(_page, parent);
}

MediaWebPage::~MediaWebPage() {
	parent()->history()->owner().unregisterWebPageItem(_page, parent());
}

std::unique_ptr<Media> MediaWebPage::clone(not_null<HistoryItem*> parent) {
	return std::make_unique<MediaWebPage>(parent, _page, _flags);
}

DocumentData *MediaWebPage::document() const {
	return _page->document;
}

PhotoData *MediaWebPage::photo() const {
	return _page->photo;
}

WebPageData *MediaWebPage::webpage() const {
	return _page;
}

MediaWebPageFlags MediaWebPage::webpageFlags() const {
	return _flags;
}

bool MediaWebPage::hasReplyPreview() const {
	if (const auto document = MediaWebPage::document()) {
		return document->hasThumbnail()
			&& !document->isPatternWallPaper();
	} else if (const auto photo = MediaWebPage::photo()) {
		return !photo->isNull();
	}
	return false;
}

Image *MediaWebPage::replyPreview() const {
	if (const auto document = MediaWebPage::document()) {
		return document->getReplyPreview(parent());
	} else if (const auto photo = MediaWebPage::photo()) {
		return photo->getReplyPreview(parent());
	}
	return nullptr;
}

bool MediaWebPage::replyPreviewLoaded() const {
	const auto spoiler = false;
	if (const auto document = MediaWebPage::document()) {
		return document->replyPreviewLoaded(spoiler);
	} else if (const auto photo = MediaWebPage::photo()) {
		return photo->replyPreviewLoaded(spoiler);
	}
	return true;
}

ItemPreview MediaWebPage::toPreview(ToPreviewOptions options) const {
	// XP walk: took v4.13.0 semantics (caption + image preview for photo/video/
	// document webpages). ours had simplified to caption-only at v4.11.4, but
	// MediaPhoto/MediaDocument::toPreview retain the same image logic on XP, so
	// it is safe. Designated -> positional (C7555).
	const auto caption = [&] {
		const auto text = options.ignoreMessageText
			? TextWithEntities()
			: options.translated
			? parent()->translatedText()
			: parent()->originalText();
		return text.empty() ? Ui::Text::Colorized(_page->url) : text;
	}();
	const auto pageTypeWithPreview = _page->type == WebPageType::Photo
		|| _page->type == WebPageType::Video
		|| _page->type == WebPageType::Document;
	if (pageTypeWithPreview || !_page->collage.items.empty()) {
		if (auto found = FindCachedPreview(options.existing, _page, false)) {
			// XP walk: ItemPreview fields: text, images, ...
			return { caption, { std::move(found) } };
		}
		auto context = std::any();
		auto images = std::vector<ItemPreviewImage>();
		auto prepared = ItemPreviewImage();
		const auto radius = ImageRoundRadius::Small;
		if (const auto photo = MediaWebPage::photo()) {
			const auto media = photo->createMediaView();
			prepared = PreparePhotoPreview(parent(), media, radius, false);
			if (prepared || !prepared.cacheKey) {
				images.push_back(std::move(prepared));
				if (!prepared.cacheKey) {
					context = media;
				}
			}
		} else {
			const auto document = MediaWebPage::document();
			if (document
				&& document->hasThumbnail()
				&& (document->isGifv() || document->isVideoFile())) {
				const auto media = document->createMediaView();
				prepared = PrepareFilePreview(parent(), media, radius, false);
				if (prepared || !prepared.cacheKey) {
					images.push_back(std::move(prepared));
					if (!prepared.cacheKey) {
						context = media;
					}
				}
			}
		}
		// XP walk: ItemPreview fields: text, images, arrowInTextPosition(-1),
		// imagesInTextPosition(0), loadingContext. arrow default -1 must be explicit.
		return {
			caption, // text
			std::move(images), // images
			-1, // arrowInTextPosition
			{}, // imagesInTextPosition
			std::move(context), // loadingContext
		};
	} else {
		return { caption }; // XP walk: text is field 0
	}
}

TextWithEntities MediaWebPage::notificationText() const {
	return parent()->originalText();
}

QString MediaWebPage::pinnedTextSubstring() const {
	return QString();
}

TextForMimeData MediaWebPage::clipboardText() const {
	return TextForMimeData();
}

bool MediaWebPage::allowsEdit() const {
	return true;
}

bool MediaWebPage::updateInlineResultMedia(const MTPMessageMedia &media) {
	return false;
}

bool MediaWebPage::updateSentMedia(const MTPMessageMedia &media) {
	return false;
}

std::unique_ptr<HistoryView::Media> MediaWebPage::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing) {
	return std::make_unique<HistoryView::WebPage>(message, _page, _flags);
}

MediaGame::MediaGame(
	not_null<HistoryItem*> parent,
	not_null<GameData*> game)
: Media(parent)
, _game(game) {
}

std::unique_ptr<Media> MediaGame::clone(not_null<HistoryItem*> parent) {
	return std::make_unique<MediaGame>(parent, _game);
}

bool MediaGame::hasReplyPreview() const {
	if (const auto document = _game->document) {
		return document->hasThumbnail();
	} else if (const auto photo = _game->photo) {
		return !photo->isNull();
	}
	return false;
}

Image *MediaGame::replyPreview() const {
	if (const auto document = _game->document) {
		return document->getReplyPreview(parent());
	} else if (const auto photo = _game->photo) {
		return photo->getReplyPreview(parent());
	}
	return nullptr;
}

bool MediaGame::replyPreviewLoaded() const {
	const auto spoiler = false;
	if (const auto document = _game->document) {
		return document->replyPreviewLoaded(spoiler);
	} else if (const auto photo = _game->photo) {
		return photo->replyPreviewLoaded(spoiler);
	}
	return true;
}

TextWithEntities MediaGame::notificationText() const {
	// Add a game controller emoji before game title.
	auto result = QString();
	result.reserve(_game->title.size() + 3);
	result.append(
		QChar(0xD83C)
	).append(
		QChar(0xDFAE)
	).append(
		QChar(' ')
	).append(_game->title);
	return { result };
}

GameData *MediaGame::game() const {
	return _game;
}

QString MediaGame::pinnedTextSubstring() const {
	const auto title = _game->title;
	return tr::lng_action_pinned_media_game(tr::now, lt_game, title);
}

TextForMimeData MediaGame::clipboardText() const {
	return TextForMimeData();
}

bool MediaGame::dropForwardedInfo() const {
	return true;
}

bool MediaGame::consumeMessageText(const TextWithEntities &text) {
	_consumedText = text;
	return true;
}

TextWithEntities MediaGame::consumedMessageText() const {
	return _consumedText;
}

bool MediaGame::updateInlineResultMedia(const MTPMessageMedia &media) {
	return updateSentMedia(media);
}

bool MediaGame::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() != mtpc_messageMediaGame) {
		return false;
	}
	parent()->history()->owner().gameConvert(
		_game, media.c_messageMediaGame().vgame());
	return true;
}

std::unique_ptr<HistoryView::Media> MediaGame::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing) {
	return std::make_unique<HistoryView::Game>(
		message,
		_game,
		_consumedText);
}

MediaInvoice::MediaInvoice(
	not_null<HistoryItem*> parent,
	const Invoice &data)
: Media(parent)
, _invoice{
	data.receiptMsgId,
	data.amount,
	data.currency,
	data.title,
	data.description,
	data.extendedPreview,
	(data.extendedMedia
		? data.extendedMedia->clone(parent)
		: nullptr),
	data.photo,
	data.isTest,
} {
	if (_invoice.extendedPreview && !_invoice.extendedMedia) {
		Ui::PreloadImageSpoiler();
	}
}

std::unique_ptr<Media> MediaInvoice::clone(not_null<HistoryItem*> parent) {
	return std::make_unique<MediaInvoice>(parent, _invoice);
}

const Invoice *MediaInvoice::invoice() const {
	return &_invoice;
}

bool MediaInvoice::hasReplyPreview() const {
	if (const auto photo = _invoice.photo) {
		return !photo->isNull();
	}
	return false;
}

Image *MediaInvoice::replyPreview() const {
	if (const auto photo = _invoice.photo) {
		return photo->getReplyPreview(parent());
	}
	return nullptr;
}

bool MediaInvoice::replyPreviewLoaded() const {
	const auto spoiler = false;
	if (const auto photo = _invoice.photo) {
		return photo->replyPreviewLoaded(spoiler);
	}
	return true;
}

TextWithEntities MediaInvoice::notificationText() const {
	return { _invoice.title };
}

QString MediaInvoice::pinnedTextSubstring() const {
	return QString::fromUtf8("\xC2\xAB")
		+ _invoice.title
		+ QString::fromUtf8("\xC2\xBB");
}

TextForMimeData MediaInvoice::clipboardText() const {
	return TextForMimeData();
}

bool MediaInvoice::updateInlineResultMedia(const MTPMessageMedia &media) {
	return true;
}

bool MediaInvoice::updateSentMedia(const MTPMessageMedia &media) {
	return true;
}

bool MediaInvoice::updateExtendedMedia(
		not_null<HistoryItem*> item,
		const MTPMessageExtendedMedia &media) {
	Expects(item == parent());

	return UpdateExtendedMedia(_invoice, item, media);
}

std::unique_ptr<HistoryView::Media> MediaInvoice::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing) {
	if (_invoice.extendedMedia) {
		return _invoice.extendedMedia->createView(
			message,
			realParent,
			replacing);
	} else if (_invoice.extendedPreview) {
		return std::make_unique<HistoryView::ExtendedPreview>(
			message,
			&_invoice);
	}
	return std::make_unique<HistoryView::Invoice>(message, &_invoice);
}

MediaPoll::MediaPoll(
	not_null<HistoryItem*> parent,
	not_null<PollData*> poll)
: Media(parent)
, _poll(poll) {
}

MediaPoll::~MediaPoll() {
}

std::unique_ptr<Media> MediaPoll::clone(not_null<HistoryItem*> parent) {
	return std::make_unique<MediaPoll>(parent, _poll);
}

PollData *MediaPoll::poll() const {
	return _poll;
}

TextWithEntities MediaPoll::notificationText() const {
	return TextWithEntities()
		.append(QChar(0xD83D))
		.append(QChar(0xDCCA))
		.append(QChar(' '))
		.append(Ui::Text::Colorized(_poll->question));
}

QString MediaPoll::pinnedTextSubstring() const {
	return QChar(171) + _poll->question + QChar(187);
}

TextForMimeData MediaPoll::clipboardText() const {
	const auto text = u"[ "_q
		+ tr::lng_in_dlg_poll(tr::now)
		+ u" : "_q
		+ _poll->question
		+ u" ]"_q
		+ ranges::accumulate(
			ranges::views::all(
				_poll->answers
			) | ranges::views::transform([](const PollAnswer &answer) {
				return "\n- " + answer.text;
			}),
			QString());
	return TextForMimeData::Simple(text);
}

bool MediaPoll::updateInlineResultMedia(const MTPMessageMedia &media) {
	return false;
}

bool MediaPoll::updateSentMedia(const MTPMessageMedia &media) {
	return false;
}

std::unique_ptr<HistoryView::Media> MediaPoll::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing) {
	return std::make_unique<HistoryView::Poll>(message, _poll);
}

MediaDice::MediaDice(not_null<HistoryItem*> parent, QString emoji, int value)
: Media(parent)
, _emoji(emoji)
, _value(value) {
}

std::unique_ptr<Media> MediaDice::clone(not_null<HistoryItem*> parent) {
	return std::make_unique<MediaDice>(parent, _emoji, _value);
}

QString MediaDice::emoji() const {
	return _emoji;
}

int MediaDice::value() const {
	return _value;
}

bool MediaDice::allowsRevoke(TimeId now) const {
	const auto peer = parent()->history()->peer;
	if (peer->isSelf() || !peer->isUser()) {
		return true;
	}
	return (now >= parent()->date() + kFastRevokeRestriction);
}

TextWithEntities MediaDice::notificationText() const {
	return { _emoji };
}

QString MediaDice::pinnedTextSubstring() const {
	return QChar(171) + notificationText().text + QChar(187);
}

TextForMimeData MediaDice::clipboardText() const {
	return { {}, notificationText() };
}

bool MediaDice::forceForwardedInfo() const {
	return true;
}

bool MediaDice::updateInlineResultMedia(const MTPMessageMedia &media) {
	return updateSentMedia(media);
}

bool MediaDice::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() != mtpc_messageMediaDice) {
		return false;
	}
	_value = media.c_messageMediaDice().vvalue().v;
	parent()->history()->owner().requestItemRepaint(parent());
	return true;
}

std::unique_ptr<HistoryView::Media> MediaDice::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing) {
	return ::Stickers::DicePacks::IsSlot(_emoji)
		? std::make_unique<HistoryView::UnwrappedMedia>(
			message,
			std::make_unique<HistoryView::SlotMachine>(message, this))
		: std::make_unique<HistoryView::UnwrappedMedia>(
			message,
			std::make_unique<HistoryView::Dice>(message, this));
}

ClickHandlerPtr MediaDice::makeHandler() const {
	return MakeHandler(parent()->history(), _emoji);
}

ClickHandlerPtr MediaDice::MakeHandler(
		not_null<History*> history,
		const QString &emoji) {
	// TODO support multi-windows.
	static auto ShownToast = base::weak_ptr<Ui::Toast::Instance>();
	static const auto HideExisting = [] {
		if (const auto toast = ShownToast.get()) {
			toast->hideAnimated();
			ShownToast = nullptr;
		}
	};
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		auto config = Ui::Toast::Config{
			{}, // title
			{ tr::lng_about_random(tr::now, lt_emoji, emoji) }, // text
			&st::historyDiceToast, // st
			Ui::Toast::kDefaultDuration * 2, // duration
			16, // maxLines
			{}, // adaptive
			true, // multiline
		};
		if (CanSend(history->peer, ChatRestriction::SendOther)) {
			auto link = Ui::Text::Link(tr::lng_about_random_send(tr::now));
			link.entities.push_back(
				EntityInText(EntityType::Semibold, 0, link.text.size()));
			config.text.append(' ').append(std::move(link));
			config.filter = crl::guard(&history->session(), [=](
					const ClickHandlerPtr &handler,
					Qt::MouseButton button) {
				if (button == Qt::LeftButton && !ShownToast.empty()) {
					auto message = Api::MessageToSend(
						Api::SendAction(history));
					message.action.clearDraft = false;
					message.textWithTags.text = emoji;

					Api::SendDice(message);
					HideExisting();
				}
				return false;
			});
		}

		HideExisting();
		const auto my = context.other.value<ClickHandlerContext>();
		const auto weak = my.sessionWindow;
		if (const auto strong = weak.get()) {
			ShownToast = strong->showToast(std::move(config));
		} else {
			ShownToast = Ui::Toast::Show(config);
		}
	});
}

MediaGiftBox::MediaGiftBox(
	not_null<HistoryItem*> parent,
	not_null<PeerData*> from,
	int months)
: MediaGiftBox(parent, from, GiftCode{ {}, {}, months }) { // XP walk: designated -> positional (C7555)
}

MediaGiftBox::MediaGiftBox(
	not_null<HistoryItem*> parent,
	not_null<PeerData*> from,
	GiftCode data)
: Media(parent)
, _from(from)
, _data(std::move(data)) {
}

std::unique_ptr<Media> MediaGiftBox::clone(not_null<HistoryItem*> parent) {
	return std::make_unique<MediaGiftBox>(parent, _from, _data);
}

not_null<PeerData*> MediaGiftBox::from() const {
	return _from;
}

const GiftCode &MediaGiftBox::data() const {
	return _data;
}

TextWithEntities MediaGiftBox::notificationText() const {
	return {};
}

QString MediaGiftBox::pinnedTextSubstring() const {
	return {};
}

TextForMimeData MediaGiftBox::clipboardText() const {
	return {};
}

bool MediaGiftBox::updateInlineResultMedia(const MTPMessageMedia &media) {
	return false;
}

bool MediaGiftBox::updateSentMedia(const MTPMessageMedia &media) {
	return false;
}

std::unique_ptr<HistoryView::Media> MediaGiftBox::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing) {
	return std::make_unique<HistoryView::ServiceBox>(
		message,
		std::make_unique<HistoryView::PremiumGift>(message, this));
}

MediaWallPaper::MediaWallPaper(
	not_null<HistoryItem*> parent,
	const WallPaper &paper,
	bool paperForBoth)
: Media(parent)
, _paper(paper)
, _paperForBoth(paperForBoth) {
}

MediaWallPaper::~MediaWallPaper() = default;

std::unique_ptr<Media> MediaWallPaper::clone(
		not_null<HistoryItem*> parent) {
	return std::make_unique<MediaWallPaper>(parent, _paper, _paperForBoth);
}

const WallPaper *MediaWallPaper::paper() const {
	return &_paper;
}

bool MediaWallPaper::paperForBoth() const {
	return _paperForBoth;
}

TextWithEntities MediaWallPaper::notificationText() const {
	return {};
}

QString MediaWallPaper::pinnedTextSubstring() const {
	return {};
}

TextForMimeData MediaWallPaper::clipboardText() const {
	return {};
}

bool MediaWallPaper::updateInlineResultMedia(const MTPMessageMedia &media) {
	return false;
}

bool MediaWallPaper::updateSentMedia(const MTPMessageMedia &media) {
	return false;
}

std::unique_ptr<HistoryView::Media> MediaWallPaper::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing) {
	return std::make_unique<HistoryView::ServiceBox>(
		message,
		std::make_unique<HistoryView::ThemeDocumentBox>(message, _paper));
}

MediaStory::MediaStory(
	not_null<HistoryItem*> parent,
	FullStoryId storyId,
	bool mention)
: Media(parent)
, _storyId(storyId)
, _mention(mention) {
	const auto owner = &parent->history()->owner();
	owner->registerStoryItem(storyId, parent);

	const auto stories = &owner->stories();
	const auto maybeStory = stories->lookup(storyId);
	if (!maybeStory && maybeStory.error() == NoStory::Unknown) {
		stories->resolve(storyId, crl::guard(this, [=] {
			if (const auto maybeStory = stories->lookup(storyId)) {
				if (!_mention && _viewMayExist) {
					parent->setText((*maybeStory)->caption());
				}
			} else {
				_expired = true;
			}
			if (_mention) {
				parent->updateStoryMentionText();
			}
			parent->history()->owner().requestItemViewRefresh(parent);
		}));
	} else if (!maybeStory) {
		_expired = true;
	}
}

MediaStory::~MediaStory() {
	const auto owner = &parent()->history()->owner();
	owner->unregisterStoryItem(_storyId, parent());
}

std::unique_ptr<Media> MediaStory::clone(not_null<HistoryItem*> parent) {
	return std::make_unique<MediaStory>(parent, _storyId, false);
}

FullStoryId MediaStory::storyId() const {
	return _storyId;
}

bool MediaStory::storyExpired(bool revalidate) {
	if (revalidate) {
		const auto stories = &parent()->history()->owner().stories();
		if (const auto maybeStory = stories->lookup(_storyId)) {
			_expired = false;
		} else if (maybeStory.error() == Data::NoStory::Deleted) {
			_expired = true;
		}
	}
	return _expired;
}

bool MediaStory::storyMention() const {
	return _mention;
}

TextWithEntities MediaStory::notificationText() const {
	const auto stories = &parent()->history()->owner().stories();
	const auto maybeStory = stories->lookup(_storyId);
	return WithCaptionNotificationText(
		((_expired
			|| (!maybeStory
				&& maybeStory.error() == Data::NoStory::Deleted))
			? tr::lng_in_dlg_story_expired
			: tr::lng_in_dlg_story)(tr::now),
		(maybeStory
			? (*maybeStory)->caption()
			: TextWithEntities()));
}

QString MediaStory::pinnedTextSubstring() const {
	return tr::lng_action_pinned_media_story(tr::now);
}

TextForMimeData MediaStory::clipboardText() const {
	return WithCaptionClipboardText(
		(_expired
			? tr::lng_in_dlg_story_expired
			: tr::lng_in_dlg_story)(tr::now),
		parent()->clipboardText());
}

bool MediaStory::dropForwardedInfo() const {
	return true;
}

bool MediaStory::updateInlineResultMedia(const MTPMessageMedia &media) {
	return false;
}

bool MediaStory::updateSentMedia(const MTPMessageMedia &media) {
	return false;
}

not_null<PhotoData*> MediaStory::LoadingStoryPhoto(
		not_null<Session*> owner) {
	return owner->photo(kLoadingStoryPhotoId);
}

std::unique_ptr<HistoryView::Media> MediaStory::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing) {
	const auto spoiler = false;
	const auto stories = &parent()->history()->owner().stories();
	const auto maybeStory = stories->lookup(_storyId);
	if (!maybeStory) {
		if (!_mention) {
			realParent->setText(TextWithEntities());
		}
		if (maybeStory.error() == Data::NoStory::Deleted) {
			_expired = true;
			return nullptr;
		}
		_expired = false;
		if (_mention) {
			return nullptr;
		}
		_viewMayExist = true;
		return std::make_unique<HistoryView::Photo>(
			message,
			realParent,
			LoadingStoryPhoto(&realParent->history()->owner()),
			spoiler);
	}
	_expired = false;
	_viewMayExist = true;
	const auto story = *maybeStory;
	if (_mention) {
		return std::make_unique<HistoryView::ServiceBox>(
			message,
			std::make_unique<HistoryView::StoryMention>(message, story));
	} else {
		realParent->setText(story->caption());
		if (const auto photo = story->photo()) {
			return std::make_unique<HistoryView::Photo>(
				message,
				realParent,
				photo,
				spoiler);
		} else {
			return std::make_unique<HistoryView::Gif>(
				message,
				realParent,
				story->document(),
				spoiler);
		}
	}
}

MediaGiveawayStart::MediaGiveawayStart(
	not_null<HistoryItem*> parent,
	const GiveawayStart &data)
: Media(parent)
, _data(data) {
	parent->history()->session().giftBoxStickersPacks().load();
}

std::unique_ptr<Media> MediaGiveawayStart::clone(
		not_null<HistoryItem*> parent) {
	return std::make_unique<MediaGiveawayStart>(parent, _data);
}

const GiveawayStart *MediaGiveawayStart::giveawayStart() const {
	return &_data;
}

TextWithEntities MediaGiveawayStart::notificationText() const {
	return {
		// XP walk: designated -> positional (C7555); member renamed
		// _giveaway -> _data (v4.13.0). text is field 0.
		tr::lng_prizes_title(tr::now, lt_count, _data.quantity), // text
	};
}

QString MediaGiveawayStart::pinnedTextSubstring() const {
	return QString::fromUtf8("\xC2\xAB")
		+ notificationText().text
		+ QString::fromUtf8("\xC2\xBB");
}

TextForMimeData MediaGiveawayStart::clipboardText() const {
	return TextForMimeData();
}

bool MediaGiveawayStart::updateInlineResultMedia(const MTPMessageMedia &media) {
	return true;
}

bool MediaGiveawayStart::updateSentMedia(const MTPMessageMedia &media) {
	return true;
}

std::unique_ptr<HistoryView::Media> MediaGiveawayStart::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing) {
	return std::make_unique<HistoryView::MediaInBubble>(
		message,
		HistoryView::GenerateGiveawayStart(message, &_data));
}

MediaGiveawayResults::MediaGiveawayResults(
	not_null<HistoryItem*> parent,
	const GiveawayResults &data)
: Media(parent)
, _data(data) {
}

std::unique_ptr<Media> MediaGiveawayResults::clone(
		not_null<HistoryItem*> parent) {
	return std::make_unique<MediaGiveawayResults>(parent, _data);
}

const GiveawayResults *MediaGiveawayResults::giveawayResults() const {
	return &_data;
}

TextWithEntities MediaGiveawayResults::notificationText() const {
	return {
		// XP walk: designated -> positional (C7555); TextWithEntities::text is field 0.
		tr::lng_prizes_results_title(tr::now), // text
	};
}

QString MediaGiveawayResults::pinnedTextSubstring() const {
	return QString::fromUtf8("\xC2\xAB")
		+ notificationText().text
		+ QString::fromUtf8("\xC2\xBB");
}

TextForMimeData MediaGiveawayResults::clipboardText() const {
	return TextForMimeData();
}

bool MediaGiveawayResults::updateInlineResultMedia(const MTPMessageMedia &media) {
	return true;
}

bool MediaGiveawayResults::updateSentMedia(const MTPMessageMedia &media) {
	return true;
}

std::unique_ptr<HistoryView::Media> MediaGiveawayResults::createView(
		not_null<HistoryView::Element*> message,
		not_null<HistoryItem*> realParent,
		HistoryView::Element *replacing) {
	return std::make_unique<HistoryView::MediaInBubble>(
		message,
		HistoryView::GenerateGiveawayResults(message, &_data));
}

} // namespace Data
