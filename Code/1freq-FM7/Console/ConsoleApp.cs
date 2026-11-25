using System.Diagnostics;
using System.Globalization;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.Win32;

internal sealed class ConsoleApp
{
    private readonly string _root;
    private readonly DeviceCatalog _catalog;
    private readonly JsonSerializerOptions _jsonOptions = new() { WriteIndented = true };
    private string _preferredProgrammerId = "usbasp";
    private string? _preferredPort;

    private string ConfigDirectory => Path.Combine(_root, "Config");
    private string FirmwareDirectory => Path.Combine(_root, "Firmware");
    private string DataDirectory => Path.Combine(_root, "Data");
    private string BackupDirectory => Path.Combine(_root, "Backup");
    private string ExtraAvrdudeConfig => Path.Combine(ConfigDirectory, "avrdude-extra.conf");
    private string SettingsPath => Path.Combine(DataDirectory, "last-settings.json");
    private string PreferencesPath => Path.Combine(DataDirectory, "app-state.json");
    private string EepromHexPath => Path.Combine(FirmwareDirectory, "eeprom.hex");

    public ConsoleApp()
    {
        _root = AppContext.BaseDirectory;
        _catalog = CatalogLoader.Load(Path.Combine(ConfigDirectory, "devices.json"));
        LoadPreferences();
    }

    public async Task RunAsync()
    {
        Console.Title = "1freq-FM Console";
        ShowBanner();
        ShowToolStatus();

        while (true)
        {
            Console.WriteLine();
            Console.WriteLine("  1. Создать / изменить настройки EEPROM");
            Console.WriteLine("  2. Проверить подключение и определить МК");
            Console.WriteLine("  3. Прошить микросинтезатор (Flash + EEPROM)");
            Console.WriteLine("  4. Записать только EEPROM");
            Console.WriteLine("  5. Считать EEPROM в резервную копию");
            Console.WriteLine("  6. Настроить USB-драйвер (Zadig)");
            Console.WriteLine("  0. Выход");
            Console.WriteLine();

            Console.Write("Введите номер от 0 до 6 [0]: ");
            var choice = Console.ReadLine()?.Trim();
            if (string.IsNullOrEmpty(choice))
                choice = "0";

            try
            {
                switch (choice)
                {
                    case "1":
                        ConfigureEeprom();
                        break;
                    case "2":
                        await CheckTargetAsync();
                        break;
                    case "3":
                        await FlashTargetAsync();
                        break;
                    case "4":
                        await WriteEepromOnlyAsync();
                        break;
                    case "5":
                        await ReadEepromAsync();
                        break;
                    case "6":
                        LaunchZadig();
                        break;
                    case "0":
                        return;
                    default:
                        Console.WriteLine("Неизвестный пункт меню.");
                        break;
                }
            }
            catch (Exception ex)
            {
                WriteError(ex.Message);
            }

            if (choice != "0")
            {
                Console.WriteLine();
                Console.Write("Нажмите Enter для возврата в меню...");
                Console.ReadLine();
            }
        }
    }

    private static void ShowBanner()
    {
        Console.ForegroundColor = ConsoleColor.Cyan;
        Console.WriteLine("==============================================");
        Console.WriteLine("             1freq-FM Console");
        Console.WriteLine("       Настройка и прошивка микросинтезатора");
        Console.WriteLine("==============================================");
        Console.ResetColor();
    }

    private void ShowToolStatus()
    {
        Console.WriteLine();
        Console.WriteLine("Каталог МК: " + _catalog.Devices.Count + " профиля(ей)");
        Console.WriteLine("AVRDUDE: " + (FindAvrdudeExecutable() is null ? "не найден" : "найден"));
        Console.WriteLine("Zadig: " + (File.Exists(Path.Combine(_root, "Tools", "Zadig.exe")) ? "найден" : "не найден"));
        Console.WriteLine("Последний программатор: " + (_preferredProgrammerId == "arduino-isp" ? "Arduino Nano ISP" : "USBasp"));
        if (!string.IsNullOrWhiteSpace(_preferredPort)) Console.WriteLine("Последний COM-порт: " + _preferredPort);
        Console.WriteLine("Папка приложения: " + _root);
    }

    private void ConfigureEeprom()
    {
        var current = ReadSettings() ?? EepromHex.CreateDefaults(_catalog.EepromLayout);
        var edited = new Dictionary<string, uint>(StringComparer.OrdinalIgnoreCase);

        Console.WriteLine();
        Console.WriteLine("Настройки микросинтезатора FM7");
        Console.WriteLine("Enter — оставить значение в квадратных скобках.");
        Console.WriteLine();

        foreach (var parameter in _catalog.EepromLayout.Parameters)
        {
            if (!current.TryGetValue(parameter.Tag, out var oldValue))
                oldValue = parameter.DefaultValue;

            while (true)
            {
                var constraint = parameter.AllowedValues.Count > 0
                    ? FormatAllowedValues(parameter.AllowedValues)
                    : $"от {parameter.MinValue}{FormatUnit(parameter.Unit)} до {parameter.MaxValue}{FormatUnit(parameter.Unit)}";

                Console.Write($"{parameter.Description}, {constraint} [{oldValue}]: ");
                var input = Console.ReadLine()?.Trim();

                if (string.IsNullOrEmpty(input))
                {
                    edited[parameter.Tag] = oldValue;
                    break;
                }

                if (uint.TryParse(input, NumberStyles.None, CultureInfo.InvariantCulture, out var value) &&
                    EepromHex.IsValueAllowed(parameter, value))
                {
                    edited[parameter.Tag] = value;
                    break;
                }

                WriteError("Недопустимое значение. Повторите ввод.");
            }
        }

        SaveSettingsAndHex(edited);
        Console.WriteLine();
        WriteSuccess("Конфигурация FM7 сохранена.");
        PrintSettings(edited);
        Console.WriteLine("Файл EEPROM: " + EepromHexPath);
    }

    private void SaveSettingsAndHex(IReadOnlyDictionary<string, uint> values)
    {
        if (!EepromHex.ValidateSettings(_catalog.EepromLayout, values, out var error))
            throw new InvalidDataException(error);

        Directory.CreateDirectory(DataDirectory);
        Directory.CreateDirectory(FirmwareDirectory);

        File.WriteAllText(SettingsPath, JsonSerializer.Serialize(values, _jsonOptions), new UTF8Encoding(false));
        File.WriteAllText(EepromHexPath, EepromHex.Build(_catalog.EepromLayout, values), Encoding.ASCII);
    }

    private Dictionary<string, uint>? ReadSettings()
    {
        if (!File.Exists(SettingsPath))
            return null;

        try
        {
            var values = JsonSerializer.Deserialize<Dictionary<string, uint>>(File.ReadAllText(SettingsPath));
            var error = "";
            if (values is not null && EepromHex.ValidateSettings(_catalog.EepromLayout, values, out error))
                return new Dictionary<string, uint>(values, StringComparer.OrdinalIgnoreCase);

            WriteError("Сохранённые настройки не подходят текущей схеме EEPROM: " +
                (values is null ? "пустой файл." : error));
            return null;
        }
        catch (Exception ex)
        {
            WriteError("Не удалось прочитать сохранённые настройки: " + ex.Message);
            return null;
        }
    }

    private static void PrintSettings(IReadOnlyDictionary<string, uint> values)
    {
        foreach (var item in values.OrderBy(x => x.Key, StringComparer.Ordinal))
            Console.WriteLine($"  {item.Key}={item.Value}");
    }

    private async Task CheckTargetAsync()
    {
        var programmer = SelectProgrammer();
        if (programmer is null)
            return;

        var device = await ProbeTargetAsync(programmer);
        if (device is null)
            return;

        Console.WriteLine();
        WriteSuccess("Микроконтроллер распознан.");
        PrintDevice(device);
        Console.WriteLine("Программатор: " + programmer.DisplayName);
    }

    private async Task FlashTargetAsync()
    {
        var programmer = SelectProgrammer();
        if (programmer is null)
            return;

        var device = await ProbeTargetAsync(programmer);
        if (device is null || !CheckProgrammerAllowed(device, programmer))
            return;

        var firmwarePath = Path.Combine(FirmwareDirectory, device.FirmwareFile);
        if (!File.Exists(firmwarePath))
        {
            WriteError($"Для {device.Name} пока нет прошивки.");
            Console.WriteLine("Ожидаемый файл: " + firmwarePath);
            Console.WriteLine("Скомпилируйте и проверьте HEX отдельно, затем положите его в Firmware.");
            return;
        }

        var flashEnd = IntelHexInspector.ValidateFile(firmwarePath, device.FlashBytes);
        var settings = ReadSettings();
        if (settings is null)
        {
            WriteError("Сначала создайте настройки EEPROM через пункт меню 1.");
            return;
        }

        SaveSettingsAndHex(settings);
        var backupPath = NewBackupPath(device, "before-flash");

        Console.WriteLine();
        PrintDevice(device);
        Console.WriteLine($"Размер HEX: адреса 0…{flashEnd - 1}, Flash МК: {device.FlashBytes} байт.");
        Console.WriteLine("Перед записью Flash AVRDUDE выполнит Chip Erase. Содержимое EEPROM предварительно сохраняется.");
        Console.WriteLine("После прошивки будут записаны новые параметры FM7.");
        Console.WriteLine();
        Console.WriteLine("Резервная копия EEPROM: " + backupPath);

        if (!Confirm("Начать прошивку?"))
            return;

        Console.WriteLine();
        Console.WriteLine("1/4. Считывание EEPROM перед стиранием...");
        if (!await RunMemoryOperationAsync(device, programmer, "eeprom", "r", backupPath, "i"))
        {
            WriteError("Не удалось сохранить EEPROM. Прошивка отменена.");
            return;
        }

        Console.WriteLine("2/4. Запись Flash...");
        if (!await RunMemoryOperationAsync(device, programmer, "flash", "w", firmwarePath, "i"))
        {
            WriteError("Запись Flash завершилась ошибкой. Резервная копия EEPROM сохранена.");
            return;
        }

        Console.WriteLine("3/4. Проверка Flash...");
        if (!await RunMemoryOperationAsync(device, programmer, "flash", "v", firmwarePath, "i"))
        {
            WriteError("Проверка Flash не пройдена. EEPROM не изменялась после неудачной проверки.");
            return;
        }

        Console.WriteLine("4/4. Запись и проверка EEPROM...");
        if (!await RunMemoryOperationAsync(device, programmer, "eeprom", "w", EepromHexPath, "i") ||
            !await RunMemoryOperationAsync(device, programmer, "eeprom", "v", EepromHexPath, "i"))
        {
            WriteError("Не удалось записать или проверить EEPROM. Резервная копия сохранена.");
            return;
        }

        WriteSuccess("Прошивка и настройки микросинтезатора записаны и проверены.");
    }

    private async Task WriteEepromOnlyAsync()
    {
        var programmer = SelectProgrammer();
        if (programmer is null)
            return;

        var device = await ProbeTargetAsync(programmer);
        if (device is null || !CheckProgrammerAllowed(device, programmer))
            return;

        var settings = ReadSettings();
        if (settings is null)
        {
            WriteError("Сначала создайте настройки EEPROM через пункт меню 1.");
            return;
        }

        SaveSettingsAndHex(settings);
        var backupPath = NewBackupPath(device, "before-eeprom-write");

        Console.WriteLine();
        PrintSettings(settings);
        Console.WriteLine("Текущая EEPROM будет сохранена в: " + backupPath);

        if (!Confirm("Записать новые настройки EEPROM?"))
            return;

        if (!await RunMemoryOperationAsync(device, programmer, "eeprom", "r", backupPath, "i"))
        {
            WriteError("Не удалось сохранить текущую EEPROM. Запись отменена.");
            return;
        }

        if (!await RunMemoryOperationAsync(device, programmer, "eeprom", "w", EepromHexPath, "i"))
        {
            WriteError("Ошибка записи EEPROM. Резервная копия сохранена.");
            return;
        }

        if (!await RunMemoryOperationAsync(device, programmer, "eeprom", "v", EepromHexPath, "i"))
        {
            WriteError("Проверка EEPROM не пройдена.");
            return;
        }

        WriteSuccess("EEPROM записана и проверена.");
    }

    private async Task ReadEepromAsync()
    {
        var programmer = SelectProgrammer();
        if (programmer is null)
            return;

        var device = await ProbeTargetAsync(programmer);
        if (device is null || !CheckProgrammerAllowed(device, programmer))
            return;

        var backupPath = NewBackupPath(device, "read");
        Console.WriteLine();
        Console.WriteLine("Файл резервной копии: " + backupPath);

        if (!await RunMemoryOperationAsync(device, programmer, "eeprom", "r", backupPath, "i"))
        {
            WriteError("Не удалось прочитать EEPROM.");
            return;
        }

        WriteSuccess("EEPROM сохранена: " + backupPath);
    }

    private bool CheckProgrammerAllowed(DeviceProfile device, ProgrammerSelection programmer)
    {
        if (device.SupportedProgrammers.Contains(programmer.Id, StringComparer.OrdinalIgnoreCase))
            return true;

        WriteError($"Программатор {programmer.DisplayName} не разрешён в профиле {device.Name}.");
        return false;
    }

    private void PrintDevice(DeviceProfile device)
    {
        Console.WriteLine($"МК: {device.Name}");
        Console.WriteLine($"Сигнатура: {device.Signature.ToUpperInvariant()}");
        Console.WriteLine($"AVRDUDE part: {device.AvrdudePart}");
        Console.WriteLine($"Flash: {device.FlashBytes} байт; EEPROM: {device.EepromBytes} байт");
        if (device.Aliases.Count > 0)
            Console.WriteLine("Варианты: " + string.Join(", ", device.Aliases));
        if (!string.IsNullOrWhiteSpace(device.Notes))
            Console.WriteLine("Примечание: " + device.Notes);
    }

    private async Task<DeviceProfile?> ProbeTargetAsync(ProgrammerSelection programmer)
    {
        var signaturePath = Path.Combine(Path.GetTempPath(), "1freq-signature-" + Guid.NewGuid().ToString("N") + ".bin");

        try
        {
            var args = BuildBaseArguments(_catalog.ProbePart, programmer);
            args.Add("-F");
            args.Add("-U");
            args.Add($"signature:r:{signaturePath}:r");

            Console.WriteLine();
            Console.WriteLine("Проверка связи и чтение сигнатуры...");
            var result = await RunAvrdudeAsync(args);

            if (!result.Success || !File.Exists(signaturePath))
            {
                WriteError("Не удалось получить сигнатуру. Проверьте питание, кабель, RESET, ISP-подключение и выбранный программатор.");
                return null;
            }

            var signatureBytes = File.ReadAllBytes(signaturePath);
            if (signatureBytes.Length < 3)
            {
                WriteError("AVRDUDE вернул неполную сигнатуру.");
                return null;
            }

            var signature = Convert.ToHexString(signatureBytes.Take(3).ToArray());
            var device = _catalog.Devices.FirstOrDefault(d =>
                d.Enabled && string.Equals(d.Signature, signature, StringComparison.OrdinalIgnoreCase));

            if (device is null)
            {
                WriteError($"Неизвестная сигнатура МК: {signature}. Запись Flash и EEPROM запрещена.");
                return null;
            }

            return device;
        }
        finally
        {
            try { if (File.Exists(signaturePath)) File.Delete(signaturePath); } catch { }
        }
    }

    private ProgrammerSelection? SelectProgrammer()
    {
        Console.WriteLine();
        Console.WriteLine("Выберите программатор:");
        Console.WriteLine("  1 - USBasp");
        Console.WriteLine("  2 - Arduino Nano с заранее загруженной ArduinoISP");
        var defaultChoice = _preferredProgrammerId == "arduino-isp" ? "2" : "1";
        Console.Write($"Введите 1 или 2 [{defaultChoice}]: ");

        var choice = Console.ReadLine()?.Trim();
        if (string.IsNullOrEmpty(choice))
            choice = defaultChoice;

        if (choice == "1")
        {
            SavePreferences("usbasp", null);
            return new ProgrammerSelection("usbasp", "USBasp", "usbasp", null, null);
        }

        if (choice != "2")
        {
            WriteError("Неизвестный вариант программатора.");
            return null;
        }

        var port = SelectComPort(_preferredPort);
        if (port is null)
            return null;

        SavePreferences("arduino-isp", port);
        Console.WriteLine("Arduino as ISP: STK500v1, 19200 бод.");
        Console.WriteLine("Nano должна быть заранее подготовлена скетчем ArduinoISP.");
        return new ProgrammerSelection("arduino-isp", "Arduino Nano (Arduino as ISP)", "stk500v1", port, 19200);
    }

    private static string? SelectComPort(string? preferredPort)
    {
        var ports = GetComPorts();

        Console.WriteLine();
        if (ports.Count > 0)
        {
            Console.WriteLine("Обнаруженные последовательные порты:");
            for (var i = 0; i < ports.Count; i++)
                Console.WriteLine($"  {i + 1}. {ports[i]}");

            Console.Write(string.IsNullOrWhiteSpace(preferredPort)
                ? $"Введите номер от 1 до {ports.Count} или имя COM-порта: "
                : $"Введите номер от 1 до {ports.Count} или имя COM-порта [{preferredPort}]: ");
        }
        else
        {
            Console.Write(string.IsNullOrWhiteSpace(preferredPort)
                ? "Введите COM-порт Nano (например COM5): "
                : $"Введите COM-порт Nano (например COM5) [{preferredPort}]: ");
        }

        var input = Console.ReadLine()?.Trim();
        if (string.IsNullOrWhiteSpace(input))
            input = preferredPort;
        if (string.IsNullOrWhiteSpace(input))
            return null;

        if (int.TryParse(input, out var index) && index >= 1 && index <= ports.Count)
            return ports[index - 1];

        if (Regex.IsMatch(input, @"^COM[1-9][0-9]*$", RegexOptions.IgnoreCase))
            return input.ToUpperInvariant();

        WriteError("Ожидался COM-порт, например COM5.");
        return null;
    }


    private void LoadPreferences()
    {
        if (!File.Exists(PreferencesPath))
            return;

        try
        {
            var saved = JsonSerializer.Deserialize<ConsolePreferences>(File.ReadAllText(PreferencesPath));
            if (saved is null)
                return;

            if (saved.ProgrammerId is "usbasp" or "arduino-isp")
                _preferredProgrammerId = saved.ProgrammerId;

            if (!string.IsNullOrWhiteSpace(saved.Port) &&
                Regex.IsMatch(saved.Port, @"^COM[1-9][0-9]*$", RegexOptions.IgnoreCase))
                _preferredPort = saved.Port.ToUpperInvariant();
        }
        catch (Exception ex)
        {
            WriteError("Не удалось прочитать сохранённый выбор программатора: " + ex.Message);
        }
    }

    private void SavePreferences(string programmerId, string? port)
    {
        _preferredProgrammerId = programmerId;
        _preferredPort = port;

        Directory.CreateDirectory(DataDirectory);
        var state = new ConsolePreferences
        {
            ProgrammerId = programmerId,
            Port = port
        };
        File.WriteAllText(PreferencesPath, JsonSerializer.Serialize(state, _jsonOptions), new UTF8Encoding(false));
    }

    private static List<string> GetComPorts()
    {
        if (!OperatingSystem.IsWindows())
            return new List<string>();

        try
        {
            using var key = Registry.LocalMachine.OpenSubKey(@"HARDWARE\DEVICEMAP\SERIALCOMM");
            if (key is null)
                return new List<string>();

            return key.GetValueNames()
                .Select(name => key.GetValue(name)?.ToString())
                .Where(value => !string.IsNullOrWhiteSpace(value))
                .Cast<string>()
                .Distinct(StringComparer.OrdinalIgnoreCase)
                .OrderBy(value => value, StringComparer.OrdinalIgnoreCase)
                .ToList();
        }
        catch
        {
            return new List<string>();
        }
    }

    private List<string> BuildBaseArguments(string part, ProgrammerSelection programmer)
    {
        var executable = FindAvrdudeExecutable();
        var config = FindAvrdudeConfig();

        if (executable is null)
            throw new FileNotFoundException("Не найден Tools/AVRDUDE/avrdude.exe. Выполните Tools/prepare-tools.ps1.");
        if (config is null)
            throw new FileNotFoundException("Не найден avrdude.conf рядом с AVRDUDE.");

        var args = new List<string> { "-C", config };

        if (File.Exists(ExtraAvrdudeConfig))
        {
            args.Add("-C");
            args.Add("+" + ExtraAvrdudeConfig);
        }

        args.Add("-N");
        args.Add("-p");
        args.Add(part);
        args.Add("-c");
        args.Add(programmer.AvrdudeId);

        if (!string.IsNullOrWhiteSpace(programmer.Port))
        {
            args.Add("-P");
            args.Add(programmer.Port);
        }

        if (programmer.BaudRate.HasValue)
        {
            args.Add("-b");
            args.Add(programmer.BaudRate.Value.ToString(CultureInfo.InvariantCulture));
        }

        args.Add("-v");
        return args;
    }

    private async Task<bool> RunMemoryOperationAsync(
        DeviceProfile device,
        ProgrammerSelection programmer,
        string memory,
        string operation,
        string filePath,
        string format)
    {
        var args = BuildBaseArguments(device.AvrdudePart, programmer);
        args.Add("-U");
        args.Add($"{memory}:{operation}:{filePath}:{format}");
        var result = await RunAvrdudeAsync(args);
        return result.Success;
    }

    private async Task<AvrdudeResult> RunAvrdudeAsync(List<string> args)
    {
        var executable = FindAvrdudeExecutable();
        if (executable is null)
            throw new FileNotFoundException("Не найден Tools/AVRDUDE/avrdude.exe. Выполните Tools/prepare-tools.ps1.");

        var startInfo = new ProcessStartInfo
        {
            FileName = executable,
            WorkingDirectory = Path.GetDirectoryName(executable)!,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            StandardOutputEncoding = Encoding.UTF8,
            StandardErrorEncoding = Encoding.UTF8
        };

        foreach (var argument in args)
            startInfo.ArgumentList.Add(argument);

        Console.ForegroundColor = ConsoleColor.DarkGray;
        Console.WriteLine("> avrdude " + string.Join(" ", args.Select(QuoteArgument)));
        Console.ResetColor();

        using var process = new Process { StartInfo = startInfo };
        process.Start();

        var stdoutTask = process.StandardOutput.ReadToEndAsync();
        var stderrTask = process.StandardError.ReadToEndAsync();
        await process.WaitForExitAsync();

        var stdout = await stdoutTask;
        var stderr = await stderrTask;
        var output = (stdout + Environment.NewLine + stderr).Trim();

        if (!string.IsNullOrWhiteSpace(output))
            Console.WriteLine(output);

        if (process.ExitCode == 0)
            return new AvrdudeResult(true, 0, output);

        WriteError("AVRDUDE завершился с кодом " + process.ExitCode + ".");
        return new AvrdudeResult(false, process.ExitCode, output);
    }

    private string? FindAvrdudeExecutable()
    {
        var directory = Path.Combine(_root, "Tools", "AVRDUDE");
        if (!Directory.Exists(directory))
            return null;

        return Directory.GetFiles(directory, "avrdude.exe", SearchOption.AllDirectories).FirstOrDefault();
    }

    private string? FindAvrdudeConfig()
    {
        var directory = Path.Combine(_root, "Tools", "AVRDUDE");
        if (!Directory.Exists(directory))
            return null;

        return Directory.GetFiles(directory, "avrdude.conf", SearchOption.AllDirectories).FirstOrDefault();
    }

    private string NewBackupPath(DeviceProfile device, string suffix)
    {
        Directory.CreateDirectory(BackupDirectory);
        var stamp = DateTime.Now.ToString("yyyyMMdd-HHmmss-fff", CultureInfo.InvariantCulture);
        return Path.Combine(BackupDirectory, $"{stamp}_{device.Id}_{suffix}.hex");
    }

    private void LaunchZadig()
    {
        var zadig = Path.Combine(_root, "Tools", "Zadig.exe");
        if (!File.Exists(zadig))
        {
            WriteError("Zadig.exe не найден. Выполните Tools/prepare-tools.ps1.");
            return;
        }

        Console.WriteLine("Zadig будет запущен отдельно.");
        Console.WriteLine("Выбирайте только устройство USBasp. Не меняйте драйверы других USB-устройств.");
        Console.WriteLine("Для обычного USBasp используйте WinUSB, если текущий драйвер несовместим.");
        Console.WriteLine("Не запускайте Zadig для Arduino Nano: она работает через COM-порт.");
        Process.Start(new ProcessStartInfo { FileName = zadig, UseShellExecute = true });
    }

    private static bool Confirm(string question)
    {
        while (true)
        {
            Console.Write(question + " ДА или НЕТ [НЕТ]: ");
            var answer = Console.ReadLine()?.Trim();

            if (string.IsNullOrEmpty(answer) ||
                string.Equals(answer, "НЕТ", StringComparison.OrdinalIgnoreCase))
                return false;

            if (string.Equals(answer, "ДА", StringComparison.OrdinalIgnoreCase))
                return true;

            WriteError("Введите ДА или НЕТ.");
        }
    }

    private static string FormatAllowedValues(IReadOnlyList<uint> values)
    {
        var items = values.Select(value => value.ToString(CultureInfo.InvariantCulture)).ToArray();

        return items.Length switch
        {
            0 => "",
            1 => items[0],
            2 => $"{items[0]} или {items[1]}",
            _ => string.Join(", ", items[..^1]) + " или " + items[^1]
        };
    }

    private static string FormatUnit(string? unit)
    {
        return string.IsNullOrWhiteSpace(unit) ? "" : " " + unit;
    }

    private static string QuoteArgument(string value)
    {
        return value.Contains(' ') ? "\"" + value.Replace("\"", "\\\"") + "\"" : value;
    }

    private static void WriteError(string message)
    {
        Console.ForegroundColor = ConsoleColor.Red;
        Console.WriteLine("ОШИБКА: " + message);
        Console.ResetColor();
    }

    private static void WriteSuccess(string message)
    {
        Console.ForegroundColor = ConsoleColor.Green;
        Console.WriteLine(message);
        Console.ResetColor();
    }
}
