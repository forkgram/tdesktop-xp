/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_credits_history_entry.h"

#include "api/api_premium.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_credits.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"

namespace Api {

Data::CreditsHistoryEntry CreditsHistoryEntryFromTL(
		const MTPStarsTransaction &tl,
		not_null<PeerData*> peer) {
	using HistoryPeerTL = MTPDstarsTransactionPeer;
	using namespace Data;
	const auto owner = &peer->owner();
	const auto photo = tl.data().vphoto()
		? owner->photoFromWeb(*tl.data().vphoto(), ImageLocation())
		: nullptr;
	auto extended = std::vector<CreditsHistoryMedia>();
	if (const auto list = tl.data().vextended_media()) {
		extended.reserve(list->v.size());
		for (const auto &media : list->v) {
			media.match([&](const MTPDmessageMediaPhoto &data) {
				if (const auto inner = data.vphoto()) {
					const auto photo = owner->processPhoto(*inner);
					if (!photo->isNull()) {
						extended.push_back(CreditsHistoryMedia{
							CreditsHistoryMediaType::Photo, // type
							photo->id, // id
						});
					}
				}
			}, [&](const MTPDmessageMediaDocument &data) {
				if (const auto inner = data.vdocument()) {
					const auto document = owner->processDocument(
						*inner,
						data.valt_documents());
					if (document->isAnimation()
						|| document->isVideoFile()
						|| document->isGifv()) {
						extended.push_back(CreditsHistoryMedia{
							CreditsHistoryMediaType::Video, // type
							document->id, // id
						});
					}
				}
			}, [&](const auto &) {});
		}
	}
	const auto barePeerId = tl.data().vpeer().match([](
			const HistoryPeerTL &p) {
		return peerFromMTP(p.vpeer());
	}, [](const auto &) {
		return PeerId(0);
	}).value;
	const auto stargift = tl.data().vstargift();
	const auto nonUniqueGift = stargift
		? stargift->match([&](const MTPDstarGift &data) {
			return &data;
		}, [](const auto &) { return (const MTPDstarGift*)nullptr; })
		: nullptr;
	const auto reaction = tl.data().is_reaction();
	const auto amount = CreditsAmountFromTL(tl.data().vamount());
	const auto starrefAmount = CreditsAmountFromTL(
		tl.data().vstarref_amount());
	const auto starrefCommission
		= tl.data().vstarref_commission_permille().value_or_empty();
	const auto starrefBarePeerId = tl.data().vstarref_peer()
		? peerFromMTP(*tl.data().vstarref_peer()).value
		: 0;
	const auto incoming = (amount >= CreditsAmount());
	const auto paidMessagesCount
		= tl.data().vpaid_messages().value_or_empty();
	const auto premiumMonthsForStars
		= tl.data().vpremium_gift_months().value_or_empty();
	const auto saveActorId = (reaction
		|| !extended.empty()
		|| paidMessagesCount) && incoming;
	const auto parsedGift = stargift
		? FromTL(&peer->session(), *stargift)
		: std::optional<Data::StarGift>();
	const auto giftStickerId = parsedGift ? parsedGift->document->id : 0;
	// XP walk: designated -> named-local (C7555; CreditsHistoryEntry large,
	// non-contiguous; avoids int64->uint64 narrowing). Moved from api_credits.cpp
	// HistoryFromTL in v5.16.0; took theirs' semantics (.vamount, ads dates, +postsSearch).
	auto entry = Data::CreditsHistoryEntry();
	entry.id = qs(tl.data().vid());
	entry.title = qs(tl.data().vtitle().value_or_empty());
	entry.description = { qs(tl.data().vdescription().value_or_empty()) };
	entry.date = base::unixtime::parse(
		tl.data().vads_proceeds_from_date().value_or(
			tl.data().vdate().v));
	entry.photoId = photo ? photo->id : 0;
	entry.extended = std::move(extended);
	entry.credits = CreditsAmountFromTL(tl.data().vamount());
	entry.bareMsgId = uint64(tl.data().vmsg_id().value_or_empty());
	entry.barePeerId = saveActorId ? peer->id.value : barePeerId;
	entry.bareGiveawayMsgId = uint64(
		tl.data().vgiveaway_post_id().value_or_empty());
	entry.bareGiftStickerId = giftStickerId;
	entry.bareActorId = saveActorId ? barePeerId : uint64(0);
	entry.uniqueGift = parsedGift ? parsedGift->unique : nullptr;
	entry.starrefAmount = paidMessagesCount ? CreditsAmount() : starrefAmount;
	entry.starrefCommission = paidMessagesCount ? 0 : starrefCommission;
	entry.starrefRecipientId = paidMessagesCount ? 0 : starrefBarePeerId;
	entry.peerType = tl.data().vpeer().match([](const HistoryPeerTL &) {
		return Data::CreditsHistoryEntry::PeerType::Peer;
	}, [](const MTPDstarsTransactionPeerPlayMarket &) {
		return Data::CreditsHistoryEntry::PeerType::PlayMarket;
	}, [](const MTPDstarsTransactionPeerFragment &) {
		return Data::CreditsHistoryEntry::PeerType::Fragment;
	}, [](const MTPDstarsTransactionPeerAppStore &) {
		return Data::CreditsHistoryEntry::PeerType::AppStore;
	}, [](const MTPDstarsTransactionPeerUnsupported &) {
		return Data::CreditsHistoryEntry::PeerType::Unsupported;
	}, [](const MTPDstarsTransactionPeerPremiumBot &) {
		return Data::CreditsHistoryEntry::PeerType::PremiumBot;
	}, [](const MTPDstarsTransactionPeerAds &) {
		return Data::CreditsHistoryEntry::PeerType::Ads;
	}, [](const MTPDstarsTransactionPeerAPI &) {
		return Data::CreditsHistoryEntry::PeerType::API;
	});
	entry.subscriptionUntil = tl.data().vsubscription_period()
		? base::unixtime::parse(base::unixtime::now()
			+ tl.data().vsubscription_period()->v)
		: QDateTime();
	entry.adsProceedsToDate = tl.data().vads_proceeds_to_date()
		? base::unixtime::parse(tl.data().vads_proceeds_to_date()->v)
		: QDateTime();
	entry.successDate = tl.data().vtransaction_date()
		? base::unixtime::parse(tl.data().vtransaction_date()->v)
		: QDateTime();
	entry.successLink = qs(tl.data().vtransaction_url().value_or_empty());
	entry.paidMessagesCount = paidMessagesCount;
	entry.paidMessagesAmount = (paidMessagesCount
		? starrefAmount
		: CreditsAmount());
	entry.paidMessagesCommission = paidMessagesCount ? starrefCommission : 0;
	entry.limitedCount = parsedGift ? parsedGift->limitedCount : 0;
	entry.limitedLeft = parsedGift ? parsedGift->limitedLeft : 0;
	entry.starsConverted = int(nonUniqueGift
		? nonUniqueGift->vconvert_stars().v
		: 0);
	entry.premiumMonthsForStars = premiumMonthsForStars;
	entry.floodSkip = int(tl.data().vfloodskip_number().value_or(0));
	entry.converted = stargift && incoming;
	entry.stargift = stargift.has_value();
	entry.postsSearch = tl.data().is_posts_search();
	entry.giftUpgraded = tl.data().is_stargift_upgrade();
	entry.giftResale = tl.data().is_stargift_resale();
	entry.giftOffer = tl.data().is_offer();
	entry.reaction = tl.data().is_reaction();
	entry.refunded = tl.data().is_refund();
	entry.pending = tl.data().is_pending();
	entry.failed = tl.data().is_failed();
	entry.in = incoming;
	entry.gift = tl.data().is_gift() || stargift.has_value();
	return entry;
}

} // namespace Api
