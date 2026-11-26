    unsigned sum = EEPROM_RECORD_SIZE + ((address >> 8) & 0xFFu) + (address & 0xFFu);
    unsigned i;
    unsigned char checksum;
    char *position = destination;
    position += sprintf(position, ":10%04X00", address);
    for (i = 0; i < EEPROM_RECORD_SIZE; ++i) {
        sum += data[i];
        position += sprintf(position, "%02X", data[i]);
    }
    checksum = (unsigned char)(0u - (sum & 0xFFu));
    sprintf(position, "%02X\r\n", checksum);
}

static int save_eeprom_file(const wchar_t *path, const EepromSettings *settings)
{
    wchar_t temporary[PATH_CAP];
    FILE *file;
    char line[64];
    unsigned char data[EEPROM_RECORD_SIZE];
    unsigned i;
    size_t root_length = wcslen(path);
    if (root_length + 5 >= PATH_CAP) return 0;
    wcscpy(temporary, path);
    wcscat(temporary, L".tmp");
    file = open_file(temporary, "wb");
    if (!file) return 0;
    for (i = 0; i < EEPROM_RECORDS; ++i) {
        char value[24];
        int text_length;
        memset(data, 0, sizeof(data));
        sprintf(value, "%s=%u", g_parameters[i].tag, (unsigned)settings->value[i]);
        text_length = (int)strlen(value);
        if (text_length >= EEPROM_RECORD_SIZE) { fclose(file); return 0; }
        memcpy(data, value, (size_t)text_length);
        make_intel_record(line, g_parameters[i].address, data);
        if (fwrite(line, 1, strlen(line), file) != strlen(line)) { fclose(file); return 0; }
    }
    if (fwrite(":00000001FF\r\n", 1, 13, file) != 13) { fclose(file); return 0; }
    if (fflush(file) != 0) { fclose(file); return 0; }
    fclose(file);
    if (!move_file_replace(temporary, path)) return 0;
    return 1;
}

static void default_settings(EepromSettings *settings)
