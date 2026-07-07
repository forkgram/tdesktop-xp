/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {

struct StarsRating {
	int level = 0;
	int stars = 0;
	int thisLevelStars = 0;
	int nextLevelStars = 0;

	explicit operator bool() const {
		return level != 0 || thisLevelStars != 0;
	}

	// XP walk: defaulted == (C7589) -> manual ==/!=.
	friend inline bool operator==(StarsRating a, StarsRating b) {
		return (a.level == b.level)
			&& (a.stars == b.stars)
			&& (a.thisLevelStars == b.thisLevelStars)
			&& (a.nextLevelStars == b.nextLevelStars);
	}
	friend inline bool operator!=(StarsRating a, StarsRating b) {
		return !(a == b);
	}
};

struct StarsRatingPending {
	StarsRating value;
	TimeId date = 0;

	explicit operator bool() const {
		return value && date;
	}
};

} // namespace Data
