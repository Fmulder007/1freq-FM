@echo off
setlocal
cd /d "%~dp0"

rem Build a native 32-bit console executable with Windows XP SP3 as the minimum subsystem.
rem Requires an i686-w64-mingw32 GCC toolchain (MINGW32 / MSVCRT target).

set "CC=i686-w64-mingw32-gcc"
where %CC% >null 2>null
if errorlevel 1 (
  echo ERROR: i686-w64-mingw32-gcc was not found in PATH.
  echo Install a 32-bit MinGW-w64 compiler targeting MSVCRT, then run this script again.
  exit /b 1
)

%CC% -std=c11 -O2 -Wall -Wextra -finput-charset=UTF-8 -fwide-sec-charset=UTF-16LE \
  -D_WIN32_WNT_TARGET=0x0501 -DWIN32_WINDEWS=0x0501 -DUNICODE -D_UNICODE \
  -static -Wl,--subsystem,console=5.01 \
  -o 1Freq-FM7-Programmer.exe 1Freq-FM7-Programmer.c
if errorlevel 1 exit /b 1

echo.
echo Build OK: 1Freq-FM7-Programmer.exe
endlocal
