// ++C C Runtime Library (libminicrt) | Platform (PE/COFF: Windows, UEFI)
// Copyright 2026 Daniel McGuire
// Licensed under the MIT License

#ifndef XXC_PE_INITFINI_H
#define XXC_PE_INITFINI_H

#include <def.h>

typedef void (*xxc_pe_fn)(void);

#if defined(__MINGW32__)

extern xxc_pe_fn __CTOR_LIST__[];
extern xxc_pe_fn __DTOR_LIST__[];

static void xxc_pe_run_ctors(void)
{
    size_t n = 0;
    while (__CTOR_LIST__[n + 1] != nullptr)
        n++;
    for (; n > 0; n--)
    {
        xxc_pe_fn f = __CTOR_LIST__[n];
        if (f != (xxc_pe_fn)(uintptr_t)-1)
            f();
    }
}

static void xxc_pe_run_dtors(void)
{
    for (xxc_pe_fn *p = &__DTOR_LIST__[1]; *p != nullptr; p++)
        if (*p != (xxc_pe_fn)(uintptr_t)-1)
            (*p)();
}

#else /* MSVC ABI */

#if defined(_MSC_VER) && !defined(__clang__)
    #pragma section(".CRT$XC",  long, read)
    #pragma section(".CRT$XCZ", long, read)
    #pragma section(".CRT$XT",  long, read)
    #pragma section(".CRT$XTZ", long, read)
    #define XXC_PE_INITSEC(name) __declspec(allocate(name))
#else
    #define XXC_PE_INITSEC(name) __attribute__((section(name)))
#endif

XXC_PE_INITSEC(".CRT$XC")  XXC_USED static xxc_pe_fn const xxc_xc_begin = 0;
XXC_PE_INITSEC(".CRT$XCZ") XXC_USED static xxc_pe_fn const xxc_xc_end   = 0;
XXC_PE_INITSEC(".CRT$XT")  XXC_USED static xxc_pe_fn const xxc_xt_begin = 0;
XXC_PE_INITSEC(".CRT$XTZ") XXC_USED static xxc_pe_fn const xxc_xt_end   = 0;

#if defined(__GNUC__) || defined(__clang__)
    #define XXC_PE_OPAQUE(p) __asm__ volatile("" : "+r"(p))
#else
    #define XXC_PE_OPAQUE(p) ((void)0)
#endif

static void xxc_pe_run_ctors(void)
{
    xxc_pe_fn const *b = &xxc_xc_begin;
    xxc_pe_fn const *e = &xxc_xc_end;
    XXC_PE_OPAQUE(b);
    XXC_PE_OPAQUE(e);
    for (xxc_pe_fn const *p = b + 1; p < e; p++)
        if (*p != nullptr)
            (*p)();
}

static void xxc_pe_run_dtors(void)
{
    xxc_pe_fn const *b = &xxc_xt_begin;
    xxc_pe_fn const *e = &xxc_xt_end;
    XXC_PE_OPAQUE(b);
    XXC_PE_OPAQUE(e);
    for (xxc_pe_fn const *p = e; p > b + 1; )
    {
        --p;
        if (*p != nullptr)
            (*p)();
    }
}

#endif /* __MINGW32__ */

#endif /* XXC_PE_INITFINI_H */
