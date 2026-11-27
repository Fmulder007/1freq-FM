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
    uint64_t value[EEPROM_RECORDS];
} EepromSettings;

typedef struct {
    unsigned count;
    unsigned address;
    unsigned type;
    unsigned char data[256];
} IntelHexRecord;
static const EepromParameter g_parameters[EEPROM_RECORDS] = {
    {"XT", 0x00, 5000000u, 110000000u, 27007840u, L"XT частота опорного генератора 5000000 - 110000000 Гц"},
    {"MD", 0x10, 5u, 6u, 5u, L"MD режим работы 5 или 6"},
    {"F1", 0x20, 15000u, 300000000u, 2000000u, L"F1 частота при минимальном напряжении 15000 - 300000000 Гц"},
    {"F2", 0x30, 15000u, 300000000u, 1000000u, L"F2 частота при максимальном напряжении 15000 - 300000000 Гц"},
    {"ST", 0x40, 0u, 1024u, 0u, L"ST 0 - 1024"}
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
    output_wide(L"\nНажмите Enter для возврата в меню...");
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

static int delete_file(const wchar_t *path)
{
#ifdef _WIN32
    if (DeleteFileW(path)) return 1;
    return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
#else
    char narrow[PATH_CAP];
    if (wcstombs(narrow, path, sizeof(narrow) - 1) == (size_t)-1) return 0;
    narrow[sizeof(narrow) - 1] = '\0';
    if (remove(narrow) == 0) return 1;
    return 0;
#endif
}

static int move_file_no_replace(const wchar_t *source, const wchar_t *destination)
{
#ifdef _WIN32
    return MoveFileW(source, destination) != 0;
#else
    char src[PATH_CAP], dst[PATH_CAP];
    if (wcstombs(src, source, sizeof(src) - 1) == (size_t)-1) return 0;
    if (wcstombs(dst, destination, sizeof(dst) - 1) == (size_t)-1) return 0;
    src[sizeof(src) - 1] = '\0';
    dst[sizeof(dst) - 1] = '\0';
    if (file_exists(destination)) return 0;
    return rename(src, dst) == 0;
#endif
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

static int parse_parameter_value(const unsigned char data[EEPROM_RECORD_SIZE], unsigned index, uint64_t *value)
{
    const char *tag = g_parameters[index].tag;
    unsigned i = 3;
    uint64_t number = 0;
    int digit_count = 0;
    if (data[0] != (unsigned char)tag[0] || data[1] != (unsigned char)tag[1] || data[2] != '=') return 0;
    while (i < EEPROM_RECORD_SIZE && data[i] >= '0' && data[i] <= '9') {
        number = number * 10u + (uint64_t)(data[i] - '0');
        digit_count++;
        i++;
    }
    if (!digit_count) return 0;
    while (i < EEPROM_RECORD_SIZE) {
        if (data[i] != 0) return 0;
        i++;
    }
    /* Reading and re-writing an existing EEPROM value must not apply editor limits. */
    *value = number;
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
            uint64_t value;
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
static void default_settings(EepromSettings *settings)
{
    unsigned i;
    for (i = 0; i < EEPROM_RECORDS; ++i) settings->value[i] = g_parameters[i].default_value;
}

static void show_settings(const EepromSettings *settings)
{
    unsigned i;
    for (i = 0; i < EEPROM_RECORDS; ++i)
        output_format(L"  %hs=%llu\n", g_parameters[i].tag, (unsigned long long)settings->value[i]);
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
        output_wide(L"ПРЕДУПРЕЖДЕНИЕ: eeprom.hex имеет неверный формат записей.\n");
        output_wide(L"Для редактирования загружены значения по умолчанию. Файл будет заменён только после сохранения.\n\n");
        default_settings(&settings);
    }
    output_wide(L"Настройки EEPROM FM7\n");
    output_wide(L"Enter — оставить прежнее значение.\n\n");
    for (i = 0; i < EEPROM_RECORDS; ++i) {
        for (;;) {
            uint32_t value;
            output_format(L"%ls [%llu]: ", g_parameters[i].description,
                          (unsigned long long)settings.value[i]);
            if (!input_line(input, INPUT_CAP)) return 0;
            trim_wide(input);
            if (!input[0]) break;
            if (parse_input_value(input, &value) && value >= g_parameters[i].min_value &&
                value <= g_parameters[i].max_value && (i != 1 || value == 5u || value == 6u)) {
                settings.value[i] = value;
                break;
            }
            output_wide(L"Недопустимое значение. Повторите ввод.\n");
        }
    }
    if (!save_eeprom_file(path, &settings)) {
        output_wide(L"ОШИБКА: не удалось сохранить eeprom.hex.\n");
        return 0;
    }
    output_wide(L"\nФайл eeprom.hex сохранён. Итоговые значения:\n");
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
    /* argv[0] must start with the executable path, never with whitespace. */
    if (command[0] != L'\0')
        append_command_text(command, capacity, L" ");
    append_command_text(command, capacity, L"\"");
    append_command_text(command, capacity, argument);
    append_command_text(command, capacity, L"\"");
}

static void build_avrdude_command(wchar_t *command, size_t capacity,
                                  const wchar_t *exe_path, const wchar_t *conf_path,
                                  const wchar_t *const operations[], unsigned operation_count)
{
    unsigned i;
    command[0] = L'\0';
    append_quoted_argument(command, capacity, exe_path);
    append_command_text(command, capacity, L" -C");
    append_quoted_argument(command, capacity, conf_path);
    append_command_text(command, capacity, L" -N -p m328p -c usbasp -B 32kHz");
    for (i = 0; i < operation_count; ++i) {
        append_command_text(command, capacity, L" -U");
        append_quoted_argument(command, capacity, operations[i]);
    }
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
    join_path(exe_path, PATH_CAP, L"avrdude.exe");
    join_path(conf_path, PATH_CAP, L"avrdude.conf");
    wcscpy(working_directory, g_root);
    if (!file_exists(exe_path) || !file_exists(conf_path)) {
        output_wide(L"ОШИБКА: рядом с программой должны находиться avrdude.exe и avrdude.conf.\n");
        return 0;
    }
    build_avrdude_command(command, MAX_COMMAND, exe_path, conf_path, operations, operation_count);
    output_wide(L"\n> AVRDUDE: USBasp, ATmega328P, ISP 32 кГц\n");
    memset(&security, 0, sizeof(security));
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) {
        output_wide(L"ОШИБКА: не удалось создать канал вывода AVRDUDE.\n");
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
        output_format(L"ОШИБКА: не удалось запустить AVRDUDE (код Windows %u).\n", (unsigned)GetLastError());
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
            output_wide(L"AVRDUDE: операция выполнена успешно.\n");
            return 1;
        }
        output_format(L"ОШИБКА: AVRDUDE завершился с кодом %u.\n", (unsigned)exit_code);
        return 0;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    output_wide(L"ОШИБКА: не удалось получить код завершения AVRDUDE.\n");
    return 0;
#else
    (void)operations;
    (void)operation_count;
    output_wide(L"Запуск AVRDUDE доступен только в Windows.\n");
    return 0;
#endif
}

static int confirm_action(const wchar_t *question)
{
    wchar_t input[INPUT_CAP];
    output_format(L"%ls (1 — да, 0 — нет) [0]: ", question);
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
        output_wide(L"ОШИБКА: ATmega328P.hex отсутствует, повреждён или выходит за объём Flash.\n");
        return 0;
    }
    if (!file_exists(avrdude_path) || !file_exists(config_path)) {
        output_wide(L"ОШИБКА: не найдены avrdude.exe и/или avrdude.conf.\n");
        return 0;
    }
    output_wide(L"\nЦелевой МК: ATmega328P\n");
    output_wide(L"Программатор: USBasp\n");
    output_wide(L"ISP SCK: 32 кГц\n");
    output_wide(L"Fuse: LFUSE=62, HFUSE=D1, EFUSE=FF\n");
    output_wide(L"HFUSE=D1 включает EESAVE: EEPROM должна сохраниться при Chip Erase.\n");
    output_wide(L"EEPROM-файл не читается и не записывается в этом пункте.\n\n");
    if (!confirm_action(L"Начать прошивку Firmware и Fuse?")) return 0;

    output_wide(L"\n1/4. Запись Fuse-битов...\n");
    if (!run_avrdude(write_fuses, 3)) return 0;
    output_wide(L"\n2/4. Проверка Fuse-битов...\n");
    if (!run_avrdude(verify_fuses, 3)) return 0;
    output_wide(L"\n3/4. Запись Firmware в Flash...\n");
    if (!run_avrdude(write_flash, 1)) return 0;
    output_wide(L"\n4/4. Проверка Flash...\n");
    if (!run_avrdude(verify_flash, 1)) return 0;
    output_wide(L"\nГотово: Fuse и Flash записаны и проверены. EEPROM не затрагивалась.\n");
    return 1;
}

static int program_eeprom(void)
{
    wchar_t eeprom_path[PATH_CAP];
    EepromSettings settings;
    const wchar_t *write_operation[1];
    const wchar_t *verify_operation[1];
    join_path(eeprom_path, PATH_CAP, L"eeprom.hex");
    if (!file_exists(eeprom_path)) {
        output_wide(L"ОШИБКА: файл eeprom.hex не найден. Сначала считайте EEPROM или сохраните настройки через пункт 2.\n");
        return 0;
    }
    if (!validate_eeprom_file(eeprom_path, &settings)) {
        output_wide(L"ОШИБКА: eeprom.hex должен содержать ровно пять записей по 16 байт\n");
        output_wide(L"по адресам 0x00, 0x10, 0x20, 0x30 и 0x40, с верными метками и контрольными суммами.\n");
        return 0;
    }
    output_wide(L"\nБудут записаны только 80 байт EEPROM (адреса 0x00–0x4F):\n");
    show_settings(&settings);
    output_wide(L"Flash и Fuse не изменяются. Чтение EEPROM микроконтроллера не выполняется.\n\n");
    if (!confirm_action(L"Записать этот файл в EEPROM?")) return 0;

    /* The Intel HEX file contains exactly five 16-byte data records; AVRDUDE writes only those addresses. */
    write_operation[0] = L"eeprom:w:eeprom.hex:i";
    verify_operation[0] = L"eeprom:v:eeprom.hex:i";
    output_wide(L"\n1/2. Запись EEPROM...\n");
    if (!run_avrdude(write_operation, 1)) return 0;
    output_wide(L"\n2/2. Проверка EEPROM...\n");
    if (!run_avrdude(verify_operation, 1)) return 0;
    output_wide(L"\nГотово: EEPROM записана и проверена. Записаны только адреса 0x00–0x4F.\n");
    return 1;
}
static int read_eeprom_from_mcu(void)
{
#ifdef _WIN32
    wchar_t raw_path[PATH_CAP];
    wchar_t temporary_hex_path[PATH_CAP];
    wchar_t eeprom_path[PATH_CAP];
    wchar_t backup_path[PATH_CAP];
    wchar_t backup_base[96];
    wchar_t backup_name[128];
    wchar_t suffix[24];
    const wchar_t *read_operation[1] = {L"eeprom:r:eeprom_read.tmp.bin:r"};
    unsigned char raw[EEPROM_RECORD_SIZE * EEPROM_RECORDS];
    FILE *file;
    size_t bytes_read;
    int has_backup = 0;
    unsigned attempt;
    SYSTEMTIME now;

    join_path(raw_path, PATH_CAP, L"eeprom_read.tmp.bin");
    join_path(temporary_hex_path, PATH_CAP, L"eeprom.hex.tmp");
    join_path(eeprom_path, PATH_CAP, L"eeprom.hex");
    delete_file(raw_path);
    delete_file(temporary_hex_path);

    output_wide(L"\nСчитывание EEPROM микроконтроллера...\n");
    if (!run_avrdude(read_operation, 1)) {
        delete_file(raw_path);
        output_wide(L"EEPROM не считана. Существующий eeprom.hex не изменён.\n");
        return 0;
    }

    file = open_file(raw_path, "rb");
    if (!file) {
        delete_file(raw_path);
        output_wide(L"ОШИБКА: AVRDUDE не создал файл считанной EEPROM.\n");
        return 0;
    }
    bytes_read = fread(raw, 1, sizeof(raw), file);
    fclose(file);
    delete_file(raw_path);
    if (bytes_read != sizeof(raw)) {
        output_wide(L"ОШИБКА: не удалось получить первые 80 байт EEPROM. Старый файл не изменён.\n");
        return 0;
    }

    /* No tag, range or numeric validation here: preserve the 80 bytes exactly as read. */
    if (!save_eeprom_raw_hex(temporary_hex_path, raw)) {
        output_wide(L"ОШИБКА: не удалось подготовить новый eeprom.hex. Старый файл не изменён.\n");
        return 0;
    }

    if (file_exists(eeprom_path)) {
        GetLocalTime(&now);
        wsprintfW(backup_base, L"eeprom_old_%04u-%02u-%02u_%02u-%02u-%02u",
                  (unsigned)now.wYear, (unsigned)now.wMonth, (unsigned)now.wDay,
                  (unsigned)now.wHour, (unsigned)now.wMinute, (unsigned)now.wSecond);

        wcscpy(backup_name, backup_base);
        wcscat(backup_name, L".hex");
        join_path(backup_path, PATH_CAP, backup_name);
        if (file_exists(backup_path)) {
            for (attempt = 1; attempt <= 99; ++attempt) {
                wsprintfW(suffix, L"_%02u.hex", attempt);
                wcscpy(backup_name, backup_base);
                wcscat(backup_name, suffix);
                join_path(backup_path, PATH_CAP, backup_name);
                if (!file_exists(backup_path)) break;
            }
            if (attempt > 99) {
                delete_file(temporary_hex_path);
                output_wide(L"ОШИБКА: не удалось подобрать имя для резервного старого файла.\n");
                return 0;
            }
        }

        if (!move_file_no_replace(eeprom_path, backup_path)) {
            delete_file(temporary_hex_path);
            output_wide(L"ОШИБКА: не удалось переименовать существующий eeprom.hex.\n");
            return 0;
        }
        has_backup = 1;
    }

    if (!move_file_no_replace(temporary_hex_path, eeprom_path)) {
        if (has_backup) {
            if (move_file_no_replace(backup_path, eeprom_path))
                output_wide(L"Не удалось установить новый файл; старый eeprom.hex восстановлен.\n");
            else
                output_format(L"ОШИБКА: новый файл не установлен. Старый файл сохранён здесь: %ls\n", backup_path);
        } else {
            output_wide(L"ОШИБКА: не удалось установить новый eeprom.hex.\n");
        }
        delete_file(temporary_hex_path);
        return 0;
    }

    if (has_backup)
        output_format(L"Старый файл переименован: %ls\n", backup_path);
    output_wide(L"EEPROM успешно считана. Сохранены первые 80 байт (адреса 0x00–0x4F).\n");
    output_wide(L"Новый файл eeprom.hex сохранён рядом с программой.\n");
    return 1;
#else
    output_wide(L"Считывание EEPROM доступно только в Windows.\n");
    return 0;
#endif
}

static int self_test_large_eeprom_value(void)
{
    wchar_t path[PATH_CAP];
    EepromSettings source;
    EepromSettings loaded;
    join_path(path, PATH_CAP, L"eeprom-selftest.tmp.hex");
    default_settings(&source);
    source.value[0] = 9999999999ULL;
    if (!save_eeprom_file(path, &source)) {
        delete_file(path);
        return 0;
    }
    if (!validate_eeprom_file(path, &loaded) || loaded.value[0] != 9999999999ULL) {
        delete_file(path);
        return 0;
    }
    delete_file(path);
    return 1;
}

#ifdef _WIN32
static int self_test_avrdude_command(void)
{
    wchar_t command[1024];
    const wchar_t *operations[] = {L"eeprom:r:eeprom_read.tmp.bin:r"};
    const wchar_t expected[] =
        L"\"C:\\Program Files\\AVRDUDE\\avrdude.exe\""
        L" -C \"C:\\Program Files\\AVRDUDE\\avrdude.conf\""
        L" -N -p m328p -c usbasp -B 32kHz"
        L" -U \"eeprom:r:eeprom_read.tmp.bin:r\"";

    build_avrdude_command(command, sizeof(command) / sizeof(command[0]),
                          L"C:\\Program Files\\AVRDUDE\\avrdude.exe",
                          L"C:\\Program Files\\AVRDUDE\\avrdude.conf",
                          operations, 1);
    if (wcscmp(command, expected) != 0) {
        output_format(L"SELF-TEST FAIL: неверная строка запуска AVRDUDE: %ls\n", command);
        return 0;
    }
    return 1;
}
#endif

static int self_test(void)
{
    wchar_t path[PATH_CAP];
    EepromSettings settings;
#ifdef _WIN32
    if (!self_test_avrdude_command()) return 1;
#endif
    join_path(path, PATH_CAP, L"eeprom.hex");
    if (!validate_eeprom_file(path, &settings)) {
        output_wide(L"SELF-TEST FAIL: eeprom.hex не соответствует схеме FM7.\n");
        return 1;
    }
    if (!self_test_large_eeprom_value()) {
        output_wide(L"SELF-TEST FAIL: не сохраняется и не читается число 9999999999.\n");
        return 1;
    }
    join_path(path, PATH_CAP, L"ATmega328P.hex");
    if (!validate_flash_file(path, 32768u)) {
        output_wide(L"SELF-TEST FAIL: ATmega328P.hex повреждён или выходит за объём Flash.\n");
        return 1;
    }
    output_wide(L"SELF-TEST OK: команда AVRDUDE, EEPROM (5 x 16 байт), большое значение 9999999999 и Flash HEX проверены.\n");
    show_settings(&settings);
    return 0;
}

static void show_menu(void)
{
    output_wide(L"\n==============================================\n");
    output_wide(L"             1Freq-FM7 Programmer\n");
    output_wide(L"==============================================\n");
    output_wide(L"  1. Считать EEPROM из микроконтроллера\n");
    output_wide(L"  2. Редактировать EEPROM-файл\n");
    output_wide(L"  3. Прошивка Firmware и Fuse\n");
    output_wide(L"  4. Записать только EEPROM\n");
    output_wide(L"  5. Выход\n\n");
    output_wide(L"Выберите пункт [5]: ");
}

int main(int argc, char **argv)
{
    wchar_t input[INPUT_CAP];
    determine_root();
#ifndef _WIN32
    setlocale(LC_ALL, "");
#endif
    if (argc > 1 && strcmp(argv[1], "--self-test") == 0) return self_test();
    for (;;) {
        show_menu();
        if (!input_line(input, INPUT_CAP)) return 0;
        trim_wide(input);
        if (!input[0] || wcscmp(input, L"5") == 0) return 0;
        if (wcscmp(input, L"1") == 0) read_eeprom_from_mcu();
        else if (wcscmp(input, L"2") == 0) edit_eeprom_file();
        else if (wcscmp(input, L"3") == 0) program_firmware_and_fuses();
        else if (wcscmp(input, L"4") == 0) program_eeprom();
        else output_wide(L"Неизвестный пункт меню. Введите 1, 2, 3, 4 или 5.\n");
        pause_for_enter();
    }
}
