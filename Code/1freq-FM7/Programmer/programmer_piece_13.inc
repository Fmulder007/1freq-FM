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
