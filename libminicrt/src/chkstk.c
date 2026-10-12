// ++C C Runtime Library (libminicrt)
// Copyright 2026 Daniel McGuire
// Licensed under the MIT License

#if defined(__x86_64__) && (defined(_WIN32) || defined(XXC_PE_TARGET))

__asm__(
    ".text\n"
    ".globl ___chkstk_ms\n"
    ".globl __chkstk\n"
    ".def ___chkstk_ms; .scl 2; .type 32; .endef\n"
    ".def __chkstk; .scl 2; .type 32; .endef\n"
    "___chkstk_ms:\n"
    "__chkstk:\n"
    "    pushq %rcx\n"
    "    pushq %rax\n"
    "    cmpq  $0x1000, %rax\n"
    "    leaq  24(%rsp), %rcx\n"
    "    jb    2f\n"
    "1:\n"
    "    subq  $0x1000, %rcx\n"
    "    orl   $0, (%rcx)\n"
    "    subq  $0x1000, %rax\n"
    "    cmpq  $0x1000, %rax\n"
    "    ja    1b\n"
    "2:\n"
    "    subq  %rax, %rcx\n"
    "    orl   $0, (%rcx)\n"
    "    popq  %rax\n"
    "    popq  %rcx\n"
    "    ret\n"
);

#endif
