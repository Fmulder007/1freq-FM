}

static int file_exists(const wchar_t *path)
{
    FILE *file = open_file(path, "rb");
    if (!file) return 0;
    fclose(file);
    return 1;
}

static int move_file_replace(const wchar_t *source, const wchar_t *destination)
{
#ifdef _WIN32
    return MoveFileExW(source, destination, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    char src[PATH_CAP], dst[PATH_CAP];
    if (wcstombs(src, source, sizeof(src) - 1) == (size_t)-1) return 0;
    if (wcstombs(dst, destination, sizeof(dst) - 1) == (size_t)-1) return 0;
    src[sizeof(src) - 1] = '\0';
    dst[sizeof(dst) - 1] = '\0';
    remove(dst);
    return rename(src, dst) == 0;
#endif
}

static int hex_nibble(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

static int parse_hex_byte(const char *text, unsigned char *value)
{
    int high = hex_nibble(text[0]);
    int low = hex_nibble(text[1]);
    if (high < 0 || low < 0) return 0;
    *value = (unsigned char)((high << 4) | low);
    return 1;
}

static int parse_intel_record(const char *line, IntelHexRecord *record)
{
    size_t length = strlen(line);
