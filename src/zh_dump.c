#include "zh_mod.h"

void dump_json_loaded_count(size_t count) {
    DWORD written;
    char line[96];
    int n;

    if (!g_dump_lock_ready || g_dump_file == INVALID_HANDLE_VALUE) {
        return;
    }

    n = _snprintf(line, sizeof(line), "[json] loaded entries: %u\r\n", (unsigned int) count);
    if (n <= 0) {
        return;
    }
    if (n > (int) sizeof(line)) {
        n = (int) sizeof(line);
    }

    EnterCriticalSection(&g_dump_lock);
    WriteFile(g_dump_file, line, (DWORD) n, &written, NULL);
    LeaveCriticalSection(&g_dump_lock);
}

void init_dump_file(void) {
    char exe_path[MAX_PATH];
    char *slash;
    DWORD n;

    n = GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return;
    }

    slash = strrchr(exe_path, '\\');
    if (!slash) {
        return;
    }

    *(slash + 1) = '\0';
    if (strlen(exe_path) + strlen("dump_hack.txt") + 1 >= MAX_PATH) {
        return;
    }
    strcat(exe_path, "dump_hack.txt");

    g_dump_file = CreateFileA(exe_path, FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

void dump_intercepted_text(const char *api_name, const char *text, int length) {
    DWORD written;
    int out_len;
    char len_buf[32];
    int len_chars;

    if (!g_dump_lock_ready || g_dump_file == INVALID_HANDLE_VALUE || !text) {
        return;
    }

    if (length < 0) {
        out_len = (int) strlen(text);
    } else {
        out_len = length;
    }

    if (out_len <= 0) {
        return;
    }

    if (out_len > 4096) {
        out_len = 4096;
    }

    EnterCriticalSection(&g_dump_lock);

    WriteFile(g_dump_file, "[", 1, &written, NULL);
    WriteFile(g_dump_file, api_name, (DWORD) strlen(api_name), &written, NULL);
    WriteFile(g_dump_file, "] ", 2, &written, NULL);

    len_chars = wsprintfA(len_buf, "(len=%d) ", out_len);
    if (len_chars > 0) {
        WriteFile(g_dump_file, len_buf, (DWORD) len_chars, &written, NULL);
    }

    WriteFile(g_dump_file, text, (DWORD) out_len, &written, NULL);
    WriteFile(g_dump_file, "\r\n", 2, &written, NULL);

    LeaveCriticalSection(&g_dump_lock);
}

void log_hook_message(const char *fmt, ...) {
    char line[512];
    int n;
    DWORD written;
    va_list ap;

    if (!fmt || !g_dump_lock_ready || g_dump_file == INVALID_HANDLE_VALUE) {
        return;
    }

    va_start(ap, fmt);
    n = _vsnprintf(line, sizeof(line) - 3, fmt, ap);
    va_end(ap);

    if (n < 0) {
        n = (int) sizeof(line) - 3;
    }

    line[n++] = '\r';
    line[n++] = '\n';
    line[n] = '\0';

    EnterCriticalSection(&g_dump_lock);
    WriteFile(g_dump_file, line, (DWORD) n, &written, NULL);
    LeaveCriticalSection(&g_dump_lock);
}

void dump_vpline_arguments(const char *fmt, va_list args) {
    const char *p;
    va_list ap;
    int arg_index = 0;
    DWORD written;

    if (!g_dump_lock_ready || g_dump_file == INVALID_HANDLE_VALUE || !fmt) {
        return;
    }

    va_copy(ap, args);

    EnterCriticalSection(&g_dump_lock);

    WriteFile(g_dump_file, "[vpline.fmt] ", 13, &written, NULL);
    WriteFile(g_dump_file, fmt, (DWORD) strlen(fmt), &written, NULL);
    WriteFile(g_dump_file, "\r\n", 2, &written, NULL);

    p = fmt;
    while (*p) {
        const char *spec_start;
        char conv;
        vpline_len_mod len_mod = VPL_LEN_NONE;

        if (*p != '%') {
            ++p;
            continue;
        }

        spec_start = p;
        ++p;

        if (*p == '%') {
            ++p;
            continue;
        }

        while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') {
            ++p;
        }

        if (*p == '*') {
            int width = va_arg(ap, int);
            char line[160];
            int n;

            ++arg_index;
            n = _snprintf(line, sizeof(line), "[vpline.arg%d] width=* -> %d\r\n", arg_index, width);
            if (n > 0) {
                if (n > (int) sizeof(line)) {
                    n = (int) sizeof(line);
                }
                WriteFile(g_dump_file, line, (DWORD) n, &written, NULL);
            }
            ++p;
        } else {
            while (*p >= '0' && *p <= '9') {
                ++p;
            }
        }

        if (*p == '.') {
            ++p;
            if (*p == '*') {
                int prec = va_arg(ap, int);
                char line[160];
                int n;

                ++arg_index;
                n = _snprintf(line, sizeof(line), "[vpline.arg%d] precision=* -> %d\r\n", arg_index, prec);
                if (n > 0) {
                    if (n > (int) sizeof(line)) {
                        n = (int) sizeof(line);
                    }
                    WriteFile(g_dump_file, line, (DWORD) n, &written, NULL);
                }
                ++p;
            } else {
                while (*p >= '0' && *p <= '9') {
                    ++p;
                }
            }
        }

        if (*p == 'h' && *(p + 1) == 'h') {
            len_mod = VPL_LEN_HH;
            p += 2;
        } else if (*p == 'h') {
            len_mod = VPL_LEN_H;
            ++p;
        } else if (*p == 'l' && *(p + 1) == 'l') {
            len_mod = VPL_LEN_LL;
            p += 2;
        } else if (*p == 'l') {
            len_mod = VPL_LEN_L;
            ++p;
        } else if (*p == 'j') {
            len_mod = VPL_LEN_J;
            ++p;
        } else if (*p == 'z') {
            len_mod = VPL_LEN_Z;
            ++p;
        } else if (*p == 't') {
            len_mod = VPL_LEN_T;
            ++p;
        } else if (*p == 'L') {
            len_mod = VPL_LEN_CAP_L;
            ++p;
        }

        conv = *p;
        if (!conv) {
            break;
        }
        ++p; {
            char spec[48];
            size_t spec_len = (size_t) (p - spec_start);
            char line[320];
            int n;

            if (spec_len >= sizeof(spec)) {
                spec_len = sizeof(spec) - 1;
            }
            memcpy(spec, spec_start, spec_len);
            spec[spec_len] = '\0';

            if (conv == 'd' || conv == 'i') {
                long long v = 0;
                ++arg_index;
                if (len_mod == VPL_LEN_LL) {
                    v = va_arg(ap, long long);
                } else if (len_mod == VPL_LEN_L) {
                    v = va_arg(ap, long);
                } else if (len_mod == VPL_LEN_J) {
                    v = va_arg(ap, intmax_t);
                } else if (len_mod == VPL_LEN_T) {
                    v = va_arg(ap, ptrdiff_t);
                } else if (len_mod == VPL_LEN_Z) {
                    v = (long long) va_arg(ap, size_t);
                } else {
                    v = va_arg(ap, int);
                }
                n = _snprintf(line, sizeof(line), "[vpline.arg%d] %s -> %lld\r\n", arg_index, spec, v);
            } else if (conv == 'u' || conv == 'o' || conv == 'x' || conv == 'X') {
                unsigned long long v = 0;
                ++arg_index;
                if (len_mod == VPL_LEN_LL) {
                    v = va_arg(ap, unsigned long long);
                } else if (len_mod == VPL_LEN_L) {
                    v = va_arg(ap, unsigned long);
                } else if (len_mod == VPL_LEN_J) {
                    v = va_arg(ap, uintmax_t);
                } else if (len_mod == VPL_LEN_T) {
                    v = (unsigned long long) va_arg(ap, ptrdiff_t);
                } else if (len_mod == VPL_LEN_Z) {
                    v = va_arg(ap, size_t);
                } else {
                    v = va_arg(ap, unsigned int);
                }
                n = _snprintf(line, sizeof(line), "[vpline.arg%d] %s -> %llu\r\n", arg_index, spec, v);
            } else if (conv == 'c') {
                int ch = va_arg(ap, int);
                ++arg_index;
                if (ch >= 32 && ch <= 126) {
                    n = _snprintf(line, sizeof(line), "[vpline.arg%d] %s -> '%c' (%d)\r\n", arg_index, spec, (char) ch,
                                  ch);
                } else {
                    n = _snprintf(line, sizeof(line), "[vpline.arg%d] %s -> (%d)\r\n", arg_index, spec, ch);
                }
            } else if (conv == 's') {
                ++arg_index;
                if (len_mod == VPL_LEN_L) {
                    const wchar_t *ws = va_arg(ap, const wchar_t *);
                    n = _snprintf(line, sizeof(line), "[vpline.arg%d] %s -> wide_str@%p\r\n", arg_index, spec,
                                  (const void *) ws);
                } else {
                    const char *s = va_arg(ap, const char *);
                    if (!s) {
                        n = _snprintf(line, sizeof(line), "[vpline.arg%d] %s -> (null)\r\n", arg_index, spec);
                    } else {
                        n = _snprintf(line, sizeof(line), "[vpline.arg%d] %s -> \"%.120s\"\r\n", arg_index, spec, s);
                    }
                }
            } else if (conv == 'p') {
                void *ptr = va_arg(ap, void *);
                ++arg_index;
                n = _snprintf(line, sizeof(line), "[vpline.arg%d] %s -> %p\r\n", arg_index, spec, ptr);
            } else if (conv == 'n') {
                void *ptr;
                ++arg_index;
                if (len_mod == VPL_LEN_HH) {
                    ptr = (void *) va_arg(ap, signed char *);
                } else if (len_mod == VPL_LEN_H) {
                    ptr = (void *) va_arg(ap, short *);
                } else if (len_mod == VPL_LEN_L) {
                    ptr = (void *) va_arg(ap, long *);
                } else if (len_mod == VPL_LEN_LL) {
                    ptr = (void *) va_arg(ap, long long *);
                } else if (len_mod == VPL_LEN_J) {
                    ptr = (void *) va_arg(ap, intmax_t *);
                } else if (len_mod == VPL_LEN_Z) {
                    ptr = (void *) va_arg(ap, size_t *);
                } else if (len_mod == VPL_LEN_T) {
                    ptr = (void *) va_arg(ap, ptrdiff_t *);
                } else {
                    ptr = (void *) va_arg(ap, int *);
                }
                n = _snprintf(line, sizeof(line), "[vpline.arg%d] %s -> store_count@%p\r\n", arg_index, spec, ptr);
            } else if (conv == 'a' || conv == 'A' || conv == 'e' || conv == 'E' ||
                       conv == 'f' || conv == 'F' || conv == 'g' || conv == 'G') {
                ++arg_index;
                if (len_mod == VPL_LEN_CAP_L) {
                    long double lv = va_arg(ap, long double);
                    n = _snprintf(line, sizeof(line), "[vpline.arg%d] %s -> %.10Lg\r\n", arg_index, spec, lv);
                } else {
                    double dv = va_arg(ap, double);
                    n = _snprintf(line, sizeof(line), "[vpline.arg%d] %s -> %.10g\r\n", arg_index, spec, dv);
                }
            } else {
                n = _snprintf(line, sizeof(line), "[vpline.arg?] unsupported spec %s\r\n", spec);
            }

            if (n > 0) {
                if (n > (int) sizeof(line)) {
                    n = (int) sizeof(line);
                }
                WriteFile(g_dump_file, line, (DWORD) n, &written, NULL);
            }
        }
    }

    LeaveCriticalSection(&g_dump_lock);
    va_end(ap);
}
