; ++C C Runtime Library (libminicrt)
; Copyright 2026 Daniel McGuire
; Licensed under the MIT License

.code

ALIGN 16
PUBLIC __chkstk
__chkstk PROC
    sub     rsp, 10h
    mov     QWORD PTR [rsp], r10
    mov     QWORD PTR [rsp+8], r11

    xor     r11, r11
    lea     r10, [rsp+18h]          ; caller's rsp
    sub     r10, rax                ; lowest addr to be used
    cmovb   r10, r11                ; clamp on wrap-around
    mov     r11, QWORD PTR gs:[10h] ; TEB.StackLimit
    cmp     r10, r11
    jae     probe_done

    and     r10w, 0F000h            ; page-align the target
probe_loop:
    lea     r11, [r11-1000h]
    mov     BYTE PTR [r11], 0       ; touch the page
    cmp     r10, r11
    jne     probe_loop

probe_done:
    mov     r10, QWORD PTR [rsp]
    mov     r11, QWORD PTR [rsp+8]
    add     rsp, 10h
    ret
__chkstk ENDP

END
