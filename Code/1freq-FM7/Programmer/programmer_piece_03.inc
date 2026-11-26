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
