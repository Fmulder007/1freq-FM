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
        sprintf(value, "%s=%llu", g_parameters[i].tag, (unsigned long long)settings->value[i]);
        text_length = (int)strlen(value);
        if (text_length >= EEPROM_RECORD_SIZE) {
            fclose(file);
            delete_file(temporary);
            return 0;
        }
        memcpy(data, value, (size_t)text_length);
        make_intel_record(line, g_parameters[i].address, data);
        if (fwrite(line, 1, strlen(line), file) != strlen(line)) {
            fclose(file);
            delete_file(temporary);
            return 0;
        }
    }
    if (fwrite(":00000001FF\r\n", 1, 13, file) != 13 || fflush(file) != 0) {
        fclose(file);
        delete_file(temporary);
        return 0;
    }
    if (fclose(file) != 0) {
        delete_file(temporary);
        return 0;
    }
    if (!move_file_replace(temporary, path)) {
        delete_file(temporary);
        return 0;
    }
    return 1;
}

/* Save the exact five raw EEPROM records without interpreting or changing their bytes. */
static int save_eeprom_raw_hex(const wchar_t *path, const unsigned char raw[EEPROM_RECORD_SIZE * EEPROM_RECORDS])
{
    FILE *file = open_file(path, "wb");
    char line[64];
    unsigned i;
    int ok = 1;
    if (!file) return 0;
    for (i = 0; i < EEPROM_RECORDS; ++i) {
        make_intel_record(line, g_parameters[i].address, raw + i * EEPROM_RECORD_SIZE);
        if (fwrite(line, 1, strlen(line), file) != strlen(line)) {
            ok = 0;
            break;
        }
    }
    if (ok && fwrite(":00000001FF\r\n", 1, 13, file) != 13) ok = 0;
    if (ok && fflush(file) != 0) ok = 0;
    if (fclose(file) != 0) ok = 0;
    if (!ok) delete_file(path);
    return ok;
}
