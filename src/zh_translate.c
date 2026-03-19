#include "zh_mod.h"

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

void free_runtime_map(void) {
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

void free_fmt_map(void) {
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
            free(g_fmt_map[i].args[j].nested_fmt);
        }
        free(g_fmt_map[i].args);
    }

    free(g_fmt_map);
    g_fmt_map = NULL;
    g_fmt_map_count = 0;
}

void free_tmpl_map(void) {
    size_t i, j;

    if (!g_tmpl_map) {
        g_tmpl_map_count = 0;
        return;
    }

    for (i = 0; i < g_tmpl_map_count; ++i) {
        free(g_tmpl_map[i].tmpl_en);
        free(g_tmpl_map[i].tmpl_zh);
        for (j = 0; j < g_tmpl_map[i].arg_count; ++j) {
            free(g_tmpl_map[i].args[j].arg_en);
            free(g_tmpl_map[i].args[j].arg_zh);
        }
        free(g_tmpl_map[i].args);
    }

    free(g_tmpl_map);
    g_tmpl_map = NULL;
    g_tmpl_map_count = 0;
}

/* ---------- makesingular port from NetHack objnam.c ---------- */

/*
 * Simplified makesingular() from NetHack's objnam.c
 * Converts English plural words to singular form for better dictionary matching.
 * Used only in partial matching to allow "bites" to match "bite": "咬"
 */
static char *zh_makesingular(const char *oldstr, char *buf, size_t bufsize) {
    char *p;
    char *bp;
    size_t len;

    if (!oldstr || !buf || bufsize < 2) {
        if (buf && bufsize > 0) buf[0] = '\0';
        return buf;
    }

    /* Skip leading spaces */
    while (*oldstr == ' ') oldstr++;
    if (!*oldstr) {
        buf[0] = '\0';
        return buf;
    }

    len = strlen(oldstr);
    if (len >= bufsize) len = bufsize - 1;
    memcpy(buf, oldstr, len);
    buf[len] = '\0';

    bp = buf;
    p = bp + len;  /* points to '\0' */

    /* Handle "foo of bar" - focus on "foo" only */
    {
        char *of_pos = strstr(bp, " of ");
        if (of_pos) {
            p = of_pos;
            *p = '\0';
        }
    }

    /* Remove -s or -es or -ies */
    if (p >= bp + 1 && (p[-1] == 's' || p[-1] == 'S')) {
        if (p >= bp + 2 && (p[-2] == 'e' || p[-2] == 'E')) {
            if (p >= bp + 3 && (p[-3] == 'i' || p[-3] == 'I')) {
                /* "ies" -> "y" (e.g., "berries" -> "berry") */
                p[-3] = (p[-3] == 'I') ? 'Y' : 'y';
                p[-2] = '\0';
                return buf;
            }
            /* "ves" -> "f" (e.g., "wolves" -> "wolf") */
            if (p >= bp + 4 && (p[-3] == 'v' || p[-3] == 'V')
                && (p[-4] == 'l' || p[-4] == 'r' || strchr("aeiouAEIOU", p[-4]))) {
                p[-3] = (p[-3] == 'V') ? 'F' : 'f';
                p[-2] = '\0';
                return buf;
            }
            /* "xes", "ches", "ses" -> drop "es" */
            if (p >= bp + 4 && (p[-3] == 'x' || p[-3] == 'X'
                                || p[-3] == 'h' || p[-3] == 'H'
                                || p[-3] == 's' || p[-3] == 'S')) {
                p[-2] = '\0';
                return buf;
            }
        } else if (p >= bp + 2 && (p[-2] == 'u' || p[-2] == 'U')
                   && (p[-1] == 's' || p[-1] == 'S')) {
            /* "us" ending - keep as-is (e.g., "lotus", "fungus") */
            return buf;
        } else if (p >= bp + 2 && p[-2] == 's') {
            /* "ss" ending - keep as-is (e.g., "glass") */
            return buf;
        }
        /* Simple -s removal */
        p[-1] = '\0';
    } else if (p >= bp + 3 && (p[-3] == 'm' || p[-3] == 'M')
               && (p[-2] == 'e' || p[-2] == 'E')
               && (p[-1] == 'n' || p[-1] == 'N')) {
        /* "men" -> "man" (e.g., "guardsmen" -> "guardsman") */
        p[-2] = (p[-2] == 'E') ? 'A' : 'a';
        p[-1] = (p[-1] == 'N') ? 'N' : 'n';
        return buf;
    }

    return buf;
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
            cJSON *nested_fmt_node;

            if (!arg_item->string) {
                continue;
            }

            arg_items = (zh_arg_item *) realloc(fi->args,
                                                (fi->arg_count + 1) * sizeof(zh_arg_item));
            if (!arg_items) {
                continue;
            }
            fi->args = arg_items;
            fi->args[fi->arg_count].arg_en = dup_string(arg_item->string);
            fi->args[fi->arg_count].nested_fmt = NULL;

            /* Check if arg value is a nested object with "fmt" field */
            if (cJSON_IsObject(arg_item)) {
                nested_fmt_node = cJSON_GetObjectItemCaseSensitive(arg_item, "fmt");
                if (nested_fmt_node && cJSON_IsString(nested_fmt_node) && nested_fmt_node->valuestring) {
                    fi->args[fi->arg_count].nested_fmt = dup_string(nested_fmt_node->valuestring);
                    /* For nested fmt, arg_zh can be from "arg" field or empty */
                    cJSON *nested_arg_node = cJSON_GetObjectItemCaseSensitive(arg_item, "arg");
                    fi->args[fi->arg_count].arg_zh = dup_string(
                        (nested_arg_node && cJSON_IsString(nested_arg_node) && nested_arg_node->valuestring)
                        ? nested_arg_node->valuestring : "");
                } else {
                    fi->args[fi->arg_count].arg_zh = dup_string("");
                }
            } else if (cJSON_IsString(arg_item)) {
                /* Simple string value */
                fi->args[fi->arg_count].arg_zh = dup_string(
                    arg_item->valuestring ? arg_item->valuestring : "");
            } else {
                fi->args[fi->arg_count].arg_zh = dup_string("");
            }

            fi->arg_count++;
        }
    }

    g_fmt_map_count = new_count;
    return true;
}

static bool add_tmpl_item(const char *en, cJSON *obj) {
    cJSON *tmpl_node, *arg_node, *arg_item;
    zh_tmpl_item *items;
    zh_tmpl_item *ti;
    size_t new_count;

    tmpl_node = cJSON_GetObjectItemCaseSensitive(obj, "tmpl");
    if (!tmpl_node || !cJSON_IsString(tmpl_node) || !tmpl_node->valuestring) {
        return false;
    }

    new_count = g_tmpl_map_count + 1;
    items = (zh_tmpl_item *) realloc(g_tmpl_map, new_count * sizeof(zh_tmpl_item));
    if (!items) {
        return false;
    }

    g_tmpl_map = items;
    ti = &g_tmpl_map[g_tmpl_map_count];
    ti->tmpl_en = dup_string(en);
    ti->tmpl_zh = dup_string(tmpl_node->valuestring);
    ti->args = NULL;
    ti->arg_count = 0;

    if (!ti->tmpl_en || !ti->tmpl_zh) {
        free(ti->tmpl_en);
        free(ti->tmpl_zh);
        return false;
    }

    arg_node = cJSON_GetObjectItemCaseSensitive(obj, "arg");
    if (arg_node && cJSON_IsObject(arg_node)) {
        cJSON_ArrayForEach(arg_item, arg_node) {
            zh_arg_item *arg_items;

            if (!arg_item->string || !cJSON_IsString(arg_item)) {
                continue;
            }

            arg_items = (zh_arg_item *) realloc(ti->args,
                                                (ti->arg_count + 1) * sizeof(zh_arg_item));
            if (!arg_items) {
                continue;
            }
            ti->args = arg_items;
            ti->args[ti->arg_count].arg_en = dup_string(arg_item->string);
            ti->args[ti->arg_count].arg_zh = dup_string(
                arg_item->valuestring ? arg_item->valuestring : "");
            ti->arg_count++;
        }
    }

    g_tmpl_map_count = new_count;
    return true;
}

static bool parse_runtime_map_json(char *json_text) {
    cJSON *root = NULL;
    cJSON *item = NULL;

    if (!json_text) {
        return false;
    }

    root = cJSON_ParseWithLengthOpts(json_text, strlen(json_text), NULL, 0);
    if (!root) {
        return false;
    }

    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    cJSON_ArrayForEach(item, root) {
        if (!item->string) {
            continue;
        }
        if (is_meta_key(item->string)) {
            continue;
        }

        /* Backward compatibility: flat key-value map */
        if (cJSON_IsString(item) && item->valuestring) {
            if (!add_runtime_item_copy(item->string, item->valuestring)) {
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
            /* Check if this is a tmpl/arg object at top level */
            if (cJSON_GetObjectItemCaseSensitive(item, "tmpl")) {
                add_tmpl_item(item->string, item);
                continue;
            }

            cJSON_ArrayForEach(sub_item, item) {
                if (!sub_item->string || is_meta_key(sub_item->string)) {
                    continue;
                }

                /* Check if sub_item is a fmt/arg object */
                if (cJSON_IsObject(sub_item)) {
                    if (cJSON_GetObjectItemCaseSensitive(sub_item, "fmt")) {
                        add_fmt_item(sub_item->string, sub_item);
                        continue;
                    }
                    /* Check if sub_item is a tmpl/arg object */
                    if (cJSON_GetObjectItemCaseSensitive(sub_item, "tmpl")) {
                        add_tmpl_item(sub_item->string, sub_item);
                        continue;
                    }
                }

                if (!cJSON_IsString(sub_item) || !sub_item->valuestring) {
                    cJSON_Delete(root);
                    free_runtime_map();
                    return false;
                }

                if (!add_runtime_item_copy(sub_item->string, sub_item->valuestring)) {
                    cJSON_Delete(root);
                    free_runtime_map();
                    return false;
                }
            }
            continue;
        }
    }

    cJSON_Delete(root);

    if (g_runtime_map_count == 0) {
        free_runtime_map();
        return false;
    }

    /* Sort by key length descending so longer needles match first */
    qsort(g_runtime_map, g_runtime_map_count,
          sizeof(zh_runtime_item), cmp_runtime_item_len_desc);

    return true;
}

void load_runtime_map_from_resource(HMODULE module) {
    HRSRC res;
    HGLOBAL res_data;
    DWORD res_size;
    const char *res_ptr;
    char *json_text;
    bool parsed_ok;

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

    free_runtime_map();
    parsed_ok = parse_runtime_map_json(json_text);
    if (!parsed_ok) {
        free_runtime_map();
    }

    dump_json_loaded_count(parsed_ok ? g_runtime_map_count : 0);

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

bool has_printf_format_spec(const char *s) {
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

char *utf8_to_local_alloc(const char *utf8_str) {
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

char *utf8_to_local_alloc_len(const char *utf8_str, int in_len) {
    WCHAR *wide_buf;
    char *local_buf;
    int wide_len, local_len;

    if (!utf8_str) {
        return NULL;
    }

    if (in_len < 0) {
        return utf8_to_local_alloc(utf8_str);
    }

    if (in_len == 0) {
        return dup_string("");
    }

    wide_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                   utf8_str, in_len, NULL, 0);
    if (wide_len <= 0) {
        return NULL;
    }

    wide_buf = (WCHAR *) malloc((size_t) (wide_len + 1) * sizeof(WCHAR));
    if (!wide_buf) {
        return NULL;
    }

    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                             utf8_str, in_len, wide_buf, wide_len)) {
        free(wide_buf);
        return NULL;
    }
    wide_buf[wide_len] = L'\0';

    local_len = WideCharToMultiByte(CP_ACP, 0, wide_buf, -1, NULL, 0, NULL, NULL);
    if (local_len <= 0) {
        free(wide_buf);
        return NULL;
    }

    local_buf = (char *) malloc((size_t) local_len);
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

bool is_likely_utf8_text(const char *s, int len) {
    const unsigned char *p;
    int i = 0;
    bool has_multibyte = false;

    if (!s) {
        return false;
    }

    if (len < 0) {
        len = (int) strlen(s);
    }
    if (len <= 0) {
        return false;
    }

    p = (const unsigned char *) s;
    while (i < len && p[i] != '\0') {
        unsigned char c = p[i];

        if (c < 0x80) {
            ++i;
            continue;
        }

        if (c >= 0xC2 && c <= 0xDF) {
            if (i + 1 >= len || (p[i + 1] & 0xC0) != 0x80) {
                return false;
            }
            has_multibyte = true;
            i += 2;
            continue;
        }

        if (c >= 0xE0 && c <= 0xEF) {
            unsigned char c1, c2;
            if (i + 2 >= len) {
                return false;
            }
            c1 = p[i + 1];
            c2 = p[i + 2];
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) {
                return false;
            }
            if (c == 0xE0 && c1 < 0xA0) {
                return false;
            }
            if (c == 0xED && c1 >= 0xA0) {
                return false;
            }
            has_multibyte = true;
            i += 3;
            continue;
        }

        if (c >= 0xF0 && c <= 0xF4) {
            unsigned char c1, c2, c3;
            if (i + 3 >= len) {
                return false;
            }
            c1 = p[i + 1];
            c2 = p[i + 2];
            c3 = p[i + 3];
            if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) {
                return false;
            }
            if (c == 0xF0 && c1 < 0x90) {
                return false;
            }
            if (c == 0xF4 && c1 > 0x8F) {
                return false;
            }
            has_multibyte = true;
            i += 4;
            continue;
        }

        return false;
    }

    return has_multibyte;
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

    return true;
}

const char *translate_text(const char *src, int length) {
    if (!should_translate(src, length)) {
        return src;
    }
    return translate_exact(src);
}

/*
 * Try to match a template pattern against input string.
 * Template format: "prefix %s suffix" or "%s suffix" or "prefix %s"
 * Returns allocated string with translated result, or NULL if no match.
 */
static char *try_match_template(const char *input, const zh_tmpl_item *ti) {
    const char *tmpl = ti->tmpl_en;
    const char *p = tmpl;
    const char *in = input;
    char *captured[16];  /* max 16 %s placeholders */
    int capture_count = 0;
    const char *last_literal_end = tmpl;
    size_t out_len = 0;
    char *output;
    char *w;
    int i;

    /* Parse template and extract captured arguments */
    while (*p) {
        if (*p == '%' && *(p + 1) == 's') {
            /* Match literal prefix before %s */
            size_t prefix_len = p - last_literal_end;
            if (prefix_len > 0) {
                if (strncmp(in, last_literal_end, prefix_len) != 0) {
                    goto cleanup_fail;
                }
                in += prefix_len;
            }

            /* Find end of captured argument */
            p += 2;  /* skip %s */
            last_literal_end = p;

            /* Find next literal or end of string */
            const char *next_literal = p;
            while (*next_literal && !(*next_literal == '%' && *(next_literal + 1) == 's')) {
                next_literal++;
            }

            size_t literal_len = next_literal - p;
            const char *arg_end;

            if (literal_len > 0) {
                /* Find where the next literal starts in input */
                arg_end = strstr(in, p);
                if (!arg_end) {
                    goto cleanup_fail;
                }
            } else {
                /* %s is at the end, capture rest of string */
                arg_end = in + strlen(in);
            }

            /* Capture the argument */
            if (capture_count < 16) {
                size_t arg_len = arg_end - in;
                captured[capture_count] = (char *) malloc(arg_len + 1);
                if (captured[capture_count]) {
                    memcpy(captured[capture_count], in, arg_len);
                    captured[capture_count][arg_len] = '\0';
                    capture_count++;
                }
            }

            in = arg_end;
        } else {
            p++;
        }
    }

    /* Match trailing literal */
    size_t trailing_len = p - last_literal_end;
    if (trailing_len > 0) {
        if (strncmp(in, last_literal_end, trailing_len) != 0 || in[trailing_len] != '\0') {
            goto cleanup_fail;
        }
    } else {
        if (*in != '\0') {
            goto cleanup_fail;
        }
    }

    /* Translate captured arguments */
    for (i = 0; i < capture_count; ++i) {
        const char *translated = NULL;
        size_t j;

        /* Try arg dictionary first */
        for (j = 0; j < ti->arg_count; ++j) {
            if (strcmp(captured[i], ti->args[j].arg_en) == 0) {
                translated = ti->args[j].arg_zh;
                break;
            }
        }

        /* Fallback to runtime_map */
        if (!translated) {
            translated = translate_exact(captured[i]);
            if (translated == captured[i]) {
                translated = NULL;
            }
        }

        if (translated) {
            free(captured[i]);
            captured[i] = dup_string(translated);
        }
    }

    /* Build output string by replacing %s in tmpl_zh with translated args */
    p = ti->tmpl_zh;
    i = 0;
    while (*p) {
        if (*p == '%' && *(p + 1) == 's') {
            if (i < capture_count && captured[i]) {
                out_len += strlen(captured[i]);
            }
            i++;
            p += 2;
        } else {
            out_len++;
            p++;
        }
    }

    output = (char *) malloc(out_len + 1);
    if (!output) {
        goto cleanup_fail;
    }

    w = output;
    p = ti->tmpl_zh;
    i = 0;
    while (*p) {
        if (*p == '%' && *(p + 1) == 's') {
            if (i < capture_count && captured[i]) {
                size_t len = strlen(captured[i]);
                memcpy(w, captured[i], len);
                w += len;
            }
            i++;
            p += 2;
        } else {
            *w++ = *p++;
        }
    }
    *w = '\0';

    /* Cleanup */
    for (i = 0; i < capture_count; ++i) {
        free(captured[i]);
    }

    return output;

cleanup_fail:
    for (i = 0; i < capture_count; ++i) {
        free(captured[i]);
    }
    return NULL;
}

char *translate_text_contains_alloc(const char *src, int length) {
    char *from_runtime;
    char *from_builtin;
    char *from_tmpl;
    char singular_buf[512];
    const char *singular_src;
    size_t i;

    if (!should_translate(src, length)) {
        return NULL;
    }

    /* Try template matching first (most specific) */
    for (i = 0; i < g_tmpl_map_count; ++i) {
        from_tmpl = try_match_template(src, &g_tmpl_map[i]);
        if (from_tmpl) {
            return from_tmpl;
        }
    }

    from_runtime = replace_from_runtime_map(src, length);
    if (from_runtime) {
        return from_runtime;
    }

    from_builtin = replace_from_builtin_map(src, length);
    if (from_builtin) {
        return from_builtin;
    }

    /* Try singular form if original didn't match (e.g., "bites" -> "bite") */
    singular_src = zh_makesingular(src, singular_buf, sizeof(singular_buf));
    if (singular_src && singular_src[0] && strcmp(singular_src, src) != 0) {
        /* Singular form is different from original, try matching it */
        from_runtime = replace_from_runtime_map(singular_src, -1);
        if (from_runtime) {
            return from_runtime;
        }

        from_builtin = replace_from_builtin_map(singular_src, -1);
        if (from_builtin) {
            return from_builtin;
        }
    }

    return NULL;
}
