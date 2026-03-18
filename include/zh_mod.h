#ifndef ZH_MOD_H
#define ZH_MOD_H

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

/* ---------- function pointer typedefs ---------- */

typedef BOOL (WINAPI *PFN_PlaySoundA)(LPCSTR, HMODULE, DWORD);
typedef BOOL (WINAPI *PFN_PlaySoundW)(LPCWSTR, HMODULE, DWORD);
typedef BOOL (WINAPI *PFN_sndPlaySoundA)(LPCSTR, UINT);
typedef BOOL (WINAPI *PFN_sndPlaySoundW)(LPCWSTR, UINT);

typedef BOOL (WINAPI *PFN_SetWindowTextA)(HWND, LPCSTR);
typedef int (WINAPI *PFN_DrawTextA)(HDC, LPCSTR, int, LPRECT, UINT);
typedef BOOL (WINAPI *PFN_TextOutA)(HDC, int, int, LPCSTR, int);

typedef void (__cdecl *PFN_vpline)(const char *, va_list);
typedef void (__cdecl *PFN_putstr)(int, int, const char *);

/* ---------- macros ---------- */

#define WINMM_EXPORT __declspec(dllexport)
#define MAX_FMT_ARG_ALLOCS 16

/* ---------- data structures ---------- */

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

/* ---------- global variables (defined in winmm_proxy_main.c) ---------- */

extern HMODULE g_real_winmm;
extern PFN_PlaySoundA g_real_PlaySoundA;
extern PFN_PlaySoundW g_real_PlaySoundW;
extern PFN_sndPlaySoundA g_real_sndPlaySoundA;
extern PFN_sndPlaySoundW g_real_sndPlaySoundW;

extern PFN_SetWindowTextA g_orig_SetWindowTextA;
extern PFN_DrawTextA g_orig_DrawTextA;
extern PFN_TextOutA g_orig_TextOutA;

extern PFN_vpline g_orig_vpline;
extern PFN_putstr g_orig_putstr;

extern LONG g_init_state;
extern HANDLE g_dump_file;
extern CRITICAL_SECTION g_dump_lock;
extern bool g_dump_lock_ready;
extern zh_runtime_item *g_runtime_map;
extern size_t g_runtime_map_count;
extern zh_fmt_item *g_fmt_map;
extern size_t g_fmt_map_count;
extern bool g_sym_initialized;

/* ---------- zh_dump.c ---------- */

void dump_json_loaded_count(size_t count);
void init_dump_file(void);
void dump_intercepted_text(const char *api_name, const char *text, int length);
void log_hook_message(const char *fmt, ...);
void dump_vpline_arguments(const char *fmt, va_list args);

/* ---------- zh_translate.c ---------- */

void free_runtime_map(void);
void free_fmt_map(void);
void load_runtime_map_from_resource(HMODULE module);
bool has_printf_format_spec(const char *s);
char *utf8_to_local_alloc(const char *utf8_str);
char *utf8_to_local_alloc_len(const char *utf8_str, int in_len);
bool is_likely_utf8_text(const char *s, int len);
const char *translate_text(const char *src, int length);
char *translate_text_contains_alloc(const char *src, int length);

/* ---------- zh_vpline.c ---------- */

void __cdecl hook_vpline(const char *line, va_list the_args);
void __cdecl hook_putstr(int winid, int attr, const char *text);

/* ---------- zh_hooks.c ---------- */

void install_text_hooks(void);
void install_symbol_hook(const char *exact_name,
                         const char *contains_name,
                         const char *search_pattern,
                         LPVOID detour,
                         LPVOID *original);

#endif /* ZH_MOD_H */
