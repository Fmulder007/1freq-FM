using System.Text.Json;
using System.Text.RegularExpressions;

internal sealed class DeviceCatalog
{
    public int SchemaVersion { get; set; } = 1;
    public string ProbePart { get; set; } = "m328p";
    public EepromLayout EepromLayout { get; set; } = new();
    public List<DeviceProfile> Devices { get; set; } = new();
}

internal sealed class EepromLayout
{
    public string FormatId { get; set; } = "FM7";
    public int RecordSize { get; set; } = 16;
    public List<EepromParameter> Parameters { get; set; } = new();
}

internal sealed class EepromParameter
{
    public string Tag { get; set; } = "";
    public int Address { get; set; }
    public uint MinValue { get; set; }
    public uint MaxValue { get; set; }
    public uint DefaultValue { get; set; }
    public List<uint> AllowedValues { get; set; } = new();
    public string Description { get; set; } = "";
}

internal sealed class DeviceProfile
{
    public string Id { get; set; } = "";
    public string Name { get; set; } = "";
    public string Signature { get; set; } = "";
    public string AvrdudePart { get; set; } = "";
    public string FirmwareFile { get; set; } = "";
    public int FlashBytes { get; set; }
    public int EepromBytes { get; set; }
    public string EepromFormat { get; set; } = "FM7";
    public bool Enabled { get; set; } = true;
    public List<string> Aliases { get; set; } = new();
    public List<string> SupportedProgrammers { get; set; } = new();
    public string Notes { get; set; } = "";
}

internal sealed record ProgrammerSelection(string Id, string DisplayName, string AvrdudeId, string? Port, int? BaudRate);
internal sealed record AvrdudeResult(bool Success, int ExitCode, string Output);

internal static class CatalogLoader
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
        ReadCommentHandling = JsonCommentHandling.Skip,
        AllowTrailingCommas = true
    };

    public static DeviceCatalog Load(string path)
    {
        if (!File.Exists(path))
            throw new FileNotFoundException("Не найден каталог микроконтроллеров Config/devices.json.", path);

        var json = File.ReadAllText(path);
        var catalog = JsonSerializer.Deserialize<DeviceCatalog>(json, JsonOptions)
            ?? throw new InvalidDataException("Config/devices.json пуст или не содержит объект каталога.");

        Validate(catalog);
        return catalog;
    }

    public static void Validate(DeviceCatalog catalog)
    {
        if (catalog.SchemaVersion != 1)
            throw new InvalidDataException($"Не поддерживается schemaVersion={catalog.SchemaVersion} в devices.json.");

        if (!Regex.IsMatch(catalog.ProbePart, @"^[A-Za-z0-9_.-]+$"))
            throw new InvalidDataException("Некорректное значение probePart.");

        if (catalog.EepromLayout.RecordSize < 4 || catalog.EepromLayout.RecordSize > 64)
            throw new InvalidDataException("eepromLayout.recordSize должен быть от 4 до 64 байт.");

        if (catalog.EepromLayout.Parameters.Count == 0)
            throw new InvalidDataException("В eepromLayout не описаны параметры.");

        var tags = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var addresses = new HashSet<int>();
        var requiredEepromBytes = 0;

        foreach (var parameter in catalog.EepromLayout.Parameters)
        {
            if (!Regex.IsMatch(parameter.Tag, @"^[A-Z][A-Z0-9]$"))
                throw new InvalidDataException($"Некорректная метка EEPROM: {parameter.Tag}.");
            if (!tags.Add(parameter.Tag))
                throw new InvalidDataException($"Повторяется метка EEPROM: {parameter.Tag}.");
            if (parameter.Address < 0 || !addresses.Add(parameter.Address))
                throw new InvalidDataException($"Некорректный или повторяющийся адрес EEPROM для {parameter.Tag}.");
            if (parameter.Address % catalog.EepromLayout.RecordSize != 0)
                throw new InvalidDataException($"Адрес {parameter.Address} для {parameter.Tag} не выровнен по размеру записи.");
            if (parameter.MinValue > parameter.MaxValue)
                throw new InvalidDataException($"Неверный диапазон значений {parameter.Tag}.");
            if (!EepromHex.IsValueAllowed(parameter, parameter.DefaultValue))
                throw new InvalidDataException($"Значение по умолчанию для {parameter.Tag} не соответствует ограничениям.");
            if (parameter.AllowedValues.Count > 0 &&
                parameter.AllowedValues.Any(v => v < parameter.MinValue || v > parameter.MaxValue))
                throw new InvalidDataException($"allowedValues параметра {parameter.Tag} выходит за заданный диапазон.");

            requiredEepromBytes = Math.Max(requiredEepromBytes, checked(parameter.Address + catalog.EepromLayout.RecordSize));
        }

        if (catalog.Devices.Count == 0)
            throw new InvalidDataException("В devices.json нет профилей микроконтроллеров.");

        var ids = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var signatures = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        foreach (var device in catalog.Devices)
        {
            if (!Regex.IsMatch(device.Id, @"^[A-Za-z0-9_-]+$") || !ids.Add(device.Id))
                throw new InvalidDataException($"Пустой или повторяющийся идентификатор МК: {device.Id}.");
            if (string.IsNullOrWhiteSpace(device.Name))
                throw new InvalidDataException($"У профиля {device.Id} не задано название.");
            if (!Regex.IsMatch(device.Signature, @"^[0-9A-Fa-f]{6}$") || !signatures.Add(device.Signature))
                throw new InvalidDataException($"Некорректная или повторяющаяся сигнатура МК: {device.Signature}.");
            if (!Regex.IsMatch(device.AvrdudePart, @"^[A-Za-z0-9_.-]+$"))
                throw new InvalidDataException($"Некорректный avrdudePart для {device.Name}.");
            if (string.IsNullOrWhiteSpace(device.FirmwareFile) ||
                !string.Equals(Path.GetFileName(device.FirmwareFile), device.FirmwareFile, StringComparison.Ordinal))
                throw new InvalidDataException($"firmwareFile для {device.Name} должен быть именем файла без каталогов.");
            if (!device.FirmwareFile.EndsWith(".hex", StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException($"Для {device.Name} ожидается HEX-файл.");
            if (device.FlashBytes <= 0 || device.EepromBytes < requiredEepromBytes)
                throw new InvalidDataException($"Недостаточный объём памяти в профиле {device.Name}.");
            if (!string.Equals(device.EepromFormat, catalog.EepromLayout.FormatId, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException($"Формат EEPROM профиля {device.Name} не совпадает с форматом каталога.");
            if (device.SupportedProgrammers.Count == 0 ||
                device.SupportedProgrammers.Any(p => p is not ("usbasp" or "arduino-isp")))
                throw new InvalidDataException($"Для {device.Name} указаны неизвестные программаторы.");
        }
    }
}

internal sealed class ConsolePreferences
{
    public string ProgrammerId { get; set; } = "usbasp";
    public string? Port { get; set; }
}
