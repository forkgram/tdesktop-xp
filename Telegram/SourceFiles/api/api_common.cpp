/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_common.h"

#include "base/qt/qt_key_modifiers.h"
#include "data/data_histories.h"
#include "data/data_thread.h"
#include "history/history.h"

namespace Api {

MTPSuggestedPost SuggestToMTP(SuggestOptions suggest) {
	using Flag = MTPDsuggestedPost::Flag;
	return suggest.exists
		? MTP_suggestedPost(
			MTP_flags((suggest.date ? Flag::f_schedule_date : Flag())
				| (suggest.price().empty() ? Flag() : Flag::f_price)),
			StarsAmountToTL(suggest.price()),
			MTP_int(suggest.date))
		: MTPSuggestedPost();
}

SendAction::SendAction(
	not_null<Data::Thread*> thread,
	SendOptions options)
: history(thread->owningHistory())
, options(options)
// XP walk: designated -> positional (C7555); FullReplyTo.messageId is field 1
, replyTo({ { history->peer->id, thread->topicRootId() } }) {
	replyTo.topicRootId = replyTo.messageId.msg;
}

SendOptions DefaultSendWhenOnlineOptions() {
	return {
		{}, // price (XP walk: v5.2.0 inserted uint64 price@0)
		{}, // sendAs
		kScheduledUntilOnlineTimestamp, // scheduled
		{}, // shortcutId
		{}, // effectId (XP walk: inserted EffectId@4, before silent@5)
		base::IsCtrlPressed(), // silent
	};
}

MTPInputReplyTo SendAction::mtpReplyTo() const {
	return Data::ReplyToForMTP(history, replyTo);
}

} // namespace Api
