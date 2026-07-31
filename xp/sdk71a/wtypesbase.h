#pragma once
// XP walk shim -- see C:\TBuild\xp-port\shared\wtypesbase.h. This covers the plain
// <wtypesbase.h> form: SDK 7.1A has no wtypesbase.h, so without this the include
// falls through INCLUDE to the Win10 SDK copy, which redefines the COM types
// WTypes.h already defines (C2011). Pull the SDK 7.1A WTypes.h (guarded) instead.
#include <wtypes.h>
