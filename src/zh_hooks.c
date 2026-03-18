#include "zh_mod.h"

/* ---------- IAT patching ---------- */

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

/* ---------- text API hooks ---------- */

static BOOL WINAPI hook_SetWindowTextA(HWND hWnd, LPCSTR lpString) {
    dump_intercepted_text("SetWindowTextA", lpString, -1);
    const char *translated = translate_text(lpString, -1);
    char *replaced = NULL;
    bool need_local_reencode = false;
    if (translated == lpString) {
        replaced = translate_text_contains_alloc(lpString, -1);
        translated = replaced ? replaced : lpString;
    }
    char *local_encoded = NULL;
    const char *final_text = translated;
    BOOL ret;

    need_local_reencode = (translated != lpString) || is_likely_utf8_text(translated, -1);
    if (need_local_reencode) {
        local_encoded = utf8_to_local_alloc_len(translated, -1);
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
    bool need_local_reencode = false;
    int encode_len = cchText;
    if (translated == lpchText) {
        replaced = translate_text_contains_alloc(lpchText, cchText);
        translated = replaced ? replaced : lpchText;
    }
    char *local_encoded = NULL;
    const char *final_text = translated;
    int out_len = cchText;
    int ret;

    need_local_reencode = (translated != lpchText) || is_likely_utf8_text(translated, cchText);
    if (need_local_reencode) {
        if (translated != lpchText) {
            dump_intercepted_text("DrawTextA.after", translated, -1);
            encode_len = -1;
        }
        local_encoded = utf8_to_local_alloc_len(translated, encode_len);
        if (local_encoded) {
            final_text = local_encoded;
            out_len = (int) strlen(local_encoded);
        } else {
            if (translated != lpchText) {
                final_text = translated;
                out_len = (int) strlen(translated);
            }
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
    bool need_local_reencode = false;
    int encode_len = c;
    if (translated == lpString) {
        replaced = translate_text_contains_alloc(lpString, c);
        translated = replaced ? replaced : lpString;
    }
    char *local_encoded = NULL;
    const char *final_text = translated;
    int out_len = c;
    BOOL ret;

    need_local_reencode = (translated != lpString) || is_likely_utf8_text(translated, c);
    if (need_local_reencode) {
        if (translated != lpString) {
            encode_len = -1;
        }
        local_encoded = utf8_to_local_alloc_len(translated, encode_len);
        if (local_encoded) {
            final_text = local_encoded;
            out_len = (int) strlen(local_encoded);
        } else {
            if (translated != lpString) {
                final_text = translated;
                out_len = (int) strlen(translated);
            }
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

void install_text_hooks(void) {
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

/* ---------- symbol resolution ---------- */

typedef struct {
    const char *exact_name;
    const char *contains_name;
    DWORD64 address;
} hook_symbol_ctx;

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

/* ---------- pattern search ---------- */

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

/* ---------- vpline signature-based resolution ---------- */

typedef struct {
    const char *label;
    const char *pattern;
} vpline_signature_item;

#if defined(_M_IX86) || defined(__i386__)
#define VPLINE_SIG_ARCH "i386"
static const vpline_signature_item vpline_signatures[] = {
    {
        "offcial msvc3.4.3",
        "81 EC 00 01 00 00 56 8B B4 24 08 01 00 00 85 F6 0F 84 ?? ?? ?? ?? 80 3E 00 0F 84 ?? ?? ?? ?? 6A 25 56 E8 ?? ?? ?? ?? 83 C4 08 85 C0 74 1A 8B 84 24 0C 01 00 00 8D 4C 24 04 50 56 51 E8 ?? ?? ?? ?? 83 C4 0C 8D 74 24 04 A0 ?? ?? ?? ?? 84 C0 75 12 56 FF 15 ?? ?? ?? ?? 83 C4 04 5E 81 C4 00 01 00 00 C3"
    },
    {
        "offcial msvc3.6.7",
        "55 8B EC 81 EC 2C 05 00 00 56 57 8D BD D4 FA FF FF B9 4B 01 00 00 B8 CC CC CC CC F3 AB A1 ?? ?? ?? ?? 33 C5 89 45 FC C7 85 E8 FA FF FF 00 00 00 00 83 7D 08 00 74 0A 8B 45 08 0F BE 08 85 C9 75 05 E9 ?? ?? ?? ?? 83 3D ?? ?? ?? ?? 00 74 05 E9 ?? ?? ?? ?? 83 3D ?? ?? ?? ?? 00 74 05 E9 ?? ?? ?? ?? 6A 25 8B 55 08 52 E8 ?? ?? ?? ?? 83 C4 08 85 C0 74 55 8B 45 0C 50 8B 4D 08 51 68 00 05 00 00 8D 95 F8 FA FF FF 52 E8 ?? ?? ?? ?? 83 C4 10 89 85 E8 FA FF FF 81 BD E8 FA FF FF 00 05 00 00 7C 1E"
    }
};
#else
#define VPLINE_SIG_ARCH "x64"
static const vpline_signature_item vpline_signatures[] = {
    {
        "msvc3.6-3.7",
        "48 89 54 24 10 48 89 4C 24 08 B8 78 05 00 00 E8 ?? ?? ?? ?? 48 2B E0 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 60 05 00 00 48 83 BC 24 80 05 00 00 00 74 ?? 48 8B 84 24 80 05 00 00 0F BE 00 85 C0 75 ?? E9"
    },
    {
        "mingw3.7",
        "55 48 81 EC 30 05 00 00 48 8D AC 24 80 00 00 00 48 89 8D ?? ?? ?? ?? 48 89 95 ?? ?? ?? ?? 48 83 BD ?? ?? ?? ?? 00 0F 84"
    }
};
#endif

static void *resolve_vpline_by_signature(void) {
    HMODULE exe = GetModuleHandleW(NULL);
    size_t i;

    if (!exe) {
        log_hook_message("[hook] vpline fallback failed: no module handle");
        return NULL;
    }

    log_hook_message("[hook] vpline symbol unresolved, trying signature fallback list (%s)", VPLINE_SIG_ARCH);

    for (i = 0; i < sizeof(vpline_signatures) / sizeof(vpline_signatures[0]); ++i) {
        void *addr = find_pattern_in_module_code(exe, vpline_signatures[i].pattern);
        if (addr) {
            log_hook_message("[hook] vpline signature match (%s) at %p", vpline_signatures[i].label, addr);
            return addr;
        }
    }

    log_hook_message("[hook] vpline signature fallback failed (no pattern matched)");
    return NULL;
}

/* ---------- MinHook-based symbol hooking ---------- */

void install_symbol_hook(const char *exact_name,
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
