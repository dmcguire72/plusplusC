// ++C C Runtime Library (libminicrt)
// Copyright 2026 Daniel McGuire
// Licensed under the MIT License

#include <atexit.h>
#include <cio.h>
#include <parr.h>
#include <alloc.h>

extern int program(parr_t csArgs);

extern void sio_init(unsigned int out, unsigned int err);

extern void xxc_io_init(void);

#ifdef _WIN32
#include <windows.h>
#include <pe_initfini.h>

XXC_NORETURN void pluspluscBoot(int argc, char **argv)
{
    sio_init(STD_OUTPUT_HANDLE, STD_ERROR_HANDLE);

    xxc_io_init(); 

    __xxc_fini_hook = xxc_pe_run_dtors;
    xxc_pe_run_ctors();

    parr_t args = parr_new((const void**)argv, (size_t)argc);

    int ret = program(args);
    
    parr_free(&args);
    /* GCOV_EXCL_START */
    exit(ret);
    __NORETURN__
    /* GCOV_EXCL_STOP */
}

#elif defined(XXC_RAWSYS)
#include <xxc_sys.h>

typedef void (*xxc_init_fn)(int, char **, char **);
typedef void (*xxc_fini_fn)(void);

extern xxc_init_fn __preinit_array_start[] __attribute__((weak));
extern xxc_init_fn __preinit_array_end[]   __attribute__((weak));
extern xxc_init_fn __init_array_start[]    __attribute__((weak));
extern xxc_init_fn __init_array_end[]      __attribute__((weak));
extern xxc_fini_fn __fini_array_start[]    __attribute__((weak));
extern xxc_fini_fn __fini_array_end[]      __attribute__((weak));

static int          g_boot_argc;
static char       **g_boot_argv;

enum { ARR_PREINIT = 0, ARR_INIT = 1, ARR_FINI = 2 };

static void run_init_array(int which, int argc, char **argv)
{
    /* GCOV_EXCL_START */
    void **fns = nullptr;
    size_t n = 0;

    if (__xxc_dynamic_linked)
    {
        n = __xxc_exe_dyn_array(which, argc, argv, &fns);
    }
    else
    {
        xxc_init_fn *b = which == ARR_PREINIT ? __preinit_array_start : __init_array_start;
        xxc_init_fn *e = which == ARR_PREINIT ? __preinit_array_end   : __init_array_end;
        if (b == nullptr || e == nullptr)
            return;
        fns = (void **)b;
        n = (size_t)(e - b);
    }
    /* GCOV_EXCL_STOP */

    for (size_t i = 0; i < n; i++)
    {
        if (fns[i] == nullptr || fns[i] == (void *)(uintptr_t)-1)
            continue;
        ((xxc_init_fn)fns[i])(argc, argv, __xxc_environ);
    }
}

static void run_fini(void)
{
    /* GCOV_EXCL_START */
    void **fns = nullptr;
    size_t n = 0;

    if (__xxc_dynamic_linked)
    {
        n = __xxc_exe_dyn_array(ARR_FINI, g_boot_argc, g_boot_argv, &fns);
    }
    else if (__fini_array_start != nullptr && __fini_array_end != nullptr)
    {
        fns = (void **)__fini_array_start;
        n = (size_t)(__fini_array_end - __fini_array_start);
    }
    /* GCOV_EXCL_STOP */

    while (n > 0)
    {
        void *f = fns[--n];
        if (f == nullptr || f == (void *)(uintptr_t)-1)
            continue;
        ((xxc_fini_fn)f)();
    }

    if (__xxc_dynamic_linked)
#if defined(__FreeBSD__)
        __xxc_run_shared_fini(1);
#else
        __xxc_run_shared_fini(0);
#endif
}

XXC_NORETURN XXC_NO_SSP void pluspluscBoot(int argc, char **argv)
{
    __xxc_platform_init(argc, argv);
    sio_init(0, 0);
    xxc_io_init();

    g_boot_argc = argc;
    g_boot_argv = argv;
    __xxc_fini_hook = run_fini;

    run_init_array(ARR_PREINIT, argc, argv);
    run_init_array(ARR_INIT, argc, argv);

    parr_t args = parr_new((const void**)argv, (size_t)argc);
    int ret = program(args);
    parr_free(&args);
    /* GCOV_EXCL_START */
    exit(ret);
    __NORETURN__
    /* GCOV_EXCL_STOP */
}

#endif
