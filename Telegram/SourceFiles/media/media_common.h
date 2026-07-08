/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/algorithm.h"

namespace Media {

enum class RepeatMode {
	None,
	One,
	All,
};

enum class OrderMode {
	Default,
	Reverse,
	Shuffle,
};

struct VideoQuality {
	// XP walk: bit-fields dropped (C7582); took theirs (+original).
	uint32 manual = 0;
	uint32 height = 0;
	uint32 original = 0;

	// XP walk: defaulted <=>/== (C++20, C7589) -> manual ==, !=, <.
	friend inline constexpr bool operator==(
			VideoQuality a,
			VideoQuality b) {
		return (a.manual == b.manual) && (a.height == b.height);
	}
	friend inline constexpr bool operator!=(
			VideoQuality a,
			VideoQuality b) {
		return !(a == b);
	}
	friend inline constexpr bool operator<(
			VideoQuality a,
			VideoQuality b) {
		return (a.manual != b.manual)
			? (a.manual < b.manual)
			: (a.height < b.height);
	}
};

inline constexpr auto kSpeedMin = 0.5;
inline constexpr auto kSpeedMax = 2.5;
inline constexpr auto kSpedUpDefault = 1.7;

[[nodiscard]] inline bool ValidFrameSize(int w, int h, int maxArea) {
	return (w > 0)
		&& (h > 0)
		&& (int64_t(w) * h <= int64_t(maxArea));
}

[[nodiscard]] inline bool ValidFrameSize(QSize size, int maxArea) {
	return ValidFrameSize(size.width(), size.height(), maxArea);
}

[[nodiscard]] inline bool EqualSpeeds(float64 a, float64 b) {
	return int(base::SafeRound(a * 10.)) == int(base::SafeRound(b * 10.));
}

} // namespace Media
