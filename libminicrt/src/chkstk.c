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

#elif defined(__i386__) && (defined(_WIN32) || defined(XXC_PE_TARGET))

__asm__(
    ".text\n"
    ".globl ___chkstk_ms\n"
    ".def ___chkstk_ms; .scl 2; .type 32; .endef\n"
    "___chkstk_ms:\n"
    "    pushl %ecx\n"
    "    pushl %eax\n"
    "    cmpl  $0x1000, %eax\n"
    "    leal  12(%esp), %ecx\n"
    "    jb    2f\n"
    "1:\n"
    "    subl  $0x1000, %ecx\n"
    "    orl   $0, (%ecx)\n"
    "    subl  $0x1000, %eax\n"
    "    cmpl  $0x1000, %eax\n"
    "    ja    1b\n"
    "2:\n"
    "    subl  %eax, %ecx\n"
    "    orl   $0, (%ecx)\n"
    "    popl  %eax\n"
    "    popl  %ecx\n"
    "    ret\n"

    ".globl __chkstk\n"
    ".globl __alloca\n"
    ".globl ___chkstk\n"
    ".def __chkstk; .scl 2; .type 32; .endef\n"
    ".def __alloca; .scl 2; .type 32; .endef\n"
    ".def ___chkstk; .scl 2; .type 32; .endef\n"
    "__chkstk:\n"
    "__alloca:\n"
    "___chkstk:\n"
    "    pushl %ecx\n"
    "    cmpl  $0x1000, %eax\n"
    "    leal  8(%esp), %ecx\n"
    "    jb    2f\n"
    "1:\n"
    "    subl  $0x1000, %ecx\n"
    "    orl   $0, (%ecx)\n"
    "    subl  $0x1000, %eax\n"
    "    cmpl  $0x1000, %eax\n"
    "    ja    1b\n"
    "2:\n"
    "    subl  %eax, %ecx\n"
    "    orl   $0, (%ecx)\n"
    "    movl  %esp, %eax\n"
    "    movl  %ecx, %esp\n"
    "    movl  (%eax), %ecx\n"
    "    movl  4(%eax), %eax\n"
    "    pushl %eax\n"
    "    ret\n"
);

#elif (defined(__aarch64__) || defined(_M_ARM64)) && (defined(_WIN32) || defined(XXC_PE_TARGET))

__asm__(
    ".text\n"
    ".globl __chkstk\n"
    ".def __chkstk; .scl 2; .type 32; .endef\n"
    ".p2align 2\n"
    "__chkstk:\n"
    "    lsl   x16, x15, #4\n"
    "    mov   x17, sp\n"
    "1:\n"
    "    sub   x17, x17, #0x1000\n"
    "    subs  x16, x16, #0x1000\n"
    "    ldr   xzr, [x17]\n"
    "    b.gt  1b\n"
    "    ret\n"
);

#endif
