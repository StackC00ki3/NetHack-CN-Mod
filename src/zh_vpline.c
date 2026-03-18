#include "zh_mod.h"

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

/*
 * Peek at the va_list and collect the actual %s argument values without
 * modifying the original va_list.  Returns the number of %s arguments found.
 */
static int collect_str_args(const char *fmt, va_list args,
                            const char *out[], int max_out) {
    va_list ap;
    const char *p;
    int count = 0;
    vpline_len_mod len_mod;
    char conv;

    va_copy(ap, args);
    p = fmt;

    while (*p) {
        len_mod = VPL_LEN_NONE;

        if (*p != '%') {
            ++p;
            continue;
        }
        ++p;
        if (*p == '%') {
            ++p;
            continue;
        }

        /* flags */
        while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') ++p;
        /* width */
        if (*p == '*') {
            va_arg(ap, int);
            ++p;
        } else { while (*p >= '0' && *p <= '9') ++p; }
        /* precision */
        if (*p == '.') {
            ++p;
            if (*p == '*') {
                va_arg(ap, int);
                ++p;
            } else { while (*p >= '0' && *p <= '9') ++p; }
        }
        /* length modifier */
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
        if (!conv) break;
        ++p;

        if (conv == 's') {
            if (len_mod == VPL_LEN_L) {
                va_arg(ap, const wchar_t *);
            } else {
                const char *s = va_arg(ap, const char *);
                if (count < max_out) {
                    out[count] = s;
                }
                count++;
            }
        } else if (conv == 'd' || conv == 'i') {
            if (len_mod == VPL_LEN_LL)
                va_arg(ap, long long);
            else if (len_mod == VPL_LEN_L)
                va_arg(ap, long);
            else if (len_mod == VPL_LEN_J)
                va_arg(ap, intmax_t);
            else if (len_mod == VPL_LEN_T)
                va_arg(ap, ptrdiff_t);
            else if (len_mod == VPL_LEN_Z)
                va_arg(ap, size_t);
            else
                va_arg(ap, int);
        } else if (conv == 'u' || conv == 'o' || conv == 'x' || conv == 'X') {
            if (len_mod == VPL_LEN_LL)
                va_arg(ap, unsigned long long);
            else if (len_mod == VPL_LEN_L)
                va_arg(ap, unsigned long);
            else if (len_mod == VPL_LEN_J)
                va_arg(ap, uintmax_t);
            else if (len_mod == VPL_LEN_T)
                va_arg(ap, ptrdiff_t);
            else if (len_mod == VPL_LEN_Z)
                va_arg(ap, size_t);
            else
                va_arg(ap, unsigned int);
        } else if (conv == 'c') {
            va_arg(ap, int);
        } else if (conv == 'p') {
            va_arg(ap, void *);
        } else if (conv == 'n') {
            if (len_mod == VPL_LEN_HH)
                va_arg(ap, signed char *);
            else if (len_mod == VPL_LEN_H)
                va_arg(ap, short *);
            else if (len_mod == VPL_LEN_L)
                va_arg(ap, long *);
            else if (len_mod == VPL_LEN_LL)
                va_arg(ap, long long *);
            else if (len_mod == VPL_LEN_J)
                va_arg(ap, intmax_t *);
            else if (len_mod == VPL_LEN_Z)
                va_arg(ap, size_t *);
            else if (len_mod == VPL_LEN_T)
                va_arg(ap, ptrdiff_t *);
            else
                va_arg(ap, int *);
        } else if (conv == 'a' || conv == 'A' || conv == 'e' || conv == 'E' ||
                   conv == 'f' || conv == 'F' || conv == 'g' || conv == 'G') {
            if (len_mod == VPL_LEN_CAP_L)
                va_arg(ap, long double);
            else
                va_arg(ap, double);
        }
    }

    va_end(ap);
    return count;
}

/*
 * Score a fmt_item candidate by counting how many of its arg keys match
 * the actual %s argument values.
 */
static int score_fmt_item(const zh_fmt_item *fi,
                          const char *str_args[], int str_count) {
    int score = 0;
    int i;
    size_t j;

    for (i = 0; i < str_count; ++i) {
        if (!str_args[i]) continue;
        for (j = 0; j < fi->arg_count; ++j) {
            if (strcmp(str_args[i], fi->args[j].arg_en) == 0) {
                score++;
                break;
            }
        }
    }
    return score;
}

/*
 * Find the best matching fmt_item for the given format string.
 * When multiple items share the same fmt_en key (ambiguous entries from
 * different source files), peek at the actual %s arguments in the va_list
 * and return the candidate whose arg dictionary matches the most.
 */
static const zh_fmt_item *find_best_fmt_item(const char *fmt_en,
                                             va_list args) {
    size_t i;
    const zh_fmt_item *first = NULL;
    bool has_duplicate = false;

    if (!fmt_en) {
        return NULL;
    }

    /* Quick scan: find first match and check for duplicates */
    for (i = 0; i < g_fmt_map_count; ++i) {
        if (strcmp(fmt_en, g_fmt_map[i].fmt_en) == 0) {
            if (!first) {
                first = &g_fmt_map[i];
            } else {
                has_duplicate = true;
                break;
            }
        }
    }

    if (!first) return NULL;
    if (!has_duplicate) return first; /* fast path: unique key */

    /* Multiple candidates – score each against actual %s args */
    {
        const char *str_args[MAX_FMT_ARG_ALLOCS];
        int str_count;
        const zh_fmt_item *best = first;
        int best_score = -1;

        str_count = collect_str_args(fmt_en, args, str_args,
                                     MAX_FMT_ARG_ALLOCS);

        for (i = 0; i < g_fmt_map_count; ++i) {
            if (strcmp(fmt_en, g_fmt_map[i].fmt_en) == 0) {
                int score = score_fmt_item(&g_fmt_map[i], str_args,
                                           str_count);
                if (score > best_score) {
                    best_score = score;
                    best = &g_fmt_map[i];
                }
            }
        }

        return best;
    }
}

/*
 * Walk the format string + va_list, translate %s args in-place via the arg map.
 * On Windows va_list is a plain char*; va_copy shares the underlying memory,
 * so writing through the copy modifies what g_orig_vpline will later read.
 * Returns the number of heap strings stored in allocs[] (caller must free).
 */
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

        if (*p != '%') {
            ++p;
            continue;
        }
        ++p;
        if (*p == '%') {
            ++p;
            continue;
        }

        /* flags */
        while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') ++p;
        /* width */
        if (*p == '*') {
            va_arg(ap, int);
            ++p;
        } else { while (*p >= '0' && *p <= '9') ++p; }
        /* precision */
        if (*p == '.') {
            ++p;
            if (*p == '*') {
                va_arg(ap, int);
                ++p;
            } else { while (*p >= '0' && *p <= '9') ++p; }
        }
        /* length modifier */
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
                    bool matched = false;
                    for (j = 0; j < fi->arg_count; ++j) {
                        if (strcmp(s, fi->args[j].arg_en) == 0) {
                            if (alloc_count < max_allocs) {
                                char *dup = _strdup(fi->args[j].arg_zh);
                                if (dup) {
                                    *slot = dup;
                                    allocs[alloc_count++] = dup;
                                }
                            }
                            matched = true;
                            break;
                        }
                    }
                    /* Fallback: exact then partial dictionary lookup */
                    if (!matched && alloc_count < max_allocs) {
                        const char *zh = translate_text(s, -1);
                        if (zh != s) {
                            char *dup = _strdup(zh);
                            if (dup) {
                                *slot = dup;
                                allocs[alloc_count++] = dup;
                            }
                        } else {
                            char *partial = translate_text_contains_alloc(s, -1);
                            if (partial) {
                                *slot = partial;
                                allocs[alloc_count++] = partial;
                            }
                        }
                    }
                }
            }
        } else if (conv == 'd' || conv == 'i') {
            if (len_mod == VPL_LEN_LL)
                va_arg(ap, long long);
            else if (len_mod == VPL_LEN_L)
                va_arg(ap, long);
            else if (len_mod == VPL_LEN_J)
                va_arg(ap, intmax_t);
            else if (len_mod == VPL_LEN_T)
                va_arg(ap, ptrdiff_t);
            else if (len_mod == VPL_LEN_Z)
                va_arg(ap, size_t);
            else
                va_arg(ap, int);
        } else if (conv == 'u' || conv == 'o' || conv == 'x' || conv == 'X') {
            if (len_mod == VPL_LEN_LL)
                va_arg(ap, unsigned long long);
            else if (len_mod == VPL_LEN_L)
                va_arg(ap, unsigned long);
            else if (len_mod == VPL_LEN_J)
                va_arg(ap, uintmax_t);
            else if (len_mod == VPL_LEN_T)
                va_arg(ap, ptrdiff_t);
            else if (len_mod == VPL_LEN_Z)
                va_arg(ap, size_t);
            else
                va_arg(ap, unsigned int);
        } else if (conv == 'c') {
            va_arg(ap, int);
        } else if (conv == 'p') {
            va_arg(ap, void *);
        } else if (conv == 'n') {
            if (len_mod == VPL_LEN_HH)
                va_arg(ap, signed char *);
            else if (len_mod == VPL_LEN_H)
                va_arg(ap, short *);
            else if (len_mod == VPL_LEN_L)
                va_arg(ap, long *);
            else if (len_mod == VPL_LEN_LL)
                va_arg(ap, long long *);
            else if (len_mod == VPL_LEN_J)
                va_arg(ap, intmax_t *);
            else if (len_mod == VPL_LEN_Z)
                va_arg(ap, size_t *);
            else if (len_mod == VPL_LEN_T)
                va_arg(ap, ptrdiff_t *);
            else
                va_arg(ap, int *);
        } else if (conv == 'a' || conv == 'A' || conv == 'e' || conv == 'E' ||
                   conv == 'f' || conv == 'F' || conv == 'g' || conv == 'G') {
            if (len_mod == VPL_LEN_CAP_L)
                va_arg(ap, long double);
            else
                va_arg(ap, double);
        }
    }

    va_end(ap);
    return alloc_count;
}

/*
 * translate_vpline_args_generic  --  translate %s arguments in the va_list
 * using translate_text / translate_text_contains_alloc when find_fmt_item()
 * found no match.  Same va_list walking logic as translate_vpline_args but
 * performs generic dictionary lookup instead of zh_fmt_item lookup.
 */
static int translate_vpline_args_generic(const char *fmt, va_list args,
                                         char *allocs[], int max_allocs) {
    va_list ap;
    const char *p;
    int alloc_count = 0;
    vpline_len_mod len_mod;
    char conv;

    va_copy(ap, args);
    p = fmt;

    while (*p) {
        len_mod = VPL_LEN_NONE;

        if (*p != '%') {
            ++p;
            continue;
        }
        ++p;
        if (*p == '%') {
            ++p;
            continue;
        }

        /* flags */
        while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') ++p;
        /* width */
        if (*p == '*') {
            va_arg(ap, int);
            ++p;
        } else { while (*p >= '0' && *p <= '9') ++p; }
        /* precision */
        if (*p == '.') {
            ++p;
            if (*p == '*') {
                va_arg(ap, int);
                ++p;
            } else { while (*p >= '0' && *p <= '9') ++p; }
        }
        /* length modifier */
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
        if (!conv) break;
        ++p;

        if (conv == 's') {
            if (len_mod == VPL_LEN_L) {
                va_arg(ap, const wchar_t *);
            } else {
                const char **slot = (const char **) ap;
                const char *s = va_arg(ap, const char *);

                if (s) {
                    const char *zh = translate_text(s, -1);
                    if (zh != s) {
                        /* Full translation found */
                        if (alloc_count < max_allocs) {
                            char *dup = _strdup(zh);
                            if (dup) {
                                *slot = dup;
                                allocs[alloc_count++] = dup;
                            }
                        }
                    } else {
                        /* Try partial translation */
                        char *partial = translate_text_contains_alloc(s, -1);
                        if (partial) {
                            if (alloc_count < max_allocs) {
                                *slot = partial;
                                allocs[alloc_count++] = partial;
                            } else {
                                free(partial);
                            }
                        }
                    }
                }
            }
        } else if (conv == 'd' || conv == 'i') {
            if (len_mod == VPL_LEN_LL)
                va_arg(ap, long long);
            else if (len_mod == VPL_LEN_L)
                va_arg(ap, long);
            else if (len_mod == VPL_LEN_J)
                va_arg(ap, intmax_t);
            else if (len_mod == VPL_LEN_T)
                va_arg(ap, ptrdiff_t);
            else if (len_mod == VPL_LEN_Z)
                va_arg(ap, size_t);
            else
                va_arg(ap, int);
        } else if (conv == 'u' || conv == 'o' || conv == 'x' || conv == 'X') {
            if (len_mod == VPL_LEN_LL)
                va_arg(ap, unsigned long long);
            else if (len_mod == VPL_LEN_L)
                va_arg(ap, unsigned long);
            else if (len_mod == VPL_LEN_J)
                va_arg(ap, uintmax_t);
            else if (len_mod == VPL_LEN_T)
                va_arg(ap, ptrdiff_t);
            else if (len_mod == VPL_LEN_Z)
                va_arg(ap, size_t);
            else
                va_arg(ap, unsigned int);
        } else if (conv == 'c') {
            va_arg(ap, int);
        } else if (conv == 'p') {
            va_arg(ap, void *);
        } else if (conv == 'n') {
            if (len_mod == VPL_LEN_HH)
                va_arg(ap, signed char *);
            else if (len_mod == VPL_LEN_H)
                va_arg(ap, short *);
            else if (len_mod == VPL_LEN_L)
                va_arg(ap, long *);
            else if (len_mod == VPL_LEN_LL)
                va_arg(ap, long long *);
            else if (len_mod == VPL_LEN_J)
                va_arg(ap, intmax_t *);
            else if (len_mod == VPL_LEN_Z)
                va_arg(ap, size_t *);
            else if (len_mod == VPL_LEN_T)
                va_arg(ap, ptrdiff_t *);
            else
                va_arg(ap, int *);
        } else if (conv == 'a' || conv == 'A' || conv == 'e' || conv == 'E' ||
                   conv == 'f' || conv == 'F' || conv == 'g' || conv == 'G') {
            if (len_mod == VPL_LEN_CAP_L)
                va_arg(ap, long double);
            else
                va_arg(ap, double);
        }
    }

    va_end(ap);
    return alloc_count;
}

/* ---------- vpline prefix splitting ---------- */

typedef struct {
    const char *en_prefix;
    const char *zh_prefix;
} vpline_prefix_item;

static const vpline_prefix_item g_vpline_prefix_map[] = {
    {"You dream that you feel ", "你梦见自己感觉"},
    {"You dream that you hear ", "你梦见自己听见"},
    {"You dream that you see ", "你梦见自己看见"},
    {"You barely hear ", "你勉强听见"},
    {"You can't ", "你不能"},
    {"You feel ", "你感觉"},
    {"You hear ", "你听见"},
    {"You sense ", "你感知到"},
    {"You see ", "你看见"},
    {"Your ", "你的"},
    {"You ", "你"}
};

static bool split_vpline_prefix(const char *line, const char **tail_out, const char **zh_prefix_out) {
    size_t i;

    if (!line || !tail_out || !zh_prefix_out) {
        return false;
    }

    *tail_out = line;
    *zh_prefix_out = NULL;

    for (i = 0; i < sizeof(g_vpline_prefix_map) / sizeof(g_vpline_prefix_map[0]); ++i) {
        size_t n = strlen(g_vpline_prefix_map[i].en_prefix);
        if (strncmp(line, g_vpline_prefix_map[i].en_prefix, n) == 0) {
            *tail_out = line + n;
            *zh_prefix_out = g_vpline_prefix_map[i].zh_prefix;
            return true;
        }
    }

    return false;
}

static char *concat_two_alloc(const char *left, const char *right) {
    size_t left_len, right_len;
    char *out;

    if (!left || !right) {
        return NULL;
    }

    left_len = strlen(left);
    right_len = strlen(right);
    out = (char *) malloc(left_len + right_len + 1);
    if (!out) {
        return NULL;
    }

    memcpy(out, left, left_len);
    memcpy(out + left_len, right, right_len);
    out[left_len + right_len] = '\0';
    return out;
}

/* ---------- hook implementations ---------- */

void __cdecl hook_vpline(const char *line, va_list the_args) {
    char *replaced = NULL;
    char *prefixed_alloc = NULL;
    const char *translated;
    const char *final_text;
    const char *tail = line;
    const char *zh_prefix = NULL;
    const zh_fmt_item *fi;
    char *gen_arg_allocs[MAX_FMT_ARG_ALLOCS];
    int gen_alloc_count = 0, gen_i;

    if (!g_orig_vpline) {
        return;
    }

    if (!line) {
        g_orig_vpline(line, the_args);
        return;
    }

    dump_intercepted_text("vpline.before", line, -1);
    dump_vpline_arguments(line, the_args);

    split_vpline_prefix(line, &tail, &zh_prefix);

    /* Check for fmt/arg format first (scores candidates on ambiguous keys) */
    fi = find_best_fmt_item(tail, the_args);
    if (fi) {
        char *arg_allocs[MAX_FMT_ARG_ALLOCS];
        int alloc_count, i;
        const char *fmt_out = fi->fmt_zh;

        alloc_count = translate_vpline_args(tail, the_args, fi,
                                            arg_allocs, MAX_FMT_ARG_ALLOCS);
        if (zh_prefix) {
            prefixed_alloc = concat_two_alloc(zh_prefix, fi->fmt_zh);
            if (prefixed_alloc) {
                fmt_out = prefixed_alloc;
            }
        }

        dump_intercepted_text("vpline.after", fmt_out, -1);
        g_orig_vpline(fmt_out, the_args);

        free(prefixed_alloc);

        for (i = 0; i < alloc_count; ++i)
            free(arg_allocs[i]);
        return;
    }

    translated = translate_text(tail, -1);
    if (translated == tail) {
        /* Skip substring replacement for format strings – it would corrupt
           format specifiers and produce garbled mixed-language output. */
        if (!has_printf_format_spec(tail)) {
            replaced = translate_text_contains_alloc(tail, -1);
            translated = replaced ? replaced : tail;
        }
    }

    /* Translate %s arguments generically when no fmt_item matched */
    if (has_printf_format_spec(tail)) {
        gen_alloc_count = translate_vpline_args_generic(tail, the_args,
                                                        gen_arg_allocs, MAX_FMT_ARG_ALLOCS);
    }

    final_text = translated;
    if (zh_prefix) {
        prefixed_alloc = concat_two_alloc(zh_prefix, final_text);
        if (prefixed_alloc) {
            final_text = prefixed_alloc;
        }
    }

    if (final_text != line) {
        dump_intercepted_text("vpline.after", final_text, -1);
    }

    g_orig_vpline(final_text, the_args);

    free(prefixed_alloc);
    free(replaced);

    for (gen_i = 0; gen_i < gen_alloc_count; ++gen_i)
        free(gen_arg_allocs[gen_i]);
}

void __cdecl hook_putstr(int winid, int attr, const char *text) {
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
