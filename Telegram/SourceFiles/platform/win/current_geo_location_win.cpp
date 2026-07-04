/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/current_geo_location_win.h"

#include "core/current_geo_location.h"

// XP walk: resolving the current location via winrt Windows.Devices.Geolocation is
// Win8+ -- its runtime pulls RoOriginateLanguageException (unresolved on XP, LNK2019).
// Stub both entry points: no current location / address is available on the XP build.

namespace Platform {

void ResolveCurrentExactLocation(Fn<void(Core::GeoLocation)> callback) {
	callback({});
}

void ResolveLocationAddress(
		const Core::GeoLocation &location,
		const QString &language,
		Fn<void(Core::GeoAddress)> callback) {
	callback({});
}

} // namespace Platform
