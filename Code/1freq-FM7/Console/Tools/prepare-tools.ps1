$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ToolDir = $PSScriptRoot
$ProjectDir = Split-Path -Parent $ToolDir
$AvrdudeDir = Join-Path $ToolDir 'AVRDUDE'
$LicenseSourceDir = Join-Path $ProjectDir 'Licenses\Source'
$TempDir = Join-Path ([IO.Path]::GetTempPath()) ('1freq-fm-tools-' + [guid]::NewGuid().ToString('N'))

$AvrdudeVersion = '8.3'
$AvrdudeUrl = 'https://github.com/avrdudes/avrdude/releases/download/v8.3/avrdude-v8.3-windows_mingw-x64.zip'
$AvrdudeSha256 = '7c42381a307ec9870a240bbefcb9b2499e90643256a766fe35baa4a6c558d994'
$ZadigUrl = 'https://zadig.akeo.ie/downloads/zadig-2.9.exe'

$SourceArchives = @(
    @{
        Name = 'avrdude-v8.3-source.zip'
        Url = 'https://github.com/avrdudes/avrdude/archive/refs/tags/v8.3.zip'
    },
    @{
        Name = 'libwdi-v1.5.1-source.zip'
        Url = 'https://github.com/pbatard/libwdi/archive/refs/tags/v1.5.1.zip'
    }
)

try {
    New-Item -ItemType Directory -Force -Path $TempDir, $AvrdudeDir, $LicenseSourceDir | Out-Null

    Write-Host 'Загрузка AVRDUDE 8.3 (MinGW x64)...'
    $AvrdudeArchive = Join-Path $TempDir 'avrdude.zip'
    Invoke-WebRequest -Uri $AvrdudeUrl -OutFile $AvrdudeArchive

    $ActualHash = (Get-FileHash -LiteralPath $AvrdudeArchive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($ActualHash -ne $AvrdudeSha256) {
        throw "Не совпала SHA-256 AVRDUDE. Получено: $ActualHash"
    }

    $AvrdudeExtract = Join-Path $TempDir 'avrdude'
    Expand-Archive -LiteralPath $AvrdudeArchive -DestinationPath $AvrdudeExtract -Force

    $Exe = Get-ChildItem -LiteralPath $AvrdudeExtract -Filter 'avrdude.exe' -File -Recurse |
        Select-Object -First 1
    $Conf = Get-ChildItem -LiteralPath $AvrdudeExtract -Filter 'avrdude.conf' -File -Recurse |
        Select-Object -First 1

    if ($null -eq $Exe -or $null -eq $Conf) {
        throw 'В архиве AVRDUDE не найдены avrdude.exe и/или avrdude.conf.'
    }

    # Сохраняем весь каталог поставки, включая DLL, нужные MinGW-сборке.
    $PayloadRoot = Split-Path -Parent $Exe.FullName
    Get-ChildItem -LiteralPath $PayloadRoot -Force |
        Copy-Item -Destination $AvrdudeDir -Recurse -Force

    if (-not (Test-Path -LiteralPath (Join-Path $AvrdudeDir 'avrdude.exe'))) {
        Copy-Item -LiteralPath $Exe.FullName -Destination $AvrdudeDir -Force
    }
    if (-not (Test-Path -LiteralPath (Join-Path $AvrdudeDir 'avrdude.conf'))) {
        Copy-Item -LiteralPath $Conf.FullName -Destination $AvrdudeDir -Force
    }

    if (-not (Test-Path -LiteralPath (Join-Path $AvrdudeDir 'avrdude.exe')) -or
        -not (Test-Path -LiteralPath (Join-Path $AvrdudeDir 'avrdude.conf'))) {
        throw 'Не удалось разложить AVRDUDE в Tools/AVRDUDE.'
    }

    Write-Host 'Загрузка Zadig 2.9...'
    $ZadigPath = Join-Path $ToolDir 'Zadig.exe'
    Invoke-WebRequest -Uri $ZadigUrl -OutFile $ZadigPath

    $Signature = Get-AuthenticodeSignature -FilePath $ZadigPath
    if ($Signature.Status -ne 'Valid') {
        throw "Подпись Zadig не прошла проверку: $($Signature.Status). Файл оставлен для ручной проверки."
    }

    $Signer = $Signature.SignerCertificate.Subject
    if ($Signer -notmatch 'Akeo Consulting') {
        throw "Неожиданный издатель Zadig: $Signer"
    }

    foreach ($Source in $SourceArchives) {
        $Destination = Join-Path $LicenseSourceDir $Source.Name
        Write-Host "Загрузка исходников $($Source.Name)..."
        Invoke-WebRequest -Uri $Source.Url -OutFile $Destination
    }

    Write-Host ''
    Write-Host 'Инструменты подготовлены.'
    Write-Host "AVRDUDE: $AvrdudeVersion, Windows MinGW x64"
    Write-Host 'Zadig: 2.9, подпись Akeo Consulting проверена.'
    Write-Host "AVRDUDE: $AvrdudeDir"
    Write-Host "Zadig: $ZadigPath"
}
finally {
    if (Test-Path -LiteralPath $TempDir) {
        Remove-Item -LiteralPath $TempDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}
