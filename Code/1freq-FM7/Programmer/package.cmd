@echo off
setlocal
cd /d "%~dp0"

set "RESULT=1"
set "PAUSE_AT_END=1"
if /I "%~1"=="--no-pause" set "PAUSE_AT_END=0"

if not exist "1Freq-FM7-Programmer.exe" (
  echo ERROR: 1Freq-FM7-Programmer.exe not found. Run build-xp-x86.cmd first.
  goto finish
)
if not exist "avrdude.exe" (
  echo ERROR: avrdude.exe not found beside the program.
  goto finish
)
if not exist "avrdude.conf" (
  echo ERROR: avrdude.conf not found beside the program.
  goto finish
)
if not exist "ATmega328P.hex" (
  echo ERROR: ATmega328P.hex not found.
  goto finish
)
if not exist "eeprom.hex" (
  echo ERROR: eeprom.hex not found.
  goto finish
)

set "OUT=dist\1Freq-FM7-Programmer"
if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%OUT%"
if errorlevel 1 (
  echo ERROR: could not create "%OUT%".
  goto finish
)

copy /y "1Freq-FM7-Programmer.exe" "%OUT%\" >nul || goto finish
copy /y "avrdude.exe" "%OUT%\" >nul || goto finish
copy /y "avrdude.conf" "%OUT%\" >nul || goto finish
copy /y "ATmega328P.hex" "%OUT%\" >nul || goto finish
copy /y "eeprom.hex" "%OUT%\" >nul || goto finish
copy /y "README.txt" "%OUT%\" >nul || goto finish

rem Copy AVRDUDE runtime DLLs, if the selected build needs any.
for %%F in ("*.dll") do if exist "%%~fF" copy /y "%%~fF" "%OUT%\" >nul

echo.
echo PORTABLE PACKAGE CREATED:
echo %OUT%
echo All runtime files are in one flat folder.
set "RESULT=0"

:finish
echo.
if "%PAUSE_AT_END%"=="1" pause
endlocal & exit /b %RESULT%
