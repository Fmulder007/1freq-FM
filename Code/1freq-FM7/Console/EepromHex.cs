using System.Globalization;
using System.Text;

internal static class EepromHex
{
    public static Dictionary<string, uint> CreateDefaults(EepromLayout layout)
    {
        return layout.Parameters.ToDictionary(p => p.Tag, p => p.DefaultValue, StringComparer.OrdinalIgnoreCase);
    }

    public static bool IsValueAllowed(EepromParameter parameter, uint value)
    {
        if (value < parameter.MinValue || value > parameter.MaxValue)
            return false;
        return parameter.AllowedValues.Count == 0 || parameter.AllowedValues.Contains(value);
    }

    public static bool ValidateSettings(
        EepromLayout layout,
        IReadOnlyDictionary<string, uint> values,
        out string error)
    {
        foreach (var parameter in layout.Parameters)
        {
            if (!values.TryGetValue(parameter.Tag, out var value))
            {
                error = $"Не задан параметр {parameter.Tag}.";
                return false;
            }
            if (!IsValueAllowed(parameter, value))
            {
                error = $"Недопустимое значение {parameter.Tag}={value}.";
                return false;
            }
        }

        error = "";
        return true;
    }

    public static string Build(EepromLayout layout, IReadOnlyDictionary<string, uint> values)
    {
        if (!ValidateSettings(layout, values, out var error))
            throw new InvalidDataException(error);

        var lines = new List<string>();
        foreach (var parameter in layout.Parameters.OrderBy(p => p.Address))
        {
            var value = values[parameter.Tag];
            var text = parameter.Tag + "=" + value.ToString(CultureInfo.InvariantCulture);
            var data = Encoding.ASCII.GetBytes(text);
            if (data.Length >= layout.RecordSize)
                throw new InvalidDataException($"Значение {parameter.Tag} не помещается в запись EEPROM.");

            var block = new byte[layout.RecordSize];
            Buffer.BlockCopy(data, 0, block, 0, data.Length);
            lines.Add(CreateRecord(parameter.Address, block));
        }

        lines.Add(":00000001FF");
        return string.Join(Environment.NewLine, lines) + Environment.NewLine;
    }

    private static string CreateRecord(int address, byte[] data)
    {
        if (address < 0 || address > ushort.MaxValue || data.Length > byte.MaxValue)
            throw new InvalidDataException("Адрес или размер записи Intel HEX выходит за допустимые пределы.");

        var sum = data.Length + ((address >> 8) & 0xFF) + (address & 0xFF);
        var builder = new StringBuilder();
        builder.Append(':');
        builder.Append(data.Length.ToString("X2", CultureInfo.InvariantCulture));
        builder.Append(address.ToString("X4", CultureInfo.InvariantCulture));
        builder.Append("00");

        foreach (var item in data)
        {
            sum += item;
            builder.Append(item.ToString("X2", CultureInfo.InvariantCulture));
        }

        var checksum = (byte)((256 - (sum & 0xFF)) & 0xFF);
        builder.Append(checksum.ToString("X2", CultureInfo.InvariantCulture));
        return builder.ToString();
    }
}

internal static class IntelHexInspector
{
    public static long ValidateFile(string path, long capacityBytes)
    {
        if (!File.Exists(path))
            throw new FileNotFoundException("HEX-файл не найден.", path);

        var lines = File.ReadAllLines(path);
        if (lines.Length == 0)
            throw new InvalidDataException("HEX-файл пуст.");

        uint upperAddress = 0;
        long highestAddress = 0;
        var sawEof = false;

        foreach (var rawLine in lines)
        {
            var line = rawLine.Trim();
            if (line.Length == 0)
                continue;
            if (sawEof)
                throw new InvalidDataException("В Intel HEX обнаружены данные после EOF.");
            if (!line.StartsWith(':') || line.Length < 11)
                throw new InvalidDataException("Некорректная строка Intel HEX: " + line);

            var count = Convert.ToInt32(line.Substring(1, 2), 16);
            var address = Convert.ToInt32(line.Substring(3, 4), 16);
            var type = Convert.ToInt32(line.Substring(7, 2), 16);
            if (line.Length != 11 + count * 2)
                throw new InvalidDataException("Неверная длина записи Intel HEX: " + line);

            var data = Convert.FromHexString(line.Substring(9, count * 2));
            var checksum = Convert.ToByte(line.Substring(9 + count * 2, 2), 16);
            var sum = count + ((address >> 8) & 0xFF) + (address & 0xFF) + type;
            foreach (var item in data)
                sum += item;
            sum += checksum;

            if ((sum & 0xFF) != 0)
                throw new InvalidDataException("Ошибка контрольной суммы Intel HEX: " + line);

            switch (type)
            {
                case 0x00:
                {
                    var absoluteAddress = (ulong)upperAddress + (uint)address;
                    var endAddress = absoluteAddress + (uint)count;
                    if (endAddress > (ulong)capacityBytes)
                        throw new InvalidDataException($"HEX-файл превышает объём памяти: конец записи {endAddress}, доступно {capacityBytes} байт.");
                    highestAddress = Math.Max(highestAddress, (long)endAddress);
                    break;
                }
                case 0x01:
                    if (count != 0 || address != 0)
                        throw new InvalidDataException("Некорректная запись EOF Intel HEX.");
                    sawEof = true;
                    break;
                case 0x02:
                    if (count != 2)
                        throw new InvalidDataException("Некорректная запись extended segment address.");
                    upperAddress = (uint)((data[0] << 8 | data[1]) << 4);
                    break;
                case 0x04:
                    if (count != 2)
                        throw new InvalidDataException("Некорректная запись extended linear address.");
                    upperAddress = (uint)((data[0] << 8 | data[1]) << 16);
                    break;
                case 0x03:
                case 0x05:
                    // Start address records do not occupy Flash memory.
                    break;
                default:
                    throw new InvalidDataException($"Неподдерживаемый тип записи Intel HEX: {type}.");
            }
        }

        if (!sawEof)
            throw new InvalidDataException("В Intel HEX отсутствует завершающая запись EOF.");
        if (highestAddress == 0)
            throw new InvalidDataException("В HEX-файле нет записей с данными.");

        return highestAddress;
    }
}
