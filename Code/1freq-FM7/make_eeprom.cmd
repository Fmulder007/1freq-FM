@echo off
setlocal
chcp 65001 >nul

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0make_eeprom.ps1"

if errorlevel 1 (
  echo.
  echo Ошибка при создании eeprom.hex.
)

echo.
pause
endlocal
