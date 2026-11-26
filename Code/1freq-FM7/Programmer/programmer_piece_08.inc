    if (!file) return 0;
    while (fgets(line, sizeof(line), file)) {
        size_t length = strlen(line);
        IntelHexRecord record;
        uint64_t absolute, end_address;
        while (length && (line[length - 1] == '\r' || line[length - 1] == '\n')) line[--length] = '\0';
        if (!length) continue;
        if (eof_seen || !parse_intel_record(line, &record)) { valid = 0; break; }
        switch (record.type) {
            case 0:
                absolute = (uint64_t)upper_address + record.address;
                end_address = absolute + record.count;
                if (end_address > capacity) { valid = 0; break; }
                if (record.count) has_data = 1;
                if (end_address > highest_address) highest_address = (uint32_t)end_address;
                break;
            case 1:
                if (record.count != 0 || record.address != 0) valid = 0;
                else eof_seen = 1;
                break;
            case 2:
                if (record.count != 2) valid = 0;
                else upper_address = ((uint32_t)record.data[0] << 8 | record.data[1]) << 4;
                break;
            case 4:
                if (record.count != 2) valid = 0;
                else upper_address = ((uint32_t)record.data[0] << 8 | record.data[1]) << 16;
                break;
            case 3:
            case 5:
                if (record.count != 4) valid = 0;
                break;
            default:
                valid = 0;
                break;
        }
        if (!valid) break;
    }
    if (ferror(file)) valid = 0;
    fclose(file);
    return valid && eof_seen && has_data && highest_address > 0;
}

static void make_intel_record(char *destination, unsigned address, const unsigned char data[EEPROM_RECORD_SIZE])
{
