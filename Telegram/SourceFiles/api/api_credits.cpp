/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_credits.h"

#include "api/api_statistics_data_deserialize.h"
#include "api/api_updates.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_app_config.h"
#include "main/main_session.h"

namespace Api {
namespace {

constexpr auto kTransactionsLimit = 100;

[[nodiscard]] Data::CreditsHistoryEntry HistoryFromTL(
		const MTPStarsTransaction &tl,
		not_null<PeerData*> peer) {
	using HistoryPeerTL = MTPDstarsTransactionPeer;
	using namespace Data;
	const auto owner = &peer->owner();
	const auto photo = tl.data().vphoto()
		? owner->photoFromWeb(*tl.data().vphoto(), ImageLocation())
		: nullptr;
	// XP walk: designated -> positional (C7555); named-local for many-field struct
	auto extended = std::vector<CreditsHistoryMedia>();
	if (const auto list = tl.data().vextended_media()) {
		extended.reserve(list->v.size());
		for (const auto &media : list->v) {
			media.match([&](const MTPDmessageMediaPhoto &photo) {
				if (const auto inner = photo.vphoto()) {
					const auto photo = owner->processPhoto(*inner);
					if (!photo->isNull()) {
						extended.push_back(CreditsHistoryMedia{
							CreditsHistoryMediaType::Photo, // type
							photo->id, // id
						});
					}
				}
			}, [&](const MTPDmessageMediaDocument &document) {
				if (const auto inner = document.vdocument()) {
					const auto document = owner->processDocument(*inner);
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
	// XP walk: designated -> positional (C7555); keep named-local to avoid
	// int64->uint64 narrowing on .credits; .gift added in v5.3.0
	auto entry = Data::CreditsHistoryEntry();
	entry.id = qs(tl.data().vid());
	entry.title = qs(tl.data().vtitle().value_or_empty());
	entry.description = qs(tl.data().vdescription().value_or_empty());
	entry.date = base::unixtime::parse(tl.data().vdate().v);
	entry.photoId = photo ? photo->id : 0;
	entry.extended = std::move(extended);
	entry.credits = tl.data().vstars().v;
	entry.bareMsgId = uint64(tl.data().vmsg_id().value_or_empty());
	entry.barePeerId = barePeerId;
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
	});
	entry.refunded = tl.data().is_refund();
	entry.pending = tl.data().is_pending();
	entry.failed = tl.data().is_failed();
	entry.successDate = tl.data().vtransaction_date()
		? base::unixtime::parse(tl.data().vtransaction_date()->v)
		: QDateTime();
	entry.successLink = qs(tl.data().vtransaction_url().value_or_empty());
	entry.in = (int64(tl.data().vstars().v) >= 0);
	entry.gift = tl.data().is_gift();
	return entry;
}

[[nodiscard]] Data::CreditsStatusSlice StatusFromTL(
		const MTPpayments_StarsStatus &status,
		not_null<PeerData*> peer) {
	peer->owner().processUsers(status.data().vusers());
	peer->owner().processChats(status.data().vchats());
	// XP walk: designated -> positional (C7555);
	// tl::conditional has no .has_value() -> use operator bool (C2039)
	return Data::CreditsStatusSlice{
		ranges::views::all(
			status.data().vhistory().v
		) | ranges::views::transform([&](const MTPStarsTransaction &tl) {
			return HistoryFromTL(tl, peer);
		}) | ranges::to_vector,
		status.data().vbalance().v,
		!status.data().vnext_offset(),
		qs(status.data().vnext_offset().value_or_empty()),
	};
}

} // namespace

CreditsTopupOptions::CreditsTopupOptions(not_null<PeerData*> peer)
: _peer(peer)
, _api(&peer->session().api().instance()) {
}

rpl::producer<rpl::no_value, QString> CreditsTopupOptions::request() {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		// XP walk: took v5.3.0 refactor (optionsFromTL lambda + giftBarePeerId);
		// code below branches self topup vs gift options through optionsFromTL
		const auto giftBarePeerId = !_peer->isSelf() ? _peer->id.value : 0;

		const auto optionsFromTL = [giftBarePeerId](const auto &options) {
			return ranges::views::all(
				options
			) | ranges::views::transform([=](const auto &option) {
				return Data::CreditTopupOption{
					option.data().vstars().v,
					qs(
						option.data().vstore_product().value_or_empty()),
					qs(option.data().vcurrency()), // currency
					option.data().vamount().v, // amount
					option.data().is_extended(), // extended
					giftBarePeerId, // giftBarePeerId (v5.3.0)
				};
			}) | ranges::to_vector;
		};
		const auto fail = [=](const MTP::Error &error) {
			consumer.put_error_copy(error.type());
		};

		if (_peer->isSelf()) {
			using TLOption = MTPStarsTopupOption;
			_api.request(MTPpayments_GetStarsTopupOptions(
			)).done([=](const MTPVector<TLOption> &result) {
				_options = optionsFromTL(result.v);
				consumer.put_done();
			}).fail(fail).send();
		} else if (const auto user = _peer->asUser()) {
			using TLOption = MTPStarsGiftOption;
			_api.request(MTPpayments_GetStarsGiftOptions(
				MTP_flags(MTPpayments_GetStarsGiftOptions::Flag::f_user_id),
				user->inputUser
			)).done([=](const MTPVector<TLOption> &result) {
				_options = optionsFromTL(result.v);
				consumer.put_done();
			}).fail(fail).send();
		}

		return lifetime;
	};
}

CreditsStatus::CreditsStatus(not_null<PeerData*> peer)
: _peer(peer)
, _api(&peer->session().api().instance()) {
}

void CreditsStatus::request(
		const Data::CreditsStatusSlice::OffsetToken &token,
		Fn<void(Data::CreditsStatusSlice)> done) {
	if (_requestId) {
		return;
	}

	using TLResult = MTPpayments_StarsStatus;

	_requestId = _api.request(MTPpayments_GetStarsStatus(
		_peer->isSelf() ? MTP_inputPeerSelf() : _peer->input
	)).done([=](const TLResult &result) {
		_requestId = 0;
		done(StatusFromTL(result, _peer));
	}).fail([=] {
		_requestId = 0;
		done({});
	}).send();
}

CreditsHistory::CreditsHistory(not_null<PeerData*> peer, bool in, bool out)
: _peer(peer)
, _flags((in == out)
	? HistoryTL::Flags(0)
	: HistoryTL::Flags(0)
		| (in ? HistoryTL::Flag::f_inbound : HistoryTL::Flags(0))
		| (out ? HistoryTL::Flag::f_outbound : HistoryTL::Flags(0)))
, _api(&peer->session().api().instance()) {
}

void CreditsHistory::request(
		const Data::CreditsStatusSlice::OffsetToken &token,
		Fn<void(Data::CreditsStatusSlice)> done) {
	if (_requestId) {
		return;
	}
	_requestId = _api.request(MTPpayments_GetStarsTransactions(
		MTP_flags(_flags),
		_peer->isSelf() ? MTP_inputPeerSelf() : _peer->input,
		MTP_string(token),
		MTP_int(kTransactionsLimit)
	)).done([=](const MTPpayments_StarsStatus &result) {
		_requestId = 0;
		done(StatusFromTL(result, _peer));
	}).fail([=] {
		_requestId = 0;
		done({});
	}).send();
}

Data::CreditTopupOptions CreditsTopupOptions::options() const {
	return _options;
}

rpl::producer<not_null<PeerData*>> PremiumPeerBot(
		not_null<Main::Session*> session) {
	const auto username = session->appConfig().get<QString>(
		u"premium_bot_username"_q,
		QString());
	if (username.isEmpty()) {
		return rpl::never<not_null<PeerData*>>();
	}
	if (const auto p = session->data().peerByUsername(username)) {
		return rpl::single<not_null<PeerData*>>(p);
	}
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		const auto api = lifetime.make_state<MTP::Sender>(&session->mtp());

		api->request(MTPcontacts_ResolveUsername(
			MTP_string(username)
		)).done([=](const MTPcontacts_ResolvedPeer &result) {
			session->data().processUsers(result.data().vusers());
			session->data().processChats(result.data().vchats());
			const auto botPeer = session->data().peerLoaded(
				peerFromMTP(result.data().vpeer()));
			if (!botPeer) {
				return consumer.put_done();
			}
			consumer.put_next(not_null{ botPeer });
		}).send();

		return lifetime;
	};
}

CreditsEarnStatistics::CreditsEarnStatistics(not_null<PeerData*> peer)
: StatisticsRequestSender(peer)
, _isUser(peer->isUser()) {
}

rpl::producer<rpl::no_value, QString> CreditsEarnStatistics::request() {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		const auto finish = [=](const QString &url) {
			makeRequest(MTPpayments_GetStarsRevenueStats(
				MTP_flags(0),
				(_isUser ? user()->input : channel()->input)
			)).done([=](const MTPpayments_StarsRevenueStats &result) {
				const auto &data = result.data();
				const auto &status = data.vstatus().data();
				// XP walk: designated -> named-local (C7555; CreditsEarnInt
				// is uint64, so positional brace-init would narrow the
				// int64 balance values)
				auto stats = Data::CreditsEarnStatistics();
				stats.revenueGraph = StatisticalGraphFromTL(
					data.vrevenue_graph());
				stats.currentBalance = status.vcurrent_balance().v;
				stats.availableBalance = status.vavailable_balance().v;
				stats.overallRevenue = status.voverall_revenue().v;
				stats.usdRate = data.vusd_rate().v;
				stats.isWithdrawalEnabled = status.is_withdrawal_enabled();
				stats.nextWithdrawalAt = status.vnext_withdrawal_at()
					? base::unixtime::parse(
						status.vnext_withdrawal_at()->v)
					: QDateTime();
				stats.buyAdsUrl = url;
				_data = std::move(stats);

				consumer.put_done();
			}).fail([=](const MTP::Error &error) {
				consumer.put_error_copy(error.type());
			}).send();
		};

		makeRequest(
			MTPpayments_GetStarsRevenueAdsAccountUrl(
				(_isUser ? user()->input : channel()->input))
		).done([=](const MTPpayments_StarsRevenueAdsAccountUrl &result) {
			finish(qs(result.data().vurl()));
		}).fail([=](const MTP::Error &error) {
			finish({});
		}).send();

		return lifetime;
	};
}

Data::CreditsEarnStatistics CreditsEarnStatistics::data() const {
	return _data;
}

} // namespace Api
