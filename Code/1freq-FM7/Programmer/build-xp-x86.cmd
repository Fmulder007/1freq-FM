@echo off
setlocal
cd /d "%~dp0"

rem Uses the portable TDM-GCC toolchain stored beside this script.
rem TDM-GCC 10.3.0 TDM64; -m32 creates a 32-bit Windows executable.
set "CC=%~dp0toolchain\bin\gcc.exe"
set "RESULT=1"
set "PAUSE_AT_END=1"
if /I "%~1"=="--no-pause" set "PAUSE_AT_END=0"

if not exist "%CC%" (
  echo ERROR: portable GCC not found:
  echo "%CC%"
  echo Download and extract the complete Programmer bundle.
  goto finish
)

echo Using compiler:
"%CC%" --version
if errorlevel 1 (
  echo ERROR: GCC could not be started.
  goto finish
)

echo.
echo Building 32-bit Windows XP-compatible executable...
"%CC%" -m32 -std=c11 -O2 -Wall -Wextra -finput-charset=UTF-8 -fwide-exec-charset=UTF-16LE ^
  -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -DUNICODE -D_UNICODE ^
  -static -Wl,--subsystem,console:5.01 ^
  -o 1Freq-FM7-Programmer.exe 1Freq-FM7-Programmer.c
if errorlevel 1 (
  echo.
  echo ERROR: compilation failed. See the messages above.
  goto finish
)

echo.
echo BUILD OK: 1Freq-FM7-Programmer.exe
set "RESULT=0"

:finish
echo.
if "%PAUSE_AT_END%"=="1" pause
endlocal & exit /b %RESULT%
