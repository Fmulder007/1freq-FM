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
    uint32_t value[EEPROM_RECORDS];
} EepromSettings;

typedef struct {
    unsigned count;
    unsigned address;
    unsigned type;
