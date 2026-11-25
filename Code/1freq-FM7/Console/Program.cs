using System.Text;

internal static class Program
{
    private static async Task<int> Main(string[] args)
    {
        Console.OutputEncoding = Encoding.UTF8;
        Console.InputEncoding = Encoding.UTF8;

        try
        {
            if (args.Any(x => string.Equals(x, "--self-test", StringComparison.OrdinalIgnoreCase)))
            {
                RunSelfTest();
                return 0;
            }

            var app = new ConsoleApp();
            await app.RunAsync();
            return 0;
        }
        catch (Exception ex)
        {
            Console.ForegroundColor = ConsoleColor.Red;
            Console.WriteLine();
            Console.WriteLine("Критическая ошибка: " + ex.Message);
            Console.ResetColor();
            Console.WriteLine("Подробности: " + ex);
            return 1;
        }
    }

    private static void RunSelfTest()
    {
        var root = AppContext.BaseDirectory;
        var catalog = CatalogLoader.Load(Path.Combine(root, "Config", "devices.json"));
        var values = EepromHex.CreateDefaults(catalog.EepromLayout);
        var hex = EepromHex.Build(catalog.EepromLayout, values);

        var temp = Path.Combine(Path.GetTempPath(), "1freq-fm-selftest-" + Guid.NewGuid().ToString("N") + ".hex");
        try
        {
            File.WriteAllText(temp, hex, Encoding.ASCII);
            var maxAddress = IntelHexInspector.ValidateFile(temp, 256);
            var expectedBytes = catalog.EepromLayout.Parameters.Count * catalog.EepromLayout.RecordSize;

            if (maxAddress != expectedBytes)
                throw new InvalidDataException($"Самопроверка EEPROM: адрес окончания {maxAddress}, ожидалось {expectedBytes}.");

            if (!hex.Contains(":00000001FF", StringComparison.Ordinal))
                throw new InvalidDataException("В Intel HEX отсутствует завершающая запись.");

            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine("SELF-TEST OK");
            Console.ResetColor();
            foreach (var item in catalog.Firmwares.Where(item => item.Value.Enabled))
            {
                var firmwarePath = Path.Combine(root, "Firmware", item.Key);
                if (!File.Exists(firmwarePath))
                    throw new FileNotFoundException("Не найден HEX из каталога: " + item.Key, firmwarePath);

                var flashEnd = IntelHexInspector.ValidateFile(firmwarePath, item.Value.FlashBytes);
                Console.WriteLine($"HEX OK: {item.Key}, занято {flashEnd} из {item.Value.FlashBytes} байт Flash.");
            }

            Console.WriteLine($"Профилей прошивок: {catalog.Firmwares.Count}");
            Console.WriteLine($"EEPROM FM7: {expectedBytes} байт, контрольные суммы Intel HEX корректны.");
        }
        finally
        {
            try { File.Delete(temp); } catch { }
        }
    }
}
