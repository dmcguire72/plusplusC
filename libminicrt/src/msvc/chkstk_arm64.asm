; ++C C Runtime Library (libminicrt)
; Copyright 2026 Daniel McGuire
; Licensed under the MIT License

    AREA |.text|, CODE, READONLY, ALIGN=2
    EXPORT __chkstk

__chkstk PROC
    lsl     x16, x15, #4
    mov     x17, sp
chkstk_loop
    sub     x17, x17, #0x1000
    subs    x16, x16, #0x1000
    ldr     xzr, [x17]
    bgt     chkstk_loop
    ret
    ENDP

    END
