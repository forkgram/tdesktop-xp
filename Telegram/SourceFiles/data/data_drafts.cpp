/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_drafts.h"

#include "api/api_text_entities.h"
#include "ui/widgets/fields/input_field.h"
#include "chat_helpers/message_field.h"
#include "history/history.h"
#include "history/history_widget.h"
#include "history/history_item_components.h"
#include "main/main_session.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "mainwidget.h"
#include "storage/localstorage.h"

namespace Data {

WebPageDraft WebPageDraft::FromItem(not_null<HistoryItem*> item) {
	const auto previewMedia = item->media();
	const auto previewPage = previewMedia
		? previewMedia->webpage()
		: nullptr;
	using PageFlag = MediaWebPageFlag;
	const auto previewFlags = previewMedia
		? previewMedia->webpageFlags()
		: PageFlag();
	return {
		// XP walk: designated -> positional (C7555)
		previewPage ? previewPage->id : 0, // id
		previewPage ? previewPage->url : QString(), // url
		!!(previewFlags & PageFlag::ForceLargeMedia), // forceLargeMedia
		!!(previewFlags & PageFlag::ForceSmallMedia), // forceSmallMedia
		item->invertMedia(), // invert
		!!(previewFlags & PageFlag::Manual), // manual
		!previewPage, // removed
	};
}

Draft::Draft(
	const TextWithTags &textWithTags,
	FullReplyTo reply,
	const MessageCursor &cursor,
	WebPageDraft webpage,
	mtpRequestId saveRequestId)
: textWithTags(textWithTags)
, reply(std::move(reply))
, cursor(cursor)
, webpage(webpage)
, saveRequestId(saveRequestId) {
}

Draft::Draft(
	not_null<const Ui::InputField*> field,
	FullReplyTo reply,
	WebPageDraft webpage,
	mtpRequestId saveRequestId)
: textWithTags(field->getTextWithTags())
, reply(std::move(reply))
, cursor(field)
, webpage(webpage) {
}

void ApplyPeerCloudDraft(
		not_null<Main::Session*> session,
		PeerId peerId,
		MsgId topicRootId,
		const MTPDdraftMessage &draft) {
	const auto history = session->data().history(peerId);
	const auto date = draft.vdate().v;
	if (history->skipCloudDraftUpdate(topicRootId, date)) {
		return;
	}
	const auto textWithTags = TextWithTags{
		qs(draft.vmessage()),
		TextUtilities::ConvertEntitiesToTextTags(
			Api::EntitiesFromMTP(
				session,
				draft.ventities().value_or_empty()))
	};
	auto replyTo = draft.vreply_to()
		? ReplyToFromMTP(history, *draft.vreply_to())
		: FullReplyTo();
	replyTo.topicRootId = topicRootId;
	auto webpage = WebPageDraft{
		// XP walk: designated -> positional (C7555)
		{}, // id
		{}, // url
		{}, // forceLargeMedia
		{}, // forceSmallMedia
		draft.is_invert_media(), // invert
		{}, // manual
		draft.is_no_webpage(), // removed
	};
	if (const auto media = draft.vmedia()) {
		media->match([&](const MTPDmessageMediaWebPage &data) {
			const auto parsed = session->data().processWebpage(
				data.vwebpage());
			if (!parsed->failed) {
				webpage.forceLargeMedia = data.is_force_large_media();
				webpage.forceSmallMedia = data.is_force_small_media();
				webpage.manual = data.is_manual();
				webpage.url = parsed->url;
				webpage.id = parsed->id;
			}
		}, [](const auto &) {});
	}
	auto cloudDraft = std::make_unique<Draft>(
		textWithTags,
		replyTo,
		MessageCursor(Ui::kQFixedMax, Ui::kQFixedMax, Ui::kQFixedMax),
		std::move(webpage));
	cloudDraft->date = date;

	history->setCloudDraft(std::move(cloudDraft));
	history->applyCloudDraft(topicRootId);
}

void ClearPeerCloudDraft(
		not_null<Main::Session*> session,
		PeerId peerId,
		MsgId topicRootId,
		TimeId date) {
	const auto history = session->data().history(peerId);
	if (history->skipCloudDraftUpdate(topicRootId, date)) {
		return;
	}

	history->clearCloudDraft(topicRootId);
	history->applyCloudDraft(topicRootId);
}

void SetChatLinkDraft(not_null<PeerData*> peer, TextWithEntities draft) {
	static const auto kInlineStart = QRegularExpression("^@[a-zA-Z0-9_]");
	if (kInlineStart.match(draft.text).hasMatch()) {
		draft = TextWithEntities().append(' ').append(std::move(draft));
	}

	const auto textWithTags = TextWithTags{
		draft.text,
		TextUtilities::ConvertEntitiesToTextTags(draft.entities)
	};
	const auto cursor = MessageCursor{
		int(textWithTags.text.size()),
		int(textWithTags.text.size()),
		Ui::kQFixedMax
	};
	const auto history = peer->owner().history(peer->id);
	const auto topicRootId = MsgId();
	history->setLocalDraft(std::make_unique<Data::Draft>(
		textWithTags,
		FullReplyTo{
			// XP walk: designated -> positional (C7555). FullReplyTo:
			// messageId, quote, storyId, topicRootId, quoteOffset
			{}, // messageId
			{}, // quote
			{}, // storyId
			topicRootId, // topicRootId
		},
		cursor,
		Data::WebPageDraft()));
	history->clearLocalEditDraft(topicRootId);
	history->session().changes().entryUpdated(
		history,
		Data::EntryUpdate::Flag::LocalDraftSet);
}

} // namespace Data
