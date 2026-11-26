@echo off
setlocal
cd /d "%~dp0"

rem Build a native 32-bit console executable for Windows XP SP3 and newer.
rem Requires MinGW-w64 i686 GCC with the MSVCRT runtime target.

set "CC=i686-w64-mingw32-gcc"
%CC% --version >nul 2>nul
if errorlevel 1 (
  echo ERROR: i686-w64-mingw32-gcc was not found in PATH.
  echo Install a 32-bit MinGW-w64 compiler targeting MSVCRT, then run this script again.
  exit /b 1
)

%CC% -std=c11 -O2 -Wall -Wextra -finput-charset=UTF-8 -fwide-exec-charset=UTF-16LE ^
  -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -DUNICODE -D_UNICODE ^
  -static -Wl,--subsystem,console:5.01 ^
  -o 1Freq-FM7-Programmer.exe 1Freq-FM7-Programmer.c
if errorlevel 1 exit /b 1

echo.
echo Build OK: 1Freq-FM7-Programmer.exe
endlocal
