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
    // make optimizer not able to assume two section markers belong to the same object.
    #define XXC_PE_OPAQUE(p) do { void *volatile xxc_v_ = (void *)(p); (p) = xxc_v_; } while (0)
#endif

#if defined(_MSC_VER) && !defined(__clang__)
    #pragma section(".rdata$xxc_ia", read)
    #pragma section(".rdata$xxc_iz", read)
    #pragma section(".rdata$xxc_ta", read)
    #pragma section(".rdata$xxc_tz", read)

    __declspec(allocate(".rdata$xxc_ia")) static const xxc_initrec_t xxc_ir_begin = { 0, 0 };
    __declspec(allocate(".rdata$xxc_iz")) static const xxc_initrec_t xxc_ir_end   = { 0, 0 };
    __declspec(allocate(".rdata$xxc_ta")) static const xxc_initrec_t xxc_tr_begin = { 0, 0 };
    __declspec(allocate(".rdata$xxc_tz")) static const xxc_initrec_t xxc_tr_end   = { 0, 0 };

    static void xxc_pe_run_ctor_records(void)
    {
        const xxc_initrec_t *b = &xxc_ir_begin;
        const xxc_initrec_t *e = &xxc_ir_end;
        u32  done = 0;
        bool have_done = false;
        XXC_PE_OPAQUE(b);
        XXC_PE_OPAQUE(e);

        for (;;)
        {
            bool found = false;
            u32  best = 0;
            for (const xxc_initrec_t *p = b; p < e; p++)
            {
                if (p->fn == nullptr || (have_done && p->prio <= done))
                    continue;
                if (!found || p->prio < best)
                {
                    best = p->prio;
                    found = true;
                }
            }
            if (!found)
                break;
            for (const xxc_initrec_t *p = b; p < e; p++)
                if (p->fn != nullptr && p->prio == best)
                    p->fn();
            done = best;
            have_done = true;
        }
    }

    static void xxc_pe_run_dtor_records(void)
    {
        const xxc_initrec_t *b = &xxc_tr_begin;
        const xxc_initrec_t *e = &xxc_tr_end;
        u32  done = 0;
        bool have_done = false;
        XXC_PE_OPAQUE(b);
        XXC_PE_OPAQUE(e);

        for (;;)
        {
            bool found = false;
            u32  best = 0;
            for (const xxc_initrec_t *p = b; p < e; p++)
            {
                if (p->fn == nullptr || (have_done && p->prio >= done))
                    continue;
                if (!found || p->prio > best)
                {
                    best = p->prio;
                    found = true;
                }
            }
            if (!found)
                break;
            for (const xxc_initrec_t *p = e; p > b; )
            {
                --p;
                if (p->fn != nullptr && p->prio == best)
                    p->fn();
            }
            done = best;
            have_done = true;
        }
    }
#else
    #define xxc_pe_run_ctor_records() ((void)0)
    #define xxc_pe_run_dtor_records() ((void)0)
#endif

static void xxc_pe_run_ctors(void)
{
    xxc_pe_run_ctor_records();

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

    xxc_pe_run_dtor_records();
}

#endif /* __MINGW32__ */

#endif /* XXC_PE_INITFINI_H */
