// ++C C Runtime Library (libminicrt)
// Copyright 2026 Daniel McGuire
// Licensed under the MIT License

#include <def.h>
#include <atexit.h>
#include <cio.h>

#define XXC_SSP_DEFAULT ((uintptr_t)0x595e9fbd94fda766ull)

XXC_USED uintptr_t __stack_chk_guard = XXC_SSP_DEFAULT & ~(uintptr_t)0xff;

XXC_NORETURN XXC_USED XXC_NO_SSP void __stack_chk_fail(void)
{
    puts("*** stack smashing detected ***: terminated\n", SIOERR);
    _exit(134);
}

#if defined(__i386__) && !defined(_WIN32)

XXC_NORETURN XXC_USED XXC_NO_SSP __attribute__((visibility("hidden"))) void __stack_chk_fail_local(void)
{
    __stack_chk_fail();
}
#endif

#if defined(_WIN32) || defined(XXC_PE_TARGET)

#if !defined(__MINGW32__)
#define XXC_MSVC_COOKIE 1
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(_M_ARM64)
#define XXC_COOKIE_64 1
#endif

#if defined(XXC_MSVC_COOKIE)
#if defined(XXC_COOKIE_64)
XXC_USED uintptr_t __security_cookie = (uintptr_t)0x00002B992DDFA232ull;
#define XXC_DEFAULT_COOKIE ((uintptr_t)0x00002B992DDFA232ull)
#else
XXC_USED uintptr_t __security_cookie = (uintptr_t)0xBB40E64Eu;
#define XXC_DEFAULT_COOKIE ((uintptr_t)0xBB40E64Eu)
#endif
#endif

#if defined(_WIN32)
#define va_start vastart_guard
#include <windows.h>
#undef va_start

__declspec(dllimport) LONG WINAPI BCryptGenRandom(void *hAlgorithm, unsigned char *pbBuffer,
                                                  ULONG cbBuffer, ULONG dwFlags);
#define XXC_BCRYPT_USE_SYSTEM_PREFERRED_RNG 0x00000002UL

static XXC_NO_SSP uint64_t xxc_mix64(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

static XXC_NO_SSP uint64_t xxc_fallback_random(void)
{
    LARGE_INTEGER pc;
    uint64_t v = 0;
    QueryPerformanceCounter(&pc);
    v = xxc_mix64((uint64_t)pc.QuadPart);
    v = xxc_mix64(v ^ GetTickCount64());
    v = xxc_mix64(v ^ ((uint64_t)GetCurrentProcessId() << 32 | GetCurrentThreadId()));
    v = xxc_mix64(v ^ (uint64_t)(uintptr_t)&pc);
    v = xxc_mix64(v ^ (uint64_t)(uintptr_t)&__stack_chk_guard);
    return v;
}

static XXC_NO_SSP uint64_t xxc_random64(void)
{
    uint64_t r = 0;
    if (BCryptGenRandom(nullptr, (unsigned char *)&r, sizeof(r), XXC_BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0 && r != 0)
        return r;
    return xxc_fallback_random();
}

#if defined(XXC_MSVC_COOKIE)
static XXC_NO_SSP uintptr_t xxc_make_cookie(uint64_t r)
{
#if defined(XXC_COOKIE_64)
    uintptr_t c = (uintptr_t)(r & 0x0000FFFFFFFFFFFFull);
#else
    uintptr_t c = (uintptr_t)r;
    if ((c & 0xFFFF0000u) == 0)
        c |= c << 16;
#endif
    if (c == 0 || c == XXC_DEFAULT_COOKIE)
        c = XXC_DEFAULT_COOKIE ^ (uintptr_t)0x5AA5;
    return c;
}
#endif

XXC_USED XXC_NO_SSP void __xxc_ssp_init(void)
{
    uint64_t r = xxc_random64();
    __stack_chk_guard = (uintptr_t)r & ~(uintptr_t)0xff;
#if defined(XXC_MSVC_COOKIE)
    __security_cookie = xxc_make_cookie(xxc_mix64(r));
#endif
}
#else
XXC_USED XXC_NO_SSP void __xxc_ssp_init(void) {}
#endif /* _WIN32 */

#if defined(XXC_MSVC_COOKIE)

#if defined(__i386__) || defined(_M_IX86)

#if defined(_MSC_VER) && !defined(__clang__)
#define XXC_FASTCALL __fastcall
#else
#define XXC_FASTCALL __attribute__((fastcall))
#endif

XXC_NORETURN XXC_USED XXC_NO_SSP void __cdecl __report_gsfailure(void)
{
    puts("*** stack buffer overrun detected ***: terminated\n", SIOERR);
    _exit(0xC0000409u & 0xff);
}

XXC_USED XXC_NO_SSP void XXC_FASTCALL __security_check_cookie(uintptr_t cookie)
{
    if (cookie != __security_cookie)
        __report_gsfailure();
}

#else /* x64 / ARM64 */

XXC_NORETURN XXC_USED XXC_NO_SSP void __report_gsfailure(uintptr_t stack_cookie)
{
    (void)stack_cookie;
    puts("*** stack buffer overrun detected ***: terminated\n", SIOERR);
    _exit(0xC0000409u & 0xff);
}

XXC_USED XXC_NO_SSP void __security_check_cookie(uintptr_t cookie)
{
    if (cookie != __security_cookie)
        __report_gsfailure(cookie);
}

#endif /* i386 */
#endif /* XXC_MSVC_COOKIE */
#endif /* PE */
