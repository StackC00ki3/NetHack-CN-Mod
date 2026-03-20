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
 * Collect writable slots of %s arguments from va_list walking by fmt order.
 * The returned slots point into the underlying argument area used by va_list.
 */
static int collect_str_slots(const char *fmt, va_list args,
                             const char **slots[], int max_slots) {
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
                const char **slot = (const char **) ap;
                va_arg(ap, const char *);
                if (count < max_slots) {
                    slots[count] = slot;
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
 * Convert positional %n$s to plain %s and return mapping between output
 * %s order and input %s order (1-based in order_map entries).
 */
static bool normalize_positional_s_format(const char *fmt_in,
                                          char **fmt_out_alloc,
                                          int order_map[],
                                          int max_order,
                                          int *order_count_out) {
    const char *p;
    char *out;
    char *w;
    int order_count = 0;
    int implicit_index = 1;
    bool found_positional = false;

    if (!fmt_in || !fmt_out_alloc || !order_map || !order_count_out) {
        return false;
    }

    out = (char *) malloc(strlen(fmt_in) + 1);
    if (!out) {
        return false;
    }

    p = fmt_in;
    w = out;

    while (*p) {
        if (*p != '%') {
            *w++ = *p++;
            continue;
        }

        *w++ = *p++; /* copy '%' */

        if (*p == '%') {
            *w++ = *p++;
            continue;
        }

        {
            const char *spec_start = p;
            const char *q = p;
            int positional = 0;

            while (*q >= '0' && *q <= '9') {
                positional = positional * 10 + (*q - '0');
                ++q;
            }
            if (q > spec_start && *q == '$') {
                found_positional = true;
                p = q + 1; /* skip n$ */
            }

            /* Copy rest of conversion and capture final specifier */
            {
                vpline_len_mod len_mod = VPL_LEN_NONE;
                char conv;

                while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') {
                    *w++ = *p++;
                }
                if (*p == '*') {
                    *w++ = *p++;
                } else {
                    while (*p >= '0' && *p <= '9') {
                        *w++ = *p++;
                    }
                }
                if (*p == '.') {
                    *w++ = *p++;
                    if (*p == '*') {
                        *w++ = *p++;
                    } else {
                        while (*p >= '0' && *p <= '9') {
                            *w++ = *p++;
                        }
                    }
                }

                if (*p == 'h' && *(p + 1) == 'h') {
                    *w++ = *p++;
                    *w++ = *p++;
                    len_mod = VPL_LEN_HH;
                } else if (*p == 'h') {
                    *w++ = *p++;
                    len_mod = VPL_LEN_H;
                } else if (*p == 'l' && *(p + 1) == 'l') {
                    *w++ = *p++;
                    *w++ = *p++;
                    len_mod = VPL_LEN_LL;
                } else if (*p == 'l') {
                    *w++ = *p++;
                    len_mod = VPL_LEN_L;
                } else if (*p == 'j') {
                    *w++ = *p++;
                    len_mod = VPL_LEN_J;
                } else if (*p == 'z') {
                    *w++ = *p++;
                    len_mod = VPL_LEN_Z;
                } else if (*p == 't') {
                    *w++ = *p++;
                    len_mod = VPL_LEN_T;
                } else if (*p == 'L') {
                    *w++ = *p++;
                    len_mod = VPL_LEN_CAP_L;
                }

                conv = *p;
                if (!conv) {
                    break;
                }
                *w++ = *p++;

                if (conv == 's' && len_mod != VPL_LEN_L) {
                    int mapped_index = positional > 0 ? positional : implicit_index;
                    if (order_count < max_order) {
                        order_map[order_count] = mapped_index;
                    }
                    order_count++;
                    implicit_index++;
                }
            }
        }
    }

    *w = '\0';

    if (!found_positional) {
        free(out);
        return false;
    }

    *fmt_out_alloc = out;
    *order_count_out = order_count;
    return true;
}

static void reorder_str_slots_for_positional_fmt(const char *src_fmt,
                                                 va_list args,
                                                 const int order_map[],
                                                 int order_count) {
    const char **slots[MAX_FMT_ARG_ALLOCS];
    const char *snapshot[MAX_FMT_ARG_ALLOCS];
    int slot_count;
    int i;

    if (!src_fmt || !order_map || order_count <= 0) {
        return;
    }

    slot_count = collect_str_slots(src_fmt, args, slots, MAX_FMT_ARG_ALLOCS);
    if (slot_count <= 0) {
        return;
    }
    if (slot_count > MAX_FMT_ARG_ALLOCS) {
        slot_count = MAX_FMT_ARG_ALLOCS;
    }

    for (i = 0; i < slot_count; ++i) {
        snapshot[i] = *slots[i];
    }

    for (i = 0; i < order_count && i < slot_count; ++i) {
        int src_index = order_map[i] - 1;
        if (src_index >= 0 && src_index < slot_count) {
            *slots[i] = snapshot[src_index];
        }
    }
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
 * If a nested_fmt is triggered, *fmt_out is set to the nested format string.
 */
static int translate_vpline_args(const char *fmt, va_list args,
                                 const zh_fmt_item *fi,
                                 char *allocs[], int max_allocs,
                                 const char **fmt_out) {
    va_list ap;
    const char *p;
    int alloc_count = 0;
    vpline_len_mod len_mod;
    char conv;
    size_t j;

    va_copy(ap, args);
    p = fmt;
    *fmt_out = fi->fmt_zh;  /* Default to normal fmt */

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
                            /* Check if this arg has nested_fmt */
                            if (fi->args[j].nested_fmt) {
                                /* Use nested fmt and translate nested arg if present */
                                *fmt_out = fi->args[j].nested_fmt;
                                if (fi->args[j].arg_zh && fi->args[j].arg_zh[0]) {
                                    if (alloc_count < max_allocs) {
                                        char *dup = _strdup(fi->args[j].arg_zh);
                                        if (dup) {
                                            *slot = dup;
                                            allocs[alloc_count++] = dup;
                                        }
                                    }
                                }
                                /* Don't process remaining args - nested fmt handles them */
                            } else {
                                /* Normal arg translation */
                                if (alloc_count < max_allocs) {
                                    char *dup = _strdup(fi->args[j].arg_zh);
                                    if (dup) {
                                        *slot = dup;
                                        allocs[alloc_count++] = dup;
                                    }
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

    /* Check for fmt/arg format first for full line (scores candidates on ambiguous keys) */
    fi = find_best_fmt_item(line, the_args);
    if (fi) {
        char *arg_allocs[MAX_FMT_ARG_ALLOCS];
        char *normalized_fmt = NULL;
        int order_map[MAX_FMT_ARG_ALLOCS];
        int order_count = 0;
        int alloc_count, i;
        const char *fmt_out = fi->fmt_zh;

        alloc_count = translate_vpline_args(line, the_args, fi,
                                            arg_allocs, MAX_FMT_ARG_ALLOCS, &fmt_out);

        if (normalize_positional_s_format(fmt_out, &normalized_fmt,
                                          order_map, MAX_FMT_ARG_ALLOCS,
                                          &order_count)) {
            reorder_str_slots_for_positional_fmt(line, the_args,
                                                 order_map, order_count);
            fmt_out = normalized_fmt;
        }

        dump_intercepted_text("vpline.after", fmt_out, -1);
        g_orig_vpline(fmt_out, the_args);

        free(normalized_fmt);
        free(prefixed_alloc);

        for (i = 0; i < alloc_count; ++i)
            free(arg_allocs[i]);
        return;
    }

    /* Check for fmt/arg format first for tail (scores candidates on ambiguous keys) */
    fi = find_best_fmt_item(tail, the_args);
    if (fi) {
        char *arg_allocs[MAX_FMT_ARG_ALLOCS];
        char *normalized_fmt = NULL;
        int alloc_count, i;
        int order_map[MAX_FMT_ARG_ALLOCS];
        int order_count = 0;
        const char *fmt_out = fi->fmt_zh;

        alloc_count = translate_vpline_args(tail, the_args, fi,
                                            arg_allocs, MAX_FMT_ARG_ALLOCS, &fmt_out);

        if (normalize_positional_s_format(fmt_out, &normalized_fmt,
                                          order_map, MAX_FMT_ARG_ALLOCS,
                                          &order_count)) {
            reorder_str_slots_for_positional_fmt(tail, the_args,
                                                 order_map, order_count);
            fmt_out = normalized_fmt;
        }

        if (zh_prefix) {
            prefixed_alloc = concat_two_alloc(zh_prefix, fmt_out);
            if (prefixed_alloc) {
                fmt_out = prefixed_alloc;
            }
        }

        dump_intercepted_text("vpline.after", fmt_out, -1);
        g_orig_vpline(fmt_out, the_args);

        free(normalized_fmt);
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
