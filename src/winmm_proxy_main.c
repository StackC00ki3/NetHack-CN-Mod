#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include <dbghelp.h>

#include "cJSON.h"
#include "MinHook.h"
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

typedef void (__cdecl *PFN_vpline)(const char *, va_list);
typedef void (__cdecl *PFN_putstr)(int, int, const char *);

#define WINMM_EXPORT __declspec(dllexport)

typedef struct {
    const char *en;
    const char *zh;
} zh_map_item;

typedef struct {
    char *en;
    char *zh;
} zh_runtime_item;

typedef struct {
    char *arg_en;
    char *arg_zh;
} zh_arg_item;

typedef struct {
    char *fmt_en;
    char *fmt_zh;
    zh_arg_item *args;
    size_t arg_count;
} zh_fmt_item;

static HMODULE g_real_winmm = NULL;
static PFN_PlaySoundA g_real_PlaySoundA = NULL;
static PFN_PlaySoundW g_real_PlaySoundW = NULL;
static PFN_sndPlaySoundA g_real_sndPlaySoundA = NULL;
static PFN_sndPlaySoundW g_real_sndPlaySoundW = NULL;

static PFN_SetWindowTextA g_orig_SetWindowTextA = NULL;
static PFN_DrawTextA g_orig_DrawTextA = NULL;
static PFN_TextOutA g_orig_TextOutA = NULL;

static PFN_vpline g_orig_vpline = NULL;
static PFN_putstr g_orig_putstr = NULL;

static LONG g_init_state = 0;
static HANDLE g_dump_file = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_dump_lock;
static bool g_dump_lock_ready = false;
static zh_runtime_item *g_runtime_map = NULL;
static size_t g_runtime_map_count = 0;
static zh_fmt_item *g_fmt_map = NULL;
static size_t g_fmt_map_count = 0;

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

static void free_fmt_map(void) {
    size_t i, j;

    if (!g_fmt_map) {
        g_fmt_map_count = 0;
        return;
    }

    for (i = 0; i < g_fmt_map_count; ++i) {
        free(g_fmt_map[i].fmt_en);
        free(g_fmt_map[i].fmt_zh);
        for (j = 0; j < g_fmt_map[i].arg_count; ++j) {
            free(g_fmt_map[i].args[j].arg_en);
            free(g_fmt_map[i].args[j].arg_zh);
        }
        free(g_fmt_map[i].args);
    }

    free(g_fmt_map);
    g_fmt_map = NULL;
    g_fmt_map_count = 0;
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

static bool add_runtime_item_copy(const char *key, const char *value) {
    char *key_copy;
    char *value_copy;

    if (!key || !value) {
        return false;
    }

    key_copy = dup_string(key);
    value_copy = dup_string(value);
    return add_runtime_item(key_copy, value_copy);
}

static bool is_meta_key(const char *key) {
    return key && key[0] == '_' && key[1] == '_';
}

/* Sort comparator: longer en-key first (descending by strlen) */
static int cmp_runtime_item_len_desc(const void *a, const void *b) {
    size_t la = strlen(((const zh_runtime_item *) a)->en);
    size_t lb = strlen(((const zh_runtime_item *) b)->en);
    return (la < lb) - (la > lb);
}

static bool add_fmt_item(const char *en, cJSON *obj) {
    cJSON *fmt_node, *arg_node, *arg_item;
    zh_fmt_item *items;
    zh_fmt_item *fi;
    size_t new_count;

    fmt_node = cJSON_GetObjectItemCaseSensitive(obj, "fmt");
    if (!fmt_node || !cJSON_IsString(fmt_node) || !fmt_node->valuestring) {
        return false;
    }

    new_count = g_fmt_map_count + 1;
    items = (zh_fmt_item *) realloc(g_fmt_map, new_count * sizeof(zh_fmt_item));
    if (!items) {
        return false;
    }

    g_fmt_map = items;
    fi = &g_fmt_map[g_fmt_map_count];
    fi->fmt_en = dup_string(en);
    fi->fmt_zh = dup_string(fmt_node->valuestring);
    fi->args = NULL;
    fi->arg_count = 0;

    if (!fi->fmt_en || !fi->fmt_zh) {
        free(fi->fmt_en);
        free(fi->fmt_zh);
        return false;
    }

    arg_node = cJSON_GetObjectItemCaseSensitive(obj, "arg");
    if (arg_node && cJSON_IsObject(arg_node)) {
        cJSON_ArrayForEach(arg_item, arg_node) {
            zh_arg_item *arg_items;

            if (!arg_item->string || !cJSON_IsString(arg_item)) {
                continue;
            }

            arg_items = (zh_arg_item *) realloc(fi->args,
                (fi->arg_count + 1) * sizeof(zh_arg_item));
            if (!arg_items) {
                continue;
            }
            fi->args = arg_items;
            fi->args[fi->arg_count].arg_en = dup_string(arg_item->string);
            fi->args[fi->arg_count].arg_zh = dup_string(
                arg_item->valuestring ? arg_item->valuestring : "");
            fi->arg_count++;
        }
    }

    g_fmt_map_count = new_count;
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
        if (is_meta_key(item->string)) {
            continue;
        }

        /* Backward compatibility: flat key-value map */
        if (cJSON_IsString(item) && item->valuestring) {
            if (!add_runtime_item_copy(item->string, item->valuestring)) {
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
            continue;
        }

        /* New format: top-level key is source file, value is object of entries */
        if (cJSON_IsObject(item)) {
            cJSON *sub_item = NULL;

            /* Check if this is a fmt/arg object at top level */
            if (cJSON_GetObjectItemCaseSensitive(item, "fmt")) {
                add_fmt_item(item->string, item);
                continue;
            }

            cJSON_ArrayForEach(sub_item, item) {
                ++item_count;

                if (!sub_item->string || is_meta_key(sub_item->string)) {
                    continue;
                }

                /* Check if sub_item is a fmt/arg object */
                if (cJSON_IsObject(sub_item)) {
                    if (cJSON_GetObjectItemCaseSensitive(sub_item, "fmt")) {
                        add_fmt_item(sub_item->string, sub_item);
                        continue;
                    }
                }

                if (!cJSON_IsString(sub_item) || !sub_item->valuestring) {
                    debug_file = CreateFileA("winmm_debug.txt", FILE_APPEND_DATA,
                                             FILE_SHARE_READ, NULL,
                                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                    if (debug_file != INVALID_HANDLE_VALUE) {
                        debug_len = sprintf(debug_buf,
                                            "Item %d: [%s.%s] is not string or no value\n",
                                            item_count, item->string, sub_item->string);
                        WriteFile(debug_file, debug_buf, debug_len, &written, NULL);
                        CloseHandle(debug_file);
                    }
                    cJSON_Delete(root);
                    free_runtime_map();
                    return false;
                }

                if (!add_runtime_item_copy(sub_item->string, sub_item->valuestring)) {
                    debug_file = CreateFileA("winmm_debug.txt", FILE_APPEND_DATA,
                                             FILE_SHARE_READ, NULL,
                                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                    if (debug_file != INVALID_HANDLE_VALUE) {
                        debug_len = sprintf(debug_buf,
                                            "Item %d: add_runtime_item failed for [%s.%s]\n",
                                            item_count, item->string, sub_item->string);
                        WriteFile(debug_file, debug_buf, debug_len, &written, NULL);
                        CloseHandle(debug_file);
                    }
                    cJSON_Delete(root);
                    free_runtime_map();
                    return false;
                }
            }
            continue;
        }

        /* Unknown top-level value type: skip to stay tolerant */
        {
            debug_file = CreateFileA("winmm_debug.txt", FILE_APPEND_DATA,
                                     FILE_SHARE_READ, NULL,
                                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (debug_file != INVALID_HANDLE_VALUE) {
                debug_len = sprintf(debug_buf, "Item %d: [%s] has unsupported value type\n", item_count, item->string);
                WriteFile(debug_file, debug_buf, debug_len, &written, NULL);
                CloseHandle(debug_file);
            }
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

    /* Sort by key length descending so longer needles match first */
    qsort(g_runtime_map, g_runtime_map_count,
          sizeof(zh_runtime_item), cmp_runtime_item_len_desc);

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

static bool is_alpha_byte(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool has_printf_format_spec(const char *s) {
    while (*s) {
        if (*s == '%') {
            s++;
            if (*s && *s != '%')
                return true;
            if (*s) s++;
        } else {
            s++;
        }
    }
    return false;
}

/*
 * Check word boundary: prevent matching a partial word.
 *  - If needle starts with a letter, the char before must not be a letter.
 *  - If needle ends   with a letter, the char after  must not be a letter.
 * e.g. needle "go" won't match inside "gold" or "goes".
 */
static bool is_word_boundary_match(const char *src, const char *pos,
                                   int needle_len, const char *end) {
    if (needle_len > 0 && is_alpha_byte(pos[0])) {
        if (pos > src && is_alpha_byte(pos[-1]))
            return false;
    }
    if (needle_len > 0 && is_alpha_byte(pos[needle_len - 1])) {
        const char *after = pos + needle_len;
        if (after < end && is_alpha_byte(*after))
            return false;
    }
    return true;
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

    /* First pass: count occurrences using memcmp + word boundary */
    end = src + src_len;
    for (p = src; p <= end - needle_len; ++p) {
        if (memcmp(p, needle, needle_len) == 0
            && is_word_boundary_match(src, p, needle_len, end)) {
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
        if (p <= end - needle_len && memcmp(p, needle, needle_len) == 0
            && is_word_boundary_match(src, p, needle_len, end)) {
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
    // for (i = 0; i < length; ++i) {
    //     if (src[i] == '%') {
    //         return false;
    //     }
    // }

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

static const zh_fmt_item *find_fmt_item(const char *fmt_en) {
    size_t i;

    if (!fmt_en) {
        return NULL;
    }

    for (i = 0; i < g_fmt_map_count; ++i) {
        if (strcmp(fmt_en, g_fmt_map[i].fmt_en) == 0) {
            return &g_fmt_map[i];
        }
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

typedef enum {
    VPL_LEN_NONE = 0,
    VPL_LEN_HH,
    VPL_LEN_H,
    VPL_LEN_L,
    VPL_LEN_LL,
    VPL_LEN_J,
    VPL_LEN_Z,
    VPL_LEN_T,
    VPL_LEN_CAP_L
} vpline_len_mod;

/*
 * Walk the format string + va_list, translate %s args in-place via the arg map.
 * On Windows va_list is a plain char*; va_copy shares the underlying memory,
 * so writing through the copy modifies what g_orig_vpline will later read.
 * Returns the number of heap strings stored in allocs[] (caller must free).
 */
#define MAX_FMT_ARG_ALLOCS 16

static int translate_vpline_args(const char *fmt, va_list args,
                                 const zh_fmt_item *fi,
                                 char *allocs[], int max_allocs) {
    va_list ap;
    const char *p;
    int alloc_count = 0;
    vpline_len_mod len_mod;
    char conv;
    size_t j;

    va_copy(ap, args);
    p = fmt;

    while (*p) {
        len_mod = VPL_LEN_NONE;

        if (*p != '%') { ++p; continue; }
        ++p;
        if (*p == '%') { ++p; continue; }

        /* flags */
        while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') ++p;
        /* width */
        if (*p == '*') { va_arg(ap, int); ++p; }
        else { while (*p >= '0' && *p <= '9') ++p; }
        /* precision */
        if (*p == '.') {
            ++p;
            if (*p == '*') { va_arg(ap, int); ++p; }
            else { while (*p >= '0' && *p <= '9') ++p; }
        }
        /* length modifier */
        if (*p == 'h' && *(p + 1) == 'h') { len_mod = VPL_LEN_HH; p += 2; }
        else if (*p == 'h') { len_mod = VPL_LEN_H; ++p; }
        else if (*p == 'l' && *(p + 1) == 'l') { len_mod = VPL_LEN_LL; p += 2; }
        else if (*p == 'l') { len_mod = VPL_LEN_L; ++p; }
        else if (*p == 'j') { len_mod = VPL_LEN_J; ++p; }
        else if (*p == 'z') { len_mod = VPL_LEN_Z; ++p; }
        else if (*p == 't') { len_mod = VPL_LEN_T; ++p; }
        else if (*p == 'L') { len_mod = VPL_LEN_CAP_L; ++p; }

        conv = *p;
        if (!conv) break;
        ++p;

        if (conv == 's') {
            if (len_mod == VPL_LEN_L) {
                va_arg(ap, const wchar_t *);
            } else {
                /* pointer to the slot BEFORE va_arg advances past it */
                const char **slot = (const char **) ap;
                const char *s = va_arg(ap, const char *);

                if (s) {
                    for (j = 0; j < fi->arg_count; ++j) {
                        if (strcmp(s, fi->args[j].arg_en) == 0) {
                            char *local_zh = utf8_to_local_alloc(fi->args[j].arg_zh);
                            if (local_zh && alloc_count < max_allocs) {
                                *slot = local_zh;
                                allocs[alloc_count++] = local_zh;
                            } else {
                                free(local_zh);
                            }
                            break;
                        }
                    }
                }
            }
        } else if (conv == 'd' || conv == 'i') {
            if (len_mod == VPL_LEN_LL) va_arg(ap, long long);
            else if (len_mod == VPL_LEN_L) va_arg(ap, long);
            else if (len_mod == VPL_LEN_J) va_arg(ap, intmax_t);
            else if (len_mod == VPL_LEN_T) va_arg(ap, ptrdiff_t);
            else if (len_mod == VPL_LEN_Z) va_arg(ap, size_t);
            else va_arg(ap, int);
        } else if (conv == 'u' || conv == 'o' || conv == 'x' || conv == 'X') {
            if (len_mod == VPL_LEN_LL) va_arg(ap, unsigned long long);
            else if (len_mod == VPL_LEN_L) va_arg(ap, unsigned long);
            else if (len_mod == VPL_LEN_J) va_arg(ap, uintmax_t);
            else if (len_mod == VPL_LEN_T) va_arg(ap, ptrdiff_t);
            else if (len_mod == VPL_LEN_Z) va_arg(ap, size_t);
            else va_arg(ap, unsigned int);
        } else if (conv == 'c') {
            va_arg(ap, int);
        } else if (conv == 'p') {
            va_arg(ap, void *);
        } else if (conv == 'n') {
            if (len_mod == VPL_LEN_HH) va_arg(ap, signed char *);
            else if (len_mod == VPL_LEN_H) va_arg(ap, short *);
            else if (len_mod == VPL_LEN_L) va_arg(ap, long *);
            else if (len_mod == VPL_LEN_LL) va_arg(ap, long long *);
            else if (len_mod == VPL_LEN_J) va_arg(ap, intmax_t *);
            else if (len_mod == VPL_LEN_Z) va_arg(ap, size_t *);
            else if (len_mod == VPL_LEN_T) va_arg(ap, ptrdiff_t *);
            else va_arg(ap, int *);
        } else if (conv == 'a' || conv == 'A' || conv == 'e' || conv == 'E' ||
                   conv == 'f' || conv == 'F' || conv == 'g' || conv == 'G') {
            if (len_mod == VPL_LEN_CAP_L) va_arg(ap, long double);
            else va_arg(ap, double);
        }
    }

    va_end(ap);
    return alloc_count;
}

static void dump_vpline_arguments(const char *fmt, va_list args) {
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
        ++p;

        {
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
                    n = _snprintf(line, sizeof(line), "[vpline.arg%d] %s -> '%c' (%d)\r\n", arg_index, spec, (char) ch, ch);
                } else {
                    n = _snprintf(line, sizeof(line), "[vpline.arg%d] %s -> (%d)\r\n", arg_index, spec, ch);
                }
            } else if (conv == 's') {
                ++arg_index;
                if (len_mod == VPL_LEN_L) {
                    const wchar_t *ws = va_arg(ap, const wchar_t *);
                    n = _snprintf(line, sizeof(line), "[vpline.arg%d] %s -> wide_str@%p\r\n", arg_index, spec, (const void *) ws);
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
    const char *translated = translate_text(lpString, -1);
    char *replaced = NULL;
    if (translated == lpString) {
        replaced = translate_text_contains_alloc(lpString, -1);
        translated = replaced ? replaced : lpString;
    }
    char *local_encoded = NULL;
    const char *final_text = translated;
    BOOL ret;

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
    const char *translated = translate_text(lpchText, cchText);
    char *replaced = NULL;
    if (translated == lpchText) {
        replaced = translate_text_contains_alloc(lpchText, cchText);
        translated = replaced ? replaced : lpchText;
    }
    char *local_encoded = NULL;
    const char *final_text = translated;
    int out_len = cchText;
    int ret;

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
    const char *translated = translate_text(lpString, c);
    char *replaced = NULL;
    if (translated == lpString) {
        replaced = translate_text_contains_alloc(lpString, c);
        translated = replaced ? replaced : lpString;
    }
    char *local_encoded = NULL;
    const char *final_text = translated;
    int out_len = c;
    BOOL ret;

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

typedef struct {
    const char *exact_name;
    const char *contains_name;
    DWORD64 address;
} hook_symbol_ctx;

static bool g_sym_initialized = false;

static BOOL CALLBACK enum_hook_symbol(PSYMBOL_INFO pSymInfo, ULONG SymbolSize, PVOID userContext) {
    hook_symbol_ctx *ctx = (hook_symbol_ctx *) userContext;
    (void) SymbolSize;

    if (!ctx || !pSymInfo || !pSymInfo->Name) {
        return TRUE;
    }

    if (ctx->exact_name && strcmp(pSymInfo->Name, ctx->exact_name) == 0) {
        ctx->address = pSymInfo->Address;
        return FALSE;
    }

    if (!ctx->address && ctx->contains_name && strstr(pSymInfo->Name, ctx->contains_name) != NULL) {
        ctx->address = pSymInfo->Address;
    }

    return TRUE;
}

static bool ensure_symbol_engine(void) {
    HANDLE process;

    if (g_sym_initialized) {
        return true;
    }

    process = GetCurrentProcess();
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    if (!SymInitialize(process, NULL, TRUE)) {
        return false;
    }

    g_sym_initialized = true;
    return true;
}

static void *resolve_symbol_address(const char *exact_name, const char *contains_name, const char *search_pattern) {
    HANDLE process = GetCurrentProcess();
    HMODULE exe = GetModuleHandleW(NULL);
    hook_symbol_ctx ctx;

    if (!exe) {
        return NULL;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.exact_name = exact_name;
    ctx.contains_name = contains_name;

    if (!ensure_symbol_engine()) {
        return NULL;
    }

    SymEnumSymbols(process, (ULONG64) (ULONG_PTR) exe, search_pattern, enum_hook_symbol, &ctx);

    return (void *) (ULONG_PTR) ctx.address;
}

static void log_hook_message(const char *fmt, ...) {
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

static void *find_pattern_in_range(const uint8_t *base, size_t size,
                                   const uint8_t *pattern, const char *mask, size_t pattern_len) {
    size_t i;
    size_t j;

    if (!base || !pattern || !mask || pattern_len == 0 || size < pattern_len) {
        return NULL;
    }

    for (i = 0; i <= size - pattern_len; ++i) {
        bool matched = true;
        for (j = 0; j < pattern_len; ++j) {
            if (mask[j] != '?' && base[i + j] != pattern[j]) {
                matched = false;
                break;
            }
        }
        if (matched) {
            return (void *) (base + i);
        }
    }

    return NULL;
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

static bool compile_pattern_string(const char *pattern_text,
                                   uint8_t **pattern_out,
                                   char **mask_out,
                                   size_t *pattern_len_out) {
    size_t text_len;
    uint8_t *pattern;
    char *mask;
    size_t out_len = 0;
    size_t i = 0;

    if (!pattern_text || !pattern_out || !mask_out || !pattern_len_out) {
        return false;
    }

    *pattern_out = NULL;
    *mask_out = NULL;
    *pattern_len_out = 0;

    text_len = strlen(pattern_text);
    if (text_len == 0) {
        return false;
    }

    pattern = (uint8_t *) malloc(text_len + 1);
    mask = (char *) malloc(text_len + 1);
    if (!pattern || !mask) {
        free(pattern);
        free(mask);
        return false;
    }

    while (i < text_len) {
        int hi;
        int lo;

        while (i < text_len && (pattern_text[i] == ' ' || pattern_text[i] == '\t' || pattern_text[i] == '\r' ||
                                pattern_text[i] == '\n')) {
            ++i;
        }
        if (i >= text_len) {
            break;
        }

        if (pattern_text[i] == '?') {
            mask[out_len] = '?';
            pattern[out_len] = 0;
            ++out_len;
            ++i;
            if (i < text_len && pattern_text[i] == '?') {
                ++i;
            }
            continue;
        }

        if (i + 1 >= text_len) {
            free(pattern);
            free(mask);
            return false;
        }

        hi = hex_nibble(pattern_text[i]);
        lo = hex_nibble(pattern_text[i + 1]);
        if (hi < 0 || lo < 0) {
            free(pattern);
            free(mask);
            return false;
        }

        pattern[out_len] = (uint8_t) ((hi << 4) | lo);
        mask[out_len] = 'x';
        ++out_len;
        i += 2;
    }

    if (out_len == 0) {
        free(pattern);
        free(mask);
        return false;
    }

    *pattern_out = pattern;
    *mask_out = mask;
    *pattern_len_out = out_len;
    return true;
}

static void *find_pattern_in_module_code(HMODULE module, const char *pattern_text) {
    PIMAGE_DOS_HEADER dos;
    PIMAGE_NT_HEADERS nt;
    PIMAGE_SECTION_HEADER sec;
    uint8_t *pattern = NULL;
    char *mask = NULL;
    size_t pattern_len = 0;
    void *result = NULL;
    WORD i;

    if (!module || !pattern_text) {
        return NULL;
    }

    if (!compile_pattern_string(pattern_text, &pattern, &mask, &pattern_len)) {
        return NULL;
    }

    dos = (PIMAGE_DOS_HEADER) module;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        goto cleanup;
    }

    nt = (PIMAGE_NT_HEADERS) ((uint8_t *) module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        goto cleanup;
    }

    sec = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        const uint8_t *section_base;
        size_t section_size;
        void *hit;

        if (!(sec->Characteristics & IMAGE_SCN_CNT_CODE)) {
            continue;
        }

        if (sec->Misc.VirtualSize == 0 || sec->VirtualAddress == 0) {
            continue;
        }

        section_base = (const uint8_t *) module + sec->VirtualAddress;
        section_size = (size_t) sec->Misc.VirtualSize;
        hit = find_pattern_in_range(section_base, section_size, pattern, mask, pattern_len);
        if (hit) {
            result = hit;
            break;
        }
    }

cleanup:
    free(pattern);
    free(mask);
    return result;
}

static void *resolve_vpline_by_signature(void) {
    static const char vpline_pattern_msvc[] =
        "48 89 54 24 10 48 89 4C 24 08 B8 78 05 00 00 E8 ?? ?? ?? ?? 48 2B E0 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 60 05 00 00 48 83 BC 24 80 05 00 00 00 74 ?? 48 8B 84 24 80 05 00 00 0F BE 00 85 C0 75 ?? E9";
    static const char vpline_pattern_mingw[] =
        "55 48 81 EC 30 05 00 00 48 8D AC 24 80 00 00 00 48 89 8D ?? ?? ?? ?? 48 89 95 ?? ?? ?? ?? 48 83 BD ?? ?? ?? ?? 00 0F 84";
    HMODULE exe = GetModuleHandleW(NULL);
    void *addr;

    if (!exe) {
        log_hook_message("[hook] vpline fallback failed: no module handle");
        return NULL;
    }

    log_hook_message("[hook] vpline symbol unresolved, trying signature fallback (msvc3.6-3.7)");
    addr = find_pattern_in_module_code(exe, vpline_pattern_msvc);
    if (addr) {
        log_hook_message("[hook] vpline signature match (msvc3.6-3.7) at %p", addr);
        return addr;
    }

    log_hook_message("[hook] msvc signature not found, trying mingw3.7 signature fallback");
    addr = find_pattern_in_module_code(exe, vpline_pattern_mingw);
    if (addr) {
        log_hook_message("[hook] vpline signature match (mingw3.7) at %p", addr);
    } else {
        log_hook_message("[hook] vpline signature fallback failed (both msvc and mingw)");
    }

    return addr;
}

static void __cdecl hook_vpline(const char *line, va_list the_args) {
    char *replaced = NULL;
    const char *translated = line;
    char *local_encoded = NULL;
    const char *final_text = line;
    const zh_fmt_item *fi;

    if (!g_orig_vpline) {
        return;
    }

    if (!line) {
        g_orig_vpline(line, the_args);
        return;
    }

    dump_intercepted_text("vpline.before", line, -1);
    dump_vpline_arguments(line, the_args);

    /* Check for fmt/arg format first */
    fi = find_fmt_item(line);
    if (fi) {
        char *local_fmt = utf8_to_local_alloc(fi->fmt_zh);
        if (local_fmt) {
            char *arg_allocs[MAX_FMT_ARG_ALLOCS];
            int alloc_count, i;

            alloc_count = translate_vpline_args(line, the_args, fi,
                                                arg_allocs, MAX_FMT_ARG_ALLOCS);
            dump_intercepted_text("vpline.after", local_fmt, -1);
            g_orig_vpline(local_fmt, the_args);

            for (i = 0; i < alloc_count; ++i)
                free(arg_allocs[i]);
            free(local_fmt);
        } else {
            g_orig_vpline(line, the_args);
        }
        return;
    }

    translated = translate_text(line, -1);
    if (translated == line) {
        /* Skip substring replacement for format strings – it would corrupt
           format specifiers and produce garbled mixed-language output. */
        if (!has_printf_format_spec(line)) {
            replaced = translate_text_contains_alloc(line, -1);
            translated = replaced ? replaced : line;
        }
    }
    final_text = translated;

    if (translated != line) {
        local_encoded = utf8_to_local_alloc(translated);
        if (local_encoded) {
            final_text = local_encoded;
        }
        dump_intercepted_text("vpline.after", final_text, -1);
    }

    g_orig_vpline(final_text, the_args);

    free(local_encoded);
    free(replaced);
}

static void __cdecl hook_putstr(int winid, int attr, const char *text) {
    char *replaced = NULL;
    const char *translated = text;
    char *local_encoded = NULL;
    const char *final_text = text;

    if (!g_orig_putstr) {
        return;
    }

    if (!text) {
        g_orig_putstr(winid, attr, text);
        return;
    }

    dump_intercepted_text("putstr.before", text, -1);

    translated = translate_text(text, -1);
    if (translated == text) {
        replaced = translate_text_contains_alloc(text, -1);
        translated = replaced ? replaced : text;
    }
    final_text = translated;

    if (translated != text) {
        local_encoded = utf8_to_local_alloc(translated);
        if (local_encoded) {
            final_text = local_encoded;
        }
        dump_intercepted_text("putstr.after", final_text, -1);
    }

    g_orig_putstr(winid, attr, final_text);

    free(local_encoded);
    free(replaced);
}

static void install_symbol_hook(const char *exact_name,
                                const char *contains_name,
                                const char *search_pattern,
                                LPVOID detour,
                                LPVOID *original) {
    MH_STATUS mh_status;
    void *target = resolve_symbol_address(exact_name, contains_name, search_pattern);

    if (!target && exact_name && strcmp(exact_name, "vpline") == 0) {
        target = resolve_vpline_by_signature();
    }

    if (!target) {
        log_hook_message("[hook] unable to resolve symbol '%s'", exact_name ? exact_name : "(null)");
        return;
    }

    log_hook_message("[hook] installing hook for '%s' at %p", exact_name ? exact_name : "(null)", target);

    mh_status = MH_Initialize();
    if (mh_status != MH_OK && mh_status != MH_ERROR_ALREADY_INITIALIZED) {
        log_hook_message("[hook] MH_Initialize failed: %d", (int) mh_status);
        return;
    }

    mh_status = MH_CreateHook(target, detour, original);
    if (mh_status != MH_OK && mh_status != MH_ERROR_ALREADY_CREATED) {
        log_hook_message("[hook] MH_CreateHook failed: %d", (int) mh_status);
        return;
    }

    mh_status = MH_EnableHook(target);
    if (mh_status != MH_OK && mh_status != MH_ERROR_ENABLED) {
        log_hook_message("[hook] MH_EnableHook failed: %d", (int) mh_status);
        return;
    }

    log_hook_message("[hook] symbol hook enabled for '%s'", exact_name ? exact_name : "(null)");
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
        install_symbol_hook("vpline", "vpline", "*vpline*", (LPVOID) hook_vpline, (LPVOID *) &g_orig_vpline);
        // install_symbol_hook("putstr", "putstr", "*putstr*", (LPVOID) hook_putstr, (LPVOID *) &g_orig_putstr);
    } else if (reason == DLL_PROCESS_DETACH) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        if (g_sym_initialized) {
            SymCleanup(GetCurrentProcess());
            g_sym_initialized = false;
        }
        free_runtime_map();
        free_fmt_map();
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
