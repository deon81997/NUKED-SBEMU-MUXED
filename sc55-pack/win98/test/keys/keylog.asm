; KEYLOG - prints every keyboard byte (port 60h, from INT 9) in hex for N seconds (default 25)
        .model tiny
        .code
        org 100h
start:
        mov     ax, 3509h
        int     21h
        mov     word ptr old9, bx
        mov     word ptr old9+2, es
        mov     dx, offset new9
        mov     ax, 2509h
        int     21h
        xor     ah, ah
        int     1Ah
        mov     t0, dx
lp:     cli
        mov     si, tail
        cmp     si, head
        sti
        je      chk
        mov     al, buf[si]
        inc     si
        and     si, 63
        mov     tail, si
        call    hex
        jmp     lp
chk:    xor     ah, ah
        int     1Ah
        sub     dx, t0
        cmp     dx, 18*25
        jb      lp
        push    ds
        lds     dx, old9
        mov     ax, 2509h
        int     21h
        pop     ds
        mov     ax, 0C00h               ; flush the BIOS key buffer
        int     21h
        mov     dl, 13
        mov     ah, 2
        int     21h
        mov     dl, 10
        int     21h
        mov     ax, 4C00h
        int     21h

hex:    push    ax
        mov     cl, 4
        shr     al, cl
        call    nib
        pop     ax
        call    nib
        mov     dl, ' '
        mov     ah, 2
        int     21h
        ret
nib:    and     al, 15
        add     al, '0'
        cmp     al, '9'
        jbe     @F
        add     al, 7
@@:     mov     dl, al
        mov     ah, 2
        int     21h
        ret

new9:   push    ax
        push    bx
        push    ds
        push    cs
        pop     ds
        in      al, 60h
        mov     bx, head
        mov     buf[bx], al
        inc     bx
        and     bx, 63
        mov     head, bx
        pop     ds
        pop     bx
        pop     ax
        jmp     cs:old9

old9    dd 0
t0      dw 0
head    dw 0
tail    dw 0
buf     db 64 dup (0)
        end start
