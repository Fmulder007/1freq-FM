@echo off
setlocal
set "AVRDUDE_DIR=%~f1"
cd /d "%~dp0"

rem Creates a flat portable application folder.
rem Usage: package.cmd "C:\path\to\AVRDUDE-7.3-x86"

if "%~1"=="" (
  echo Usage: package.cmd "path to the folder containing avrdude.exe and avrdude.conf"
  exit /b 1
)
if not exist "1Freq-FM7-Programmer.exe" (
  echo ERROR: build 1Freq-FM7-Programmer.exe first.
  exit /b 1
)
if not exist "%AVRDUDE_DIR%\avrdude.exe" (
  echo ERROR: avrdude.exe not found in "%~1".
  exit /b 1
)
if not exist "%AVRDUDE_DIR%\avrdude.conf" (
  echo ERROR: avrdude.conf not found in "%~1".
  exit /b 1
)
if not exist "..\Console\Firmware\ATmega328P.hex" (
  echo ERROR: the existing ATmega328P.hex was not found.
  exit /b 1
)
if not exist "eeprom.hex" (
  echo ERROR: Programmer\eeprom.hex was not found.
  exit /b 1
)

set "OUT=dist\1Freq-FM7-Programmer"
if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%OUT%"
if errorlevel 1 exit /b 1

copy /y "1Freq-FM7-Programmer.exe" "%OUT%\" >nul || exit /b 1
copy /y "%AVRDUDE_DIR%\avrdude.exe" "%OUT%\" >nul || exit /b 1
copy /y "%AVRDUDE_DIR%\avrdude.conf" "%OUT%\" >nul || exit /b 1
copy /y "..\Console\Firmware\ATmega328P.hex" "%OUT%\" >nul || exit /b 1
copy /y "eeprom.hex" "%OUT%\" >nul || exit /b 1
copy /y "README.txt" "%OUT%\" >nul || exit /b 1

rem Copy DLL dependencies from the selected AVRDUDE build, if present.
for %%F in ("%AVRDUDE_DIR%\*.dll") do if exist "%%~fF" copy /y "%%~fF" "%OUT%\" >nul

echo.
echo Package created: %OUT%
echo The package is flat: no Tools, Config, Firmware or Data subfolders.
endlocal
