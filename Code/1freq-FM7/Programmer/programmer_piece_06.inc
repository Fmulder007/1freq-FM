    unsigned char checksum;
    unsigned sum;
    unsigned i;
    if (length < 11 || line[0] != ':') return 0;
    {
        unsigned char count, ah, al, type;
        if (!parse_hex_byte(line + 1, &count) || !parse_hex_byte(line + 3, &ah) ||
            !parse_hex_byte(line + 5, &al) || !parse_hex_byte(line + 7, &type)) return 0;
        record->count = count;
        record->address = ((unsigned)ah << 8) | al;
        record->type = type;
    }
    if (length != 11u + record->count * 2u) return 0;
    sum = record->count + ((record->address >> 8) & 0xFFu) + (record->address & 0xFFu) + record->type;
    for (i = 0; i < record->count; ++i) {
        if (!parse_hex_byte(line + 9 + i * 2, &record->data[i])) return 0;
        sum += record->data[i];
    }
    if (!parse_hex_byte(line + 9 + record->count * 2, &checksum)) return 0;
    sum += checksum;
    return (sum & 0xFFu) == 0;
}

static int parse_parameter_value(const unsigned char data[EEPROM_RECORD_SIZE], unsigned index, uint32_t *value)
{
    const char *tag = g_parameters[index].tag;
    unsigned i = 3;
    uint64_t number = 0;
    int digit_count = 0;
    if (data[0] != (unsigned char)tag[0] || data[1] != (unsigned char)tag[1] || data[2] != '=') return 0;
    while (i < EEPROM_RECORD_SIZE && data[i] >= '0' && data[i] <= '9') {
        number = number * 10u + (unt64_t)(data[i] - '0');
        if (number > 0xFFFFFFFFu) return 0;
        digit_count++;
        i++;
    }
    if (!digit_count) return 0;
    while (i < EEPROM_RECORD_SIZE) {
        if (data[i] != 0) return 0;
        i++;
    }
    if (number < g_parameters[index].min_value || number > g_parameters[index].max_value) return 0;
    if (index == 1 && number != 5u && number != 6u) return 0;
    *value = (uint32_t)number;
    return 1;
