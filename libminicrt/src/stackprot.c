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
#if !defined(__MINGW32__) && (defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(_M_ARM64))
XXC_USED uintptr_t __security_cookie = (uintptr_t)0x00002B992DDFA232ull;

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
#endif
#endif
