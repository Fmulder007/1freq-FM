    while (length && iswspace(text[length - 1])) text[--length] = L'\0';
}

static void join_path(wchar_t *destination, size_t capacity, const wchar_t *name)
{
    size_t length = wcslen(g_root);
    if (capacity == 0) return;
    destination[0] = L'\0';
    if (length + wcslen(name) + 2 >= capacity) return;
    wcscpy(destination, g_root);
#ifdef _WIN32
    if (length && g_root[length - 1] != L'\\' && g_root[length - 1] != L'/')
        wcscat(destination, L"\\");
#else
    if (length && g_root[length - 1] != L'/')
        wcscat(destination, L"/");
#endif
    wcscat(destination, name);
}

static void determine_root(void)
{
#ifdef _WIN32
    DWORD length = GetModuleFileNameW(NULL, g_root, PATH_CAP);
    if (!length || length >= PATH_CAP) {
        wcscpy(g_root, L".");
        return;
    }
    while (length && g_root[length - 1] != L'\\' && g_root[length - 1] != L'/') length--;
    if (length) g_root[length - 1] = L'\0';
    else wcscpy(g_root, L".");
#else
    wcscpy(g_root, L".");
#endif
}

static FILE *open_file(const wchar_t *path, const char *mode)
{
#ifdef _WIN32
    wchar_t wide_mode[8];
    size_t i;
    for (i = 0; mode[i] && i + 1 < 8; ++i) wide_mode[i] = (wchar_t)mode[i];
    wide_mode[i] = L'\0';
    return _wfopen(path, wide_mode);
#else
    char narrow[PATH_CAP];
    if (wcstombs(narrow, path, sizeof(narrow) - 1) == (size_t)-1) return NULL;
    narrow[sizeof(narrow) - 1] = '\0';
    return fopen(narrow, mode);
#endif