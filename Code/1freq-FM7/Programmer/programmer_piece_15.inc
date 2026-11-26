    EepromSettings settings;
    join_path(path, PATH_CAP, L"eeprom.hex");
    if (!validate_eeprom_file(path, &settings)) {
        output_wide(L"SELF-TEST FAIL: eeprom.hex не соответствует схеме FM7.\n");
        return 1;
    }
    join_path(path, PATH_CAP, L"ATmega328P.hex");
    if (!validate_flash_file(path, 32768u)) {
        output_wide(L"SELF-TEST FAIL: ATmega328P.hex повреждён или выходит за объём Flash.\n");
        return 1;
    }
    output_wide(L"SELF-TEST OK: EEPROM (5 x 16 байт) и Flash HEX проверены.\n");
    show_settings(&settings);
    return 0;
}

static void show_menu(void)
{
    output_wide(L"\n==============================================\n");
    output_wide(L"             1Freq-FM7 Programmer\n");
    output_wide(L"==============================================\n");
    output_wide(L"  1. Работа с EEPROM-файлом\n");
    output_wide(L"  2. Прошивка Firmware и Fuse\n");
    output_wide(L"  3. Прошивка EEPROM\n");
    output_wide(L"  4. Выход\n\n");
    output_wide(L"Выберите пункт [4]: ");
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
        if (!input[0] || wcscmp(input, L"4") == 0) return 0;
        if (wcscmp(input, L"1") == 0) edit_eeprom_file();
        else if (wcscmp(input, L"2") == 0) program_firmware_and_fuses();
        else if (wcscmp(input, L"3") == 0) program_eeprom();
        else output_wide(L"Неизвестный пункт меню. Введите 1, 2, 3 или 4.\n");
