/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_statistics.h"

#include "api/api_statistics_data_deserialize.h"
#include "apiwrap.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_story.h"
#include "history/history.h"
#include "main/main_session.h"

namespace Api {
namespace {

[[nodiscard]] Data::StatisticalValue StatisticalValueFromTL(
		const MTPStatsAbsValueAndPrev &tl) {
	const auto current = tl.data().vcurrent().v;
	const auto previous = tl.data().vprevious().v;
	return Data::StatisticalValue{
		current, // value
		previous, // previousValue
		previous // growthRatePercentage
			? std::abs((current - previous) / float64(previous) * 100.)
			: 0,
	};
}

[[nodiscard]] Data::ChannelStatistics ChannelStatisticsFromTL(
		const MTPDstats_broadcastStats &data) {
	const auto &tlUnmuted = data.venabled_notifications().data();
	const auto unmuted = (!tlUnmuted.vtotal().v)
		? 0.
		: std::clamp(
			tlUnmuted.vpart().v / tlUnmuted.vtotal().v * 100.,
			0.,
			100.);
	using Recent = MTPPostInteractionCounters;
	auto recentMessages = ranges::views::all(
		data.vrecent_posts_interactions().v
	) | ranges::views::transform([&](const Recent &tl) {
		// XP walk: designated -> positional (C7555). v4.12.0 splits the counters
		// into story/message variants. StatisticsMessageInteractionInfo order:
		// messageId, storyId, viewsCount, forwardsCount, reactionsCount.
		return tl.match([&](const MTPDpostInteractionCountersStory &data) {
			return Data::StatisticsMessageInteractionInfo{
				{}, // messageId
				data.vstory_id().v, // storyId
				data.vviews().v, // viewsCount
				data.vforwards().v, // forwardsCount
				data.vreactions().v, // reactionsCount
			};
		}, [&](const MTPDpostInteractionCountersMessage &data) {
			return Data::StatisticsMessageInteractionInfo{
				data.vmsg_id().v, // messageId
				{}, // storyId
				data.vviews().v, // viewsCount
				data.vforwards().v, // forwardsCount
				data.vreactions().v, // reactionsCount
			};
		});
	}) | ranges::to_vector;

	return {
		data.vperiod().data().vmin_date().v, // startDate
		data.vperiod().data().vmax_date().v, // endDate

		// XP walk: designated -> positional (C7555). v4.12.0 added meanReactionCount
		// and the three story mean counts (in ChannelStatistics declaration order).
		StatisticalValueFromTL(data.vfollowers()), // memberCount
		StatisticalValueFromTL(data.vviews_per_post()), // meanViewCount
		StatisticalValueFromTL(data.vshares_per_post()), // meanShareCount
		StatisticalValueFromTL(
			data.vreactions_per_post()), // meanReactionCount
		StatisticalValueFromTL(
			data.vviews_per_story()), // meanStoryViewCount
		StatisticalValueFromTL(
			data.vshares_per_story()), // meanStoryShareCount
		StatisticalValueFromTL(
			data.vreactions_per_story()), // meanStoryReactionCount

		unmuted, // enabledNotificationsPercentage

		StatisticalGraphFromTL(
			data.vgrowth_graph()), // memberCountGraph

		StatisticalGraphFromTL(
			data.vfollowers_graph()), // joinGraph

		StatisticalGraphFromTL(
			data.vmute_graph()), // muteGraph

		StatisticalGraphFromTL(
			data.vtop_hours_graph()), // viewCountByHourGraph

		StatisticalGraphFromTL(
			data.vviews_by_source_graph()), // viewCountBySourceGraph

		StatisticalGraphFromTL(
			data.vnew_followers_by_source_graph()), // joinBySourceGraph

		StatisticalGraphFromTL(
			data.vlanguages_graph()), // languageGraph

		StatisticalGraphFromTL(
			data.vinteractions_graph()), // messageInteractionGraph

		StatisticalGraphFromTL(
			data.viv_interactions_graph()), // instantViewInteractionGraph

		// XP walk: designated -> positional (C7555). v4.12.0 added the reactions /
		// story graphs before recentMessageInteractions (declaration order).
		StatisticalGraphFromTL(
			data.vreactions_by_emotion_graph()), // reactionsByEmotionGraph
		StatisticalGraphFromTL(
			data.vstory_interactions_graph()), // storyInteractionsGraph
		StatisticalGraphFromTL(
			data.vstory_reactions_by_emotion_graph()), // storyReactionsByEmotionGraph
		std::move(recentMessages), // recentMessageInteractions
	};
}

[[nodiscard]] Data::SupergroupStatistics SupergroupStatisticsFromTL(
		const MTPDstats_megagroupStats &data) {
	using Senders = MTPStatsGroupTopPoster;
	using Administrators = MTPStatsGroupTopAdmin;
	using Inviters = MTPStatsGroupTopInviter;

	auto topSenders = ranges::views::all(
		data.vtop_posters().v
	) | ranges::views::transform([&](const Senders &tl) {
		return Data::StatisticsMessageSenderInfo{
			UserId(tl.data().vuser_id().v), // userId
			tl.data().vmessages().v, // sentMessageCount
			tl.data().vavg_chars().v, // averageCharacterCount
		};
	}) | ranges::to_vector;
	auto topAdministrators = ranges::views::all(
		data.vtop_admins().v
	) | ranges::views::transform([&](const Administrators &tl) {
		return Data::StatisticsAdministratorActionsInfo{
			UserId(tl.data().vuser_id().v), // userId
			tl.data().vdeleted().v, // deletedMessageCount
			tl.data().vkicked().v, // bannedUserCount
			tl.data().vbanned().v, // restrictedUserCount
		};
	}) | ranges::to_vector;
	auto topInviters = ranges::views::all(
		data.vtop_inviters().v
	) | ranges::views::transform([&](const Inviters &tl) {
		return Data::StatisticsInviterInfo{
			UserId(tl.data().vuser_id().v), // userId
			tl.data().vinvitations().v, // addedMemberCount
		};
	}) | ranges::to_vector;

	return {
		data.vperiod().data().vmin_date().v, // startDate
		data.vperiod().data().vmax_date().v, // endDate

		StatisticalValueFromTL(data.vmembers()), // memberCount
		StatisticalValueFromTL(data.vmessages()), // messageCount
		StatisticalValueFromTL(data.vviewers()), // viewerCount
		StatisticalValueFromTL(data.vposters()), // senderCount

		StatisticalGraphFromTL(
			data.vgrowth_graph()), // memberCountGraph

		StatisticalGraphFromTL(
			data.vmembers_graph()), // joinGraph

		StatisticalGraphFromTL(
			data.vnew_members_by_source_graph()), // joinBySourceGraph

		StatisticalGraphFromTL(
			data.vlanguages_graph()), // languageGraph

		StatisticalGraphFromTL(
			data.vmessages_graph()), // messageContentGraph

		StatisticalGraphFromTL(
			data.vactions_graph()), // actionGraph

		StatisticalGraphFromTL(
			data.vtop_hours_graph()), // dayGraph

		StatisticalGraphFromTL(
			data.vweekdays_graph()), // weekGraph

		std::move(topSenders), // topSenders
		std::move(topAdministrators), // topAdministrators
		std::move(topInviters), // topInviters
	};
}

} // namespace

Statistics::Statistics(not_null<ChannelData*> channel)
: StatisticsRequestSender(channel) {
}

rpl::producer<rpl::no_value, QString> Statistics::request() {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		if (!channel()->isMegagroup()) {
			makeRequest(MTPstats_GetBroadcastStats(
				MTP_flags(MTPstats_GetBroadcastStats::Flags(0)),
				channel()->inputChannel
			)).done([=](const MTPstats_BroadcastStats &result) {
				_channelStats = ChannelStatisticsFromTL(result.data());
				consumer.put_done();
			}).fail([=](const MTP::Error &error) {
				consumer.put_error_copy(error.type());
			}).send();
		} else {
			makeRequest(MTPstats_GetMegagroupStats(
				MTP_flags(MTPstats_GetMegagroupStats::Flags(0)),
				channel()->inputChannel
			)).done([=](const MTPstats_MegagroupStats &result) {
				const auto &data = result.data();
				_supergroupStats = SupergroupStatisticsFromTL(data);
				channel()->owner().processUsers(data.vusers());
				consumer.put_done();
			}).fail([=](const MTP::Error &error) {
				consumer.put_error_copy(error.type());
			}).send();
		}

		return lifetime;
	};
}

Statistics::GraphResult Statistics::requestZoom(
		const QString &token,
		float64 x) {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();
		const auto wasEmpty = _zoomDeque.empty();
		_zoomDeque.push_back([=] {
			makeRequest(MTPstats_LoadAsyncGraph(
				MTP_flags(x
					? MTPstats_LoadAsyncGraph::Flag::f_x
					: MTPstats_LoadAsyncGraph::Flag(0)),
				MTP_string(token),
				MTP_long(x)
			)).done([=](const MTPStatsGraph &result) {
				consumer.put_next(StatisticalGraphFromTL(result));
				consumer.put_done();
				if (!_zoomDeque.empty()) {
					_zoomDeque.pop_front();
					if (!_zoomDeque.empty()) {
						_zoomDeque.front()();
					}
				}
			}).fail([=](const MTP::Error &error) {
				consumer.put_error_copy(error.type());
			}).send();
		});
		if (wasEmpty) {
			_zoomDeque.front()();
		}

		return lifetime;
	};
}

Data::ChannelStatistics Statistics::channelStats() const {
	return _channelStats;
}

Data::SupergroupStatistics Statistics::supergroupStats() const {
	return _supergroupStats;
}

PublicForwards::PublicForwards(
	not_null<ChannelData*> channel,
	Data::RecentPostId fullId)
: StatisticsRequestSender(channel)
, _fullId(fullId) {
}

void PublicForwards::request(
		const Data::PublicForwardsSlice::OffsetToken &token,
		Fn<void(Data::PublicForwardsSlice)> done) {
	if (_requestId) {
		return;
	}
	const auto channel = StatisticsRequestSender::channel();
	const auto processResult = [=](const MTPstats_PublicForwards &tl) {
		using Messages = QVector<Data::RecentPostId>;
		_requestId = 0;

		// XP walk: v4.13.0 unified request()/requestStory() into one
		// processResult that matches both message and story forwards.
		const auto &data = tl.data();
		auto &owner = channel->owner();

		owner.processUsers(data.vusers());
		owner.processChats(data.vchats());

		const auto nextToken = data.vnext_offset()
			? qs(*data.vnext_offset())
			: Data::PublicForwardsSlice::OffsetToken();

		// XP walk: v4.13.0 removed the separate requestStory(); both message
		// and story forwards now flow through this single processResult.
		const auto fullCount = data.vcount().v;

		auto recentList = Messages(data.vforwards().v.size());
		for (const auto &tlForward : data.vforwards().v) {
			tlForward.match([&](const MTPDpublicForwardMessage &data) {
				const auto &message = data.vmessage();
				const auto msgId = IdFromMessage(message);
				const auto peerId = PeerFromMessage(message);
				const auto lastDate = DateFromMessage(message);
				if (const auto peer = owner.peerLoaded(peerId)) {
					if (!lastDate) {
						return;
					}
					owner.addNewMessage(
						message,
						MessageFlags(),
						NewMessageType::Existing);
					// XP walk: designated -> positional (C7555). RecentPostId.
					recentList.push_back({ { peerId, msgId } }); // messageId
				}
			}, [&](const MTPDpublicForwardStory &data) {
				const auto story = owner.stories().applySingle(
					peerFromMTP(data.vpeer()),
					data.vstory());
				if (story) {
					// XP walk: designated -> positional (C7555). RecentPostId: messageId, storyId.
					recentList.push_back({ {}, story->fullId() }); // storyId
				}
			});
		}

		const auto allLoaded = nextToken.isEmpty() || (nextToken == token);
		_lastTotal = std::max(_lastTotal, fullCount);
		// XP walk: designated -> positional (C7555). PublicForwardsSlice:
		// list, total, allLoaded, token.
		done({
			std::move(recentList), // list
			_lastTotal, // total
			allLoaded, // allLoaded
			nextToken, // token
		});
	};

	constexpr auto kLimit = tl::make_int(100);
	if (_fullId.messageId) {
		_requestId = makeRequest(MTPstats_GetMessagePublicForwards(
			channel->inputChannel,
			MTP_int(_fullId.messageId.msg),
			MTP_string(token),
			kLimit
		)).done(processResult).fail([=] { _requestId = 0; }).send();
	} else if (_fullId.storyId) {
		_requestId = makeRequest(MTPstats_GetStoryPublicForwards(
			channel->input,
			MTP_int(_fullId.storyId.story),
			MTP_string(token),
			kLimit
		)).done(processResult).fail([=] { _requestId = 0; }).send();
	}
}

MessageStatistics::MessageStatistics(
	not_null<ChannelData*> channel,
	FullMsgId fullId)
: StatisticsRequestSender(channel)
, _publicForwards(channel, { fullId }) // XP walk: RecentPostId::messageId
, _fullId(fullId) {
}

MessageStatistics::MessageStatistics(
	not_null<ChannelData*> channel,
	FullStoryId storyId)
: StatisticsRequestSender(channel)
, _publicForwards(channel, { {}, storyId }) // XP walk: messageId gap, storyId
, _storyId(storyId) {
}

Data::PublicForwardsSlice MessageStatistics::firstSlice() const {
	return _firstSlice;
}

void MessageStatistics::request(Fn<void(Data::MessageStatistics)> done) {
	if (channel()->isMegagroup()) {
		return;
	}
	const auto requestFirstPublicForwards = [=](
			const Data::StatisticalGraph &messageGraph,
			const Data::StatisticalGraph &reactionsGraph,
			const Data::StatisticsMessageInteractionInfo &info) {
		const auto callback = [=](Data::PublicForwardsSlice slice) {
			const auto total = slice.total;
			_firstSlice = std::move(slice);
			done({
				// XP walk: designated -> positional (C7555). MessageStatistics:
				// messageInteractionGraph, reactionsByEmotionGraph, publicForwards,
				// privateForwards, views, reactions. v4.12.0 added the reactions
				// graph + reactions count.
				messageGraph, // messageInteractionGraph
				reactionsGraph, // reactionsByEmotionGraph
				total, // publicForwards
				info.forwardsCount - total, // privateForwards
				info.viewsCount, // views
				info.reactionsCount, // reactions
			});
		};
		_publicForwards.request({}, callback);
	};

	const auto requestPrivateForwards = [=](
			const Data::StatisticalGraph &messageGraph,
			const Data::StatisticalGraph &reactionsGraph) {
		api().request(MTPchannels_GetMessages(
			channel()->inputChannel,
			MTP_vector<MTPInputMessage>(
				1,
				MTP_inputMessageID(MTP_int(_fullId.msg))))
		).done([=](const MTPmessages_Messages &result) {
			const auto process = [&](const MTPVector<MTPMessage> &messages) {
				const auto &message = messages.v.front();
				return message.match([&](const MTPDmessage &data) {
					auto reactionsCount = 0;
					if (const auto tlReactions = data.vreactions()) {
						const auto &tlCounts = tlReactions->data().vresults();
						for (const auto &tlCount : tlCounts.v) {
							reactionsCount += tlCount.data().vcount().v;
						}
					}
					return Data::StatisticsMessageInteractionInfo{
						// XP walk: designated -> positional (C7555).
						// StatisticsMessageInteractionInfo: messageId, storyId,
						// viewsCount, forwardsCount, reactionsCount. v4.12.0 added
						// storyId (gap here) + reactionsCount.
						IdFromMessage(message), // messageId
						{}, // storyId
						data.vviews()
							? data.vviews()->v
							: 0, // viewsCount
						data.vforwards()
							? data.vforwards()->v
							: 0, // forwardsCount
						reactionsCount, // reactionsCount
					};
				}, [](const MTPDmessageEmpty &) {
					return Data::StatisticsMessageInteractionInfo();
				}, [](const MTPDmessageService &) {
					return Data::StatisticsMessageInteractionInfo();
				});
			};

			auto info = result.match([&](const MTPDmessages_messages &data) {
				return process(data.vmessages());
			}, [&](const MTPDmessages_messagesSlice &data) {
				return process(data.vmessages());
			}, [&](const MTPDmessages_channelMessages &data) {
				return process(data.vmessages());
			}, [](const MTPDmessages_messagesNotModified &) {
				return Data::StatisticsMessageInteractionInfo();
			});

			requestFirstPublicForwards(
				messageGraph,
				reactionsGraph,
				std::move(info));
		}).fail([=](const MTP::Error &error) {
			requestFirstPublicForwards(messageGraph, reactionsGraph, {});
		}).send();
	};

	const auto requestStoryPrivateForwards = [=](
			const Data::StatisticalGraph &messageGraph,
			const Data::StatisticalGraph &reactionsGraph) {
		api().request(MTPstories_GetStoriesByID(
			channel()->input,
			MTP_vector<MTPint>(1, MTP_int(_storyId.story)))
		).done([=](const MTPstories_Stories &result) {
			const auto &storyItem = result.data().vstories().v.front();
			auto info = storyItem.match([&](const MTPDstoryItem &data) {
				if (!data.vviews()) {
					return Data::StatisticsMessageInteractionInfo();
				}
				const auto &tlViews = data.vviews()->data();
				// XP walk: designated -> positional (C7555).
				// StatisticsMessageInteractionInfo: messageId, storyId, viewsCount,
				// forwardsCount, reactionsCount. messageId (1) gap.
				return Data::StatisticsMessageInteractionInfo{
					{}, // messageId
					data.vid().v, // storyId
					tlViews.vviews_count().v, // viewsCount
					tlViews.vforwards_count().value_or(0), // forwardsCount
					tlViews.vreactions_count().value_or(0), // reactionsCount
				};
			}, [](const auto &) {
				return Data::StatisticsMessageInteractionInfo();
			});

			requestFirstPublicForwards(
				messageGraph,
				reactionsGraph,
				std::move(info));
		}).fail([=](const MTP::Error &error) {
			requestFirstPublicForwards(messageGraph, reactionsGraph, {});
		}).send();
	};

	if (_storyId) {
		makeRequest(MTPstats_GetStoryStats(
			MTP_flags(MTPstats_GetStoryStats::Flags(0)),
			channel()->input,
			MTP_int(_storyId.story)
		)).done([=](const MTPstats_StoryStats &result) {
			const auto &data = result.data();
			requestStoryPrivateForwards(
				StatisticalGraphFromTL(data.vviews_graph()),
				StatisticalGraphFromTL(data.vreactions_by_emotion_graph()));
		}).fail([=](const MTP::Error &error) {
			requestStoryPrivateForwards({}, {});
		}).send();
	} else {
		makeRequest(MTPstats_GetMessageStats(
			MTP_flags(MTPstats_GetMessageStats::Flags(0)),
			channel()->inputChannel,
			MTP_int(_fullId.msg.bare)
		)).done([=](const MTPstats_MessageStats &result) {
			const auto &data = result.data();
			requestPrivateForwards(
				StatisticalGraphFromTL(data.vviews_graph()),
				StatisticalGraphFromTL(data.vreactions_by_emotion_graph()));
		}).fail([=](const MTP::Error &error) {
			requestPrivateForwards({}, {});
		}).send();
	}
}

Boosts::Boosts(not_null<PeerData*> peer)
: _peer(peer)
, _api(&peer->session().api().instance()) {
}

rpl::producer<rpl::no_value, QString> Boosts::request() {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();
		const auto channel = _peer->asChannel();
		if (!channel) {
			return lifetime;
		}

		_api.request(MTPpremium_GetBoostsStatus(
			_peer->input
		)).done([=](const MTPpremium_BoostsStatus &result) {
			const auto &data = result.data();
			channel->updateLevelHint(data.vlevel().v);
			const auto hasPremium = !!data.vpremium_audience();
			const auto premiumMemberCount = hasPremium
				? std::max(0, int(data.vpremium_audience()->data().vpart().v))
				: 0;
			const auto participantCount = hasPremium
				? std::max(
					int(data.vpremium_audience()->data().vtotal().v),
					premiumMemberCount)
				: 0;
			const auto premiumMemberPercentage = (participantCount > 0)
				? (100. * premiumMemberCount / participantCount)
				: 0;

			const auto slots = data.vmy_boost_slots();
			_boostStatus.overview = Data::BoostsOverview{
				// XP walk: designated -> positional (C7555). BoostsOverview order:
				// group, mine, level, boostCount, currentLevelBoostCount,
				// nextLevelBoostCount, premiumMemberCount, premiumMemberPercentage.
				channel->isMegagroup(), // group
				slots ? int(slots->v.size()) : 0, // mine
				std::max(data.vlevel().v, 0), // level
				std::max(
					data.vboosts().v,
					data.vcurrent_level_boosts().v), // boostCount
				data.vcurrent_level_boosts().v, // currentLevelBoostCount
				data.vnext_level_boosts() // nextLevelBoostCount
					? data.vnext_level_boosts()->v
					: 0,
				premiumMemberCount, // premiumMemberCount
				premiumMemberPercentage, // premiumMemberPercentage
			};
			_boostStatus.link = qs(data.vboost_url());

			if (data.vprepaid_giveaways()) {
				_boostStatus.prepaidGiveaway = ranges::views::all(
					data.vprepaid_giveaways()->v
				) | ranges::views::transform([](const MTPPrepaidGiveaway &r) {
					return r.match([&](const MTPDprepaidGiveaway &data) {
						// XP walk: designated -> named-local (C7555).
						auto result = Data::BoostPrepaidGiveaway();
						result.date = base::unixtime::parse(data.vdate().v);
						result.id = data.vid().v;
						result.months = data.vmonths().v;
						result.quantity = data.vquantity().v;
						return result;
					}, [&](const MTPDprepaidStarsGiveaway &data) {
						// XP walk: designated -> named-local (C7555).
						auto result = Data::BoostPrepaidGiveaway();
						result.date = base::unixtime::parse(data.vdate().v);
						result.id = data.vid().v;
						result.credits = data.vstars().v;
						result.quantity = data.vquantity().v;
						result.boosts = data.vboosts().v;
						return result;
					});
				}) | ranges::to_vector;
			}

			using namespace Data;
			// XP walk: designated -> positional (C7555)
			requestBoosts({ {}, false }, [=](BoostsListSlice &&slice) {
				_boostStatus.firstSliceBoosts = std::move(slice);
				// XP walk: designated -> positional (C7555)
				requestBoosts({ {}, true }, [=](BoostsListSlice &&s) {
					_boostStatus.firstSliceGifts = std::move(s);
					consumer.put_done();
				});
			});
		}).fail([=](const MTP::Error &error) {
			consumer.put_error_copy(error.type());
		}).send();

		return lifetime;
	};
}

void Boosts::requestBoosts(
		const Data::BoostsListSlice::OffsetToken &token,
		Fn<void(Data::BoostsListSlice)> done) {
	if (_requestId) {
		return;
	}
	constexpr auto kTlFirstSlice = tl::make_int(kFirstSlice);
	constexpr auto kTlLimit = tl::make_int(kLimit);
	const auto gifts = token.gifts;
	_requestId = _api.request(MTPpremium_GetBoostsList(
		gifts
			? MTP_flags(MTPpremium_GetBoostsList::Flag::f_gifts)
			: MTP_flags(0),
		_peer->input,
		MTP_string(token.next),
		token.next.isEmpty() ? kTlFirstSlice : kTlLimit
	)).done([=](const MTPpremium_BoostsList &result) {
		_requestId = 0;

		const auto &data = result.data();
		_peer->owner().processUsers(data.vusers());

		auto list = std::vector<Data::Boost>();
		list.reserve(data.vboosts().v.size());
		constexpr auto kMonthsDivider = int(30 * 86400);
		for (const auto &boost : data.vboosts().v) {
			const auto &data = boost.data();
			const auto path = data.vused_gift_slug()
				? (u"giftcode/"_q + qs(data.vused_gift_slug()->v))
				: QString();
			auto giftCodeLink = !path.isEmpty()
				? Data::GiftCodeLink{
					_peer->session().createInternalLink(path),
					_peer->session().createInternalLinkFull(path),
					qs(data.vused_gift_slug()->v),
				}
				: Data::GiftCodeLink();
			list.push_back({
				// XP walk: designated -> positional (C7555).
				qs(data.vid()), // id
				UserId(data.vuser_id().value_or_empty()), // userId
				data.vgiveaway_msg_id()
					? FullMsgId{ _peer->id, data.vgiveaway_msg_id()->v }
					: FullMsgId(), // giveawayMessage
				base::unixtime::parse(data.vdate().v), // date
				base::unixtime::parse(data.vexpires().v), // expiresAt
				((data.vexpires().v - data.vdate().v)
					/ kMonthsDivider), // expiresAfterMonths
				std::move(giftCodeLink), // giftCodeLink
				data.vmultiplier().value_or_empty(), // multiplier
				data.vstars().value_or_empty(), // credits
				data.is_gift(), // isGift
				data.is_giveaway(), // isGiveaway
				data.is_unclaimed(), // isUnclaimed
			});
		}
		done(Data::BoostsListSlice{
			// XP walk: designated -> positional (C7555)
			std::move(list), // list
			data.vcount().v, // multipliedTotal
			(data.vcount().v == data.vboosts().v.size()), // allLoaded
			Data::BoostsListSlice::OffsetToken{
				data.vnext_offset()
					? qs(*data.vnext_offset())
					: QString(), // next
				gifts, // gifts -- XP walk: v4.11.4 added OffsetToken::gifts
			}, // token
		});
	}).fail([=] {
		_requestId = 0;
	}).send();
}

Data::BoostStatus Boosts::boostStatus() const {
	return _boostStatus;
}

ChannelEarnStatistics::ChannelEarnStatistics(not_null<ChannelData*> channel)
: StatisticsRequestSender(channel) {
}

rpl::producer<rpl::no_value, QString> ChannelEarnStatistics::request() {
	return [=](auto consumer) {
		auto lifetime = rpl::lifetime();

		makeRequest(MTPstats_GetBroadcastRevenueStats(
			MTP_flags(0),
			channel()->inputChannel
		)).done([=](const MTPstats_BroadcastRevenueStats &result) {
			const auto &data = result.data();
			const auto &balances = data.vbalances().data();
			_data = Data::EarnStatistics{ // XP walk: designated -> positional (C7555)
				StatisticalGraphFromTL(
					data.vtop_hours_graph()), // topHoursGraph
				StatisticalGraphFromTL(data.vrevenue_graph()), // revenueGraph
				balances.vcurrent_balance().v, // currentBalance
				balances.vavailable_balance().v, // availableBalance
				balances.voverall_revenue().v, // overallRevenue
				data.vusd_rate().v, // usdRate
			};

			requestHistory({}, [=](Data::EarnHistorySlice &&slice) {
				_data.firstHistorySlice = std::move(slice);

				api().request(
					MTPchannels_GetFullChannel(channel()->inputChannel)
				).done([=](const MTPmessages_ChatFull &result) {
					result.data().vfull_chat().match([&](
							const MTPDchannelFull &d) {
						_data.switchedOff = d.is_restricted_sponsored();
					}, [](const auto &) {
					});
					consumer.put_done();
				}).fail([=](const MTP::Error &error) {
					consumer.put_error_copy(error.type());
				}).send();
			});
		}).fail([=](const MTP::Error &error) {
			consumer.put_error_copy(error.type());
		}).send();

		return lifetime;
	};
}

void ChannelEarnStatistics::requestHistory(
		const Data::EarnHistorySlice::OffsetToken &token,
		Fn<void(Data::EarnHistorySlice)> done) {
	if (_requestId) {
		return;
	}
	constexpr auto kTlFirstSlice = tl::make_int(kFirstSlice);
	constexpr auto kTlLimit = tl::make_int(kLimit);
	_requestId = api().request(MTPstats_GetBroadcastRevenueTransactions(
		channel()->inputChannel,
		MTP_int(token),
		(!token) ? kTlFirstSlice : kTlLimit
	)).done([=](const MTPstats_BroadcastRevenueTransactions &result) {
		_requestId = 0;

		const auto &tlTransactions = result.data().vtransactions().v;

		auto list = std::vector<Data::EarnHistoryEntry>();
		list.reserve(tlTransactions.size());
		for (const auto &tlTransaction : tlTransactions) {
			list.push_back(tlTransaction.match([&](
					const MTPDbroadcastRevenueTransactionProceeds &d) {
				return Data::EarnHistoryEntry{ // XP walk: designated -> positional (C7555)
					Data::EarnHistoryEntry::Type::In, // type
					{}, // status
					d.vamount().v, // amount
					base::unixtime::parse(d.vfrom_date().v), // date
					base::unixtime::parse(d.vto_date().v), // dateTo
				};
			}, [&](const MTPDbroadcastRevenueTransactionWithdrawal &d) {
				return Data::EarnHistoryEntry{ // XP walk: designated -> positional (C7555)
					Data::EarnHistoryEntry::Type::Out, // type
					d.is_pending()
						? Data::EarnHistoryEntry::Status::Pending
						: d.is_failed()
						? Data::EarnHistoryEntry::Status::Failed
						: Data::EarnHistoryEntry::Status::Success, // status
					(std::numeric_limits<Data::EarnInt>::max()
						- d.vamount().v
						+ 1), // amount
					base::unixtime::parse(d.vdate().v), // date
					{}, // dateTo
					{}, // provider (was: qs(d.vprovider()))
					d.vtransaction_date()
						? base::unixtime::parse(d.vtransaction_date()->v)
						: QDateTime(), // successDate
					d.vtransaction_url()
						? qs(*d.vtransaction_url())
						: QString(), // successLink
				};
			}, [&](const MTPDbroadcastRevenueTransactionRefund &d) {
				return Data::EarnHistoryEntry{ // XP walk: designated -> positional (C7555)
					Data::EarnHistoryEntry::Type::Return, // type
					{}, // status
					d.vamount().v, // amount
					base::unixtime::parse(d.vdate().v), // date
					// provider omitted (was: qs(d.vprovider()))
				};
			}));
		}
		const auto nextToken = token + tlTransactions.size();
		done(Data::EarnHistorySlice{ // XP walk: designated -> positional (C7555)
			std::move(list), // list
			result.data().vcount().v, // total
			(result.data().vcount().v == nextToken), // allLoaded
			Data::EarnHistorySlice::OffsetToken(nextToken), // token
		});
	}).fail([=] {
		done({});
		_requestId = 0;
	}).send();
}

Data::EarnStatistics ChannelEarnStatistics::data() const {
	return _data;
}

} // namespace Api
