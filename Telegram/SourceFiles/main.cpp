/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/launcher.h"

#ifdef _WIN32
#include "platform/win/xp_selftest_win.h"
#endif // _WIN32

int main(int argc, char *argv[]) {
#ifdef _WIN32
	// XP walk: -xpselftest exercises the subsystems that historically broke only
	// after the build was already on the VM (FFmpeg registration, the opus
	// encoder's sample format, rlottie, the static image plugins) and returns an
	// exit code. It runs BEFORE the launcher, so it needs no window, no account
	// and no working directory - and therefore runs on the CI runner too.
	if (const auto code = Platform::XpSelfTest::MaybeRun(argc, argv)) {
		return *code;
	}
#endif // _WIN32
	const auto launcher = Core::Launcher::Create(argc, argv);
	return launcher ? launcher->exec() : 1;
}
