/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "ui/text/text_entity.h"

#include <QtCore/QByteArray>

#include <memory>
#include <optional>
#include <vector>

class DocumentData;
class PhotoData;
class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Iv {

struct RichPage {
	struct RichText {
		TextWithEntities text;
		QString anchorId;
		std::vector<QString> anchorIds;

		[[nodiscard]] friend inline bool operator==(const RichText &a, const RichText &b) {
			return (a.text == b.text)
				&& (a.anchorId == b.anchorId)
				&& (a.anchorIds == b.anchorIds);
		}
		[[nodiscard]] friend inline bool operator!=(const RichText &a, const RichText &b) {
			return !(a == b);
		}
	};
	enum class BlockKind : uchar {
		Unsupported,
		Heading,
		Paragraph,
		Footer,
		Thinking,
		AuthorDate,
		Code,
		Divider,
		Anchor,
		List,
		Quote,
		Photo,
		Video,
		Embed,
		EmbedPost,
		GroupedMedia,
		Channel,
		Audio,
		Math,
		Table,
		Details,
		RelatedArticles,
		Map,
	};
	enum class ListKind : uchar {
		Bullet,
		Ordered,
	};
	enum class TaskState : uchar {
		None,
		Unchecked,
		Checked,
	};
	struct OrderedListData {
		bool reversed = false;
		std::optional<int> start;
		std::optional<QString> type;

		[[nodiscard]] friend inline bool operator==(const OrderedListData &a, const OrderedListData &b) {
			return (a.reversed == b.reversed)
				&& (a.start == b.start)
				&& (a.type == b.type);
		}
		[[nodiscard]] friend inline bool operator!=(const OrderedListData &a, const OrderedListData &b) {
			return !(a == b);
		}
	};
	struct OrderedListItemData {
		std::optional<QString> num;
		std::optional<int> value;
		std::optional<QString> type;

		[[nodiscard]] bool isEmpty() const {
			return !num.has_value() || num->isEmpty();
		}
		[[nodiscard]] bool hasRawText() const {
			return num.has_value() && !num->isEmpty();
		}
		[[nodiscard]] QString rawText() const {
			return num.value_or(QString());
		}
		operator QString() const {
			return rawText();
		}

		[[nodiscard]] friend inline bool operator==(const OrderedListItemData &a, const OrderedListItemData &b) {
			return (a.num == b.num)
				&& (a.value == b.value)
				&& (a.type == b.type);
		}
		[[nodiscard]] friend inline bool operator!=(const OrderedListItemData &a, const OrderedListItemData &b) {
			return !(a == b);
		}
	};
	enum class GroupedMediaIntent : uchar {
		Collage,
		Slideshow,
	};
	enum class TableAlignment : uchar {
		Left,
		Center,
		Right,
	};
	enum class TableVerticalAlignment : uchar {
		Top,
		Middle,
		Bottom,
	};
	struct Block;
	struct ListItem {
		TaskState taskState = TaskState::None;
		OrderedListItemData number;
		QString anchorId;
		RichText text;
		std::vector<Block> blocks;

		[[nodiscard]] friend inline bool operator==(const ListItem &a, const ListItem &b) {
			return (a.taskState == b.taskState)
				&& (a.number == b.number)
				&& (a.anchorId == b.anchorId)
				&& (a.text == b.text)
				&& (a.blocks == b.blocks);
		}
		[[nodiscard]] friend inline bool operator!=(const ListItem &a, const ListItem &b) {
			return !(a == b);
		}
	};
	struct GroupedMediaItem {
		BlockKind kind = BlockKind::Unsupported;
		PhotoData *photo = nullptr;
		DocumentData *document = nullptr;
		uint64 photoId = 0;
		uint64 documentId = 0;
		int width = 0;
		int height = 0;
		bool autoplay = false;
		bool loop = false;
		bool spoiler = false;

		[[nodiscard]] friend inline bool operator==(const GroupedMediaItem &a, const GroupedMediaItem &b) {
			return (a.kind == b.kind)
				&& (a.photo == b.photo)
				&& (a.document == b.document)
				&& (a.photoId == b.photoId)
				&& (a.documentId == b.documentId)
				&& (a.width == b.width)
				&& (a.height == b.height)
				&& (a.autoplay == b.autoplay)
				&& (a.loop == b.loop)
				&& (a.spoiler == b.spoiler);
		}
		[[nodiscard]] friend inline bool operator!=(const GroupedMediaItem &a, const GroupedMediaItem &b) {
			return !(a == b);
		}
	};
	struct TableCell {
		RichText text;
		int colspan = 1;
		int rowspan = 1;
		bool header = false;
		TableAlignment alignment = TableAlignment::Left;
		TableVerticalAlignment verticalAlignment
			= TableVerticalAlignment::Top;

		[[nodiscard]] friend inline bool operator==(const TableCell &a, const TableCell &b) {
			return (a.text == b.text)
				&& (a.colspan == b.colspan)
				&& (a.rowspan == b.rowspan)
				&& (a.header == b.header)
				&& (a.alignment == b.alignment)
				&& (a.verticalAlignment == b.verticalAlignment);
		}
		[[nodiscard]] friend inline bool operator!=(const TableCell &a, const TableCell &b) {
			return !(a == b);
		}
	};
	struct TableRow {
		std::vector<TableCell> cells;

		[[nodiscard]] friend inline bool operator==(const TableRow &a, const TableRow &b) {
			return (a.cells == b.cells);
		}
		[[nodiscard]] friend inline bool operator!=(const TableRow &a, const TableRow &b) {
			return !(a == b);
		}
	};
	struct RelatedArticle {
		QString url;
		uint64 webpageId = 0;
		PhotoData *photo = nullptr;
		uint64 photoId = 0;
		QString title;
		QString description;
		QString author;
		TimeId publishedDate = 0;

		[[nodiscard]] friend inline bool operator==(const RelatedArticle &a, const RelatedArticle &b) {
			return (a.url == b.url)
				&& (a.webpageId == b.webpageId)
				&& (a.photo == b.photo)
				&& (a.photoId == b.photoId)
				&& (a.title == b.title)
				&& (a.description == b.description)
				&& (a.author == b.author)
				&& (a.publishedDate == b.publishedDate);
		}
		[[nodiscard]] friend inline bool operator!=(const RelatedArticle &a, const RelatedArticle &b) {
			return !(a == b);
		}
	};
	struct Block {
		BlockKind kind = BlockKind::Unsupported;
		QString anchorId;
		RichText text;
		RichText caption;
		QString language;
		QString formula;
		QString url;
		QByteArray html;
		QString author;
		QString username;
		QString channelTitle;
		QString audioTitle;
		QString audioPerformer;
		QString audioFileName;
		TimeId date = 0;
		int audioDuration = 0;
		int headingLevel = 0;
		int width = 0;
		int height = 0;
		int zoom = 0;
		uint64 photoId = 0;
		uint64 documentId = 0;
		uint64 channelId = 0;
		bool fullWidth = false;
		bool fixedHeight = false;
		bool allowScrolling = false;
		bool autoplay = false;
		bool loop = false;
		bool spoiler = false;
		bool open = false;
		bool bordered = false;
		bool striped = false;
		bool pullquote = false;
		ListKind listKind = ListKind::Bullet;
		OrderedListData orderedList;
		GroupedMediaIntent mediaIntent = GroupedMediaIntent::Collage;
		PhotoData *photo = nullptr;
		DocumentData *document = nullptr;
		PeerData *peer = nullptr;
		float64 latitude = 0.;
		float64 longitude = 0.;
		uint64 accessHash = 0;
		std::vector<Block> blocks;
		std::vector<ListItem> listItems;
		std::vector<GroupedMediaItem> mediaItems;
		std::vector<TableRow> tableRows;
		std::vector<RelatedArticle> relatedArticles;

		[[nodiscard]] friend inline bool operator==(const Block &a, const Block &b) {
			return (a.kind == b.kind)
				&& (a.anchorId == b.anchorId)
				&& (a.text == b.text)
				&& (a.caption == b.caption)
				&& (a.language == b.language)
				&& (a.formula == b.formula)
				&& (a.url == b.url)
				&& (a.html == b.html)
				&& (a.author == b.author)
				&& (a.username == b.username)
				&& (a.channelTitle == b.channelTitle)
				&& (a.audioTitle == b.audioTitle)
				&& (a.audioPerformer == b.audioPerformer)
				&& (a.audioFileName == b.audioFileName)
				&& (a.date == b.date)
				&& (a.audioDuration == b.audioDuration)
				&& (a.headingLevel == b.headingLevel)
				&& (a.width == b.width)
				&& (a.height == b.height)
				&& (a.zoom == b.zoom)
				&& (a.photoId == b.photoId)
				&& (a.documentId == b.documentId)
				&& (a.channelId == b.channelId)
				&& (a.fullWidth == b.fullWidth)
				&& (a.fixedHeight == b.fixedHeight)
				&& (a.allowScrolling == b.allowScrolling)
				&& (a.autoplay == b.autoplay)
				&& (a.loop == b.loop)
				&& (a.spoiler == b.spoiler)
				&& (a.open == b.open)
				&& (a.bordered == b.bordered)
				&& (a.striped == b.striped)
				&& (a.pullquote == b.pullquote)
				&& (a.listKind == b.listKind)
				&& (a.orderedList == b.orderedList)
				&& (a.mediaIntent == b.mediaIntent)
				&& (a.photo == b.photo)
				&& (a.document == b.document)
				&& (a.peer == b.peer)
				&& (a.latitude == b.latitude)
				&& (a.longitude == b.longitude)
				&& (a.accessHash == b.accessHash)
				&& (a.blocks == b.blocks)
				&& (a.listItems == b.listItems)
				&& (a.mediaItems == b.mediaItems)
				&& (a.tableRows == b.tableRows)
				&& (a.relatedArticles == b.relatedArticles);
		}
		[[nodiscard]] friend inline bool operator!=(const Block &a, const Block &b) {
			return !(a == b);
		}
	};
	QString url;
	bool rtl = false;
	bool part = false;
	int views = 0;
	std::vector<Block> blocks;

	[[nodiscard]] friend inline bool operator==(const RichPage &a, const RichPage &b) {
		return (a.url == b.url)
			&& (a.rtl == b.rtl)
			&& (a.part == b.part)
			&& (a.views == b.views)
			&& (a.blocks == b.blocks);
	}
	[[nodiscard]] friend inline bool operator!=(const RichPage &a, const RichPage &b) {
		return !(a == b);
	}
};

struct RichMessageLimits {
	int lengthLimit = 32768;
	int maxBlocks = 500;
	int maxDepth = 16;
	int maxMedia = 50;
	int maxTableCols = 20;
};

enum class RichMessageLimitError : unsigned char {
	Length,
	Blocks,
	Depth,
	Media,
	TableColumns,
};

struct RichPageLinkUrl {
	QString url;
	uint64 webpageId = 0;
};

[[nodiscard]] RichMessageLimits ResolveRichMessageLimits(
	not_null<Main::Session*> session);
[[nodiscard]] bool RichPagesEqual(
	const RichPage &a,
	const RichPage &b);
[[nodiscard]] std::optional<RichMessageLimitError> ValidateRichMessage(
	const RichPage &page,
	const RichMessageLimits &limits);
[[nodiscard]] QString EncodeRichPageLinkUrl(
	const QString &url,
	uint64 webpageId);
[[nodiscard]] std::optional<RichPageLinkUrl> DecodeRichPageLinkUrl(
	const QString &data);
[[nodiscard]] std::shared_ptr<const RichPage> ParseRichPage(
	not_null<Main::Session*> session,
	const MTPRichMessage &message);
[[nodiscard]] std::shared_ptr<const RichPage> ParseRichPage(
	not_null<Main::Session*> session,
	const MTPPage &page);
[[nodiscard]] std::shared_ptr<const RichPage> ParseRichPage(
	not_null<Main::Session*> session,
	const MTPDwebPage &webpage);
[[nodiscard]] std::optional<TextWithEntities> SerializeAsSimple(
	const RichPage &page);
[[nodiscard]] bool RichPageUsesPremiumFormatting(const RichPage &page);
[[nodiscard]] bool RichPageIsFlattenSafe(const RichPage &page);
[[nodiscard]] RichPage SplitTextIntoRichPage(TextWithEntities text);
[[nodiscard]] TextWithEntities FlattenRichPageSummary(
	const RichPage &page,
	bool emptyFallback = true);
[[nodiscard]] TextWithEntities FlattenRichPageSummary(
	const std::shared_ptr<const RichPage> &page,
	bool emptyFallback = true);
[[nodiscard]] TextWithEntities FlattenRichPageToSimpleText(
	const RichPage &page);

} // namespace Iv
