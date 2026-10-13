// ++C C Runtime Library (libminicrt) | Platform (win32)
// Copyright 2026 Daniel McGuire
// Licensed under the MIT License

#define va_start vastart_guard
#include <windows.h>
#undef va_start

#include <def.h>
#include "commandline.h"

extern void exit(int status);
typedef void (*atexit_func_t)(void);
extern int atexit(atexit_func_t func);

extern void pluspluscBoot(int argc, char **argv);
extern void __xxc_ssp_init(void);

extern XXC_DYN_IMPORT HANDLE _xxc_w32_heap;

int argc = 0;
char **argv = nullptr;

volatile int _fltused = 0;

void freeArgs(void)
{
    FreeArgvA(argc, argv);
}

XXC_NORETURN void start(void)
{
    __xxc_ssp_init();
    _xxc_w32_heap = GetProcessHeap();
    argv = GetArgvA(&argc);
    atexit(freeArgs);
    pluspluscBoot(argc, argv);
    __NORETURN__ // GCOV_EXCL_LINE
}