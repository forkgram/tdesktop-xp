/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "iv/markdown/iv_markdown_parse.h"
#include "iv/markdown/iv_markdown_parse_finalize.h"
#include "iv/markdown/iv_markdown_parse_validate.h"

#include <utility>

namespace Iv::Markdown {

const MarkdownParseLimits &ParseLimitsForIv() {
	static const auto result = MarkdownParseLimits{
		4 * 1024 * 1024, // maxSourceBytes
		100000, // maxCmarkNodes
		128, // maxNesting
		64 * 1024, // maxFormulaBytes
		10000, // maxFormulaCount
	};
	return result;
}

ParseResult ParseMarkdownForIv(const QByteArray &source, ParseOptions options) {
	auto validated = ValidateMarkdownSourceForIv(source, std::move(options));
	return validated.ok
		? ParseMarkdownForIv(std::move(validated.source))
		: Failure(
			std::move(validated.source.sourceName),
			std::move(validated.error));
}

} // namespace Iv::Markdown
