// ++C C Runtime Library (libminicrt) | Platform (win32)
// Copyright 2026 Daniel McGuire
// Licensed under the MIT License

#define va_start vastart_guard
#include <windows.h>
#undef va_start

#include <pe_initfini.h>

extern void __xxc_ssp_init(void);

volatile int _fltused = 0;

static BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    (void)hinst;
    (void)reason;
    (void)reserved;
    return TRUE;
}

#if defined(__MINGW32__)
    #define XXC_DLL_ENTRY DllMainCRTStartup
#else
    #define XXC_DLL_ENTRY _DllMainCRTStartup
#endif

XXC_NO_SSP BOOL WINAPI XXC_DLL_ENTRY(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        __xxc_ssp_init();
        xxc_pe_run_ctors();
    }

    BOOL ok = DllMain(hinst, reason, reserved);

    if ((reason == DLL_PROCESS_ATTACH && !ok) || reason == DLL_PROCESS_DETACH)
        xxc_pe_run_dtors();
    return ok;
}
