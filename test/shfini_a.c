// ++C Runtime Library - shared-object destructor test (library A: dependency)
// Copyright 2026 Daniel McGuire
// Licensed under the MIT License
#include <def.h>
#include <xxc_sys.h>

static int g_a_done;

int shfini_a_done(void) { return g_a_done; }

__attribute__((destructor)) static void a_dtor(void)
{
    g_a_done = 1;
    sys_write(1, "A\n", 2);
}
