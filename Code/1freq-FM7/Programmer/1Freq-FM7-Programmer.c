#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <stdarg.h>
#include <locale.h>

#ifdef _WIN32
#ifndef WINVER
#define WINVER 0x0501
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#include <windows.h>
#endif

#define APP_TITLE L"1Freq-FM7 Programmer"
#define PATH_CAP 32768
#define INPUT_CAP 256
#define MAX_COMMAND 32768
#define EEPROM_RECORD_SIZE 16
#define EEPROM_RECORDS 5

typedef struct {
    const char *tag;
    unsigned address;
    uint32_t min_value;
    uint32_t max_value;
    uint32_t default_value;
    const wchar_t *description;
} EepromParameter;

typedef struct {
    uint32_t value[EEPROM_RECORDS];
} EepromSettings;

typedef struct {
    unsigned count;
    unsigned address;
    unsigned type;
    unsigned char data[256];
} IntelHexRecord;

static const EepromParameter g_parameters[EEPROM_RECORDS] = {
    {"XT", 0x00, 5000000u, 110000000u, 27007840u, L"XT â€” Ğ¾Ğ¿Ğ¾Ñ€Ğ½Ğ°Ñ Ñ‡Ğ°ÑÑ‚Ğ¾Ñ‚Ğ° ĞºĞ²Ğ°Ñ€Ñ†Ğ° Si5351, Ğ“Ñ†"},
    {"MD", 0x10, 5u, 6u, 5u, L"MD â€” Ñ€ĞµĞ¶Ğ¸Ğ¼ Ñ€Ğ°Ğ±Ğ¾Ñ‚Ñ‹ (5 Ğ¸Ğ»Ğ¸ 6)"},
    {"F1", 0x20, 15000u, 300000000u, 2000000u, L"F1 â€” Ñ‡Ğ°ÑÑ‚Ğ¾Ñ‚Ğ° Ğ¿Ñ€Ğ¸ Ğ¼Ğ¸Ğ½Ğ¸Ğ¼Ğ°Ğ»ÑŒĞ½Ğ¾Ğ¼ Ğ½Ğ°Ğ¿Ñ€ÑĞ¶ĞµĞ½Ğ¸Ğ¸ PC0, Ğ“Ñ†"},
    {"F2", 0x30, 15000u, 300000000u, 1000000u, L"F2 â€” Ñ‡Ğ°ÑÑ‚Ğ¾Ñ‚Ğ° Ğ¿Ñ€Ğ¸ Ğ¼Ğ°ĞºÑĞ¸Ğ¼Ğ°Ğ»ÑŒĞ½Ğ¾Ğ¼ Ğ½Ğ°Ğ¿Ñ€ÑĞ¶ĞµĞ½Ğ¸Ğ¸ PC0, Ğ“Ñ†"},
    {"ST", 0x40, 0u, 1024u, 0u, L"ST â€” ĞºĞ¾Ğ»Ğ¸Ñ‡ĞµÑÑ‚Ğ²Ğ¾ ÑˆĞ°Ğ³Ğ¾Ğ² (0 Ğ¾Ğ·Ğ½Ğ°Ñ‡Ğ°ĞµÑ‚ 1024)"}
};

static wchar_t g_root[PATH_CAP];

static void output_wide(const wchar_t *text)
{
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
        DWORD written = 0;
        WriteConsoleW(h, text, (DWORD)wcslen(text), &written, NULL);
        return;
    }
    {
        int bytes_needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
        if (bytes_needed > 1) {
            char *utf8 = (char *)malloc((size_t)bytes_needed);
            if (utf8) {
                DWORD written = 0;
                WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, bytes_needed, NULL, NULL);
                WriteFile(h, utf8, (DWORD)(bytes_needed - 1), &written, NULL);
                free(utf8);
            }
        }
    }
#else
    fputws(text, stdout);
    fflush(stdout);
#endif
}

static void output_format(const wchar_t *format, ...)
{
    wchar_t buffer[2048];
    va_list args;
    va_start(args, format);
    vswprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), format, args);
    va_end(args);
    buffer[(sizeof(buffer) / sizeof(buffer[0])) - 1] = L'\0';
    output_wide(buffer);
}

static int input_line(wchar_t *buffer, size_t capacity)
{
    if (!buffer || capacity < 2) return 0;
#ifdef _WIN32
    {
        HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
        DWORD read_count = 0;
        if (h == INVALID_HANDLE_VALUE || !ReadConsoleW(h, buffer, (DWORD)(capacity - 1), &read_count, NULL)) {
            buffer[0] = L'\0';
            return 0;
        }
        buffer[read_count] = L'\0';
    }
#else
    if (!fgetws(buffer, (int)capacity, stdin)) {
        buffer[0] = L'\0';
        return 0;
    }
#endif
    while (*buffer && (buffer[wcslen(buffer) - 1] == L'\r' || buffer[wcslen(buffer) - 1] == L'\n'))
        buffer[wcslen(buffer) - 1] = L'\0';
    return 1;
}

static void pause_for_enter(void)
{
    wchar_t line[INPUT_CAP];
    output_wide(L"\nĞĞ°Ğ¶Ğ¼Ğ¸Ñ‚Ğµ Enter Ğ´Ğ»Ñ Ğ²Ğ¾Ğ·Ğ²Ñ€Ğ°Ñ‚Ğ° Ğ² Ğ¼ĞµĞ½Ñ...");
    input_line(line, INPUT_CAP);
}

static void trim_wide(wchar_t *text)
{
    size_t start = 0;
    size_t length;
    while (text[start] && iswspace(text[start])) start++;
    if (start) memmove(text, text + start, (wcslen(text + start) + 1) * sizeof(wchar_t));
    length = wcslen(text);
    while (length && iswspace(text[length - 1])) text[--length] = L'\0';
}

static void join_path(wchar_t *destination, size_t capacity, const wchar_t *name)
{
    size_t length = wcslen(g_root);
    if (capacity == 0) return;
    destination[0] = L'\0';
    if (length + wcslen(name) + 2 >= capacity) return;
    wcscpy(destination, g_root);
#ifdef _WIN32
    if (length && g_root[length - 1] != L'\\' && g_root[length - 1] != L'/')
        wcscat(destination, L"\\");
#else
    if (length && g_root[length - 1] != L'/')
        wcscat(destination, L"/");
#endif
    wcscat(destination, name);
}

static void determine_root(void)
{
#ifdef _WIN32
    DWORD length = GetModuleFileNameW(NULL, g_root, PATH_CAP);
    if (!length || length >= PATH_CAP) {
        wcscpy(g_root, L".");
        return;
    }
    while (length && g_root[length - 1] != L'\\' && g_root[length - 1] != L'/') length--;
    if (length) g_root[length - 1] = L'\0';
    else wcscpy(g_root, L".");
#else
    wcscpy(g_root, L".");
#endif
}

static FILE *open_file(const wchar_t *path, const char *mode)
{
#ifdef _WIN32
    wchar_t wide_mode[8];
    size_t i;
    for (i = 0; mode[i] && i + 1 < 8; ++i) wide_mode[i] = (wchar_t)mode[i];
    wide_mode[i] = L'\0';
    return _wfopen(path, wide_mode);
#else
    char narrow[PATH_CAP];
    if (wcstombs(narrow, path, sizeof(narrow) - 1) == (size_t)-1) return NULL;
    narrow[sizeof(narrow) - 1] = '\0';
    return fopen(narrow, mode);
#endif
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
        number = number * 10u + (uint64_t)(data[i] - '0');
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
{
    unsigned i;
    for (i = 0; i < EEPROM_RECORDS; ++i) settings->value[i] = g_parameters[i].default_value;
}

static void show_settings(const EepromSettings *settings)
{
    unsigned i;
    for (i = 0; i < EEPROM_RECORDS; ++i)
        output_format(L"  %hs=%u\n", g_parameters[i].tag, (unsigned)settings->value[i]);
}

static int parse_input_value(const wchar_t *text, uint32_t *value)
{
    wchar_t *end = NULL;
    unsigned long number;
    if (!text || !*text) return 0;
    if (*text == L'-' || *text == L'+') return 0;
    number = wcstoul(text, &end, 10);
    if (end == text || *end != L'\0' || number > 0xFFFFFFFFul) return 0;
    *value = (uint32_t)number;
    return 1;
}

static int edit_eeprom_file(void)
{
    wchar_t path[PATH_CAP];
    EepromSettings settings;
    wchar_t input[INPUT_CAP];
    unsigned i;
    join_path(path, PATH_CAP, L"eeprom.hex");
    default_settings(&settings);
    if (file_exists(path) && !validate_eeprom_file(path, &settings)) {
        output_wide(L"ĞŸĞ Ğ•Ğ”Ğ£ĞŸĞ Ğ•Ğ–Ğ”Ğ•ĞĞ˜Ğ•: eeprom.hex Ğ¿Ğ¾Ğ²Ñ€ĞµĞ¶Ğ´Ñ‘Ğ½ Ğ¸Ğ»Ğ¸ Ğ¸Ğ¼ĞµĞµÑ‚ Ğ½ĞµĞ²ĞµÑ€Ğ½Ñ‹Ğ¹ Ñ„Ğ¾Ñ€Ğ¼Ğ°Ñ‚.\n");
        output_wide(L"Ğ”Ğ»Ñ Ñ€ĞµĞ´Ğ°ĞºÑ‚Ğ¸Ñ€Ğ¾Ğ²Ğ°Ğ½Ğ¸Ñ Ğ·Ğ°Ğ³Ñ€ÑƒĞ¶ĞµĞ½Ñ‹ Ğ·Ğ½Ğ°Ñ‡ĞµĞ½Ğ¸Ñ Ğ¿Ğ¾ ÑƒĞ¼Ğ¾Ğ»Ñ‡Ğ°Ğ½Ğ¸Ñ. Ğ¤Ğ°Ğ¹Ğ» Ğ±ÑƒĞ´ĞµÑ‚ Ğ·Ğ°Ğ¼ĞµĞ½Ñ‘Ğ½ Ñ‚Ğ¾Ğ»ÑŒĞºĞ¾ Ğ¿Ğ¾ÑĞ»Ğµ ÑĞ¾Ñ…Ñ€Ğ°Ğ½ĞµĞ½Ğ¸Ñ.\n\n");
        default_settings(&settings);
    }
    output_wide(L"ĞĞ°ÑÑ‚Ñ€Ğ¾Ğ¹ĞºĞ¸ EEPROM FM7\n");
    output_wide(L"Enter â€” Ğ¾ÑÑ‚Ğ°Ğ²Ğ¸Ñ‚ÑŒ Ğ¿Ñ€ĞµĞ¶Ğ½ĞµĞµ Ğ·Ğ½Ğ°Ñ‡ĞµĞ½Ğ¸Ğµ.\n\n");
    for (i = 0; i < EEPROM_RECORDS; ++i) {
        for (;;) {
            uint32_t value;
            output_format(L"%ls [%u]: ", g_parameters[i].description, (unsigned)settings.value[i]);
            if (!input_line(input, INPUT_CAP)) return 0;
            trim_wide(input);
            if (!input[0]) break;
            if (parse_input_value(input, &value) && value >= g_parameters[i].min_value &&
                value <= g_parameters[i].max_value && (i != 1 || value == 5u || value == 6u)) {
                settings.value[i] = value;
                break;
            }
            output_wide(L"ĞĞµĞ´Ğ¾Ğ¿ÑƒÑÑ‚Ğ¸Ğ¼Ğ¾Ğµ Ğ·Ğ½Ğ°Ñ‡ĞµĞ½Ğ¸Ğµ. ĞŸĞ¾Ğ²Ñ‚Ğ¾Ñ€Ğ¸Ñ‚Ğµ Ğ²Ğ²Ğ¾Ğ´.\n");
        }
    }
    if (!save_eeprom_file(path, &settings)) {
        output_wide(L"ĞĞ¨Ğ˜Ğ‘ĞšĞ: Ğ½Ğµ ÑƒĞ´Ğ°Ğ»Ğ¾ÑÑŒ ÑĞ¾Ñ…Ñ€Ğ°Ğ½Ğ¸Ñ‚ÑŒ eeprom.hex.\n");
        return 0;
    }
    output_wide(L"\nĞ¤Ğ°Ğ¹Ğ» eeprom.hex ÑĞ¾Ñ…Ñ€Ğ°Ğ½Ñ‘Ğ½. Ğ˜Ñ‚Ğ¾Ğ³Ğ¾Ğ²Ñ‹Ğµ Ğ·Ğ½Ğ°Ñ‡ĞµĞ½Ğ¸Ñ:\n");
    show_settings(&settings);
    return 1;
}

#ifdef _WIN32
static void append_command_text(wchar_t *command, size_t capacity, const wchar_t *text)
{
    size_t used = wcslen(command);
    size_t add = wcslen(text);
    if (used + add + 1 < capacity) wcscat(command, text);
}

static void append_quoted_argument(wchar_t *command, size_t capacity, const wchar_t *argument)
{
    append_command_text(command, capacity, L" \"");
    append_command_text(command, capacity, argument);
    append_command_text(command, capacity, L"\"");
}

static void output_process_bytes(const char *bytes, DWORD count)
{
    int wide_count;
    wchar_t *wide;
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;
    DWORD console_mode = 0;
    if (!count) return;
    wide_count = MultiByteToWideChar(CP_UTF8, 0, bytes, (int)count, NULL, 0);
    if (wide_count <= 0) wide_count = MultiByteToWideChar(CP_ACP, 0, bytes, (int)count, NULL, 0);
    if (wide_count <= 0) return;
    wide = (wchar_t *)malloc(((size_t)wide_count + 1) * sizeof(wchar_t));
    if (!wide) return;
    if (!MultiByteToWideChar(CP_UTF8, 0, bytes, (int)count, wide, wide_count))
        MultiByteToWideChar(CP_ACP, 0, bytes, (int)count, wide, wide_count);
    if (GetConsoleMode(output, &console_mode)) {
        DWORD ignored;
        WriteConsoleW(output, wide, (DWORD)wide_count, &ignored, NULL);
    } else {
        int utf8_count = WideCharToMultiByte(CP_UTF8, 0, wide, wide_count, NULL, 0, NULL, NULL);
        if (utf8_count > 0) {
            char *utf8 = (char *)malloc((size_t)utf8_count);
            if (utf8) {
                WideCharToMultiByte(CP_UTF8, 0, wide, wide_count, utf8, utf8_count, NULL, NULL);
                WriteFile(output, utf8, (DWORD)utf8_count, &written, NULL);
                free(utf8);
            }
        }
    }
    free(wide);
}
#endif

static int run_avrdude(const wchar_t *const operations[], unsigned operation_count)
{
#ifdef _WIN32
    wchar_t exe_path[PATH_CAP], conf_path[PATH_CAP];
    wchar_t command[MAX_COMMAND];
    wchar_t working_directory[PATH_CAP];
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    SECURITY_ATTRIBUTES security;
    HANDLE read_pipe = NULL, write_pipe = NULL;
    DWORD exit_code = 1;
    unsigned i;
    join_path(exe_path, PATH_CAP, L"avrdude.exe");
    join_path(conf_path, PATH_CAP, L"avrdude.conf");
    wcscpy(working_directory, g_root);
    if (!file_exists(exe_path) || !file_exists(conf_path)) {
        output_wide(L"ĞĞ¨Ğ˜Ğ‘ĞšĞ: Ñ€ÑĞ´Ğ¾Ğ¼ Ñ Ğ¿Ñ€Ğ¾Ğ³Ñ€Ğ°Ğ¼Ğ¼Ğ¾Ğ¹ Ğ´Ğ¾Ğ»Ğ¶Ğ½Ñ‹ Ğ½Ğ°Ñ…Ğ¾Ğ´Ğ¸Ñ‚ÑŒÑÑ avrdude.exe Ğ¸ avrdude.conf.\n");
        return 0;
    }
    command[0] = L'\0';
    append_quoted_argument(command, MAX_COMMAND, exe_path);
    append_command_text(command, MAX_COMMAND, L" -C");
    append_quoted_argument(command, MAX_COMMAND, conf_path);
    append_command_text(command, MAX_COMMAND, L" -N -p m328p -c usbasp -B 32kHz");
    for (i = 0; i < operation_count; ++i) {
        append_command_text(command, MAX_COMMAND, L" -U");
        append_quoted_argument(command, MAX_COMMAND, operations[i]);
    }
    output_wide(L"\n> AVRDUDE: USBasp, ATmega328P, ISP 32 ĞºĞ“Ñ†\n");
    memset(&security, 0, sizeof(security));
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) {
        output_wide(L"ĞĞ¨Ğ˜Ğ‘ĞšĞ: Ğ½Ğµ ÑƒĞ´Ğ°Ğ»Ğ¾ÑÑŒ ÑĞ¾Ğ·Ğ´Ğ°Ñ‚ÑŒ ĞºĞ°Ğ½Ğ°Ğ» Ğ²Ñ‹Ğ²Ğ¾Ğ´Ğ° AVRDUDE.\n");
        return 0;
    }
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = write_pipe;
    startup.hStdError = write_pipe;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    memset(&process, 0, sizeof(process));
    if (!CreateProcessW(exe_path, command, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL,
                        working_directory, &startup, &process)) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        output_format(L"ĞĞ¨Ğ˜Ğ‘ĞšĞ: Ğ½Ğµ ÑƒĞ´Ğ°Ğ»Ğ¾ÑÑŒ Ğ·Ğ°Ğ¿ÑƒÑÑ‚Ğ¸Ñ‚ÑŒ AVRDUDE (ĞºĞ¾Ğ´ Windows %u).\n", (unsigned)GetLastError());
        return 0;
    }
    CloseHandle(write_pipe);
    for (;;) {
        char buffer[2048];
        DWORD bytes_read = 0;
        if (!ReadFile(read_pipe, buffer, sizeof(buffer), &bytes_read, NULL) || bytes_read == 0) break;
        output_process_bytes(buffer, bytes_read);
    }
    CloseHandle(read_pipe);
    WaitForSingleObject(process.hProcess, INFINITE);
    if (GetExitCodeProcess(process.hProcess, &exit_code)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        if (exit_code == 0) {
            output_wide(L"AVRDUDE: Ğ¾Ğ¿ĞµÑ€Ğ°Ñ†Ğ¸Ñ Ğ²Ñ‹Ğ¿Ğ¾Ğ»Ğ½ĞµĞ½Ğ° ÑƒÑĞ¿ĞµÑˆĞ½Ğ¾.\n");
            return 1;
        }
        output_format(L"ĞĞ¨Ğ˜Ğ‘ĞšĞ: AVRDUDE Ğ·Ğ°Ğ²ĞµÑ€ÑˆĞ¸Ğ»ÑÑ Ñ ĞºĞ¾Ğ´Ğ¾Ğ¼ %u.\n", (unsigned)exit_code);
        return 0;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    output_wide(L"ĞĞ¨Ğ˜Ğ‘ĞšĞ: Ğ½Ğµ ÑƒĞ´Ğ°Ğ»Ğ¾ÑÑŒ Ğ¿Ğ¾Ğ»ÑƒÑ‡Ğ¸Ñ‚ÑŒ ĞºĞ¾Ğ´ Ğ·Ğ°Ğ²ĞµÑ€ÑˆĞµĞ½Ğ¸Ñ AVRDUDE.\n");
    return 0;
#else
    (void)operations;
    (void)operation_count;
    output_wide(L"Ğ—Ğ°Ğ¿ÑƒÑĞº AVRDUDE Ğ´Ğ¾ÑÑ‚ÑƒĞ¿ĞµĞ½ Ñ‚Ğ¾Ğ»ÑŒĞºĞ¾ Ğ² Windows.\n");
    return 0;
#endif
}

static int confirm_action(const wchar_t *question)
{
    wchar_t input[INPUT_CAP];
    output_format(L"%ls (1 â€” Ğ´Ğ°, 0 â€” Ğ½ĞµÑ‚) [0]: ", question);
    if (!input_line(input, INPUT_CAP)) return 0;
    trim_wide(input);
    return wcscmp(input, L"1") == 0;
}

static int program_firmware_and_fuses(void)
{
    wchar_t firmware_path[PATH_CAP];
    wchar_t avrdude_path[PATH_CAP];
    wchar_t config_path[PATH_CAP];
    const wchar_t *write_fuses[] = {
        L"lfuse:w:0x62:m", L"hfuse:w:0xD1:m", L"efuse:w:0xFF:m"
    };
    const wchar_t *verify_fuses[] = {
        L"lfuse:v:0x62:m", L"hfuse:v:0xD1:m", L"efuse:v:0xFF:m"
    };
    const wchar_t *write_flash[] = {L"flash:w:ATmega328P.hex:i"};
    const wchar_t *verify_flash[] = {L"flash:v:ATmega328P.hex:i"};
    join_path(firmware_path, PATH_CAP, L"ATmega328P.hex");
    join_path(avrdude_path, PATH_CAP, L"avrdude.exe");
    join_path(config_path, PATH_CAP, L"avrdude.conf");
    if (!file_exists(firmware_path) || !validate_flash_file(firmware_path, 32768u)) {
        output_wide(L"ĞĞ¨Ğ˜Ğ‘ĞšĞ: ATmega328P.hex Ğ¾Ñ‚ÑÑƒÑ‚ÑÑ‚Ğ²ÑƒĞµÑ‚, Ğ¿Ğ¾Ğ²Ñ€ĞµĞ¶Ğ´Ñ‘Ğ½ Ğ¸Ğ»Ğ¸ Ğ²Ñ‹Ñ…Ğ¾Ğ´Ğ¸Ñ‚ Ğ·Ğ° Ğ¾Ğ±ÑŠÑ‘Ğ¼ Flash.\n");
        return 0;
    }
    if (!file_exists(avrdude_path) || !file_exists(config_path)) {
        output_wide(L"ĞĞ¨Ğ˜Ğ‘ĞšĞ: Ğ½Ğµ Ğ½Ğ°Ğ¹Ğ´ĞµĞ½Ñ‹ avrdude.exe Ğ¸/Ğ¸Ğ»Ğ¸ avrdude.conf.\n");
        return 0;
    }
    output_wide(L"\nĞ¦ĞµĞ»ĞµĞ²Ğ¾Ğ¹ ĞœĞš: ATmega328P\n");
    output_wide(L"ĞŸÑ€Ğ¾Ğ³Ñ€Ğ°Ğ¼Ğ¼Ğ°Ñ‚Ğ¾Ñ€: USBasp\n");
    output_wide(L"ISP SCK: 32 ĞºĞ“Ñ†\n");
    output_wide(L"Fuse: LFUSE=62, HFUSE=D1, EFUSE=FF\n");
    output_wide(L"HFUSE=D1 Ğ²ĞºĞ»ÑÑ‡Ğ°ĞµÑ‚ EESAVE: EEPROM Ğ´Ğ¾Ğ»Ğ¶Ğ½Ğ° ÑĞ¾Ñ…Ñ€Ğ°Ğ½Ğ¸Ñ‚ÑŒÑÑ Ğ¿Ñ€Ğ¸ Chip Erase.\n");
    output_wide(L"EEPROM-Ñ„Ğ°Ğ¹Ğ» Ğ½Ğµ Ñ‡Ğ¸Ñ‚Ğ°ĞµÑ‚ÑÑ Ğ¸ Ğ½Ğµ Ğ·Ğ°Ğ¿Ğ¸ÑÑ‹Ğ²Ğ°ĞµÑ‚ÑÑ Ğ² ÑÑ‚Ğ¾Ğ¼ Ğ¿ÑƒĞ½ĞºÑ‚Ğµ.\n\n");
    if (!confirm_action(L"ĞĞ°Ñ‡Ğ°Ñ‚ÑŒ Ğ¿Ñ€Ğ¾ÑˆĞ¸Ğ²ĞºÑƒ Firmware Ğ¸ Fuse?")) return 0;

    output_wide(L"\n1/4. Ğ—Ğ°Ğ¿Ğ¸ÑÑŒ Fuse-Ğ±Ğ¸Ñ‚Ğ¾Ğ²...\n");
    if (!run_avrdude(write_fuses, 3)) return 0;
    output_wide(L"\n2/4. ĞŸÑ€Ğ¾Ğ²ĞµÑ€ĞºĞ° Fuse-Ğ±Ğ¸Ñ‚Ğ¾Ğ²...\n");
    if (!run_avrdude(verify_fuses, 3)) return 0;
    output_wide(L"\n3/4. Ğ—Ğ°Ğ¿Ğ¸ÑÑŒ Firmware Ğ² Flash...\n");
    if (!run_avrdude(write_flash, 1)) return 0;
    output_format(L"\n4/4. Ğ—ô`4/´,´-t`4.´,›\Ú‹‹—ˆŠNÂˆYˆ
\[—Ø]œ™YJ™\šYWÙ›\ÚJJH™]\›ˆÂˆİ]]İÚYJ—´$ô/´`´/´,´/ˆ\ÙH4.›\Ú4-ô,4/ô.4`t,4/tbÈ4.4/ô`4/´,´-t`4-t/tbËˆQT“ÓH4/t-H4-ô,4`´`4,4,ô.4,´,4.ô,4`tc—ˆŠNÂˆ™]\›ˆNÂŸB‚œİ]XÈ[›ÙÜ˜[WÙY\›ÛJ›ÚY
BÂˆØÚ\—İY\›ÛWÜ]ÔUĞĞTNÂˆY\›ÛTÙ][™ÜÈÙ][™ÜÎÂˆÛÛœİØÚ\—İ
Üš]WÛÜ\˜][Û–ÌWNÂˆÛÛœİØÚ\—İ
™\šYWÛÜ\˜][Û–ÌWNÂˆ›Ú[—Ü]
Y\›ÛWÜ]UĞĞT™Y\›ÛKš^ŠNÂˆYˆ
Yš[WÙ^\İÊY\›ÛWÜ]
JHÂˆİ]]İÚYJ´'´*4&4$t&´$ˆ4a4,4.t.ÈY\›ÛKš^4/t-H4/t,4.t-4-t/Kˆ4(t/t,4aô,4.ô,4,´bô/ô/´.ô/t.4`´-H4/ô`ô/t.´`ˆK—ˆŠNÂˆ™]\›ˆÂˆBˆYˆ
]˜[Y]WÙY\›ÛWÙš[JY\›ÛWÜ]	œÙ][™ÜÊJHÂˆİ]]İÚYJ´'´*4&4$t&´$ˆY\›ÛKš^4-4/´.ô-´-t/H4`t/´-4-t`4-´,4`´c4`4/´,´/t/ˆ4/ôcô`´c4.´/´`4`4-t.´`´/tbôaH4-ô,4/ô.4`t-t.H4/ô/ˆMˆ4,t,4.t`—ˆŠNÂˆİ]]İÚYJ´/ô/ˆ4,4-4`4-t`t,4/LŒÌ4.4`H4,´-t`4/tbô/4.4/4-t`´.´,4/4.4.4.´/´/t`´`4/´.ôc4/tbô/4.4`t`ô/4/4,4/4.—ˆŠNÂˆ™]\›ˆÂˆBˆİ]]İÚYJ—´$t`ô-4`ô`ˆ4-ô,4/ô.4`t,4/tbÈ4`´/´.ôc4.´/ˆ4,t,4.t`ˆQT“ÓH
4,4-4`4-t`t,8 $ÌŠN—ˆŠNÂˆÚİ×ÜÙ][™ÜÊ	œÙ][™ÜÊNÂˆİ]]İÚYJ‘›\Ú4.\ÙH4/t-H4.4-ô/4-t/tcôc´`´`tcËˆ4)ô`´-t/t.4-HQT“ÓH4/4.4.´`4/´.´/´/t`´`4/´.ô.ô-t`4,4/t-H4,´bô/ô/´.ô/tcô-t`´`tcË——ˆŠNÂˆYˆ
XÛÛ™š\›WØXİ[ÛŠ´%ô,4/ô.4`t,4`´c4ct`´/´`ˆ4a4,4.t.È4,ˆQT“ÓOÈŠJH™]\›ˆÂ‚ˆÊˆH[[Vš[HÛÛZ[œÈ^XİHš]™HM‹X]H]H™XÛÜ™ÎÈU”‘QHÜš]\ÈÛ›HÜÙHY™\ÜÙ\Ëˆ
‹ÂˆÜš]WÛÜ\˜][Û–ÌHH™Y\›ÛNÎ™Y\›ÛKš^šHÂˆ™\šYWÛÜ\˜][Û–ÌHH™Y\›ÛN™Y\›ÛKš^šHÂˆİ]]İÚYJ—ŒKÌ‹ˆ4%ô,4/ô.4`tcQT“ÓK‹‹—ˆŠNÂˆYˆ
\[—Ø]œ™YJÜš]WÛÜ\˜][Û‹JJH™]\›ˆÂˆİ]]İÚYJ—Œ‹Ì‹ˆ4'ô`4/´,´-t`4.´,QT“ÓK‹‹—ˆŠNÂˆYˆ
\[—Ø]œ™YJ™\šYWÛÜ\˜][Û‹JJH™]\›ˆÂˆİ]]İÚYJ—´$ô/´`´/´,´/ˆQT“ÓH4-ô,4/ô.4`t,4/t,4.4/ô`4/´,´-t`4-t/t,ˆ4%ô,4/ô.4`t,4/tbÈ4`´/´.ôc4.´/ˆ4,4-4`4-t`t,8 $Ì‹—ˆŠNÂˆ™]\›ˆNÂŸB‚œİ]XÈ[Ù[—İ\İ
›ÚY
BÂˆØÚ\—İ]ÔUĞĞTNÂˆY\›ÛTÙ][™ÜÈÙ][™ÜÎÂˆ›Ú[—Ü]
]UĞĞT™Y\›ÛKš^ŠNÂˆYˆ
]˜[Y]WÙY\›ÛWÙš[J]	œÙ][™ÜÊJHÂˆİ]]İÚYJ”ÑS‹UTÕRSˆY\›ÛKš^4/t-H4`t/´/´`´,´-t`´`t`´,´`ô-t`ˆ4`tat-t/4-H“MË—ˆŠNÂˆ™]\›ˆNÂˆBˆ›Ú[—Ü]
]UĞĞTUYYØLÌš^ŠNÂˆYˆ
]˜[Y]WÙ›\ÚÙš[J]ÌÍJJHÂˆİ]]İÚYJ”ÑS‹UTÕRSˆUYYØLÌš^4/ô/´,´`4-t-´-4dt/H4.4.ô.4,´bôat/´-4.4`ˆ4-ô,4/´,tb´dt/›\Ú—ˆŠNÂˆ™]\›ˆNÂˆBˆİ]]İÚYJ”ÑS‹UTÕÒÎˆQT“ÓH
HMˆ4,t,4.t`ŠH4.›\ÚV4/ô`4/´,´-t`4-t/tbË—ˆŠNÂˆÚİ×ÜÙ][™ÜÊ	œÙ][™ÜÊNÂˆ™]\›ˆÂŸB‚œİ]XÈ›ÚYÚİ×ÛY[J›ÚY
BÂˆİ]]İÚYJ—OOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOWˆŠNÂˆİ]]İÚYJˆQœ™\KQ“MÈ›ÙÜ˜[[Y\—ˆŠNÂˆİ]]İÚYJOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOWˆŠNÂˆİ]]İÚYJˆKˆ4(4,4,t/´`´,4`HQT“ÓKta4,4.t.ô/´/ˆŠNÂˆİ]]İÚYJˆ‹ˆ4'ô`4/´b4.4,´.´,š\›]Ø\™H4.\ÙWˆŠNÂˆİ]]İÚYJˆËˆ4'ô`4/´b4.4,´.´,QT“ÓWˆŠNÂˆİ]]İÚYJˆˆ4$´bôat/´-—ˆŠNÂˆİ]]İÚYJ´$´bô,t-t`4.4`´-H4/ô`ô/t.´`ˆÍNˆŠNÂŸB‚š[XZ[Š[\™ØËÚ\ˆ
Š˜\™İŠBÂˆØÚ\—İ[œ]ÒS”UĞĞTNÂˆ]\›Z[™WÜ›Ûİ

NÂˆÚY›™YˆÕÒSŒÌ‚ˆÙ]ØØ[J×ĞSˆŠNÂˆÙ[™Y‚ˆYˆ
\™ØÈˆH	‰ˆİ˜Û\
\™İ–ÌWK‹K\Ù[‹]\İŠHOH
H™]\›ˆÙ[—İ\İ

NÂˆ›Üˆ
ÎÊHÂˆÚİ×ÛY[J
NÂˆYˆ
Z[œ]Û[™J[œ]S”UĞĞT
JH™]\›ˆÂˆš[WİÚYJ[œ]
NÂˆYˆ
Z[œ]ÌHØÜØÛ\
[œ]ŠHOH
H™]\›ˆÂˆYˆ
ØÜØÛ\
[œ]ŒHŠHOH
HY]ÙY\›ÛWÙš[J
NÂˆ[ÙHYˆ
ØÜØÛ\
[œ]ŒˆŠHOH
H›ÙÜ˜[WÙš\›]Ø\™WØ[™Ù\Ù\Ê
NÂˆ[ÙHYˆ
ØÜØÛ\
[œ]ŒÈŠHOH
H›ÙÜ˜[WÙY\›ÛJ
NÂˆ[ÙHİ]]İÚYJ´'t-t.4-ô,´-t`t`´/tbô.H4/ô`ô/t.´`ˆ4/4-t/tc‹ˆ4$´,´-t-4.4`´-HK‹È4.4.ô.—ˆŠNÂˆ]\ÙWÙ›Ü—Ù[\Š
NÂˆBŸB