using System.Diagnostics;
using System.Globalization;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.Win32;

internal sealed class ConsoleApp
{
    private static readonly object AvrdudeOutputLock = new();

    private readonly string _root;
    private readonly FirmwareCatalog _catalog;
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
    private string EepromHexPath => Path.Combine(DataDirectory, "eeprom.hex");

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
            Console.WriteLine("  2. Прошить микросинтезатор (Flash + EEPROM)");
            Console.WriteLine("  3. Записать только EEPROM");
            Console.WriteLine("  4. Считать EEPROM в резервную копию");
            Console.WriteLine("  5. Настроить USB-драйвер (Zadig)");
            Console.WriteLine("  0. Выход");
            Console.WriteLine();

            Console.Write("Введите номер от 0 до 5 [0]: ");
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
                        await FlashTargetAsync();
                        break;
                    case "3":
                        await WriteEepromOnlyAsync();
                        break;
                    case "4":
                        await ReadEepromAsync();
                        break;
                    case "5":
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
        Console.WriteLine("Каталог МК: " + _catalog.Firmwares.Count + " профиля(ей)");
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

    private FirmwareProfile? SelectFirmware()
    {
        var items = _catalog.Firmwares
            .Where(item => item.Value.Enabled)
            .ToList();

        if (items.Count == 0)
        {
            WriteError("В Config/devices.json нет включённых профилей прошивок.");
            return null;
        }

        Console.WriteLine();
        Console.WriteLine("Выберите микроконтроллер:");
        for (var i = 0; i < items.Count; i++)
            Console.WriteLine($"  {i + 1}. {items[i].Value.Name} — {items[i].Key}");
        Console.WriteLine("  0. Назад");
        Console.Write($"Введите номер от 0 до {items.Count} [0]: ");

        var input = Console.ReadLine()?.Trim();
        if (string.IsNullOrEmpty(input) || input == "0")
            return null;

        if (!int.TryParse(input, NumberStyles.None, CultureInfo.InvariantCulture, out var index) ||
            index < 1 || index > items.Count)
        {
            WriteError("Выбран неверный номер микроконтроллера.");
            return null;
        }

        return items[index - 1].Value;
    }

    private async Task FlashTargetAsync()
    {
        var firmware = SelectFirmware();
        if (firmware is null)
            return;

        var programmer = SelectProgrammer();
        if (programmer is null || !CheckProgrammerAllowed(firmware, programmer))
            return;

        var firmwarePath = Path.Combine(FirmwareDirectory, firmware.FileName);
        if (!File.Exists(firmwarePath))
        {
            WriteError($"Не найден HEX для {firmware.Name}: {firmwarePath}");
            return;
        }

        var flashEnd = IntelHexInspector.ValidateFile(firmwarePath, firmware.FlashBytes);
        var settings = ReadSettings();
        if (settings is null)
        {
            WriteError("Сначала создайте настройки EEPROM через пункт меню 1.");
            return;
        }

        SaveSettingsAndHex(settings);
        var writeFuses = SelectFuseProgramming(firmware);
        if (writeFuses is null)
            return;

        var backupPath = NewBackupPath(firmware, "before-flash");

        Console.WriteLine();
        PrintFirmware(firmware);
        Console.WriteLine($"Размер HEX: адреса 0…{flashEnd - 1}, Flash МК: {firmware.FlashBytes} байт.");
        Console.WriteLine("Перед записью Flash AVRDUDE выполнит Chip Erase. Содержимое EEPROM предварительно сохраняется.");
        Console.WriteLine("После прошивки будут записаны новые параметры FM7.");
        Console.WriteLine();
        Console.WriteLine("Резервная копия EEPROM: " + backupPath);

        if (!Confirm("Начать прошивку?", ConsoleColor.Yellow))
            return;

        Console.WriteLine();
        Console.WriteLine("1/5. Считывание EEPROM перед стиранием...");
        if (!await RunMemoryOperationAsync(firmware, programmer, "eeprom", "r", backupPath, "i"))
        {
            WriteError("Не удалось сохранить EEPROM. Прошивка отменена.");
            return;
        }

        if (writeFuses == true)
        {
            Console.WriteLine("2/5. Запись и проверка Fuse-битов...");
            if (!await RunFuseSetAsync(firmware, programmer, "w") ||
                !await RunFuseSetAsync(firmware, programmer, "v"))
            {
                WriteError("Не удалось записать или проверить Fuse-биты. Запись Flash отменена; резервная копия EEPROM сохранена.");
                return;
            }
        }
        else
        {
            Console.WriteLine("2/5. Fuse-биты не изменяются.");
        }

        Console.WriteLine("3/5. Запись Flash...");
        if (!await RunMemoryOperationAsync(firmware, programmer, "flash", "w", firmwarePath, "i"))
        {
            WriteError("Запись Flash завершилась ошибкой. Резервная копия EEPROM сохранена.");
            return;
        }

        Console.WriteLine("4/5. Проверка Flash...");
        if (!await RunMemoryOperationAsync(firmware, programmer, "flash", "v", firmwarePath, "i"))
        {
            WriteError("Проверка Flash не пройдена. EEPROM не изменялась после неудачной проверки.");
            return;
        }

        Console.WriteLine("5/5. Запись и проверка EEPROM...");
        if (!await RunMemoryOperationAsync(firmware, programmer, "eeprom", "w", EepromHexPath, "i") ||
            !await RunMemoryOperationAsync(firmware, programmer, "eeprom", "v", EepromHexPath, "i"))
        {
            WriteError("Не удалось записать или проверить EEPROM. Резервная копия сохранена.");
            return;
        }

        WriteSuccess(writeFuses == true
            ? "Fuse-биты, Flash и настройки микросинтезатора записаны и проверены."
            : "Flash и настройки микросинтезатора записаны и проверены; Fuse-биты не изменялись.");
    }

    private bool? SelectFuseProgramming(FirmwareProfile firmware)
    {
        var fuses = firmware.Fuses;
        var available = new List<(string Name, string? Value)>
        {
            ("LFUSE", fuses?.Low),
            ("HFUSE", fuses?.High),
            ("EFUSE", fuses?.Extended)
        }.Where(item => !string.IsNullOrWhiteSpace(item.Value)).ToList();

        if (available.Count == 0)
        {
            Console.WriteLine("В профиле этого МК fuse-значения не заданы. Fuse-биты не изменяются.");
            return false;
        }

        Console.WriteLine();
        Console.WriteLine("Fuse-биты из профиля " + firmware.Name + ":");
        foreach (var item in available)
            Console.WriteLine($"  {item.Name} = 0x{item.Value!.ToUpperInvariant()}");
        Console.WriteLine();
        Console.WriteLine("  1. Прошить HEX и записать эти Fuse-биты");
        Console.WriteLine("  2. Прошить только HEX, не меняя Fuse-биты");
        Console.WriteLine("  0. Назад");
        Console.Write("Введите 0, 1 или 2 [1]: ");

        var choice = Console.ReadLine()?.Trim();
        if (string.IsNullOrEmpty(choice))
            choice = "1";

        return choice switch
        {
            "1" => true,
            "2" => false,
            "0" => null,
            _ => InvalidFuseChoice()
        };
    }

    private static bool? InvalidFuseChoice()
    {
        WriteError("Неизвестный вариант. Прошивка отменена.");
        return null;
    }

    private async Task<bool> RunFuseSetAsync(
        FirmwareProfile firmware,
        ProgrammerSelection programmer,
        string operation)
    {
        var args = BuildBaseArguments(firmware.AvrdudePart, programmer);
        var fuses = firmware.Fuses;

        var writes = new List<(string Memory, string? Value)>
        {
            ("lfuse", fuses?.Low),
            ("hfuse", fuses?.High),
            ("efuse", fuses?.Extended)
        };

        foreach (var item in writes.Where(item => !string.IsNullOrWhiteSpace(item.Value)))
        {
            args.Add("-U");
            args.Add($"{item.Memory}:{operation}:0x{item.Value}:m");
        }

        var result = await RunAvrdudeAsync(args);
        return result.Success;
    }

    private async Task WriteEepromOnlyAsync()
    {
        var firmware = SelectFirmware();
        if (firmware is null)
            return;

        var programmer = SelectProgrammer();
        if (programmer is null || !CheckProgrammerAllowed(firmware, programmer))
            return;

        var settings = ReadSettings();
        if (settings is null)
        {
            WriteError("Сначала создайте настройки EEPROM через пункт меню 1.");
            return;
        }

        SaveSettingsAndHex(settings);
        var backupPath = NewBackupPath(firmware, "before-eeprom-write");

        Console.WriteLine();
        PrintFirmware(firmware);
        PrintSettings(settings);
        Console.WriteLine("Текущая EEPROM будет сохранена в: " + backupPath);

        if (!Confirm("Записать новые настройки EEPROM?"))
            return;

        if (!await RunMemoryOperationAsync(firmware, programmer, "eeprom", "r", backupPath, "i"))
        {
            WriteError("Не удалось сохранить текущую EEPROM. Запись отменена.");
            return;
        }

        if (!await RunMemoryOperationAsync(firmware, programmer, "eeprom", "w", EepromHexPath, "i"))
        {
            WriteError("Ошибка записи EEPROM. Резервная копия сохранена.");
            return;
        }

        if (!await RunMemoryOperationAsync(firmware, programmer, "eeprom", "v", EepromHexPath, "i"))
        {
            WriteError("Проверка EEPROM не пройдена.");
            return;
        }

        WriteSuccess("EEPROM записана и проверена.");
    }

    private async Task ReadEepromAsync()
    {
        var firmware = SelectFirmware();
        if (firmware is null)
            return;

        var programmer = SelectProgrammer();
        if (programmer is null || !CheckProgrammerAllowed(firmware, programmer))
            return;

        var backupPath = NewBackupPath(firmware, "read");
        Console.WriteLine();
        PrintFirmware(firmware);
        Console.WriteLine("Файл резервной копии: " + backupPath);

        if (!await RunMemoryOperationAsync(firmware, programmer, "eeprom", "r", backupPath, "i"))
        {
            WriteError("Не удалось прочитать EEPROM.");
            return;
        }

        WriteSuccess("EEPROM сохранена: " + backupPath);
    }

    private bool CheckProgrammerAllowed(FirmwareProfile firmware, ProgrammerSelection programmer)
    {
        if (firmware.SupportedProgrammers.Contains(programmer.Id, StringComparer.OrdinalIgnoreCase))
            return true;

        WriteError($"Программатор {programmer.DisplayName} не разрешён в профиле {firmware.Name}.");
        return false;
    }

    private static void PrintFirmware(FirmwareProfile firmware)
    {
        Console.WriteLine($"МК: {firmware.Name}");
        Console.WriteLine($"HEX: {firmware.FileName}");
        Console.WriteLine($"AVRDUDE part: {firmware.AvrdudePart}");
        Console.WriteLine($"Flash: {firmware.FlashBytes} байт; EEPROM: {firmware.EepromBytes} байт");
        if (!string.IsNullOrWhiteSpace(firmware.Notes))
            Console.WriteLine("Примечание: " + firmware.Notes);
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

            var ports = new List<string>();
            foreach (var name in key.GetValueNames())
            {
                var value = key.GetValue(name)?.ToString();
                if (!string.IsNullOrWhiteSpace(value))
                    ports.Add(value);
            }

            return ports
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
        args.Add("-F"); // Пользователь сам выбирает профиль; сверку сигнатуры не выполняем.
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

        // USBasp supports an explicit ISP SCK rate. Use its 32 kHz setting
        // instead of relying on the programmer firmware's AUTO default.
        // ArduinoISP/STK500v1 does not support AVRDUDE bitclock control.
        if (string.Equals(programmer.Id, "usbasp", StringComparison.OrdinalIgnoreCase))
        {
            args.Add("-B");
            args.Add("32kHz");
        }

        args.Add("-v");
        return args;
    }

    private async Task<bool> RunMemoryOperationAsync(
        FirmwareProfile firmware,
        ProgrammerSelection programmer,
        string memory,
        string operation,
        string filePath,
        string format)
    {
        var args = BuildBaseArguments(firmware.AvrdudePart, programmer);
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

        // AVRDUDE writes its progress to stderr. Drain both redirected streams
        // concurrently and forward each received block immediately, while also
        // retaining the complete output for the operation result.
        var stdoutTask = ForwardAvrdudeStreamAsync(process.StandardOutput, Console.Out);
        var stderrTask = ForwardAvrdudeStreamAsync(process.StandardError, Console.Error);

        await process.WaitForExitAsync();

        var stdout = await stdoutTask;
        var stderr = await stderrTask;
        var output = (stdout + Environment.NewLine + stderr).Trim();

        if (process.ExitCode == 0)
            return new AvrdudeResult(true, 0, output);

        WriteError("AVRDUDE завершился с кодом " + process.ExitCode + ".");
        return new AvrdudeResult(false, process.ExitCode, output);
    }

    private static async Task<string> ForwardAvrdudeStreamAsync(StreamReader reader, TextWriter destination)
    {
        var captured = new StringBuilder();
        var buffer = new char[256];

        while (true)
        {
            var count = await reader.ReadAsync(buffer, 0, buffer.Length);
            if (count == 0)
                break;

            captured.Append(buffer, 0, count);

            // stdout and stderr can arrive simultaneously. Keep each block
            // intact so the two readers cannot overwrite or interleave writes.
            lock (AvrdudeOutputLock)
            {
                destination.Write(buffer, 0, count);
                destination.Flush();
            }
        }

        return captured.ToString();
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

    private string NewBackupPath(FirmwareProfile firmware, string suffix)
    {
        Directory.CreateDirectory(BackupDirectory);
        var stamp = DateTime.Now.ToString("yyyyMMdd-HHmmss-fff", CultureInfo.InvariantCulture);
        return Path.Combine(BackupDirectory, $"{stamp}_{Path.GetFileNameWithoutExtension(firmware.FileName)}_{suffix}.hex");
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

    private static bool Confirm(string question, ConsoleColor? promptColor = null)
    {
        while (true)
        {
            var previousColor = Console.ForegroundColor;
            try
            {
                if (promptColor.HasValue)
                    Console.ForegroundColor = promptColor.Value;

                Console.Write(question + " (1 — да, 0 — нет) [0]: ");
            }
            finally
            {
                Console.ForegroundColor = previousColor;
            }

            var answer = Console.ReadLine()?.Trim();

            if (string.IsNullOrEmpty(answer) || answer == "0")
                return false;

            if (answer == "1")
                return true;

            WriteError("Введите 1 (да) или 0 (нет).");
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
