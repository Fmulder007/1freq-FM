using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;

internal sealed class FirmwareCatalog
{
    public int SchemaVersion { get; set; } = 2;
    public EepromLayout EepromLayout { get; set; } = new();
    public Dictionary<string, FirmwareProfile> Firmwares { get; set; } = new(StringComparer.OrdinalIgnoreCase);
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
    public string Unit { get; set; } = "";
}

internal sealed class FirmwareProfile
{
    [JsonIgnore]
    public string FileName { get; set; } = "";

    public string Name { get; set; } = "";
    public string AvrdudePart { get; set; } = "";
    public int FlashBytes { get; set; }
    public int EepromBytes { get; set; }
    public string EepromFormat { get; set; } = "FM7";
    public FuseSettings? Fuses { get; set; }
    public bool Enabled { get; set; } = true;
    public List<string> SupportedProgrammers { get; set; } = new();
    public string Notes { get; set; } = "";
}

internal sealed class FuseSettings
{
    public string? Low { get; set; }
    public string? High { get; set; }
    public string? Extended { get; set; }
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

    public static FirmwareCatalog Load(string path)
    {
        if (!File.Exists(path))
            throw new FileNotFoundException("Не найден каталог прошивок Config/devices.json.", path);

        var json = File.ReadAllText(path);
        var catalog = JsonSerializer.Deserialize<FirmwareCatalog>(json, JsonOptions)
            ?? throw new InvalidDataException("Config/devices.json пуст или не содержит объект каталога.");

        Validate(catalog);
        return catalog;
    }

    public static void Validate(FirmwareCatalog catalog)
    {
        if (catalog.SchemaVersion != 2)
            throw new InvalidDataException($"Не поддерживается schemaVersion={catalog.SchemaVersion} в devices.json.");

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

        if (catalog.Firmwares.Count == 0)
            throw new InvalidDataException("В devices.json нет записей в разделе firmwares.");

        var fileNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var item in catalog.Firmwares)
        {
            var fileName = item.Key;
            var firmware = item.Value;

            if (string.IsNullOrWhiteSpace(fileName) ||
                !string.Equals(Path.GetFileName(fileName), fileName, StringComparison.Ordinal) ||
                !fileName.EndsWith(".hex", StringComparison.OrdinalIgnoreCase) ||
                !fileNames.Add(fileName))
                throw new InvalidDataException($"Некорректное или повторяющееся имя HEX-файла: {fileName}.");

            firmware.FileName = fileName;

            if (string.IsNullOrWhiteSpace(firmware.Name))
                throw new InvalidDataException($"Для {fileName} не задано название микроконтроллера.");
            if (!Regex.IsMatch(firmware.AvrdudePart, @"^[A-Za-z0-9_.-]+$"))
                throw new InvalidDataException($"Некорректный avrdudePart для {firmware.Name}.");
            if (firmware.FlashBytes <= 0 || firmware.EepromBytes < requiredEepromBytes)
                throw new InvalidDataException($"Недостаточный объём памяти в профиле {firmware.Name}.");
            if (!string.Equals(firmware.EepromFormat, catalog.EepromLayout.FormatId, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException($"Формат EEPROM профиля {firmware.Name} не совпадает с форматом каталога.");

            if (firmware.Fuses is not null)
            {
                var fuseValues = new[] { firmware.Fuses.Low, firmware.Fuses.High, firmware.Fuses.Extended };
                if (fuseValues.All(string.IsNullOrWhiteSpace))
                    throw new InvalidDataException($"Для {firmware.Name} либо укажите fuse-значения, либо удалите объект fuses.");
                foreach (var fuse in fuseValues.Where(value => !string.IsNullOrWhiteSpace(value)))
                {
                    if (!Regex.IsMatch(fuse!, @"^[0-9A-Fa-f]{2}$"))
                        throw new InvalidDataException($"Fuse-значения для {firmware.Name} должны содержать по два шестнадцатеричных символа.");
                }
            }

            if (firmware.SupportedProgrammers.Count == 0 ||
                firmware.SupportedProgrammers.Any(p => p is not ("usbasp" or "arduino-isp")))
                throw new InvalidDataException($"Для {firmware.Name} указаны неизвестные программаторы.");
        }
    }
}

internal sealed class ConsolePreferences
{
    public string ProgrammerId { get; set; } = "usbasp";
    public string? Port { get; set; }
}
