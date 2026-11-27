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
