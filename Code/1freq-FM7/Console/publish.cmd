@echo off
setlocal
cd /d "%~dp0"

echo ==============================================
echo 1freq-FM Console - Windows x64 release
echo ==============================================
echo.

if not exist "Tools\AVRDUDE\avrdude.exe" (
    echo Preparing bundled AVRDUDE and Zadig...
    powershell -NoProfile -ExecutionPolicy Bypass -File "Tools\prepare-tools.ps1"
    if errorlevel 1 (
        echo.
        echo Tool preparation failed.
        exit /b 1
    )
)

dotnet publish "1freq-FM.Console.csproj" -c Release -r win-x64 --self-contained true ^
    -p:PublishSingleFile=true -o "dist\1freq-FM-Console"

if errorlevel 1 (
    echo.
    echo PUBLISH FAILED
    exit /b 1
)

if not exist "dist" mkdir "dist"
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "Compress-Archive -Path 'dist\1freq-FM-Console\*' -DestinationPath 'dist\1freq-FM-Console-win-x64.zip' -Force"

if errorlevel 1 (
    echo.
    echo ZIP creation failed.
    exit /b 1
)

echo.
echo READY: dist\1freq-FM-Console-win-x64.zip
exit /b 0
