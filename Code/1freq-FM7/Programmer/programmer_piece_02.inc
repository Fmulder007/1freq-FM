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
