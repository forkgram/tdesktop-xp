/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/platform/win/base_windows_h.h"

namespace Platform {
namespace AppUserModelId {

void CleanupShortcut();
void CheckPinned();

[[nodiscard]] const std::wstring &Id();
bool ValidateShortcut();

[[nodiscard]] const PROPERTYKEY &Key();

[[nodiscard]] const std::wstring &MyExecutablePath();

struct UniqueFileId {
	std::uint64_t part1 = 0;
	std::uint64_t part2 = 0;

	[[nodiscard]] bool valid() const {
		return part1 || part2;
	}
	[[nodiscard]] explicit operator bool() const {
		return valid();
	}

	// XP walk: defaulted <=>/== need C++20; the code only uses == and != on
	// UniqueFileId, so provide those explicitly (C++17 has no operator<=>).
	[[nodiscard]] friend inline bool operator==(
			UniqueFileId a,
			UniqueFileId b) {
		return (a.part1 == b.part1) && (a.part2 == b.part2);
	}
	[[nodiscard]] friend inline bool operator!=(
			UniqueFileId a,
			UniqueFileId b) {
		return !(a == b);
	}
};

[[nodiscard]] UniqueFileId GetUniqueFileId(LPCWSTR path);
[[nodiscard]] UniqueFileId MyExecutablePathId();

} // namespace AppUserModelId
} // namespace Platform
