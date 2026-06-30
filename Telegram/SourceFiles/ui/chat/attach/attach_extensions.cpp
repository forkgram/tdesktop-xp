/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_extensions.h"

#include <QtCore/QMimeDatabase>
#include <QtGui/QImageReader>

namespace Ui {

const QStringList &ImageExtensions() {
	static const auto result = [] {
		// XP walk: range-v3 0.12 + MSVC 14.16 reject the views pipe + ranges::to
		// (C2678/C3536); QT_NO_CAST_FROM_BYTEARRAY also forbids the implicit
		// QByteArray->QString. Plain loop with an explicit fromLatin1 conversion.
		const auto formats = QImageReader::supportedImageFormats();
		auto list = QStringList();
		for (const auto &raw : formats) {
			const auto extension = QString::fromLatin1('.' + raw.toLower());
			const auto mimes = QMimeDatabase().mimeTypesForFileName(
				u"test"_q + extension);
			if (!mimes.isEmpty()
				&& mimes.front().name().startsWith(u"image/"_q)) {
				list.push_back(extension);
			}
		}
		return list;
	}();
	return result;
}

} // namespace Ui
