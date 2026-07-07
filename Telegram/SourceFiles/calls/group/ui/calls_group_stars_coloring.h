/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Calls::Group::Ui {

using namespace ::Ui;

struct StarsColoring {
	int bgLight = 0;
	int bgDark = 0;
	int fromStars = 0;
	TimeId secondsPin = 0;
	int charactersMax = 0;
	int emojiLimit = 0;

	// XP walk: defaulted <=>/== (C++20) -> manual ==/!=/< over the 6 fields.
	friend inline bool operator==(
			const StarsColoring &a,
			const StarsColoring &b) {
		return (a.bgLight == b.bgLight) && (a.bgDark == b.bgDark)
			&& (a.fromStars == b.fromStars) && (a.secondsPin == b.secondsPin)
			&& (a.charactersMax == b.charactersMax) && (a.emojiLimit == b.emojiLimit);
	}
	friend inline bool operator!=(
			const StarsColoring &a,
			const StarsColoring &b) {
		return !(a == b);
	}
	friend inline bool operator<(
			const StarsColoring &a,
			const StarsColoring &b) {
		if (a.bgLight != b.bgLight) return a.bgLight < b.bgLight;
		if (a.bgDark != b.bgDark) return a.bgDark < b.bgDark;
		if (a.fromStars != b.fromStars) return a.fromStars < b.fromStars;
		if (a.secondsPin != b.secondsPin) return a.secondsPin < b.secondsPin;
		if (a.charactersMax != b.charactersMax) return a.charactersMax < b.charactersMax;
		return a.emojiLimit < b.emojiLimit;
	}
};

[[nodiscard]] StarsColoring StarsColoringForCount(
	const std::vector<StarsColoring> &colorings,
	int stars);

[[nodiscard]] int StarsRequiredForMessage(
	const std::vector<StarsColoring> &colorings,
	const TextWithTags &text);

[[nodiscard]] object_ptr<Ui::RpWidget> VideoStreamStarsLevel(
	not_null<Ui::RpWidget*> box,
	const std::vector<StarsColoring> &colorings,
	rpl::producer<int> starsValue);

} // namespace Calls::Group::Ui
