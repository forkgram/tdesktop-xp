/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <optional>

namespace Platform::XpSelfTest {

// XP walk: `Telegram.exe -xpselftest [report.txt]` runs the subsystems that have
// broken this port only at RUNTIME - FFmpeg registration and codec set, the opus
// encoder's sample format, rlottie, the static Qt image plugins, OpenAL - and
// returns an exit code instead of needing a human in front of the VM.
//
// Returns nullopt when the flag is absent, so main() proceeds normally.
// Exit codes: 0 everything passed (or was skipped), 1 a probe failed,
// 3 a probe hung and the watchdog killed the process.
[[nodiscard]] std::optional<int> MaybeRun(int argc, char *argv[]);

} // namespace Platform::XpSelfTest
