$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$hexPath = Join-Path $PSScriptRoot 'eeprom.hex'

$Default = @{
    XT = '27007840'
    MD = '5'
    F1 = '2000000'
    F2 = '1000000'
    ST = '0'
}

$Limits = @{
    XTMin = [uint64]7000000
    XTMax = [uint64]33000000
    FMin  = [uint64]15000
    FMax  = [uint64]150000000
    STMin = [uint64]0
    STMax = [uint64]1024
}

function Read-IntelHex {
    param([string]$Path)

    $memory = New-Object byte[] 80
    for ($i = 0; $i -lt $memory.Length; $i++) {
        $memory[$i] = 255
    }

    if (-not (Test-Path -LiteralPath $Path)) {
        return ,$memory
    }

    foreach ($line in Get-Content -LiteralPath $Path) {
        $line = $line.Trim()
        if ($line.Length -eq 0) { continue }

        if (-not $line.StartsWith(':') -or $line.Length -lt 11) {
            throw "Некорректная строка Intel HEX: $line"
        }

        $length = [Convert]::ToInt32($line.Substring(1, 2), 16)
        $address = [Convert]::ToInt32($line.Substring(3, 4), 16)
        $type = [Convert]::ToInt32($line.Substring(7, 2), 16)

        if ($line.Length -ne (11 + 2 * $length)) {
            throw "Неверная длина записи Intel HEX: $line"
        }

        $sum = $length + (($address -shr 8) -band 255) +
               ($address -band 255) + $type

        $data = New-Object byte[] $length
        for ($i = 0; $i -lt $length; $i++) {
            $value = [Convert]::ToInt32($line.Substring(9 + 2 * $i, 2), 16)
            $data[$i] = [byte]$value
            $sum += $value
        }

        $checksum = [Convert]::ToInt32($line.Substring(9 + 2 * $length, 2), 16)
        if ((($sum + $checksum) -band 255) -ne 0) {
            throw "Ошибка контрольной суммы Intel HEX: $line"
        }

        if ($type -eq 0) {
            if (($address + $length) -gt $memory.Length) {
                throw "Запись выходит за пределы области EEPROM FM7."
            }

            for ($i = 0; $i -lt $length; $i++) {
                $memory[$address + $i] = $data[$i]
            }
        }
        elseif ($type -eq 1) {
            break
        }
        else {
            throw "Неподдерживаемый тип записи Intel HEX: $type"
        }
    }

    return ,$memory
}

function Get-Parameter {
    param(
        [byte[]]$Memory,
        [int]$Address,
        [string]$Tag
    )

    $length = -1
    for ($i = 0; $i -lt 16; $i++) {
        if ($Memory[$Address + $i] -eq 0) {
            $length = $i
            break
        }
    }

    if ($length -lt 4) { return $null }

    $text = [Text.Encoding]::ASCII.GetString($Memory, $Address, $length)
    $pattern = '^' + [regex]::Escape($Tag) + '=(\d+)$'

    if ($text -match $pattern) {
        return $Matches[1]
    }

    return $null
}

function Test-Config {
    param([hashtable]$Config)

    foreach ($key in @('XT', 'MD', 'F1', 'F2', 'ST')) {
        if ($null -eq $Config[$key]) { return $false }
        if ($Config[$key] -notmatch '^\d+$') { return $false }
    }

    $xt = [uint64]$Config.XT
    $md = [uint64]$Config.MD
    $f1 = [uint64]$Config.F1
    $f2 = [uint64]$Config.F2
    $st = [uint64]$Config.ST

    return (
        $xt -ge $Limits.XTMin -and $xt -le $Limits.XTMax -and
        ($md -eq 5 -or $md -eq 6) -and
        $f1 -ge $Limits.FMin -and $f1 -le $Limits.FMax -and
        $f2 -ge $Limits.FMin -and $f2 -le $Limits.FMax -and
        $st -ge $Limits.STMin -and $st -le $Limits.STMax
    )
}

function Read-UInt32Value {
    param(
        [string]$Prompt,
        [string]$Current,
        [uint64]$Minimum,
        [uint64]$Maximum
    )

    while ($true) {
        $answer = Read-Host ($Prompt + ' [' + $Current + ']')
        if ([string]::IsNullOrWhiteSpace($answer)) {
            $answer = $Current
        }

        $parsed = [uint64]0
        $isNumber = [uint64]::TryParse($answer, [ref]$parsed)

        if (-not $isNumber -or $parsed -lt $Minimum -or $parsed -gt $Maximum) {
            Write-Host ('Введите целое число от ' + $Minimum + ' до ' + $Maximum + '.') -ForegroundColor Red
            continue
        }

        return [uint32]$parsed
    }
}

function New-ParameterBlock {
    param(
        [string]$Tag,
        [uint32]$Value
    )

    $text = $Tag + '=' + $Value.ToString()
    $encoded = [Text.Encoding]::ASCII.GetBytes($text)

    if ($encoded.Length -ge 16) {
        throw ('Значение параметра ' + $Tag + ' не помещается в 16 байт.')
    }

    $block = New-Object byte[] 16
    [Array]::Copy($encoded, $block, $encoded.Length)
    return ,$block
}

function New-IntelHexRecord {
    param(
        [int]$Address,
        [byte[]]$Data
    )

    $length = $Data.Length
    $sum = $length + (($Address -shr 8) -band 255) + ($Address -band 255)
    $dataText = ''

    foreach ($item in $Data) {
        $sum += [int]$item
        $dataText += $item.ToString('X2')
    }

    $checksum = ((256 - ($sum -band 255)) -band 255)

    return ':' + $length.ToString('X2') +
        $Address.ToString('X4') + '00' +
        $dataText + $checksum.ToString('X2')
}

try {
    $memory = Read-IntelHex -Path $hexPath

    $current = @{
        XT = Get-Parameter -Memory $memory -Address 0  -Tag 'XT'
        MD = Get-Parameter -Memory $memory -Address 16 -Tag 'MD'
        F1 = Get-Parameter -Memory $memory -Address 32 -Tag 'F1'
        F2 = Get-Parameter -Memory $memory -Address 48 -Tag 'F2'
        ST = Get-Parameter -Memory $memory -Address 64 -Tag 'ST'
    }

    # Старые FM5/FM6 здесь специально не распознаются и не переносятся.
    if (-not (Test-Config -Config $current)) {
        $current = $Default.Clone()
    }

    Write-Host ''
    Write-Host '=============================================='
    Write-Host ' Генератор EEPROM для 1freq-FM7'
    Write-Host ' XT - частота кварца Si5351, Гц'
    Write-Host ' MD - режим: 5 (два состояния) или 6 (плавно)'
    Write-Host ' F1 - частота при минимальном напряжении PC0'
    Write-Host ' F2 - частота при максимальном напряжении PC0'
    Write-Host ' ST - число шагов; 0 означает 1024 шага'
    Write-Host '=============================================='
    Write-Host ''
    Write-Host 'Текущая конфигурация:'
    Write-Host (' XT=' + $current.XT)
    Write-Host (' MD=' + $current.MD)
    Write-Host (' F1=' + $current.F1)
    Write-Host (' F2=' + $current.F2)
    Write-Host (' ST=' + $current.ST)
    Write-Host ''

    $xt = Read-UInt32Value -Prompt 'Опорная частота XT, Гц' -Current $current.XT -Minimum $Limits.XTMin -Maximum $Limits.XTMax

    do {
        $mdText = Read-Host ('Режим MD: 5 или 6 [' + $current.MD + ']')
        if ([string]::IsNullOrWhiteSpace($mdText)) { $mdText = $current.MD }

        if ($mdText -ne '5' -and $mdText -ne '6') {
            Write-Host 'Допустимы только режимы 5 и 6.' -ForegroundColor Red
        }
    } while ($mdText -ne '5' -and $mdText -ne '6')

    $md = [uint32]$mdText
    $f1 = Read-UInt32Value -Prompt 'Частота F1, Гц' -Current $current.F1 -Minimum $Limits.FMin -Maximum $Limits.FMax
    $f2 = Read-UInt32Value -Prompt 'Частота F2, Гц' -Current $current.F2 -Minimum $Limits.FMin -Maximum $Limits.FMax
    $st = Read-UInt32Value -Prompt 'Количество шагов ST (0 = 1024)' -Current $current.ST -Minimum $Limits.STMin -Maximum $Limits.STMax

    $blocks = @(
        (New-ParameterBlock -Tag 'XT' -Value $xt),
        (New-ParameterBlock -Tag 'MD' -Value $md),
        (New-ParameterBlock -Tag 'F1' -Value $f1),
        (New-ParameterBlock -Tag 'F2' -Value $f2),
        (New-ParameterBlock -Tag 'ST' -Value $st)
    )

    $lines = New-Object 'System.Collections.Generic.List[string]'
    for ($i = 0; $i -lt $blocks.Count; $i++) {
        $lines.Add((New-IntelHexRecord -Address ($i * 16) -Data $blocks[$i]))
    }
    $lines.Add(':00000001FF')

    [IO.File]::WriteAllLines($hexPath, $lines.ToArray(), [Text.Encoding]::ASCII)

    Write-Host ''
    Write-Host 'Создана конфигурация:'
    Write-Host (' XT=' + $xt + ' Гц')
    Write-Host (' MD=' + $md)
    Write-Host (' F1=' + $f1 + ' Гц')
    Write-Host (' F2=' + $f2 + ' Гц')
    Write-Host (' ST=' + $st)
    Write-Host ''
    Write-Host ('Файл: ' + $hexPath)
    Write-Host 'Запишите eeprom.hex в память EEPROM микроконтроллера.'
}
catch {
    Write-Host ''
    Write-Host ('ОШИБКА: ' + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
