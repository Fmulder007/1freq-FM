}

static int validate_eeprom_file(const wchar_t *path, EepromSettings *settings)
{
    FILE *file = open_file(path, "rb");
    char line[600];
    unsigned record_number = 0;
    int eof_seen = 0;
    int valid = 1;
    if (!file) return 0;
    while (fgets(line, sizeof(line), file)) {
        size_t length = strlen(line);
        IntelHexRecord record;
        while (length && (line[length - 1] == '\r' || line[length - 1] == '\n')) line[--length] = '\0';
        if (length == 0) continue;
        if (eof_seen || !parse_intel_record(line, &record)) { valid = 0; break; }
        if (record_number < EEPROM_RECORDS) {
            uint32_t value;
            if (record.type != 0 || record.count != EEPROM_RECORD_SIZE ||
                record.address != g_parameters[record_number].address ||
                !parse_parameter_value(record.data, record_number, &value)) {
                valid = 0;
                break;
            }
            if (settings) settings->value[record_number] = value;
            record_number++;
        } else {
            if (record.type != 1 || record.count != 0 || record.address != 0) { valid = 0; break; }
            eof_seen = 1;
        }
    }
    if (ferror(file)) valid = 0;
    fclose(file);
    return valid && record_number == EEPROM_RECORDS && eof_seen;
}

static int validate_flash_file(const wchar_t *path, uint32_t capacity)
{
    FILE *file = open_file(path, "rb");
    char line[600];
    uint32_t upper_address = 0;
    uint32_t highest_address = 0;
    int eof_seen = 0;
    int has_data = 0;
    int valid = 1;