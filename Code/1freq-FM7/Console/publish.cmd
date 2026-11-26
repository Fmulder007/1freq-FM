@echo off
setlocal
cd /d "%~dp0"
if errorlevel 1 goto WorkingDirectoryFailed

set "PUBLISH_DIR=dist\1freq-FM-Console"
set "PACKAGE_NAME=1freq-FM-Console-win-x64"
set "ZIP_PATH=dist\%PACKAGE_NAME%.zip"

echo ==============================================
echo 1freq-FM Console - Windows x64 release
echo ==============================================
echo.

if not exist "Tools\AVRDUDE\avrdude.exe" goto PrepareTools
if not exist "Tools\AVRDUDE\avrdude.conf" goto PrepareTools
if not exist "Tools\Zadig.exe" goto PrepareTools
goto ToolsReady

:PrepareTools
echo Preparing bundled AVRDUDE and Zadig...
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "Tools\prepare-tools.ps1"
if errorlevel 1 goto ToolPreparationFailed

:ToolsReady
if not exist "Config\devices.json" goto SourceConfigMissing
if not exist "Firmware\" goto SourceFirmwareMissing
if not exist "Data\README.md" goto SourceDataReadmeMissing

REM Remove obsolete staging directory left by older versions of this script.
if exist "dist\_package" rmdir /S /Q "dist\_package"
if exist "dist\_package" goto ObsoletePackageCleanupFailed

REM Do not leave an old ZIP that could be mistaken for this build.
if exist "%ZIP_PATH%" del /F /Q "%ZIP_PATH%"
if exist "%ZIP_PATH%" goto OldZipCleanupFailed

echo Cleaning previous publish output...
if exist "%PUBLISH_DIR%" rmdir /S /Q "%PUBLISH_DIR%"
if exist "%PUBLISH_DIR%" goto PublishDirectoryCleanupFailed

dotnet publish "1freq-FM.Console.csproj" -c Release -r win-x64 --self-contained true ^
    -p:PublishSingleFile=true -o "%PUBLISH_DIR%"

if errorlevel 1 goto PublishFailed

echo.
echo Copying required application resources...
REM Copy explicitly so the release does not depend on SDK content-glob behaviour.
robocopy "Config" "%PUBLISH_DIR%\Config" /E /R:2 /W:1 /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 goto ResourceCopyFailed

robocopy "Firmware" "%PUBLISH_DIR%\Firmware" /E /R:2 /W:1 /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 goto ResourceCopyFailed

robocopy "Tools" "%PUBLISH_DIR%\Tools" /E /R:2 /W:1 /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 goto ResourceCopyFailed

robocopy "Licenses" "%PUBLISH_DIR%\Licenses" /E /R:2 /W:1 /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 goto ResourceCopyFailed

if not exist "%PUBLISH_DIR%\Data" mkdir "%PUBLISH_DIR%\Data"
copy /Y "Data\README.md" "%PUBLISH_DIR%\Data\README.md" >nul
if errorlevel 1 goto ResourceCopyFailed

if not exist "%PUBLISH_DIR%\1freq-FM.exe" goto PublishedExeMissing
if not exist "%PUBLISH_DIR%\Config\devices.json" goto PublishedConfigMissing
if not exist "%PUBLISH_DIR%\Tools\AVRDUDE\avrdude.exe" goto PublishedToolsMissing
if not exist "%PUBLISH_DIR%\Tools\AVRDUDE\avrdude.conf" goto PublishedToolsMissing
if not exist "%PUBLISH_DIR%\Tools\Zadig.exe" goto PublishedToolsMissing

echo.
echo Running self-test on the published application...
"%PUBLISH_DIR%\1freq-FM.exe" --self-test
if errorlevel 1 goto SelfTestFailed

if not exist "dist" mkdir "dist"
if not exist "dist\." goto DistDirectoryCreateFailed

echo.
echo Creating ZIP with a top-level folder...
REM Add the top-level package folder directly to each ZIP entry; no staging directory is needed.
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command ^
    "$ErrorActionPreference='Stop'; Add-Type -AssemblyName System.IO.Compression; Add-Type -AssemblyName System.IO.Compression.FileSystem; $source=(Resolve-Path '%PUBLISH_DIR%').Path.TrimEnd('\'); $root='%PACKAGE_NAME%'; $destination=Join-Path (Get-Location) '%ZIP_PATH%'; $files=@(Get-ChildItem -LiteralPath $source -File -Recurse); if ($files.Count -eq 0) { throw 'Published application directory is empty.' }; $archive=[System.IO.Compression.ZipFile]::Open($destination,[System.IO.Compression.ZipArchiveMode]::Create); try { foreach($file in $files) { $relative=$file.FullName.Substring($source.Length).TrimStart([char[]]@('\','/')).Replace('\','/'); $entryName=$root + '/' + $relative; [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,$file.FullName,$entryName,[System.IO.Compression.CompressionLevel]::Optimal) | Out-Null } } finally { $archive.Dispose() }"
if errorlevel 1 goto ZipCreationFailed

echo.
echo Verifying ZIP contents...
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command ^
    "$ErrorActionPreference='Stop'; Add-Type -AssemblyName System.IO.Compression; Add-Type -AssemblyName System.IO.Compression.FileSystem; $zip=[System.IO.Compression.ZipFile]::OpenRead((Resolve-Path '%ZIP_PATH%').Path); try { $names=@($zip.Entries | ForEach-Object { $_.FullName }); $required=@('%PACKAGE_NAME%/1freq-FM.exe','%PACKAGE_NAME%/Config/devices.json','%PACKAGE_NAME%/Tools/AVRDUDE/avrdude.exe','%PACKAGE_NAME%/Tools/AVRDUDE/avrdude.conf','%PACKAGE_NAME%/Tools/Zadig.exe'); foreach($name in $required) { if ($names -notcontains $name) { throw ('ZIP is missing: ' + $name) } }; if (-not ($names | Where-Object { $_.StartsWith('%PACKAGE_NAME%/Firmware/') })) { throw 'ZIP is missing Firmware files.' }; if (@($names | Where-Object { -not $_.StartsWith('%PACKAGE_NAME%/') }).Count -gt 0) { throw 'ZIP contains files outside the top-level package folder.' } } finally { $zip.Dispose() }"
if errorlevel 1 goto ZipVerificationFailed

echo.
echo READY: %ZIP_PATH%
echo ZIP contains folder: %PACKAGE_NAME%\
echo Published files remain in: %PUBLISH_DIR%\
echo.
pause
exit /b 0

:WorkingDirectoryFailed
echo ERROR: Cannot switch to the Console project directory.
goto Failed

:ToolPreparationFailed
echo.
echo Tool preparation failed.
goto Failed

:SourceConfigMissing
echo ERROR: Source file Config\devices.json is missing.
goto Failed

:SourceFirmwareMissing
echo ERROR: Source Firmware directory is missing.
goto Failed

:SourceDataReadmeMissing
echo ERROR: Source file Data\README.md is missing.
goto Failed

:ObsoletePackageCleanupFailed
echo ERROR: Cannot remove obsolete dist\_package directory.
goto Failed

:OldZipCleanupFailed
echo ERROR: Cannot remove the previous ZIP file.
goto Failed

:PublishDirectoryCleanupFailed
echo ERROR: Cannot clean previous publish directory: %PUBLISH_DIR%
goto Failed

:PublishFailed
echo.
echo PUBLISH FAILED.
goto Failed

:ResourceCopyFailed
echo ERROR: Failed to copy one or more required resource directories.
goto Failed

:PublishedExeMissing
echo ERROR: Published 1freq-FM.exe was not found.
goto Failed

:PublishedConfigMissing
echo ERROR: Config\devices.json was not copied into the published application.
goto Failed

:PublishedToolsMissing
echo ERROR: One or more programmer tools are missing from the published application.
goto Failed

:SelfTestFailed
echo ERROR: Published application self-test failed. ZIP was not created.
goto Failed

:DistDirectoryCreateFailed
echo ERROR: Cannot create dist directory.
goto Failed

:ZipCreationFailed
echo ERROR: ZIP creation failed.
goto Failed

:ZipVerificationFailed
echo ERROR: ZIP verification failed. The archive is incomplete or has a wrong folder structure.
goto Failed

:Failed
echo.
pause
exit /b 1
