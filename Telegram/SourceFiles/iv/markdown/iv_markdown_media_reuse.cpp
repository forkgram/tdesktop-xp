/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_media_reuse.h"

#include <cstring>
#include <optional>
#include <utility>

namespace Iv::Markdown {
namespace {

struct MediaReuseGroupedItem {
	PreparedMediaItemKind kind = PreparedMediaItemKind::Photo;
	uint64 backingId = 0;
	int width = 0;
	int height = 0;
	bool spoiler = false;

	friend inline bool operator==(
			const MediaReuseGroupedItem &a,
			const MediaReuseGroupedItem &b) {
		return (a.kind == b.kind)
			&& (a.backingId == b.backingId)
			&& (a.width == b.width)
			&& (a.height == b.height)
			&& (a.spoiler == b.spoiler);
	}
};

struct MediaReuseKey {
	PreparedBlockKind kind = PreparedBlockKind::Paragraph;
	PreparedMediaItemKind mediaKind = PreparedMediaItemKind::Photo;
	PreparedGroupedMediaIntent groupedIntent
		= PreparedGroupedMediaIntent::Collage;
	uint64 backingId = 0;
	uint64 accessHash = 0;
	int width = 0;
	int height = 0;
	int zoom = 0;
	int duration = 0;
	double latitude = 0.;
	double longitude = 0.;
	QString title;
	QString performer;
	QString fileName;
	QString username;
	QString url;
	QString urlOverride;
	bool viewerOpen = false;
	bool spoiler = false;
	std::vector<MediaReuseGroupedItem> groupedItems;

	friend inline bool operator==(
			const MediaReuseKey &a,
			const MediaReuseKey &b) {
		return (a.kind == b.kind)
			&& (a.mediaKind == b.mediaKind)
			&& (a.groupedIntent == b.groupedIntent)
			&& (a.backingId == b.backingId)
			&& (a.accessHash == b.accessHash)
			&& (a.width == b.width)
			&& (a.height == b.height)
			&& (a.zoom == b.zoom)
			&& (a.duration == b.duration)
			&& (a.latitude == b.latitude)
			&& (a.longitude == b.longitude)
			&& (a.title == b.title)
			&& (a.performer == b.performer)
			&& (a.fileName == b.fileName)
			&& (a.username == b.username)
			&& (a.url == b.url)
			&& (a.urlOverride == b.urlOverride)
			&& (a.viewerOpen == b.viewerOpen)
			&& (a.spoiler == b.spoiler)
			&& (a.groupedItems == b.groupedItems);
	}
};

void MediaReuseHashCombine(size_t *result, size_t value) {
	*result = (*result * 1315423911U) ^ value;
}

template <typename Value>
void MediaReuseHashCombine(size_t *result, const Value &value) {
	MediaReuseHashCombine(result, size_t(qHash(value)));
}

[[nodiscard]] uint64 MediaReuseDoubleBits(double value) {
	if (value == 0.) {
		return 0;
	}
	auto result = uint64(0);
	std::memcpy(&result, &value, sizeof(result));
	return result;
}

struct MediaReuseKeyHasher {
	[[nodiscard]] size_t operator()(
			const MediaReuseKey &key) const noexcept;
};

size_t MediaReuseKeyHasher::operator()(
		const MediaReuseKey &key) const noexcept {
	auto result = size_t(0);
	MediaReuseHashCombine(&result, int(key.kind));
	MediaReuseHashCombine(&result, int(key.mediaKind));
	MediaReuseHashCombine(&result, int(key.groupedIntent));
	MediaReuseHashCombine(&result, key.backingId);
	MediaReuseHashCombine(&result, key.accessHash);
	MediaReuseHashCombine(&result, key.width);
	MediaReuseHashCombine(&result, key.height);
	MediaReuseHashCombine(&result, key.zoom);
	MediaReuseHashCombine(&result, key.duration);
	MediaReuseHashCombine(&result, MediaReuseDoubleBits(key.latitude));
	MediaReuseHashCombine(&result, MediaReuseDoubleBits(key.longitude));
	MediaReuseHashCombine(&result, key.title);
	MediaReuseHashCombine(&result, key.performer);
	MediaReuseHashCombine(&result, key.fileName);
	MediaReuseHashCombine(&result, key.username);
	MediaReuseHashCombine(&result, key.url);
	MediaReuseHashCombine(&result, key.urlOverride);
	MediaReuseHashCombine(&result, int(key.viewerOpen));
	MediaReuseHashCombine(&result, int(key.spoiler));
	for (const auto &item : key.groupedItems) {
		MediaReuseHashCombine(&result, int(item.kind));
		MediaReuseHashCombine(&result, item.backingId);
		MediaReuseHashCombine(&result, item.width);
		MediaReuseHashCombine(&result, item.height);
		MediaReuseHashCombine(&result, int(item.spoiler));
	}
	return result;
}

using MediaBlockReusePool = std::unordered_map<
	MediaReuseKey,
	std::vector<std::shared_ptr<MediaBlock>>,
	MediaReuseKeyHasher>;

[[nodiscard]] MediaReuseGroupedItem MediaReuseGroupedItemForPreparedMedia(
		const PreparedMediaItemData &media) {
	return {
		media.kind, // kind
		media.id, // backingId
		media.width, // width
		media.height, // height
		media.spoiler, // spoiler
	};
}

[[nodiscard]] PreparedMediaBlockId MediaIdForPreparedBlock(
		const PreparedBlock &block) {
	switch (block.kind) {
	case PreparedBlockKind::Photo:
		return block.photo.id;
	case PreparedBlockKind::Video:
		return block.video.id;
	case PreparedBlockKind::Audio:
		return block.audio.id;
	case PreparedBlockKind::Map:
		return block.map.id;
	case PreparedBlockKind::Channel:
		return block.channel.id;
	case PreparedBlockKind::GroupedMedia:
		return block.groupedMedia.id;
	default:
		return {};
	}
}

[[nodiscard]] std::optional<MediaReuseKey> MediaKeyForPreparedBlock(
		const PreparedBlock &block) {
	switch (block.kind) {
	case PreparedBlockKind::Photo:
		return MediaReuseKey{
			block.kind, // kind
			PreparedMediaItemKind::Photo, // mediaKind
			PreparedGroupedMediaIntent::Collage, // groupedIntent
			block.photo.photoId, // backingId
			0, // accessHash
			block.photo.width, // width
			block.photo.height, // height
			0, // zoom
			0, // duration
			0., // latitude
			0., // longitude
			QString(), // title
			QString(), // performer
			QString(), // fileName
			QString(), // username
			QString(), // url
			block.photo.urlOverride, // urlOverride
			block.photo.viewerOpen, // viewerOpen
			block.photo.spoiler, // spoiler
		};
	case PreparedBlockKind::Video:
		return MediaReuseKey{
			block.kind, // kind
			block.video.media.kind, // mediaKind
			PreparedGroupedMediaIntent::Collage, // groupedIntent
			block.video.media.id, // backingId
			0, // accessHash
			block.video.media.width, // width
			block.video.media.height, // height
			0, // zoom
			0, // duration
			0., // latitude
			0., // longitude
			QString(), // title
			QString(), // performer
			QString(), // fileName
			QString(), // username
			QString(), // url
			QString(), // urlOverride
			false, // viewerOpen
			block.video.media.spoiler, // spoiler
		};
	case PreparedBlockKind::Audio:
		return MediaReuseKey{
			block.kind, // kind
			PreparedMediaItemKind::Photo, // mediaKind
			PreparedGroupedMediaIntent::Collage, // groupedIntent
			block.audio.documentId, // backingId
			0, // accessHash
			0, // width
			0, // height
			0, // zoom
			block.audio.duration, // duration
			0., // latitude
			0., // longitude
			block.audio.title, // title
			block.audio.performer, // performer
			block.audio.fileName, // fileName
		};
	case PreparedBlockKind::Map:
		return MediaReuseKey{
			block.kind, // kind
			PreparedMediaItemKind::Photo, // mediaKind
			PreparedGroupedMediaIntent::Collage, // groupedIntent
			0, // backingId
			block.map.accessHash, // accessHash
			block.map.width, // width
			block.map.height, // height
			block.map.zoom, // zoom
			0, // duration
			block.map.latitude, // latitude
			block.map.longitude, // longitude
			QString(), // title
			QString(), // performer
			QString(), // fileName
			QString(), // username
			block.map.url, // url
		};
	case PreparedBlockKind::Channel:
		return MediaReuseKey{
			block.kind, // kind
			PreparedMediaItemKind::Photo, // mediaKind
			PreparedGroupedMediaIntent::Collage, // groupedIntent
			block.channel.channelId, // backingId
			0, // accessHash
			0, // width
			0, // height
			0, // zoom
			0, // duration
			0., // latitude
			0., // longitude
			block.channel.title, // title
			QString(), // performer
			QString(), // fileName
			block.channel.username, // username
		};
	case PreparedBlockKind::GroupedMedia: {
		auto groupedItems = std::vector<MediaReuseGroupedItem>();
		groupedItems.reserve(block.groupedMedia.items.size());
		for (const auto &item : block.groupedMedia.items) {
			groupedItems.push_back(
				MediaReuseGroupedItemForPreparedMedia(item.media));
		}
		return MediaReuseKey{
			block.kind, // kind
			PreparedMediaItemKind::Photo, // mediaKind
			block.groupedMedia.intent, // groupedIntent
			0, // backingId
			0, // accessHash
			0, // width
			0, // height
			0, // zoom
			0, // duration
			0., // latitude
			0., // longitude
			QString(), // title
			QString(), // performer
			QString(), // fileName
			QString(), // username
			QString(), // url
			QString(), // urlOverride
			false, // viewerOpen
			false, // spoiler
			std::move(groupedItems), // groupedItems
		};
	}
	default:
		return std::nullopt;
	}
}

void ClearMediaBlockReusePool(MediaBlockReusePool *pool) {
	if (!pool) {
		return;
	}
	for (const auto &entry : *pool) {
		for (const auto &block : entry.second) {
			if (block) {
				block->setHost(nullptr);
			}
		}
	}
	pool->clear();
}

void CollectOldMediaBlocksForReuse(
		const std::vector<PreparedBlock> &blocks,
		MediaBlockStorage *oldBlocks,
		MediaBlockReusePool *pool) {
	if (!oldBlocks || !pool) {
		return;
	}
	for (const auto &block : blocks) {
		if (const auto id = MediaIdForPreparedBlock(block); id) {
			if (const auto key = MediaKeyForPreparedBlock(block)) {
				if (const auto i = oldBlocks->find(id.value);
					i != end(*oldBlocks)) {
					if (i->second) {
						(*pool)[*key].push_back(std::move(i->second));
					}
					oldBlocks->erase(i);
				}
			}
		}
		CollectOldMediaBlocksForReuse(block.children, oldBlocks, pool);
	}
}

void CollectReusedMediaBlocks(
		const std::vector<PreparedBlock> &blocks,
		MediaBlockReusePool *pool,
		MediaBlockStorage *reusedBlocks) {
	if (!pool || !reusedBlocks) {
		return;
	}
	for (const auto &block : blocks) {
		if (const auto id = MediaIdForPreparedBlock(block); id) {
			if (const auto key = MediaKeyForPreparedBlock(block)) {
				if (auto i = pool->find(*key); i != end(*pool)) {
					if (!i->second.empty()) {
						auto reused = std::move(i->second.front());
						i->second.erase(begin(i->second));
						if (reused) {
							reusedBlocks->emplace(id.value, std::move(reused));
						}
					}
					if (i->second.empty()) {
						pool->erase(i);
					}
				}
			}
		}
		CollectReusedMediaBlocks(block.children, pool, reusedBlocks);
	}
}

} // namespace

void ClearMediaBlockStorage(MediaBlockStorage *blocks) {
	if (!blocks) {
		return;
	}
	for (const auto &entry : *blocks) {
		if (entry.second) {
			entry.second->setHost(nullptr);
		}
	}
	blocks->clear();
}

MediaBlockStorage ReuseMediaBlocks(
		const std::vector<PreparedBlock> &oldPreparedBlocks,
		MediaBlockStorage *oldMediaBlocks,
		const std::vector<PreparedBlock> &newPreparedBlocks) {
	auto pool = MediaBlockReusePool();
	auto result = MediaBlockStorage();
	CollectOldMediaBlocksForReuse(
		oldPreparedBlocks,
		oldMediaBlocks,
		&pool);
	CollectReusedMediaBlocks(
		newPreparedBlocks,
		&pool,
		&result);
	ClearMediaBlockStorage(oldMediaBlocks);
	ClearMediaBlockReusePool(&pool);
	return result;
}

} // namespace Iv::Markdown
