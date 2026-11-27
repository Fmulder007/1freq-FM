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
