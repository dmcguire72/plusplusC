// ++C C Runtime Library (libminicrt)
// Copyright 2026 Daniel McGuire
// Licensed under the MIT License

#include <mem.h>
#include <def.h>

#if defined(_MSC_VER) && !defined(__clang__)
    #include <intrin.h>
    #pragma function(memcpy)
    #pragma function(memset)
    #pragma function(memmove)
    #pragma function(memcmp)
    #pragma optimize("", off)
#elif defined(__GNUC__) && !defined(__clang__)
    #pragma GCC optimize("no-tree-loop-distribute-patterns")
#endif

#if !defined(XXC_MEM_PORTABLE) && (defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86))
    #define XXC_MEM_X86 1
#endif

#if defined(_MSC_VER) && !defined(__clang__)
    #define LD16(p)    (*(const __unaligned u16 *)(p))
    #define LD32(p)    (*(const __unaligned u32 *)(p))
    #define LD64(p)    (*(const __unaligned u64 *)(p))
    #define ST16(p, x) (*(__unaligned u16 *)(p) = (x))
    #define ST32(p, x) (*(__unaligned u32 *)(p) = (x))
    #define ST64(p, x) (*(__unaligned u64 *)(p) = (x))
#else
    typedef struct { u16 v; } __attribute__((packed, may_alias)) xxc_u16u;
    typedef struct { u32 v; } __attribute__((packed, may_alias)) xxc_u32u;
    typedef struct { u64 v; } __attribute__((packed, may_alias)) xxc_u64u;
    #define LD16(p)    (((const xxc_u16u *)(p))->v)
    #define LD32(p)    (((const xxc_u32u *)(p))->v)
    #define LD64(p)    (((const xxc_u64u *)(p))->v)
    #define ST16(p, x) (((xxc_u16u *)(p))->v = (x))
    #define ST32(p, x) (((xxc_u32u *)(p))->v = (x))
    #define ST64(p, x) (((xxc_u64u *)(p))->v = (x))
#endif

#if defined(_MSC_VER) && !defined(__clang__)
    #define XXC_REP_MOVS_MIN 32
    #define XXC_REP_STOS_MIN 32
#else
    #ifndef XXC_REP_MOVS_MIN
        #define XXC_REP_MOVS_MIN 512
    #endif
    #ifndef XXC_REP_STOS_MIN
        #define XXC_REP_STOS_MIN 512
    #endif
#endif

#if defined(XXC_MEM_X86)
static XXC_NO_SSP inline void xxc_rep_movsb(unsigned char *d, const unsigned char *s, size_t n)
{
    #if defined(_MSC_VER) && !defined(__clang__)
    __movsb(d, s, n);
    #else
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    #endif
}

static XXC_NO_SSP inline void xxc_rep_stosb(unsigned char *d, unsigned char c, size_t n)
{
    #if defined(_MSC_VER) && !defined(__clang__)
    __stosb(d, c, n);
    #else
    __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory");
    #endif
}
#endif

static XXC_NO_SSP inline void copy_upto32(unsigned char *d, const unsigned char *s, size_t n)
{
    if (n >= 16)
    {
        u64 a = LD64(s), b = LD64(s + 8), c = LD64(s + n - 16), e = LD64(s + n - 8);
        ST64(d, a); ST64(d + 8, b); ST64(d + n - 16, c); ST64(d + n - 8, e);
    }
    else if (n >= 8)
    {
        u64 a = LD64(s), b = LD64(s + n - 8);
        ST64(d, a); ST64(d + n - 8, b);
    }
    else if (n >= 4)
    {
        u32 a = LD32(s), b = LD32(s + n - 4);
        ST32(d, a); ST32(d + n - 4, b);
    }
    else if (n >= 2)
    {
        u16 a = LD16(s), b = LD16(s + n - 2);
        ST16(d, a); ST16(d + n - 2, b);
    }
    else if (n == 1)
        *d = *s;
}

static XXC_NO_SSP inline void copy_forward(unsigned char *d, const unsigned char *s, size_t n)
{
#if defined(XXC_MEM_X86)
    if (n > XXC_REP_MOVS_MIN)
    {
        xxc_rep_movsb(d, s, n);
        return;
    }
#endif
    u64 t0 = LD64(s + n - 32), t1 = LD64(s + n - 24), t2 = LD64(s + n - 16), t3 = LD64(s + n - 8);
    size_t i = 0;
    for (; i + 32 <= n; i += 32)
    {
        u64 a = LD64(s + i), b = LD64(s + i + 8), c = LD64(s + i + 16), e = LD64(s + i + 24);
        ST64(d + i, a); ST64(d + i + 8, b); ST64(d + i + 16, c); ST64(d + i + 24, e);
    }
    ST64(d + n - 32, t0); ST64(d + n - 24, t1); ST64(d + n - 16, t2); ST64(d + n - 8, t3);
}

static XXC_NO_SSP inline void copy_backward(unsigned char *d, const unsigned char *s, size_t n)
{
    u64 h0 = LD64(s), h1 = LD64(s + 8), h2 = LD64(s + 16), h3 = LD64(s + 24);
    for (size_t j = n; j >= 32; j -= 32)
    {
        u64 a = LD64(s + j - 32), b = LD64(s + j - 24), c = LD64(s + j - 16), e = LD64(s + j - 8);
        ST64(d + j - 32, a); ST64(d + j - 24, b); ST64(d + j - 16, c); ST64(d + j - 8, e);
    }
    ST64(d, h0); ST64(d + 8, h1); ST64(d + 16, h2); ST64(d + 24, h3);
}

XXC_USED XXC_NO_SSP void* memcpy(void* dest, const void* src, size_t n)
{
    if (n <= 32)
        copy_upto32((unsigned char *)dest, (const unsigned char *)src, n);
    else
        copy_forward((unsigned char *)dest, (const unsigned char *)src, n);
    return dest;
}

XXC_USED XXC_NO_SSP void* memmove(void* dest, const void* src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0)
        return dest;
    if (n <= 32)
        copy_upto32(d, s, n);
    else if (d < s || d >= s + n)
        copy_forward(d, s, n);
    else
        copy_backward(d, s, n);
    return dest;
}

XXC_USED XXC_NO_SSP void* memset(void* dest, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    u64 v = (u64)(unsigned char)c * 0x0101010101010101ull;

    if (n <= 32)
    {
        if (n >= 16)
        {
            ST64(d, v); ST64(d + 8, v); ST64(d + n - 16, v); ST64(d + n - 8, v);
        }
        else if (n >= 8)
        {
            ST64(d, v); ST64(d + n - 8, v);
        }
        else if (n >= 4)
        {
            ST32(d, (u32)v); ST32(d + n - 4, (u32)v);
        }
        else if (n >= 2)
        {
            ST16(d, (u16)v); ST16(d + n - 2, (u16)v);
        }
        else if (n == 1)
            *d = (unsigned char)c;
        return dest;
    }

#if defined(XXC_MEM_X86)
    if (n > XXC_REP_STOS_MIN)
    {
        xxc_rep_stosb(d, (unsigned char)c, n);
        return dest;
    }
#endif
    size_t i = 0;
    for (; i + 32 <= n; i += 32)
    {
        ST64(d + i, v); ST64(d + i + 8, v); ST64(d + i + 16, v); ST64(d + i + 24, v);
    }
    ST64(d + n - 32, v); ST64(d + n - 24, v); ST64(d + n - 16, v); ST64(d + n - 8, v);
    return dest;
}

XXC_USED XXC_NO_SSP int memcmp(const void* a, const void* b, size_t n)
{
    const unsigned char* pa = (const unsigned char*)a;
    const unsigned char* pb = (const unsigned char*)b;
    size_t i = 0;

    for (; i + 8 <= n; i += 8)
    {
        if (LD64(pa + i) != LD64(pb + i))
            break;
    }
    for (; i < n; i++)
    {
        if (pa[i] != pb[i])
            return (int)pa[i] - (int)pb[i];
    }
    return 0;
}

#if defined(_MSC_VER) && !defined(__clang__)
#pragma optimize("", on)
#endif
