@echo off
setlocal
cd /d "%~dp0"

dotnet build "1freq-FM.Console.csproj" -c Release
if errorlevel 1 (
    echo.
    echo BUILD FAILED
    exit /b 1
)

echo.
echo BUILD OK
exit /b 0
