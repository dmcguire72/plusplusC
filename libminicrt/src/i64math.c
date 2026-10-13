// ++C C Runtime Library (libminicrt)
// Copyright 2026 Daniel McGuire
// Licensed under the MIT License

#if (defined(__i386__) || defined(_M_IX86)) && defined(_WIN32) && (defined(__clang__) || defined(__GNUC__))

#include <def.h>

static XXC_NO_SSP uint64_t xxc_udivmod(uint64_t n, uint64_t d, uint64_t *rem)
{
    uint64_t q = 0, r = 0;
    for (int i = 63; i >= 0; i--)
    {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d)
        {
            r -= d;
            q |= (uint64_t)1 << i;
        }
    }
    *rem = r;
    return q;
}

#define XXC_U64(lo, hi) (((uint64_t)(uint32_t)(hi) << 32) | (uint32_t)(lo))

XXC_USED XXC_NO_SSP uint64_t xxc_udiv64(uint32_t nlo, uint32_t nhi, uint32_t dlo, uint32_t dhi)
{
    uint64_t r;
    return xxc_udivmod(XXC_U64(nlo, nhi), XXC_U64(dlo, dhi), &r);
}

XXC_USED XXC_NO_SSP uint64_t xxc_urem64(uint32_t nlo, uint32_t nhi, uint32_t dlo, uint32_t dhi)
{
    uint64_t r;
    xxc_udivmod(XXC_U64(nlo, nhi), XXC_U64(dlo, dhi), &r);
    return r;
}

static XXC_NO_SSP int64_t xxc_sdivmod(int64_t n, int64_t d, int64_t *rem)
{
    uint64_t un = (uint64_t)n, ud = (uint64_t)d, r;
    int neg = 0;
    if (n < 0) { un = (uint64_t)0 - un; neg ^= 1; }
    if (d < 0) { ud = (uint64_t)0 - ud; neg ^= 1; }
    uint64_t q = xxc_udivmod(un, ud, &r);
    *rem = (int64_t)(n < 0 ? (uint64_t)0 - r : r);
    return (int64_t)(neg ? (uint64_t)0 - q : q);
}

XXC_USED XXC_NO_SSP uint64_t xxc_sdiv64(uint32_t nlo, uint32_t nhi, uint32_t dlo, uint32_t dhi)
{
    int64_t r;
    return (uint64_t)xxc_sdivmod((int64_t)XXC_U64(nlo, nhi), (int64_t)XXC_U64(dlo, dhi), &r);
}

XXC_USED XXC_NO_SSP uint64_t xxc_srem64(uint32_t nlo, uint32_t nhi, uint32_t dlo, uint32_t dhi)
{
    int64_t r;
    xxc_sdivmod((int64_t)XXC_U64(nlo, nhi), (int64_t)XXC_U64(dlo, dhi), &r);
    return (uint64_t)r;
}

XXC_USED XXC_NO_SSP uint64_t __udivdi3(uint64_t n, uint64_t d) { uint64_t r; return xxc_udivmod(n, d, &r); }
XXC_USED XXC_NO_SSP uint64_t __umoddi3(uint64_t n, uint64_t d) { uint64_t r; xxc_udivmod(n, d, &r); return r; }
XXC_USED XXC_NO_SSP int64_t  __divdi3(int64_t n, int64_t d)    { int64_t r; return xxc_sdivmod(n, d, &r); }
XXC_USED XXC_NO_SSP int64_t  __moddi3(int64_t n, int64_t d)    { int64_t r; xxc_sdivmod(n, d, &r); return r; }

#define XXC_I64_THUNK(sym, fn)                                         \
    ".globl " sym "\n"                                                 \
    ".def " sym "; .scl 2; .type 32; .endef\n"                         \
    sym ":\n"                                                          \
    "    pushl 16(%esp)\n"                                             \
    "    pushl 16(%esp)\n"                                             \
    "    pushl 16(%esp)\n"                                             \
    "    pushl 16(%esp)\n"                                             \
    "    call " fn "\n"                                                \
    "    addl  $16, %esp\n"                                            \
    "    ret   $16\n"

__asm__(
    ".text\n"
    XXC_I64_THUNK("__aulldiv", "_xxc_udiv64")
    XXC_I64_THUNK("__aullrem", "_xxc_urem64")
    XXC_I64_THUNK("__alldiv",  "_xxc_sdiv64")
    XXC_I64_THUNK("__allrem",  "_xxc_srem64")
);

#endif
