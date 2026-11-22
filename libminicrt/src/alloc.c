// ++C C Runtime Library (libminicrt)
// Copyright 2026 Daniel McGuire
// Licensed under the MIT License

#include <alloc.h>
#include <def.h>
#include <mem.h>

#ifdef __UEFI__

#include <sys_uefi.h>

void *malloc(size_t size)
{
    return __uefi_malloc(size);
}

void *calloc(size_t n, size_t size)
{
    return __uefi_calloc(n, size);
}

void* realloc(void* p, size_t size)
{
    return __uefi_realloc(p, size);
}

void free(void *p) 
{
    __uefi_free(p);
}

size_t malloc_usable_size(void *p) 
{
    return __uefi_usable_size(p);
}

#elif defined(_WIN32) || defined(XXC_RAWSYS)

#if !defined(_WIN32)
#include <xxc_sys.h>
#endif
#include <sys_alloc.h>

void *malloc(size_t size) 
{
    return __xxc_malloc(size);
}

void *calloc(size_t count, size_t size) 
{
    return __xxc_calloc(count, size);
}

void *realloc(void *block, size_t size) 
{
    return __xxc_realloc(block, size);
}

void free(void *block)
{
    __xxc_free(block);
}

size_t malloc_usable_size(void *block)
{
    return __xxc_usable_size(block);
}

#endif /* __UEFI__ / _WIN32 / XXC_RAWSYS */