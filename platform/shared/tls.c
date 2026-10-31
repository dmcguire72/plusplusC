// ++C C Runtime Library (libminicrt) | Platform (Linux/FreeBSD)
// Copyright 2026 Daniel McGuire
// Licensed under the MIT License

#include "../sys_thread.h"
#include <mem.h>
XXC_NO_SSP_BEGIN


#define AT_NULL   0
#define AT_PHDR   3
#define AT_PHENT  4
#define AT_PHNUM  5
#define AT_BASE   7
#if defined(__FreeBSD__)
#define AT_RANDOM 16
#else
#define AT_RANDOM 25
#endif
#define PT_PHDR   6
#define PT_TLS    7

#if defined(__x86_64__) || defined(__aarch64__)
typedef struct { u32 p_type, p_flags; u64 p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align; } xxc_phdr_t;
#else
typedef struct { u32 p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align; } xxc_phdr_t;
#endif

#define PT_DYNAMIC 2
#define DT_DEBUG   21

#if defined(__x86_64__) || defined(__aarch64__)
typedef struct { unsigned char e_ident[16]; u16 e_type, e_machine; u32 e_version; u64 e_entry, e_phoff, e_shoff; u32 e_flags; u16 e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx; } xxc_ehdr_t;
typedef struct { i64 d_tag; u64 d_val; } xxc_dyn_t;
typedef u64 xxc_addr_t;
#else
typedef struct { unsigned char e_ident[16]; u16 e_type, e_machine; u32 e_version, e_entry, e_phoff, e_shoff, e_flags; u16 e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx; } xxc_ehdr_t;
typedef struct { i32 d_tag; u32 d_val; } xxc_dyn_t;
typedef u32 xxc_addr_t;
#endif

typedef struct xxc_link_map {
    xxc_addr_t           l_addr;
    char                *l_name;
    xxc_dyn_t           *l_ld;
    struct xxc_link_map *l_next, *l_prev;
} xxc_link_map_t;

typedef struct { int r_version; xxc_link_map_t *r_map; } xxc_r_debug_t;

#if defined(__x86_64__) || defined(__i386__)
#define XXC_TCB_SIZE 64
#define XXC_TCB_CANARY_IDX 5
#else
#define XXC_TCB_SIZE 16
#endif

#define PT_LOAD    1
#define DT_NULL    0
#define DT_NEEDED  1
#define DT_PLTRELSZ 2
#define DT_STRTAB  5
#define DT_RELA    7
#define DT_RELASZ  8
#define DT_FINI    13
#define DT_SONAME  14
#define DT_REL     17
#define DT_RELSZ   18
#define DT_JMPREL  23
#define DT_INIT_ARRAY      25
#define DT_FINI_ARRAY      26
#define DT_INIT_ARRAYSZ    27
#define DT_FINI_ARRAYSZ    28
#define DT_PREINIT_ARRAY   32
#define DT_PREINIT_ARRAYSZ 33
#define DT_RELRSZ  35
#define DT_RELR    36

#if defined(__x86_64__)
#define XXC_R_RELATIVE  8
#define XXC_R_IRELATIVE 37
#elif defined(__aarch64__)
#define XXC_R_RELATIVE  1027
#define XXC_R_IRELATIVE 1032
#elif defined(__i386__)
#define XXC_R_RELATIVE  8
#define XXC_R_IRELATIVE 42
#endif

extern uintptr_t __stack_chk_guard;

__thread struct xxc_thread *__xxc_self;
struct xxc_thread           __xxc_main_thread;

char **__xxc_environ;
int    __xxc_dynamic_linked;

static struct {
    const char *image;
    size_t      filesz;
    size_t      memsz;
    size_t      align;
    size_t      off;
} g_tls = { nullptr, 0, 0, 1, 0 };

#if defined(__i386__)
static u32 g_gs_entry;
#endif

static inline uintptr_t align_up_u(uintptr_t v, uintptr_t a)   { return (v + a - 1) & ~(a - 1); }
static inline uintptr_t align_down_u(uintptr_t v, uintptr_t a) { return v & ~(a - 1); }

static XXC_NO_SSP uintptr_t phdr_bias(const xxc_phdr_t *ph, size_t phnum)
{
    for (size_t i = 0; i < phnum; i++)
        if (ph[i].p_type == PT_PHDR) return (uintptr_t)ph - (uintptr_t)ph[i].p_vaddr;
    for (size_t i = 0; i < phnum; i++)
        if (ph[i].p_type == PT_LOAD && ph[i].p_offset == 0)
            return ((uintptr_t)ph - sizeof(xxc_ehdr_t)) - (uintptr_t)ph[i].p_vaddr;
    return 0;
}

static XXC_NO_SSP void tls_scan(int argc, char **argv)
{
    char **envp = argv + argc + 1;
    while (*envp) envp++;
    const unsigned long *av = (const unsigned long *)(envp + 1);

    const xxc_phdr_t *ph = nullptr;
    size_t phnum = 0;
    for (; av[0] != AT_NULL; av += 2)
    {
        if (av[0] == AT_PHDR)  ph = (const xxc_phdr_t *)av[1];
        if (av[0] == AT_PHNUM) phnum = (size_t)av[1];
    }
    if (ph == nullptr) return;

    uintptr_t bias = phdr_bias(ph, phnum);

    for (size_t i = 0; i < phnum; i++)
    {
        if (ph[i].p_type != PT_TLS) continue;
        g_tls.image  = (const char *)(bias + (uintptr_t)ph[i].p_vaddr);
        g_tls.filesz = (size_t)ph[i].p_filesz;
        g_tls.memsz  = (size_t)ph[i].p_memsz;
        g_tls.align  = ph[i].p_align ? (size_t)ph[i].p_align : 1;
        break;
    }

#if defined(__aarch64__)
    g_tls.off = XXC_TCB_SIZE + ((-(uintptr_t)XXC_TCB_SIZE + (uintptr_t)g_tls.image) & (g_tls.align - 1));
#else
    g_tls.off = g_tls.memsz + ((-g_tls.memsz - (uintptr_t)g_tls.image) & (g_tls.align - 1));
#endif
}

size_t __xxc_tls_area_size(void)
{
    size_t a = g_tls.align < 16 ? 16 : g_tls.align;
#if defined(__aarch64__)
    return g_tls.off + g_tls.memsz + a + XXC_TCB_SIZE;
#else
    return g_tls.off + a + XXC_TCB_SIZE * 2;
#endif
}

XXC_NO_SSP char *__xxc_tls_setup(char *region_end, void **tp_out)
{
    uintptr_t a = g_tls.align < 16 ? 16 : g_tls.align;
    char *tp, *block, *lowest;

#if defined(__aarch64__)
    tp    = (char *)align_down_u((uintptr_t)region_end - (g_tls.off + g_tls.memsz), a);
    block = tp + g_tls.off;
    lowest = tp;
#else
    tp    = (char *)align_down_u((uintptr_t)region_end - XXC_TCB_SIZE, a);
    block = tp - g_tls.off;
    lowest = g_tls.memsz ? block : tp;
    *(void **)tp = tp;
#if defined(__x86_64__) || defined(__i386__)
    ((uintptr_t *)tp)[XXC_TCB_CANARY_IDX] = __stack_chk_guard;
#endif
#endif

    if (g_tls.memsz)
    {
        if (g_tls.filesz)
            memcpy(block, g_tls.image, g_tls.filesz);
        memset(block + g_tls.filesz, 0, g_tls.memsz - g_tls.filesz);
    }

    *tp_out = tp;
    return lowest;
}

static XXC_NO_SSP void set_thread_pointer(void *tp)
{
#if defined(__x86_64__) && defined(__FreeBSD__)
    unsigned long base = (unsigned long)tp;
    (void)syscall(XXC_SYS_sysarch, 129L /* AMD64_SET_FSBASE */, (long)&base);
#elif defined(__x86_64__)
    (void)syscall(__NR_arch_prctl, 0x1002L, (long)tp);
#elif defined(__i386__)
    xxc_user_desc_t d;
    d.entry_number = (u32)-1;
    d.base_addr    = (u32)(uintptr_t)tp;
    d.limit        = 0xfffff;
    d.flags        = 0x51;
    (void)syscall(__NR_set_thread_area, (long)&d);
    g_gs_entry = d.entry_number;
    unsigned sel = (d.entry_number << 3) | 3;
    __asm__ volatile("movw %w0, %%gs" :: "r"(sel) : "memory");
#elif defined(__aarch64__)
    __asm__ volatile("msr tpidr_el0, %0" :: "r"(tp) : "memory");
#endif
}

#if defined(__i386__)
void __xxc_fill_user_desc(struct xxc_thread *t, void *tp)
{
    t->user_desc.entry_number = g_gs_entry;
    t->user_desc.base_addr    = (u32)(uintptr_t)tp;
    t->user_desc.limit        = 0xfffff;
    t->user_desc.flags        = 0x51;
}
#endif

static int has_interp(int argc, char **argv)
{
    char **envp = argv + argc + 1;
    while (*envp) envp++;
    for (const unsigned long *av = (const unsigned long *)(envp + 1); av[0] != AT_NULL; av += 2)
        if (av[0] == AT_BASE) return av[1] != 0;
    return 0;
}

static void *get_thread_pointer(void)
{
    void *tp;
#if defined(__x86_64__)
    __asm__ volatile("movq %%fs:0, %0" : "=r"(tp));
#elif defined(__i386__)
    __asm__ volatile("movl %%gs:0, %0" : "=r"(tp));
#elif defined(__aarch64__)
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(tp));
#endif
    return tp;
}

static void tls_accum(const xxc_phdr_t *ph, size_t phnum, size_t *total, size_t *maxal)
{
    for (size_t i = 0; i < phnum; i++)
    {
        if (ph[i].p_type != PT_TLS) continue;
        size_t al = ph[i].p_align ? (size_t)ph[i].p_align : 1;
        *total += (size_t)align_up_u((uintptr_t)ph[i].p_memsz, al) + al;
        if (al > *maxal) *maxal = al;
    }
}

static int tls_snapshot_dynamic(int argc, char **argv)
{
    char **envp = argv + argc + 1;
    while (*envp) envp++;
    const unsigned long *av = (const unsigned long *)(envp + 1);

    const xxc_phdr_t *ph = nullptr;
    size_t phnum = 0;
    for (; av[0] != AT_NULL; av += 2)
    {
        if (av[0] == AT_PHDR)  ph = (const xxc_phdr_t *)av[1];
        if (av[0] == AT_PHNUM) phnum = (size_t)av[1];
    }
    if (ph == nullptr) return 0;

    uintptr_t bias = phdr_bias(ph, phnum);

    xxc_r_debug_t *rd = nullptr;
    for (size_t i = 0; i < phnum && !rd; i++)
    {
        if (ph[i].p_type != PT_DYNAMIC) continue;
        for (const xxc_dyn_t *d = (const xxc_dyn_t *)(bias + (uintptr_t)ph[i].p_vaddr); d->d_tag != 0; d++)
            if (d->d_tag == DT_DEBUG) { rd = (xxc_r_debug_t *)(uintptr_t)d->d_val; break; }
    }
    if (rd == nullptr || !rd->r_map) return 0;

    size_t total = 0, maxal = 1;

    tls_accum(ph, phnum, &total, &maxal);

    for (const xxc_link_map_t *m = rd->r_map->l_next; m; m = m->l_next)
    {
        const xxc_ehdr_t *eh = (const xxc_ehdr_t *)(uintptr_t)m->l_addr;
        if (eh == nullptr || eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E' ||
            eh->e_ident[2] != 'L'  || eh->e_ident[3] != 'F')
            continue;
        tls_accum((const xxc_phdr_t *)((const char *)eh + eh->e_phoff), eh->e_phnum, &total, &maxal);
    }

    if (!total) return 0;

    size_t a = maxal < 16 ? 16 : maxal;
    size_t n = (size_t)align_up_u(total, a);

    char *snap = (char *)sys_mmap(nullptr, align_up_u(n, 4096), PROT_READ | PROT_WRITE,
                                  MAP_PRIVATE | MAP_ANON, -1, 0);
    if (snap == nullptr) return 0;

    const char *tp = (const char *)get_thread_pointer();
#if defined(__aarch64__)
    memcpy(snap, tp + XXC_TCB_SIZE, n);
    g_tls.off = XXC_TCB_SIZE;
#else
    memcpy(snap, tp - n, n);
    g_tls.off = n;
#endif
    g_tls.image  = snap;
    g_tls.filesz = n;
    g_tls.memsz  = n;
    g_tls.align  = a;
    return 1;
}

size_t __xxc_exe_dyn_array(int which, int argc, char **argv, void ***out)
{
    /* GCOV_EXCL_START */
    *out = nullptr;
#if defined(__linux__)
    static const long tag_arr[3] = { DT_PREINIT_ARRAY,   DT_INIT_ARRAY,   DT_FINI_ARRAY   };
    static const long tag_sz[3]  = { DT_PREINIT_ARRAYSZ, DT_INIT_ARRAYSZ, DT_FINI_ARRAYSZ };

    char **envp = argv + argc + 1;
    while (*envp) envp++;
    const unsigned long *av = (const unsigned long *)(envp + 1);

    const xxc_phdr_t *ph = nullptr;
    size_t phnum = 0;
    for (; av[0] != AT_NULL; av += 2)
    {
        if (av[0] == AT_PHDR)  ph = (const xxc_phdr_t *)av[1];
        if (av[0] == AT_PHNUM) phnum = (size_t)av[1];
    }
    if (ph == nullptr) return 0;

    uintptr_t bias = phdr_bias(ph, phnum);

    for (size_t i = 0; i < phnum; i++)
    {
        if (ph[i].p_type != PT_DYNAMIC) continue;

        void **fns = nullptr;
        size_t bytes = 0;

        for (const xxc_dyn_t *d = (const xxc_dyn_t *)(bias + (uintptr_t)ph[i].p_vaddr); d->d_tag != 0; d++)
        {
            if (d->d_tag == tag_arr[which])
            {
                uintptr_t a = (uintptr_t)d->d_val;
                if (a < bias) a += bias;
                fns = (void **)a;
            }
            if (d->d_tag == tag_sz[which]) bytes = (size_t)d->d_val;
        }

        if (fns == nullptr) return 0;
        *out = fns;
        return bytes / sizeof(void *);
    }
#else
    (void)which; (void)argc; (void)argv;
#endif
    return 0;
    /* GCOV_EXCL_STOP */
}

static XXC_NO_SSP const unsigned long *auxv_of(int argc, char **argv)
{
    char **envp = argv + argc + 1;
    while (*envp) envp++;
    return (const unsigned long *)(envp + 1);
}

static XXC_NO_SSP void ssp_seed(const unsigned long *av)
{
    uintptr_t r = 0;
    for (; av[0] != AT_NULL; av += 2)
        if (av[0] == AT_RANDOM && av[1] != 0)
        {
            r = *(const uintptr_t *)av[1];
            break;
        }
    if (r == 0)
    {
        uintptr_t tmp = 0;
        if (sys_getrandom(&tmp, sizeof(tmp), 1 /* GRND_NONBLOCK */) == (long)sizeof(tmp))
            r = tmp;
    }
    if (r != 0)
        __stack_chk_guard = r & ~(uintptr_t)0xff;
}

#if defined(XXC_R_RELATIVE)
static XXC_NO_SSP void self_relocate(const unsigned long *av)
{
    const xxc_phdr_t *ph = nullptr;
    size_t phnum = 0;
    for (; av[0] != AT_NULL; av += 2)
    {
        if (av[0] == AT_PHDR)  ph = (const xxc_phdr_t *)av[1];
        if (av[0] == AT_PHNUM) phnum = (size_t)av[1];
        if (av[0] == AT_BASE && av[1] != 0) return;
    }
    if (ph == nullptr) return;

    const xxc_phdr_t *dynph = nullptr;
    for (size_t i = 0; i < phnum; i++)
        if (ph[i].p_type == PT_DYNAMIC) dynph = &ph[i];
    if (dynph == nullptr) return;
    uintptr_t bias = phdr_bias(ph, phnum);

    const xxc_dyn_t *d = (const xxc_dyn_t *)(bias + (uintptr_t)dynph->p_vaddr);
    uintptr_t rela = 0, relasz = 0, relr = 0, relrsz = 0, jmprel = 0, pltrelsz = 0;
#if defined(__i386__)
    uintptr_t rel = 0, relsz = 0;
#endif
    for (; d->d_tag != DT_NULL; d++)
    {
        switch (d->d_tag)
        {
        case DT_RELA:      rela     = (uintptr_t)d->d_val; break;
        case DT_RELASZ:    relasz   = (uintptr_t)d->d_val; break;
#if defined(__i386__)
        case DT_REL:       rel      = (uintptr_t)d->d_val; break;
        case DT_RELSZ:     relsz    = (uintptr_t)d->d_val; break;
#endif
        case DT_RELR:      relr     = (uintptr_t)d->d_val; break;
        case DT_RELRSZ:    relrsz   = (uintptr_t)d->d_val; break;
        case DT_JMPREL:    jmprel   = (uintptr_t)d->d_val; break;
        case DT_PLTRELSZ:  pltrelsz = (uintptr_t)d->d_val; break;
        }
    }

#if defined(__i386__)
    for (int pass = 0; pass < 2; pass++)
    {
        uintptr_t tab = pass ? jmprel : rel, sz = pass ? pltrelsz : relsz;
        if (!tab) continue;
        const struct { uintptr_t off, info; } *r = (const void *)(bias + tab);
        for (size_t n = sz / sizeof(*r); n; n--, r++)
        {
            uintptr_t *where = (uintptr_t *)(bias + r->off);
            switch (r->info & 0xff)
            {
            case 0: break;
            case XXC_R_RELATIVE:  *where += bias; break;
            case XXC_R_IRELATIVE: *where = ((uintptr_t (*)(void))(bias + *where))(); break;
            default: goto bad;
            }
        }
    }
#else
    for (int pass = 0; pass < 2; pass++)
    {
        uintptr_t tab = pass ? jmprel : rela, sz = pass ? pltrelsz : relasz;
        if (!tab) continue;
        const struct { uintptr_t off, info; intptr_t addend; } *r = (const void *)(bias + tab);
        for (size_t n = sz / sizeof(*r); n; n--, r++)
        {
            uintptr_t *where = (uintptr_t *)(bias + r->off);
            switch ((u32)r->info)
            {
            case 0: break;
            case XXC_R_RELATIVE:  *where = bias + (uintptr_t)r->addend; break;
            case XXC_R_IRELATIVE: *where = ((uintptr_t (*)(void))(bias + (uintptr_t)r->addend))(); break;
            default: goto bad;
            }
        }
    }
#endif

    if (relr)
    {
        const uintptr_t *e = (const uintptr_t *)(bias + relr);
        uintptr_t *where = nullptr;
        for (size_t n = relrsz / sizeof(*e); n; n--, e++)
        {
            uintptr_t v = *e;
            if (!(v & 1))
            {
                where = (uintptr_t *)(bias + v);
                *where++ += bias;
            }
            else
            {
                for (size_t i = 0; (v >>= 1) != 0; i++)
                    if (v & 1) where[i] += bias;
                where += 8 * sizeof(uintptr_t) - 1;
            }
        }
    }
    return;
bad:
    {
        static const char msg[] = "xxc: unsupported relocation in static-pie image\n";
        sys_write(2, msg, sizeof(msg) - 1);
        sys_exit(127);
    }
}
#else
static XXC_NO_SSP void self_relocate(const unsigned long *av) { (void)av; }
#endif

static int g_boot_argc;
static char **g_boot_argv;

typedef struct {
    const xxc_link_map_t **maps;
    int                   *order;
    unsigned char         *state;
    int                    n, cnt;
} fini_ctx_t;

static uintptr_t dyn_adj(uintptr_t v, uintptr_t base) { return v < base ? v + base : v; }

static const char *dyn_strtab(const xxc_link_map_t *m)
{
    for (const xxc_dyn_t *d = m->l_ld; d && d->d_tag != DT_NULL; d++)
        if (d->d_tag == DT_STRTAB) return (const char *)dyn_adj((uintptr_t)d->d_val, (uintptr_t)m->l_addr);
    return nullptr;
}

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static const char *base_name(const char *p)
{
    const char *b = p;
    for (; *p; p++) if (*p == '/') b = p + 1;
    return b;
}

static int map_has_name(const xxc_link_map_t *m, const char *want)
{
    if (m->l_name && m->l_name[0] && str_eq(base_name(m->l_name), want)) return 1;
    const char *st = dyn_strtab(m);
    if (st)
        for (const xxc_dyn_t *d = m->l_ld; d && d->d_tag != DT_NULL; d++)
            if (d->d_tag == DT_SONAME && str_eq(st + d->d_val, want)) return 1;
    return 0;
}

static void fini_visit(fini_ctx_t *c, int i)
{
    c->state[i] = 1;
    const xxc_link_map_t *m = c->maps[i];
    const char *st = dyn_strtab(m);
    if (st)
        for (const xxc_dyn_t *d = m->l_ld; d && d->d_tag != DT_NULL; d++)
        {
            if (d->d_tag != DT_NEEDED) continue;
            const char *want = st + d->d_val;
            for (int j = 0; j < c->n; j++)
                if (!c->state[j] && map_has_name(c->maps[j], want)) { fini_visit(c, j); break; }
        }
    c->order[c->cnt++] = i;
}

static void run_map_fini(const xxc_link_map_t *m)
{
    uintptr_t base = (uintptr_t)m->l_addr, arr = 0, sz = 0, fn = 0;
    for (const xxc_dyn_t *d = m->l_ld; d && d->d_tag != DT_NULL; d++)
    {
        if (d->d_tag == DT_FINI_ARRAY)   arr = dyn_adj((uintptr_t)d->d_val, base);
        if (d->d_tag == DT_FINI_ARRAYSZ) sz  = (uintptr_t)d->d_val;
        if (d->d_tag == DT_FINI)         fn  = dyn_adj((uintptr_t)d->d_val, base);
    }
    if (arr)
        for (size_t k = sz / sizeof(void *); k > 0; k--)
        {
            void (*f)(void) = ((void (**)(void))arr)[k - 1];
            if (f && f != (void (*)(void))(uintptr_t)-1) f();
        }
    if (fn) ((void (*)(void))fn)();
}

void __xxc_run_shared_fini(int include_main)
{
    if (!__xxc_dynamic_linked || g_boot_argv == nullptr) return;

    const unsigned long *av = auxv_of(g_boot_argc, g_boot_argv);
    const xxc_phdr_t *ph = nullptr;
    size_t phnum = 0;
    for (; av[0] != AT_NULL; av += 2)
    {
        if (av[0] == AT_PHDR)  ph = (const xxc_phdr_t *)av[1];
        if (av[0] == AT_PHNUM) phnum = (size_t)av[1];
    }
    if (ph == nullptr) return;
    uintptr_t bias = phdr_bias(ph, phnum);

    xxc_r_debug_t *rd = nullptr;
    for (size_t i = 0; i < phnum && !rd; i++)
    {
        if (ph[i].p_type != PT_DYNAMIC) continue;
        for (const xxc_dyn_t *d = (const xxc_dyn_t *)(bias + (uintptr_t)ph[i].p_vaddr); d->d_tag != DT_NULL; d++)
            if (d->d_tag == DT_DEBUG) { rd = (xxc_r_debug_t *)(uintptr_t)d->d_val; break; }
    }
    if (rd == nullptr || rd->r_map == nullptr) return;

    int n = 0;
    for (const xxc_link_map_t *m = rd->r_map; m; m = m->l_next) n++;

    size_t bytes = (size_t)n * (sizeof(void *) + sizeof(int) + 1) + 64;
    char *mem = (char *)sys_mmap(nullptr, align_up_u(bytes, 4096), PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANON, -1, 0);
    if (mem == nullptr) return;

    fini_ctx_t c;
    c.maps  = (const xxc_link_map_t **)mem;
    c.order = (int *)(mem + (size_t)n * sizeof(void *));
    c.state = (unsigned char *)(c.order + n);
    c.n = n;
    c.cnt = 0;
    int i = 0;
    for (const xxc_link_map_t *m = rd->r_map; m; m = m->l_next) c.maps[i++] = m;

    fini_visit(&c, 0);
    for (int j = 0; j < n; j++)
        if (!c.state[j]) fini_visit(&c, j);

    for (int k = c.cnt - 1; k >= 0; k--)
    {
        if (c.order[k] == 0 && !include_main) continue;
        run_map_fini(c.maps[c.order[k]]);
    }
    sys_munmap(mem, align_up_u(bytes, 4096));
}

XXC_NO_SSP void __xxc_platform_init(int argc, char **argv)
{
    const unsigned long *auxv = auxv_of(argc, argv);
    self_relocate(auxv);
    ssp_seed(auxv);

    __xxc_environ = argv + argc + 1;
    g_boot_argc = argc;
    g_boot_argv = argv;

    if (has_interp(argc, argv))
    {
        __xxc_dynamic_linked = 1;
        (void)tls_snapshot_dynamic(argc, argv);
        struct xxc_thread *t = &__xxc_main_thread;
        t->self = t;
        atomic_i32_init(&t->tid, (i32)sys_gettid());
        atomic_i32_init(&t->state, XXC_THREAD_DETACHED);
        __xxc_self = t;
        return;
    }
    tls_scan(argc, argv);

    size_t len = align_up_u(__xxc_tls_area_size() + 64, 4096);
    char *area = (char *)sys_mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (area == nullptr)
    {
        static const char msg[] = "xxc: cannot allocate TLS block\n";
        sys_write(2, msg, sizeof(msg) - 1);
        sys_exit(127);
    }

    void *tp;
    (void)__xxc_tls_setup(area + len, &tp);
    set_thread_pointer(tp);

    struct xxc_thread *t = &__xxc_main_thread;
    t->self  = t;
    atomic_i32_init(&t->tid, (i32)sys_gettid());
    atomic_i32_init(&t->state, XXC_THREAD_DETACHED);
    __xxc_self = t;
}
XXC_NO_SSP_END
