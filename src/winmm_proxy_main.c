#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "cJSON.h"
#include "resource.h"

/*
 * NetHackW drop-in localization mod:
 * 1) Built as winmm.dll proxy (loaded automatically by NetHackW.exe)
 * 2) IAT-hooks selected ANSI text APIs and translates known strings
 */

typedef BOOL (WINAPI *PFN_PlaySoundA)(LPCSTR, HMODULE, DWORD);

typedef BOOL (WINAPI *PFN_PlaySoundW)(LPCWSTR, HMODULE, DWORD);

typedef BOOL (WINAPI *PFN_sndPlaySoundA)(LPCSTR, UINT);

typedef BOOL (WINAPI *PFN_sndPlaySoundW)(LPCWSTR, UINT);

typedef BOOL (WINAPI *PFN_SetWindowTextA)(HWND, LPCSTR);

typedef int (WINAPI *PFN_DrawTextA)(HDC, LPCSTR, int, LPRECT, UINT);

typedef BOOL (WINAPI *PFN_TextOutA)(HDC, int, int, LPCSTR, int);

#define WINMM_EXPORT __declspec(dllexport)

typedef struct {
    const char *en;
    const char *zh;
} zh_map_item;

typedef struct {
    char *en;
    char *zh;
} zh_runtime_item;

static HMODULE g_real_winmm = NULL;
static PFN_PlaySoundA g_real_PlaySoundA = NULL;
static PFN_PlaySoundW g_real_PlaySoundW = NULL;
static PFN_sndPlaySoundA g_real_sndPlaySoundA = NULL;
static PFN_sndPlaySoundW g_real_sndPlaySoundW = NULL;

static PFN_SetWindowTextA g_orig_SetWindowTextA = NULL;
static PFN_DrawTextA g_orig_DrawTextA = NULL;
static PFN_TextOutA g_orig_TextOutA = NULL;

static LONG g_init_state = 0;
static HANDLE g_dump_file = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_dump_lock;
static bool g_dump_lock_ready = false;
static zh_runtime_item *g_runtime_map = NULL;
static size_t g_runtime_map_count = 0;

static const zh_map_item g_builtin_zh_map[] = {
    {"Messages", "消息"},
    {"Menu/Text", "菜单/文本"},
    {"Text", "文本"},
    {"Status", "状态"},
    {"Map", "地图"},
    {"Save?", "要保存吗？"},
    {"Search for:", "搜索："},
    {"Count: All", "数量：全部"},
    {"No news.", "暂无新闻。"},
    {"Cannot open clipboard", "无法打开剪贴板"},
    {"Cannot create map window", "无法创建地图窗口"},
    {"Cannot create message window", "无法创建消息窗口"},
    {"Cannot create menu window", "无法创建菜单窗口"},
    {"Cannot create text window", "无法创建文本窗口"},
    {"Cannot create rip window", "无法创建墓碑窗口"},
    {"cannot get text view window", "无法获取文本视图窗口"},
    {"cannot insert menu item", "无法插入菜单项"},
    {"out of memory", "内存不足"},
    {"--More--", "--更多--"}
};

static void free_runtime_map(void) {
    size_t i;

    if (!g_runtime_map) {
        g_runtime_map_count = 0;
        return;
    }

    for (i = 0; i < g_runtime_map_count; ++i) {
        free(g_runtime_map[i].en);
        free(g_runtime_map[i].zh);
    }

    free(g_runtime_map);
    g_runtime_map = NULL;
    g_runtime_map_count = 0;
}

static char *dup_string(const char *src) {
    size_t n;
    char *dst;

    if (!src) {
        return NULL;
    }

    n = strlen(src);
    dst = (char *) malloc(n + 1);
    if (!dst) {
        return NULL;
    }

    memcpy(dst, src, n + 1);
    return dst;
}

static bool add_runtime_item(char *key, char *value) {
    zh_runtime_item *items;
    size_t new_count;

    if (!key || !value) {
        free(key);
        free(value);
        return false;
    }

    new_count = g_runtime_map_count + 1;
    items = (zh_runtime_item *) realloc(g_runtime_map, new_count * sizeof(zh_runtime_item));
    if (!items) {
        free(key);
        free(value);
        return false;
    }

    g_runtime_map = items;
    g_runtime_map[g_runtime_map_count].en = key;
    g_runtime_map[g_runtime_map_count].zh = value;
    g_runtime_map_count = new_count;
    return true;
}

static bool parse_runtime_map_json(char *json_text) {
    cJSON *root = NULL;
    cJSON *item = NULL;
    HANDLE debug_file;
    DWORD written;
    int item_count = 0;

    if (!json_text) {
        return false;
    }

    root = cJSON_ParseWithLengthOpts(json_text, strlen(json_text), NULL, 0);
    if (!root) {
        debug_file = CreateFileA("winmm_debug.txt", FILE_APPEND_DATA,
                                 FILE_SHARE_READ, NULL,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (debug_file != INVALID_HANDLE_VALUE) {
            WriteFile(debug_file, "ERROR: cJSON_Parse failed\n", 27, &written, NULL);
            CloseHandle(debug_file);
        }
        return false;
    }

    if (!cJSON_IsObject(root)) {
        debug_file = CreateFileA("winmm_debug.txt", FILE_APPEND_DATA,
                                 FILE_SHARE_READ, NULL,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (debug_file != INVALID_HANDLE_VALUE) {
            WriteFile(debug_file, "ERROR: root is not object\n", 27, &written, NULL);
            CloseHandle(debug_file);
        }
        cJSON_Delete(root);
        return false;
    }

    cJSON_ArrayForEach(item, root) {
        char *key_copy;
        char *value_copy;
        char debug_buf[256];
        int debug_len;

        ++item_count;

        if (!item->string) {
            debug_file = CreateFileA("winmm_debug.txt", FILE_APPEND_DATA,
                                     FILE_SHARE_READ, NULL,
                                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (debug_file != INVALID_HANDLE_VALUE) {
                debug_len = sprintf(debug_buf, "Item %d: no string key\n", item_count);
                WriteFile(debug_file, debug_buf, debug_len, &written, NULL);
                CloseHandle(debug_file);
            }
            continue;
        }
        if (item->string[0] == '_' && item->string[1] == '_') {
            continue;
        }
        if (!cJSON_IsString(item) || !item->valuestring) {
            debug_file = CreateFileA("winmm_debug.txt", FILE_APPEND_DATA,
                                     FILE_SHARE_READ, NULL,
                                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (debug_file != INVALID_HANDLE_VALUE) {
                debug_len = sprintf(debug_buf, "Item %d: [%s] is not string or no value\n", item_count, item->string);
                WriteFile(debug_file, debug_buf, debug_len, &written, NULL);
                CloseHandle(debug_file);
            }
            cJSON_Delete(root);
            free_runtime_map();
            return false;
        }

        key_copy = dup_string(item->string);
        value_copy = dup_string(item->valuestring);
        if (!add_runtime_item(key_copy, value_copy)) {
            debug_file = CreateFileA("winmm_debug.txt", FILE_APPEND_DATA,
                                     FILE_SHARE_READ, NULL,
                                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (debug_file != INVALID_HANDLE_VALUE) {
                debug_len = sprintf(debug_buf, "Item %d: add_runtime_item failed\n", item_count);
                WriteFile(debug_file, debug_buf, debug_len, &written, NULL);
                CloseHandle(debug_file);
            }
            cJSON_Delete(root);
            free_runtime_map();
            return false;
        }
    }

    cJSON_Delete(root);

    debug_file = CreateFileA("winmm_debug.txt", FILE_APPEND_DATA,
                             FILE_SHARE_READ, NULL,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (debug_file != INVALID_HANDLE_VALUE) {
        char debug_buf[128];
        int debug_len = sprintf(debug_buf, "Parse complete: processed %d items, added %u to runtime map\n",
                                item_count, (unsigned int) g_runtime_map_count);
        WriteFile(debug_file, debug_buf, debug_len, &written, NULL);
        CloseHandle(debug_file);
    }

    if (g_runtime_map_count == 0) {
        debug_file = CreateFileA("winmm_debug.txt", FILE_APPEND_DATA,
                                 FILE_SHARE_READ, NULL,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (debug_file != INVALID_HANDLE_VALUE) {
            WriteFile(debug_file, "ERROR: g_runtime_map_count is 0\n", 34, &written, NULL);
            CloseHandle(debug_file);
        }
        free_runtime_map();
        return false;
    }

    return true;
}

static void load_runtime_map_from_resource(HMODULE module) {
    HRSRC res;
    HGLOBAL res_data;
    DWORD res_size;
    const char *res_ptr;
    char *json_text;
    HANDLE debug_file;
    DWORD written;

    if (!module) {
        return;
    }

    res = FindResourceA(module, MAKEINTRESOURCEA(IDR_ZH_MAP_JSON), RT_RCDATA);
    if (!res) {
        return;
    }

    res_size = SizeofResource(module, res);
    if (res_size == 0) {
        return;
    }

    res_data = LoadResource(module, res);
    if (!res_data) {
        return;
    }

    res_ptr = (const char *) LockResource(res_data);
    if (!res_ptr) {
        return;
    }

    json_text = (char *) malloc((size_t) res_size + 1);
    if (!json_text) {
        return;
    }

    memcpy(json_text, res_ptr, res_size);
    json_text[res_size] = '\0';


    /* Debug: dump loaded JSON content */
    debug_file = CreateFileA("winmm_debug.txt", FILE_WRITE_DATA,
                             FILE_SHARE_READ, NULL,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (debug_file != INVALID_HANDLE_VALUE) {
        // WriteFile(debug_file, "=== JSON LOADED ===\n", 21, &written, NULL);
        WriteFile(debug_file, json_text, res_size, &written, NULL);
        WriteFile(debug_file, "\n=== JSON SIZE ===\n", 20, &written, NULL);
        char size_buf[64];
        size_t size_len = (size_t) sprintf(size_buf, "%u bytes\n", res_size);
        WriteFile(debug_file, size_buf, (DWORD) size_len, &written, NULL);
        CloseHandle(debug_file);
    }

    free_runtime_map();
    if (parse_runtime_map_json(json_text)) {
        /* Debug: dump parsed entries */
        debug_file = CreateFileA("winmm_debug.txt", FILE_APPEND_DATA,
                                 FILE_SHARE_READ, NULL,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (debug_file != INVALID_HANDLE_VALUE) {
            char count_buf[64];
            size_t count_len = (size_t) sprintf(count_buf, "=== PARSED %u ENTRIES ===\n",
                                                (unsigned int) g_runtime_map_count);
            WriteFile(debug_file, count_buf, (DWORD) count_len, &written, NULL);
            for (size_t i = 0; i < g_runtime_map_count && i < 5; ++i) {
                WriteFile(debug_file, "[", 1, &written, NULL);
                WriteFile(debug_file, g_runtime_map[i].en, (DWORD) strlen(g_runtime_map[i].en), &written, NULL);
                WriteFile(debug_file, "] => [", 6, &written, NULL);
                WriteFile(debug_file, g_runtime_map[i].zh, (DWORD) strlen(g_runtime_map[i].zh), &written, NULL);
                WriteFile(debug_file, "]\n", 2, &written, NULL);
            }
            CloseHandle(debug_file);
        }
    } else {
        free_runtime_map();
    }

    free(json_text);
}

static bool has_alpha_len(const char *s, int len) {
    int i;
    if (!s || len <= 0) {
        return false;
    }
    for (i = 0; i < len; ++i) {
        if ((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z')) {
            return true;
        }
    }
    return false;
}

static char *replace_all_substr_len(const char *src, int src_len,
                                    const char *needle, int needle_len,
                                    const char *replacement, int repl_len) {
    int count = 0;
    int new_len;
    int i = 0;
    const char *p;
    const char *end;
    char *out;
    char *w;

    if (!src || !needle || !replacement) {
        return NULL;
    }

    /* Convert negative lengths to strlen */
    if (src_len < 0) {
        src_len = (int) strlen(src);
    }
    if (needle_len < 0) {
        needle_len = (int) strlen(needle);
    }
    if (repl_len < 0) {
        repl_len = (int) strlen(replacement);
    }

    if (needle_len == 0 || src_len < needle_len) {
        return NULL;
    }

    /* First pass: count occurrences using memcmp */
    end = src + src_len;
    for (p = src; p <= end - needle_len; ++p) {
        if (memcmp(p, needle, needle_len) == 0) {
            ++count;
        }
    }

    if (count == 0) {
        return NULL;
    }

    /* Compute new size */
    new_len = src_len + count * (repl_len - needle_len);
    out = (char *) malloc(new_len + 1);
    if (!out) {
        return NULL;
    }

    /* Second pass: build output */
    p = src;
    w = out;
    while (p < end) {
        if (p <= end - needle_len && memcmp(p, needle, needle_len) == 0) {
            /* Found a match */
            memcpy(w, replacement, repl_len);
            w += repl_len;
            p += needle_len;
        } else {
            *w++ = *p++;
        }
    }

    *w = '\0';
    return out;
}

static char *replace_from_runtime_map(const char *src, int src_len) {
    size_t i;
    char *current = NULL;
    const char *input = src;
    int input_len = src_len;

    for (i = 0; i < g_runtime_map_count; ++i) {
        char *replaced = replace_all_substr_len(input, input_len, g_runtime_map[i].en, -1, g_runtime_map[i].zh, -1);
        if (!replaced) {
            continue;
        }
        if (current && current != src) {
            free(current);
        }
        current = replaced;
        input = current;
        input_len = -1; /* now it's NUL-terminated */
    }

    return current;
}

static char *replace_from_builtin_map(const char *src, int src_len) {
    size_t i;
    char *current = NULL;
    const char *input = src;
    int input_len = src_len;

    for (i = 0; i < sizeof(g_builtin_zh_map) / sizeof(g_builtin_zh_map[0]); ++i) {
        char *replaced = replace_all_substr_len(input, input_len, g_builtin_zh_map[i].en, -1, g_builtin_zh_map[i].zh,
                                                -1);
        if (!replaced) {
            continue;
        }
        if (current && current != src) {
            free(current);
        }
        current = replaced;
        input = current;
        input_len = -1; /* now it's NUL-terminated */
    }

    return current;
}

static char *utf8_to_local_alloc(const char *utf8_str) {
    WCHAR *wide_buf;
    char *local_buf;
    int wide_len, local_len;

    if (!utf8_str) {
        return NULL;
    }

    /* UTF-8 -> UTF-16 */
    wide_len = MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, NULL, 0);
    if (wide_len <= 0) {
        return NULL;
    }

    wide_buf = (WCHAR *) malloc(wide_len * sizeof(WCHAR));
    if (!wide_buf) {
        return NULL;
    }

    if (!MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, wide_buf, wide_len)) {
        free(wide_buf);
        return NULL;
    }

    /* UTF-16 -> Local codepage (GBK/GB2312) */
    local_len = WideCharToMultiByte(CP_ACP, 0, wide_buf, -1, NULL, 0, NULL, NULL);
    if (local_len <= 0) {
        free(wide_buf);
        return NULL;
    }

    local_buf = (char *) malloc(local_len);
    if (!local_buf) {
        free(wide_buf);
        return NULL;
    }

    if (!WideCharToMultiByte(CP_ACP, 0, wide_buf, -1, local_buf, local_len, NULL, NULL)) {
        free(wide_buf);
        free(local_buf);
        return NULL;
    }

    free(wide_buf);
    return local_buf;
}

static const char *translate_exact(const char *src) {
    size_t i;
    const char *zh_result = NULL;

    if (!src || !*src) {
        return src;
    }

    for (i = 0; i < g_runtime_map_count; ++i) {
        if (strcmp(src, g_runtime_map[i].en) == 0) {
            zh_result = g_runtime_map[i].zh;
            break;
        }
    }

    if (!zh_result) {
        for (i = 0; i < sizeof(g_builtin_zh_map) / sizeof(g_builtin_zh_map[0]); ++i) {
            if (strcmp(src, g_builtin_zh_map[i].en) == 0) {
                zh_result = g_builtin_zh_map[i].zh;
                break;
            }
        }
    }

    return zh_result ? zh_result : src;
}

static bool should_translate(const char *src, int length) {
    int i;
    if (!src) {
        return false;
    }

    /* Convert negative length to strlen */
    if (length < 0) {
        length = (int) strlen(src);
    }

    if (length < 4) {
        return false;
    }

    if (!has_alpha_len(src, length)) {
        return false;
    }

    /* avoid touching format strings and short symbols/glyph output */
    for (i = 0; i < length; ++i) {
        if (src[i] == '%') {
            return false;
        }
    }

    return true;
}

static const char *translate_text(const char *src, int length) {
    if (!should_translate(src, length)) {
        return src;
    }
    return translate_exact(src);
}

static char *translate_text_contains_alloc(const char *src, int length) {
    char *from_runtime;
    char *from_builtin;

    if (!should_translate(src, length)) {
        return NULL;
    }

    from_runtime = replace_from_runtime_map(src, length);
    if (from_runtime) {
        return from_runtime;
    }

    from_builtin = replace_from_builtin_map(src, length);
    if (from_builtin) {
        return from_builtin;
    }

    return NULL;
}

static void init_dump_file(void) {
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

static void dump_intercepted_text(const char *api_name, const char *text, int length) {
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

static void init_real_winmm(void) {
    WCHAR sysdir[MAX_PATH];
    WCHAR path[MAX_PATH];
    UINT n;

    if (g_real_winmm) {
        return;
    }

    n = GetSystemDirectoryW(sysdir, MAX_PATH);
    if (n == 0 || n > MAX_PATH - 12) {
        return;
    }

    memcpy(path, sysdir, n * sizeof(WCHAR));
    path[n] = L'\\';
    path[n + 1] = L'w';
    path[n + 2] = L'i';
    path[n + 3] = L'n';
    path[n + 4] = L'm';
    path[n + 5] = L'm';
    path[n + 6] = L'.';
    path[n + 7] = L'd';
    path[n + 8] = L'l';
    path[n + 9] = L'l';
    path[n + 10] = L'\0';

    g_real_winmm = LoadLibraryW(path);
    if (!g_real_winmm) {
        return;
    }

    g_real_PlaySoundA = (PFN_PlaySoundA) GetProcAddress(g_real_winmm, "PlaySoundA");
    g_real_PlaySoundW = (PFN_PlaySoundW) GetProcAddress(g_real_winmm, "PlaySoundW");
    g_real_sndPlaySoundA = (PFN_sndPlaySoundA) GetProcAddress(g_real_winmm, "sndPlaySoundA");
    g_real_sndPlaySoundW = (PFN_sndPlaySoundW) GetProcAddress(g_real_winmm, "sndPlaySoundW");
}

static void ensure_real_winmm(void) {
    LONG state = InterlockedCompareExchange(&g_init_state, 1, 0);
    if (state == 0) {
        init_real_winmm();
        InterlockedExchange(&g_init_state, 2);
    } else {
        while (InterlockedCompareExchange(&g_init_state, 2, 2) != 2) {
            Sleep(1);
        }
    }
}

static bool patch_iat_one(HMODULE module, const char *import_dll,
                          const char *func_name, void *new_fn, void **orig_fn) {
    PIMAGE_DOS_HEADER dos;
    PIMAGE_NT_HEADERS nt;
    IMAGE_DATA_DIRECTORY imports;
    PIMAGE_IMPORT_DESCRIPTOR desc;

    dos = (PIMAGE_DOS_HEADER) module;
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }

    nt = (PIMAGE_NT_HEADERS) ((uint8_t *) module + dos->e_lfanew);
    if (!nt || nt->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!imports.VirtualAddress || !imports.Size) {
        return false;
    }

    desc = (PIMAGE_IMPORT_DESCRIPTOR) ((uint8_t *) module + imports.VirtualAddress);
    for (; desc->Name; ++desc) {
        const char *dll_name = (const char *) ((uint8_t *) module + desc->Name);
        if (_stricmp(dll_name, import_dll) != 0) {
            continue;
        }

        PIMAGE_THUNK_DATA oft = NULL;
        PIMAGE_THUNK_DATA ft = NULL;

        if (desc->OriginalFirstThunk) {
            oft = (PIMAGE_THUNK_DATA) ((uint8_t *) module + desc->OriginalFirstThunk);
        }
        ft = (PIMAGE_THUNK_DATA) ((uint8_t *) module + desc->FirstThunk);
        if (!ft) {
            return false;
        }

        for (; ft->u1.Function; ++ft) {
            const char *name = NULL;

            if (oft) {
                if (!(oft->u1.Ordinal & IMAGE_ORDINAL_FLAG)) {
                    PIMAGE_IMPORT_BY_NAME ibn = (PIMAGE_IMPORT_BY_NAME) ((uint8_t *) module + oft->u1.AddressOfData);
                    name = (const char *) ibn->Name;
                }
                ++oft;
            } else {
                if (!(ft->u1.Ordinal & IMAGE_ORDINAL_FLAG)) {
                    PIMAGE_IMPORT_BY_NAME ibn = (PIMAGE_IMPORT_BY_NAME) ((uint8_t *) module + ft->u1.AddressOfData);
                    name = (const char *) ibn->Name;
                }
            }

            if (name && strcmp(name, func_name) == 0) {
                DWORD old_prot;
                SIZE_T patch_size = sizeof(void *);
                if (!VirtualProtect(&ft->u1.Function, patch_size, PAGE_READWRITE, &old_prot)) {
                    return false;
                }
                if (orig_fn && !*orig_fn) {
                    *orig_fn = (void *) (ULONG_PTR) ft->u1.Function;
                }
                ft->u1.Function = (ULONG_PTR) new_fn;
                VirtualProtect(&ft->u1.Function, patch_size, old_prot, &old_prot);
                FlushInstructionCache(GetCurrentProcess(), &ft->u1.Function, patch_size);
                return true;
            }
        }
    }

    return false;
}

static BOOL WINAPI hook_SetWindowTextA(HWND hWnd, LPCSTR lpString) {
    dump_intercepted_text("SetWindowTextA", lpString, -1);
    char *replaced = translate_text_contains_alloc(lpString, -1);
    const char *translated = replaced ? replaced : translate_text(lpString, -1);
    char *local_encoded = NULL;
    const char *final_text = translated;
    BOOL ret;

    /* Convert UTF-8 to local codepage if text was translated */
    if (translated != lpString) {
        local_encoded = utf8_to_local_alloc(translated);
        if (local_encoded) {
            final_text = local_encoded;
        }
    }

    if (g_orig_SetWindowTextA) {
        ret = g_orig_SetWindowTextA(hWnd, final_text);
        free(local_encoded);
        free(replaced);
        return ret;
    }

    free(local_encoded);
    free(replaced);
    return FALSE;
}

static int WINAPI hook_DrawTextA(HDC hdc, LPCSTR lpchText, int cchText,
                                 LPRECT lprc, UINT format) {
    dump_intercepted_text("DrawTextA.before", lpchText, cchText);
    char *replaced = translate_text_contains_alloc(lpchText, cchText);
    const char *translated = replaced ? replaced : translate_text(lpchText, cchText);
    char *local_encoded = NULL;
    const char *final_text = translated;
    int out_len = cchText;
    int ret;

    /* Convert UTF-8 to local codepage if text was translated */
    if (translated != lpchText) {
        dump_intercepted_text("DrawTextA.after", translated, -1);
        local_encoded = utf8_to_local_alloc(translated);
        if (local_encoded) {
            final_text = local_encoded;
            out_len = (int) strlen(local_encoded);
        } else {
            final_text = translated;
            out_len = (int) strlen(translated);
        }
    }

    if (g_orig_DrawTextA) {
        ret = g_orig_DrawTextA(hdc, final_text, out_len, lprc, format);
        free(local_encoded);
        free(replaced);
        return ret;
    }

    free(local_encoded);
    free(replaced);
    return 0;
}

static BOOL WINAPI hook_TextOutA(HDC hdc, int x, int y, LPCSTR lpString, int c) {
    dump_intercepted_text("TextOutA", lpString, c);
    char *replaced = translate_text_contains_alloc(lpString, c);
    const char *translated = replaced ? replaced : translate_text(lpString, c);
    char *local_encoded = NULL;
    const char *final_text = translated;
    int out_len = c;
    BOOL ret;

    /* Convert UTF-8 to local codepage if text was translated */
    if (translated != lpString) {
        local_encoded = utf8_to_local_alloc(translated);
        if (local_encoded) {
            final_text = local_encoded;
            out_len = (int) strlen(local_encoded);
        } else {
            final_text = translated;
            out_len = (int) strlen(translated);
        }
    }

    if (g_orig_TextOutA) {
        ret = g_orig_TextOutA(hdc, x, y, final_text, out_len);
        free(local_encoded);
        free(replaced);
        return ret;
    }

    free(local_encoded);
    free(replaced);
    return FALSE;
}

static void install_text_hooks(void) {
    HMODULE exe = GetModuleHandleW(NULL);
    if (!exe) {
        return;
    }

    patch_iat_one(exe, "USER32.dll", "SetWindowTextA", (void *) hook_SetWindowTextA,
                  (void **) &g_orig_SetWindowTextA);
    patch_iat_one(exe, "USER32.dll", "SetWindowText", (void *) hook_SetWindowTextA,
                  (void **) &g_orig_SetWindowTextA);
    patch_iat_one(exe, "USER32.dll", "DrawTextA", (void *) hook_DrawTextA,
                  (void **) &g_orig_DrawTextA);
    patch_iat_one(exe, "GDI32.dll", "TextOutA", (void *) hook_TextOutA,
                  (void **) &g_orig_TextOutA);
}

WINMM_EXPORT BOOL WINAPI PlaySoundA(LPCSTR pszSound, HMODULE hmod, DWORD fdwSound) {
    ensure_real_winmm();
    if (g_real_PlaySoundA) {
        return g_real_PlaySoundA(pszSound, hmod, fdwSound);
    }
    return FALSE;
}

WINMM_EXPORT BOOL WINAPI PlaySoundW(LPCWSTR pszSound, HMODULE hmod, DWORD fdwSound) {
    ensure_real_winmm();
    if (g_real_PlaySoundW) {
        return g_real_PlaySoundW(pszSound, hmod, fdwSound);
    }
    return FALSE;
}

WINMM_EXPORT BOOL WINAPI sndPlaySoundA(LPCSTR pszSound, UINT fuSound) {
    ensure_real_winmm();
    if (g_real_sndPlaySoundA) {
        return g_real_sndPlaySoundA(pszSound, fuSound);
    }
    return FALSE;
}

WINMM_EXPORT BOOL WINAPI sndPlaySoundW(LPCWSTR pszSound, UINT fuSound) {
    ensure_real_winmm();
    if (g_real_sndPlaySoundW) {
        return g_real_sndPlaySoundW(pszSound, fuSound);
    }
    return FALSE;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    (void) reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        InitializeCriticalSection(&g_dump_lock);
        g_dump_lock_ready = true;
        load_runtime_map_from_resource(hModule);
        init_dump_file();
        install_text_hooks();
    } else if (reason == DLL_PROCESS_DETACH) {
        free_runtime_map();
        if (g_dump_file != INVALID_HANDLE_VALUE) {
            CloseHandle(g_dump_file);
            g_dump_file = INVALID_HANDLE_VALUE;
        }
        if (g_dump_lock_ready) {
            DeleteCriticalSection(&g_dump_lock);
            g_dump_lock_ready = false;
        }
    }

    return TRUE;
}
