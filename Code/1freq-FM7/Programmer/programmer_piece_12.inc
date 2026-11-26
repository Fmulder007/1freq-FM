
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
        output_wide(L"ОШИБКА: рядом с программой должны находиться avrdude.exe и avrdude.conf.\n");
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
