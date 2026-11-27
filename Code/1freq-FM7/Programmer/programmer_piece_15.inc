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

    output_wide(L"\\nСчитывание EEPROM микроконтроллера...\\n");
    if (!run_avrdude(read_operation, 1)) {
        delete_file(raw_path);
        output_wide(L"EEPROM не считана. Существующий eeprom.hex не изменён.\\n");
        return 0;
    }

    file = open_file(raw_path, "rb");
    if (!file) {
        delete_file(raw_path);
        output_wide(L"ОШИБКА: AVRDUDE не создал файл считанной EEPROM.\\n");
        return 0;
    }
    bytes_read = fread(raw, 1, sizeof(raw), file);
    fclose(file);
    delete_file(raw_path);
    if (bytes_read != sizeof(raw)) {
        output_wide(L"ОШИБКА: не удалось получить первые 80 байт EEPROM. Старый файл не изменён.\\n");
        return 0;
    }

    /* No tag, range or numeric validation here: preserve the 80 bytes exactly as read. */
    if (!save_eeprom_raw_hex(temporary_hex_path, raw)) {
        output_wide(L"ОШИБКА: не удалось подготовить новый eeprom.hex. Старый файл не изменён.\\n");
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
                output_wide(L"ОШИБКА: не удалось подобрать имя для резервного старого файла.\\n");
                return 0;
            }
        }

        if (!move_file_no_replace(eeprom_path, backup_path)) {
            delete_file(temporary_hex_path);
            output_wide(L"ОШИБКА: не удалось переименовать существующий eeprom.hex.\\n");
            return 0;
        }
        has_backup = 1;
    }

    if (!move_file_no_replace(temporary_hex_path, eeprom_path)) {
        if (has_backup) {
            if (move_file_no_replace(backup_path, eeprom_path))
                output_wide(L"Не удалось установить новый файл; старый eeprom.hex восстановлен.\\n");
            else
                output_format(L"ОШИБКА: новый файл не установлен. Старый файл сохранён здесь: %ls\\n", backup_path);
        } else {
            output_wide(L"ОШИБКА: не удалось установить новый eeprom.hex.\\n");
        }
        delete_file(temporary_hex_path);
        return 0;
    }

    if (has_backup)
        output_format(L"Старый файл переименован: %ls\\n", backup_path);
    output_wide(L"EEPROM успешно считана. Сохранены первые 80 байт (адреса 0x00–0x4F).\\n");
    output_wide(L"Новый файл eeprom.hex сохранён рядом с программой.\\n");
    return 1;
#else
    output_wide(L"Считывание EEPROM доступно только в Windows.\\n");
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

static int self_test(void)
{
    wchar_t path[PATH_CAP];
    EepromSettings settings;
    join_path(path, PATH_CAP, L"eeprom.hex");
    if (!validate_eeprom_file(path, &settings)) {
        output_wide(L"SELF-TEST FAIL: eeprom.hex не соответствует схеме FM7.\\n");
        return 1;
    }
    if (!self_test_large_eeprom_value()) {
        output_wide(L"SELF-TEST FAIL: не сохраняется и не читается число 9999999999.\\n");
        return 1;
    }
    join_path(path, PATH_CAP, L"ATmega328P.hex");
    if (!validate_flash_file(path, 32768u)) {
        output_wide(L"SELF-TEST FAIL: ATmega328P.hex повреждён или выходит за объём Flash.\\n");
        return 1;
    }
    output_wide(L"SELF-TEST OK: EEPROM (5 x 16 байт), большое значение 9999999999 и Flash HEX проверены.\\n");
    show_settings(&settings);
    return 0;
}

static void show_menu(void)
{
    output_wide(L"\\n==============================================\\n");
    output_wide(L"             1Freq-FM7 Programmer\\n");
    output_wide(L"==============================================\\n");
    output_wide(L"  1. Считать EEPROM из микроконтроллера\\n");
    output_wide(L"  2. Редактировать EEPROM-файл\\n");
    output_wide(L"  3. Прошивка Firmware и Fuse\\n");
    output_wide(L"  4. Записать только EEPROM\\n");
    output_wide(L"  5. Выход\\n\\n");
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
        else output_wide(L"Неизвестный пункт меню. Введите 1, 2, 3, 4 или 5.\\n");
        pause_for_enter();
    }
}
