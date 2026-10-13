// ++C C Runtime Library (libminicrt)
// Copyright 2026 Daniel McGuire
// Licensed under the MIT License

#include <clock.h>

#if defined(__UEFI__)
#include <sys_uefi.h>

extern u64 __uefi_monotonic_ns(void);
u64 clock_monotonic_ns(void)
{
    return __uefi_monotonic_ns();
}

#elif defined(_WIN32)
#include <windows.h>

u64 clock_monotonic_ns(void)
{
    static volatile LONGLONG freq = 0;
    LONGLONG f = freq;
    if (f == 0)
    {
        LARGE_INTEGER q;
        QueryPerformanceFrequency(&q);
        f = q.QuadPart ? q.QuadPart : 1;
        freq = f;
    }
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    u64 secs = (u64)c.QuadPart / (u64)f;
    u64 rem  = (u64)c.QuadPart % (u64)f;
    return secs * 1000000000ull + (rem * 1000000000ull) / (u64)f;
}

#elif defined(XXC_RAWSYS)
#include <xxc_sys.h>

u64 clock_monotonic_ns(void)
{
    xxc_timespec_t ts;
    /* GCOV_EXCL_START */
    if (sys_clock_gettime(XXC_CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    /* GCOV_EXCL_STOP */
    return (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
}
#endif

u64 clock_monotonic_ms(void)
{
    return clock_monotonic_ns() / 1000000ull;
}

u64 clock_realtime_ms(void)
{
#if defined(__UEFI__)
    return __uefi_unix_time() * 1000ull;
#elif defined(_WIN32)
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    u64 total_100ns = ((u64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (total_100ns - 116444736000000000ull) / 10000ull;
#elif defined(XXC_RAWSYS)
    xxc_timespec_t ts;
    if (sys_clock_gettime(0, &ts) != 0) return 0;
    return (u64)ts.tv_sec * 1000ull + (u64)ts.tv_nsec / 1000000ull;
#else
    return 0;
#endif
}