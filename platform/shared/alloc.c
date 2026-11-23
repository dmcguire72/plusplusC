// ++C C Runtime Library (libminicrt) | Platform (Linux v2.4.0, FreeBSD v1.0-RELEASE, NT_x86 4.0+, NT_x64 5.2+)
// Copyright 2026 Daniel McGuire
// Licensed under the MIT License

#include "../sys_alloc.h"
#include <atomic.h>
#include <crt_lock.h>
#include <mem.h>

#define XXC_PAGE_SHIFT     16
#define XXC_PAGE_SIZE      ((size_t)1 << XXC_PAGE_SHIFT)
#define XXC_PAGE_MASK      (~(uintptr_t)(XXC_PAGE_SIZE - 1))
#define XXC_PAGE_HDR       128
#define XXC_CHUNK_PAGES    16
#define XXC_SMALL_MAX      8192
#define XXC_NUM_CLASSES    32
#define XXC_SPARE_MAX      2
#define XXC_POOL_KEEP      64
#define XXC_LARGE_HDR      64
#define XXC_LARGE_SLOTS    16
#define XXC_LARGE_CACHE_MAX_ITEM ((size_t)2 << 20)
#define XXC_MAX_REQUEST    (((size_t)-1) >> 2)

#define XXC_MAGIC_SMALL    0x58414C53u
#define XXC_MAGIC_LARGE    0x58414C4Cu

typedef struct xxc_block { struct xxc_block *next; } xxc_block;

typedef struct xxc_page {
    u32                 magic;
    u8                  cls;
    u8                  full;
    u16                 pad;
    struct xxc_heap    *heap;
    xxc_block          *free;
    struct xxc_page    *next, *prev;
    u32                 block_size;
    u32                 capacity;
    u32                 fresh;
    u32                 used;
} xxc_page;

typedef struct xxc_large {
    u32                 magic;
    u32                 pad;
    size_t              map_size;
    size_t              reserve_size;
} xxc_large;

struct xxc_arena;

typedef struct xxc_heap {
    struct xxc_arena   *arena;
    xxc_page           *pages[XXC_NUM_CLASSES];
    xxc_page           *spare;
    u32                 nspare;
    struct xxc_heap    *next_idle;
    _Alignas(XXC_CACHELINE) atomic_ptr_t delayed;
    atomic_u32_t        idle;
} xxc_heap;

typedef struct xxc_arena {
    crt_lock_t          lock;
    xxc_heap           *idle_heaps;
    xxc_page           *page_pool;
    size_t              pool_count;
    char               *chunk_cur, *chunk_end;
    char               *heap_cur,  *heap_end;
    struct { char *base; size_t size; size_t reserve; } large_cache[XXC_LARGE_SLOTS];
} xxc_arena;

_Static_assert(sizeof(xxc_page) <= XXC_PAGE_HDR, "page header too large");
_Static_assert(sizeof(xxc_large) <= XXC_LARGE_HDR, "large header too large");

#if defined(_MSC_VER) && !defined(__clang__)
    #include <intrin.h>
    #define XXC_TRAP() __fastfail(7)
#else
    #define XXC_TRAP() __builtin_trap()
#endif

static inline unsigned xxc_log2_u32(unsigned v)
{
#if defined(_MSC_VER) && !defined(__clang__)
    unsigned long i;
    _BitScanReverse(&i, v);
    return (unsigned)i;
#else
    return 31u - (unsigned)__builtin_clz(v);
#endif
}

#if defined(_WIN32)
#include <windows.h>

static void os_write_err(const char *msg, size_t n)
{
    DWORD w;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), msg, (DWORD)n, &w, nullptr);
}

static inline char *os_map(size_t size)
{
    return (char *)VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

static inline char *os_map_aligned(size_t size)
{
    return os_map(size);
}

static inline void os_unmap(void *p, size_t size)
{
    (void)size;
    VirtualFree(p, 0, MEM_RELEASE);
}

static inline void os_discard(void *p, size_t size)
{
    VirtualAlloc(p, size, MEM_RESET, PAGE_READWRITE);
}

#define XXC_LARGE_HEADROOM_MIN ((size_t)1 << 20)

static char *os_map_large(size_t need, size_t *reserve_out)
{
#if defined(_WIN64)
    if (need >= XXC_LARGE_HEADROOM_MIN)
    {
        size_t rsv = need * 2;
        char *b = (char *)VirtualAlloc(nullptr, rsv, MEM_RESERVE, PAGE_READWRITE);
        if (b != nullptr)
        {
            if (VirtualAlloc(b, need, MEM_COMMIT, PAGE_READWRITE) != nullptr)
            {
                *reserve_out = rsv;
                return b;
            }
            VirtualFree(b, 0, MEM_RELEASE);
            return nullptr;
        }
    }
#endif
    char *b = os_map(need);
    if (b != nullptr) *reserve_out = need;
    return b;
}

static bool os_large_grow(xxc_large *L, size_t need)
{
    if (need <= L->map_size) return true;
    if (need > L->reserve_size) return false;
    if (VirtualAlloc((char *)L + L->map_size, need - L->map_size, MEM_COMMIT, PAGE_READWRITE) == nullptr)
        return false;
    L->map_size = need;
    return true;
}

#define XXC_TLS_SLOT_NONE  0xFFFFFFFFu
#define XXC_TLS_SLOT_SLOW  0x80000000u
#if defined(_WIN64)
    #define XXC_TEB_TLSSLOTS 0x1480
#else
    #define XXC_TEB_TLSSLOTS 0x0E10
#endif

static atomic_u32_t g_tls_slot = ATOMIC_INIT(XXC_TLS_SLOT_NONE);

static inline xxc_heap *t_heap_get(void)
{
    u32 s = atomic_u32_load_explicit(&g_tls_slot, ATOMIC_ACQUIRE);
    if (XXC_UNLIKELY(s == XXC_TLS_SLOT_NONE)) return nullptr;
    if (XXC_LIKELY(!(s & XXC_TLS_SLOT_SLOW)))
        return ((xxc_heap **)((char *)NtCurrentTeb() + XXC_TEB_TLSSLOTS))[s];
    DWORD err = GetLastError();
    xxc_heap *h = (xxc_heap *)TlsGetValue(s & ~XXC_TLS_SLOT_SLOW);
    SetLastError(err);
    return h;
}

static bool t_heap_slot_init_locked(void)
{
    if (atomic_u32_load(&g_tls_slot) != XXC_TLS_SLOT_NONE) return true;
    DWORD s = TlsAlloc();
    if (s == TLS_OUT_OF_INDEXES) return false;

    u32 v = s;
    if (s < 64)
    {
        TlsSetValue(s, (LPVOID)(uintptr_t)0x58584331u);
        if (((uintptr_t *)((char *)NtCurrentTeb() + XXC_TEB_TLSSLOTS))[s] != (uintptr_t)0x58584331u)
            v = s | XXC_TLS_SLOT_SLOW;
        TlsSetValue(s, nullptr);
    }
    else
        v = s | XXC_TLS_SLOT_SLOW;
    atomic_u32_store_explicit(&g_tls_slot, v, ATOMIC_RELEASE);
    return true;
}

static inline void t_heap_set(xxc_heap *h)
{
    TlsSetValue(atomic_u32_load(&g_tls_slot) & ~XXC_TLS_SLOT_SLOW, h);
}

#else

static __thread xxc_heap *t_heap;

static inline xxc_heap *t_heap_get(void) { return t_heap; }
static inline void      t_heap_set(xxc_heap *h) { t_heap = h; }
static inline bool      t_heap_slot_init_locked(void) { return true; }

static void os_write_err(const char *msg, size_t n)
{
    sys_write(2, msg, n);
}

static inline char *os_map(size_t size)
{
    return (char *)sys_mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
}

static inline void os_unmap(void *p, size_t size)   { sys_munmap(p, size); }
static inline void os_discard(void *p, size_t size) { sys_madvise(p, size, MADV_DONTNEED); }

static char *os_map_aligned(size_t size)
{
    size_t total = size + XXC_PAGE_SIZE;
    if (total < size) return nullptr;
    char *raw = os_map(total);
    if (raw == nullptr) return nullptr;

    uintptr_t a = ((uintptr_t)raw + XXC_PAGE_SIZE - 1) & XXC_PAGE_MASK;
    size_t head = a - (uintptr_t)raw;
    size_t tail = total - head - size;
    if (head) os_unmap(raw, head);
    if (tail) os_unmap((char *)a + size, tail);
    return (char *)a;
}

static inline char *os_map_large(size_t need, size_t *reserve_out)
{
    *reserve_out = need;
    return os_map_aligned(need);
}

static inline bool os_large_grow(xxc_large *L, size_t need)
{
    (void)L; (void)need;
    return false;
}
#endif

#if defined(_WIN32)
static atomic_ptr_t g_arena_ptr = ATOMIC_INIT(nullptr);

static xxc_arena *arena_get(void)
{
    xxc_arena *a = (xxc_arena *)atomic_ptr_load_explicit(&g_arena_ptr, ATOMIC_ACQUIRE);
    if (XXC_LIKELY(a != nullptr)) return a;

    _Static_assert(sizeof(xxc_arena) <= XXC_PAGE_SIZE, "arena must fit in one mapping");
    xxc_arena *fresh = (xxc_arena *)os_map(XXC_PAGE_SIZE);
    if (fresh == nullptr) return nullptr;

    void *expected = nullptr;
    if (atomic_ptr_cas(&g_arena_ptr, &expected, fresh))
        return fresh;
    os_unmap(fresh, XXC_PAGE_SIZE);
    return (xxc_arena *)expected;
}
#else
static xxc_arena g_arena_storage;
static inline xxc_arena *arena_get(void) { return &g_arena_storage; }
#endif

static XXC_NORETURN void fatal(const char *msg)
{
    size_t n = 0;
    while (msg[n]) n++;
    os_write_err(msg, n);
    XXC_TRAP();
}

static inline size_t align_up_sz(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }
static inline xxc_page *page_of(const void *p) { return (xxc_page *)((uintptr_t)p & XXC_PAGE_MASK); }

static inline unsigned class_of(size_t size)
{
    if (size <= 128)
        return (unsigned)((size + 15) >> 4) - 1;
    unsigned s = (unsigned)size - 1;
    unsigned m = xxc_log2_u32(s);
    return 8 + (m - 7) * 4 + ((s >> (m - 2)) & 3);
}

static inline u32 class_size(unsigned cls)
{
    if (cls < 8)
        return (cls + 1) * 16;
    unsigned k = cls - 8, m = 7 + k / 4, j = k % 4;
    return (1u << m) + ((1u << m) / 4) * (j + 1);
}

static xxc_page *pool_get_locked(xxc_arena *a)
{
    xxc_page *p = a->page_pool;
    if (p != nullptr)
    {
        a->page_pool = p->next;
        a->pool_count--;
        return p;
    }
    if (a->chunk_cur == a->chunk_end)
    {
        char *c = os_map_aligned(XXC_CHUNK_PAGES * XXC_PAGE_SIZE);
        if (c == nullptr) return nullptr;
        a->chunk_cur = c;
        a->chunk_end = c + XXC_CHUNK_PAGES * XXC_PAGE_SIZE;
    }
    p = (xxc_page *)a->chunk_cur;
    a->chunk_cur += XXC_PAGE_SIZE;
    return p;
}

static void pool_put_locked(xxc_arena *a, xxc_page *p)
{
    if (a->pool_count >= XXC_POOL_KEEP)
        os_discard(p, XXC_PAGE_SIZE);
    p->next = a->page_pool;
    a->page_pool = p;
    a->pool_count++;
}

static xxc_heap *heap_struct_new_locked(xxc_arena *a)
{
    size_t sz = align_up_sz(sizeof(xxc_heap), XXC_CACHELINE);
    if ((size_t)(a->heap_end - a->heap_cur) < sz)
    {
        char *m = os_map(XXC_PAGE_SIZE);
        if (m == nullptr) return nullptr;
        a->heap_cur = m;
        a->heap_end = m + XXC_PAGE_SIZE;
    }
    xxc_heap *h = (xxc_heap *)a->heap_cur;
    a->heap_cur += sz;
    h->arena = a;
    return h;
}

static inline void list_unlink(xxc_heap *h, xxc_page *pg)
{
    if (pg->prev != nullptr) pg->prev->next = pg->next; else h->pages[pg->cls] = pg->next;
    if (pg->next != nullptr) pg->next->prev = pg->prev;
    pg->next = pg->prev = nullptr;
}

static inline void list_push(xxc_heap *h, xxc_page *pg)
{
    pg->prev = nullptr;
    pg->next = h->pages[pg->cls];
    if (pg->next != nullptr) pg->next->prev = pg;
    h->pages[pg->cls] = pg;
}

static void page_maybe_retire(xxc_heap *h, xxc_page *pg, bool locked)
{
    if (h->pages[pg->cls] == pg && pg->next == nullptr)
        return;
    list_unlink(h, pg);

    xxc_arena *a = h->arena;
    if (locked) { pool_put_locked(a, pg); return; }
    if (h->nspare < XXC_SPARE_MAX)
    {
        pg->next = h->spare;
        h->spare = pg;
        h->nspare++;
        return;
    }
    __crt_lock_acquire(&a->lock);
    pool_put_locked(a, pg);
    __crt_lock_release(&a->lock);
}

static inline void page_free_local(xxc_heap *h, xxc_page *pg, xxc_block *b, bool locked)
{
    b->next = pg->free;
    pg->free = b;
    pg->used--;
    if (XXC_UNLIKELY(pg->full))
    {
        pg->full = 0;
        list_push(h, pg);
    }
    if (XXC_UNLIKELY(pg->used == 0))
        page_maybe_retire(h, pg, locked);
}

static void heap_drain_delayed(xxc_heap *h, bool locked)
{
    if (!atomic_ptr_load_explicit(&h->delayed, ATOMIC_RELAXED))
        return;
    xxc_block *b = (xxc_block *)atomic_ptr_exchange_explicit(&h->delayed, nullptr, ATOMIC_ACQUIRE);
    while (b)
    {
        xxc_block *next = b->next;
        page_free_local(h, page_of(b), b, locked);
        b = next;
    }
}

static xxc_page *page_new(xxc_heap *h, unsigned cls)
{
    xxc_page *pg = h->spare;
    if (pg != nullptr)
    {
        h->spare = pg->next;
        h->nspare--;
    }
    else
    {
        xxc_arena *a = h->arena;
        __crt_lock_acquire(&a->lock);
        pg = pool_get_locked(a);
        __crt_lock_release(&a->lock);
        if (pg == nullptr) return nullptr;
    }
    pg->magic      = XXC_MAGIC_SMALL;
    pg->heap       = h;
    pg->cls        = (u8)cls;
    pg->full       = 0;
    pg->block_size = class_size(cls);
    pg->capacity   = (u32)((XXC_PAGE_SIZE - XXC_PAGE_HDR) / pg->block_size);
    pg->fresh      = 0;
    pg->used       = 0;
    pg->free       = nullptr;
    list_push(h, pg);
    return pg;
}

static XXC_NOINLINE xxc_heap *heap_acquire(void)
{
    xxc_arena *a = arena_get();
    if (a == nullptr) return nullptr;

    __crt_lock_acquire(&a->lock);
    xxc_heap *h = nullptr;
    if (t_heap_slot_init_locked())
    {
        h = a->idle_heaps;
        if (h != nullptr)
            a->idle_heaps = h->next_idle;
        else
            h = heap_struct_new_locked(a);
        if (h != nullptr)
            atomic_u32_store(&h->idle, 0);
    }
    __crt_lock_release(&a->lock);

    if (h != nullptr)
        t_heap_set(h);
    return h;
}

#if !defined(_WIN32)
void __xxc_heap_fork_prepare(void)
{
    __crt_lock_acquire(&arena_get()->lock);
}

void __xxc_heap_fork_release(void)
{
    __crt_lock_release(&arena_get()->lock);
}
#endif

void __xxc_heap_thread_exit(void)
{
    xxc_heap *h = t_heap_get();
    if (h == nullptr) return;

    xxc_arena *a = h->arena;
    heap_drain_delayed(h, false);

    for (unsigned c = 0; c < XXC_NUM_CLASSES; c++)
    {
        xxc_page *pg = h->pages[c];
        while (pg != nullptr)
        {
            xxc_page *next = pg->next;
            if (pg->used == 0)
            {
                list_unlink(h, pg);
                __crt_lock_acquire(&a->lock);
                pool_put_locked(a, pg);
                __crt_lock_release(&a->lock);
            }
            pg = next;
        }
    }

    __crt_lock_acquire(&a->lock);
    while (h->spare)
    {
        xxc_page *pg = h->spare;
        h->spare = pg->next;
        pool_put_locked(a, pg);
    }
    h->nspare = 0;
    h->next_idle = a->idle_heaps;
    a->idle_heaps = h;
    atomic_u32_store(&h->idle, 1);
    __crt_lock_release(&a->lock);

    t_heap_set(nullptr);
}

static XXC_NOINLINE void *small_alloc_slow(xxc_heap *h, unsigned cls)
{
    heap_drain_delayed(h, false);

    for (;;)
    {
        xxc_page *pg = h->pages[cls];
        if (pg == nullptr)
        {
            pg = page_new(h, cls);
            if (pg == nullptr) return nullptr;
        }
        xxc_block *b = pg->free;
        if (b)
        {
            pg->free = b->next;
            pg->used++;
            return b;
        }
        if (pg->fresh < pg->capacity)
        {
            b = (xxc_block *)((char *)pg + XXC_PAGE_HDR + (size_t)pg->fresh * pg->block_size);
            pg->fresh++;
            pg->used++;
            return b;
        }
        list_unlink(h, pg);
        pg->full = 1;
    }
}

static inline void *small_alloc(size_t size)
{
    xxc_heap *h = t_heap_get();
    if (XXC_UNLIKELY(h == nullptr))
    {
        h = heap_acquire();
        if (h == nullptr) return nullptr;
    }
    unsigned cls = class_of(size);
    xxc_page *pg = h->pages[cls];
    if (XXC_LIKELY(pg != nullptr))
    {
        xxc_block *b = pg->free;
        if (XXC_LIKELY(b != nullptr))
        {
            pg->free = b->next;
            pg->used++;
            return b;
        }
    }
    return small_alloc_slow(h, cls);
}

static void free_remote(xxc_heap *h, xxc_block *b)
{
    if (XXC_UNLIKELY(atomic_u32_load_explicit(&h->idle, ATOMIC_ACQUIRE)))
    {
        xxc_arena *a = h->arena;
        __crt_lock_acquire(&a->lock);
        if (atomic_u32_load(&h->idle))
        {
            heap_drain_delayed(h, true);
            page_free_local(h, page_of(b), b, true);
            __crt_lock_release(&a->lock);
            return;
        }
        __crt_lock_release(&a->lock);
    }

    void *old = atomic_ptr_load_explicit(&h->delayed, ATOMIC_RELAXED);
    do {
        b->next = (xxc_block *)old;
    } while (!atomic_ptr_cas_weak_explicit(&h->delayed, &old, b, ATOMIC_RELEASE, ATOMIC_RELAXED));
}

static void *large_alloc(size_t size, bool *zeroed)
{
    if (size > XXC_MAX_REQUEST) return nullptr;
    size_t need = align_up_sz(size + XXC_LARGE_HDR, XXC_PAGE_SIZE);
    xxc_arena *a = arena_get();

    char *base = nullptr;
    size_t msize = 0, rsize = 0;

    if (a != nullptr)
    {
        __crt_lock_acquire(&a->lock);
        int best = -1;
        for (int i = 0; i < XXC_LARGE_SLOTS; i++)
        {
            size_t s = a->large_cache[i].size;
            if (a->large_cache[i].base && s >= need && s <= need * 2 && (best < 0 || s < a->large_cache[best].size))
                best = i;
        }
        if (best >= 0)
        {
            base = a->large_cache[best].base;
            msize = a->large_cache[best].size;
            rsize = a->large_cache[best].reserve;
            a->large_cache[best].base = nullptr;
        }
        __crt_lock_release(&a->lock);
    }

    if (base != nullptr)
    {
        if (zeroed) *zeroed = false;
    }
    else
    {
        base = os_map_large(need, &rsize);
        if (base == nullptr) return nullptr;
        msize = need;
        if (zeroed) *zeroed = true;
    }

    xxc_large *L = (xxc_large *)base;
    L->magic = XXC_MAGIC_LARGE;
    L->map_size = msize;
    L->reserve_size = rsize;
    return base + XXC_LARGE_HDR;
}

static void large_free(xxc_large *L)
{
    size_t msize = L->map_size, rsize = L->reserve_size;
    L->magic = 0;

    if (msize <= XXC_LARGE_CACHE_MAX_ITEM)
    {
        xxc_arena *a = arena_get();
        if (a != nullptr)
        {
            __crt_lock_acquire(&a->lock);
            for (int i = 0; i < XXC_LARGE_SLOTS; i++)
            {
                if (a->large_cache[i].base == nullptr)
                {
                    a->large_cache[i].base = (char *)L;
                    a->large_cache[i].size = msize;
                    a->large_cache[i].reserve = rsize;
                    __crt_lock_release(&a->lock);
                    return;
                }
            }
            __crt_lock_release(&a->lock);
        }
    }
    os_unmap(L, rsize);
}

static inline void *alloc_impl(size_t size, bool *zeroed)
{
    if (zeroed) *zeroed = false;
    if (XXC_UNLIKELY(size == 0)) size = 1;
    if (XXC_LIKELY(size <= XXC_SMALL_MAX))
        return small_alloc(size);
    return large_alloc(size, zeroed);
}

void *__xxc_malloc(size_t size)
{
    return alloc_impl(size, nullptr);
}

void *__xxc_calloc(size_t count, size_t size)
{
    size_t total = count * size;
    if (count != 0 && total / count != size) return nullptr;
    if (total == 0) total = 1; 
        
    bool zeroed;
    void *p = alloc_impl(total, &zeroed);
    if (p && !zeroed)
        memset(p, 0, total);
    return p;
}

void __xxc_free(void *ptr)
{
    if (ptr == nullptr) return;
    xxc_page *pg = page_of(ptr);
    if (XXC_LIKELY(pg->magic == XXC_MAGIC_SMALL))
    {
        xxc_heap *h = pg->heap;
        if (XXC_LIKELY(h == t_heap_get()))
            page_free_local(h, pg, (xxc_block *)ptr, false);
        else
            free_remote(h, (xxc_block *)ptr);
        return;
    }
    if (pg->magic == XXC_MAGIC_LARGE)
    {
        large_free((xxc_large *)pg);
        return;
    }
    fatal("free(): invalid pointer\n");
}

size_t __xxc_usable_size(void *ptr)
{
    if (ptr == nullptr) return 0;
    xxc_page *pg = page_of(ptr);
    if (pg->magic == XXC_MAGIC_SMALL) return pg->block_size;
    if (pg->magic == XXC_MAGIC_LARGE) return ((xxc_large *)pg)->map_size - XXC_LARGE_HDR;
    fatal("malloc_usable_size(): invalid pointer\n");
}

void *__xxc_realloc(void *ptr, size_t size)
{
    if (ptr == nullptr) return __xxc_malloc(size);
    if (size == 0)
    {
        __xxc_free(ptr);
        return nullptr;
    }

    size_t usable = __xxc_usable_size(ptr);
    if (size <= usable && size >= usable / 2)
        return ptr;

    if (size > usable && size <= XXC_MAX_REQUEST)
    {
        xxc_page *pg = page_of(ptr);
        if (pg->magic == XXC_MAGIC_LARGE &&
            os_large_grow((xxc_large *)pg, align_up_sz(size + XXC_LARGE_HDR, XXC_PAGE_SIZE)))
            return ptr;
    }

    void *n = __xxc_malloc(size);
    if (n == nullptr) return nullptr;
    memcpy(n, ptr, usable < size ? usable : size);
    __xxc_free(ptr);
    return n;
}
