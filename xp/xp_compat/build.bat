@echo off
rem Build xp_compat.dll with v141_xp toolchain to match Telegram.exe.
setlocal enableextensions

set "VS_VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
call "%VS_VCVARS%" amd64_x86 -vcvars_ver=14.16
if errorlevel 1 (echo [ERROR] vcvarsall failed & exit /b 1)

set "MSVC_INC=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.16.27023\include"
set "MSVC_LIB=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.16.27023\lib\x86"
set "SDK71A_INC=C:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A\Include"
set "SDK71A_LIB=C:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A\Lib"

set "WK10_INC="
set "UCRT_INC="
set "UCRT_LIB="
set "WK10_UM_LIB="
for /f "tokens=*" %%I in ('dir /b /o-n "C:\Program Files (x86)\Windows Kits\10\Include" 2^>nul') do (
    if not defined UCRT_INC if exist "C:\Program Files (x86)\Windows Kits\10\Include\%%I\ucrt\stdio.h" (
        set "UCRT_INC=C:\Program Files (x86)\Windows Kits\10\Include\%%I\ucrt"
        set "WK10_INC=%%I"
    )
)
for /f "tokens=*" %%I in ('dir /b /o-n "C:\Program Files (x86)\Windows Kits\10\Lib" 2^>nul') do (
    if not defined UCRT_LIB if exist "C:\Program Files (x86)\Windows Kits\10\Lib\%%I\ucrt\x86\ucrt.lib" set "UCRT_LIB=C:\Program Files (x86)\Windows Kits\10\Lib\%%I\ucrt\x86"
    if not defined WK10_UM_LIB if exist "C:\Program Files (x86)\Windows Kits\10\Lib\%%I\um\x86\ntdll.lib" set "WK10_UM_LIB=C:\Program Files (x86)\Windows Kits\10\Lib\%%I\um\x86"
)

rem Use WK10 um/shared headers for modern SRWLOCK/INIT_ONCE typedefs that
rem SDK 7.1A doesn't have. Runtime stays XP since we call no Vista+ APIs.
set "INCLUDE=%MSVC_INC%;C:\Program Files (x86)\Windows Kits\10\Include\%WK10_INC%\um;C:\Program Files (x86)\Windows Kits\10\Include\%WK10_INC%\shared;%UCRT_INC%;%SDK71A_INC%"
set "LIB=%MSVC_LIB%;%SDK71A_LIB%;%WK10_UM_LIB%;%UCRT_LIB%"

cd /d "%~dp0"
del /q xp_compat.obj xp_compat.dll xp_compat.lib xp_compat.exp 2>nul

cl /nologo /c /MT /O2 /W3 /GS- /Gs9999999 ^
    /D_USING_V110_SDK71_=1 /D_WIN32_WINNT=0x0501 /DWINVER=0x0501 ^
    /DNTDDI_VERSION=0x05010300 ^
    xp_compat.c
if errorlevel 1 (echo [ERROR] cl failed & exit /b 1)

link /nologo /DLL /MACHINE:X86 /SUBSYSTEM:WINDOWS,5.01 ^
    /DEF:xp_compat.def ^
    /NODEFAULTLIB /ENTRY:DllMain ^
    /OUT:xp_compat.dll ^
    /IMPLIB:xp_compat.lib ^
    xp_compat.obj kernel32.lib
if errorlevel 1 (echo [ERROR] link failed & exit /b 1)
if errorlevel 1 (echo [ERROR] link failed & exit /b 1)

echo.
echo Built xp_compat.dll and xp_compat.lib successfully.
dir xp_compat.dll xp_compat.lib
