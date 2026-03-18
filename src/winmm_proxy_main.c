#include "zh_mod.h"

/* ---------- global variable definitions ---------- */

HMODULE g_real_winmm = NULL;
PFN_PlaySoundA g_real_PlaySoundA = NULL;
PFN_PlaySoundW g_real_PlaySoundW = NULL;
PFN_sndPlaySoundA g_real_sndPlaySoundA = NULL;
PFN_sndPlaySoundW g_real_sndPlaySoundW = NULL;

PFN_SetWindowTextA g_orig_SetWindowTextA = NULL;
PFN_DrawTextA g_orig_DrawTextA = NULL;
PFN_TextOutA g_orig_TextOutA = NULL;

PFN_vpline g_orig_vpline = NULL;
PFN_putstr g_orig_putstr = NULL;

LONG g_init_state = 0;
HANDLE g_dump_file = INVALID_HANDLE_VALUE;
CRITICAL_SECTION g_dump_lock;
bool g_dump_lock_ready = false;
zh_runtime_item *g_runtime_map = NULL;
size_t g_runtime_map_count = 0;
zh_fmt_item *g_fmt_map = NULL;
size_t g_fmt_map_count = 0;
bool g_sym_initialized = false;

/* ---------- winmm proxy forwarding ---------- */

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

/* ---------- winmm proxy exports ---------- */

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

/* ---------- DLL entry point ---------- */

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    (void) reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        InitializeCriticalSection(&g_dump_lock);
        g_dump_lock_ready = true;
        init_dump_file();
        load_runtime_map_from_resource(hModule);
        install_text_hooks();
        install_symbol_hook("vpline", "vpline", "*vpline*", (LPVOID) hook_vpline,
                            (LPVOID *) &g_orig_vpline);
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
