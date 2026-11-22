; ++C C Runtime Library (libminicrt)
; Copyright 2026 Daniel McGuire
; Licensed under the MIT License

.686
.model flat

.code

ALIGN 16
PUBLIC __chkstk
PUBLIC __alloca_probe
__chkstk PROC
__alloca_probe::
    push    ecx
    lea     ecx, [esp+8]
    sub     ecx, eax
    sbb     eax, eax
    not     eax
    and     ecx, eax
    mov     eax, esp
    and     eax, 0FFFFF000h
probe_loop:
    cmp     ecx, eax
    jb      probe_done
    mov     eax, ecx
    pop     ecx
    xchg    esp, eax
    mov     eax, DWORD PTR [eax]
    mov     DWORD PTR [esp], eax
    ret
probe_done:
    sub     eax, 1000h
    test    DWORD PTR [eax], eax
    jmp     probe_loop
__chkstk ENDP

END
