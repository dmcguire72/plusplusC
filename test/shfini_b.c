// ++C Runtime Library - shared-object destructor test (library B: depends on A)
// Copyright 2026 Daniel McGuire
// Licensed under the MIT License
#include <def.h>
#include <xxc_sys.h>

extern int shfini_a_done(void);

__attribute__((destructor)) static void b_dtor(void)
{
    /* B depends on A, so A's destructor must not have run yet */
    if (shfini_a_done())
        sys_write(1, "ORDER-VIOLATION\n", 16);
    sys_write(1, "B\n", 2);
}

int shfini_b_touch(void) { return 1; }
