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
