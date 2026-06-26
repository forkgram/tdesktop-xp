# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

{
  'conditions': [[ 'build_win', {
    'defines': [
      'TDESKTOP_DISABLE_CRASH_REPORTS',
    ],
    'library_dirs': [
      '<(libs_loc)/ffmpeg',
    ],
    'libraries': [
      # 14.44-cl emits float/double->unsigned CRT helpers (__ftoul2/__dtoul3 and
      # their _legacy forms) absent from the linked 14.16 (v141_xp) CRT. fpcompat.lib
      # carries the 14.44 ftol2.obj+ftol3.obj (pure FP math, XP-safe); listed first
      # so the 14.16 CRT copies are never pulled.
      'C:/TBuild/xp-port/fpcompat/fpcompat.lib',
      '-lzlibstat',
      '-lLzmaLib',
      '-lUxTheme',
      '-lDbgHelp',
      '-lOpenAL32',
      '-lopus',
      '-lRstrtmgr',
      '-lWtsapi32', # Qt's qwindows plugin imports WTSQuerySessionInformationW/WTSFreeMemory.
    ],
    'msvs_settings': {
      'VCLinkerTool': {
        'AdditionalOptions': [
          'libavformat/libavformat.a',
          'libavcodec/libavcodec.a',
          'libavutil/libavutil.a',
          'libswresample/libswresample.a',
          'libswscale/libswscale.a',
          # fpcompat.lib + the 14.16 CRT both carry ftol2/ftol3 objects; the shared
          # symbols clash. Identical pure-FP math, so accept the dup (linker keeps
          # fpcompat's, listed first) -- the only multiply-defined set.
          '/FORCE:MULTIPLE',
          # XP thunk objects (linked directly, precede kernel32.lib): define the CRT's
          # __imp_ slots for the Vista+ APIs absent on XP (Fls*,
          # GetNumaHighestNodeNumber) so they are not imported at all.
          'C:/TBuild/xp-port/fpcompat/xpfls_c.obj',
          'C:/TBuild/xp-port/fpcompat/xpfls_asm.obj',
        ],
      },
      'VCManifestTool': {
        'AdditionalManifestFiles': '<(res_loc)/winrc/Telegram.manifest',
      }
    },
    'configurations': {
      'Debug': {
        'library_dirs': [
          '<(libs_loc)/lzma/C/Util/LzmaLib/Debug',
          '<(libs_loc)/opus/win32/VS2015/Win32/Debug',
          '<(libs_loc)/openal-soft/build/Debug',
          '<(libs_loc)/zlib/contrib/vstudio/vc14/x86/ZlibStatDebug',
        ],
      },
      'Release': {
        'library_dirs': [
          '<(libs_loc)/lzma/C/Util/LzmaLib/Release',
          '<(libs_loc)/opus/win32/VS2015/Win32/Release',
          '<(libs_loc)/openal-soft/build/Release',
          '<(libs_loc)/zlib/contrib/vstudio/vc14/x86/ZlibStatReleaseWithoutAsm',
        ],
      },
    },
  }], [ 'build_uwp', {
    'defines': [
      'TDESKTOP_DISABLE_AUTOUPDATE',
      'OS_WIN_STORE',
    ]
  }]],
}
