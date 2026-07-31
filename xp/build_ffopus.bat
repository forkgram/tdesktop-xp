@echo off
rem Build the FFmpeg inventory / voice-capture harness (see ffopus_xp.cpp).
rem Run it through the v141_xp environment wrapper, e.g.
rem   powershell C:\TBuild\xp-port\cmake_xp.ps1 cmd /c <repo>\xp\build_ffopus.bat
rem Override the library locations if your checkout lives elsewhere:
rem   set XP_LIBS_DIR=D:\somewhere\Libraries-walk
rem   set XP_FPCOMPAT=D:\somewhere\fpcompat
setlocal
if "%XP_LIBS_DIR%"=="" set "XP_LIBS_DIR=C:\TBuild\xp-port\Libraries-walk"
if "%XP_FPCOMPAT%"=="" set "XP_FPCOMPAT=C:\TBuild\xp-port\fpcompat"
set "FF=%XP_LIBS_DIR%\ffmpeg"
set "OUTDIR=%~dp0"
cl /nologo /MT /EHsc /D_USING_V110_SDK71_ /I%FF% "%~dp0ffopus_xp.cpp" ^
  %FF%\libavfilter\libavfilter.a %FF%\libavformat\libavformat.a %FF%\libavcodec\libavcodec.a ^
  %FF%\libswresample\libswresample.a %FF%\libswscale\libswscale.a ^
  %FF%\libavutil\libavutil.a ^
  "%XP_LIBS_DIR%\opus\out\Release\opus.lib" ^
  "%XP_FPCOMPAT%\fpcompat.lib" ^
  ws2_32.lib secur32.lib bcrypt.lib user32.lib ole32.lib strmiids.lib uuid.lib psapi.lib advapi32.lib shell32.lib ^
  /link /FORCE:MULTIPLE /OUT:"%OUTDIR%ffopus_xp.exe"
endlocal
