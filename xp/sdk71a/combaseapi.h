#pragma once
// XP walk shim. Modern code includes <combaseapi.h> (the Win8+ split of objbase.h);
// SDK 7.1A has no combaseapi.h, so the include falls through to the Win10 SDK copy
// (reached via the Win10 ucrt INCLUDE entry + "..\\um"), which then fails under
// /Zc:preprocessor (C1012) and pulls the conflicting Win10 wtypesbase.h. Redirect
// to SDK 7.1A's objbase.h, which provides the same classic COM base for XP.
#include <objbase.h>
