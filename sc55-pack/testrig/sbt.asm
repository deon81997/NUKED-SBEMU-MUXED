; SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
; SBT.COM - plays 1 s of a 440 Hz square wave on SB 220h, DMA 1, 8-bit single cycle (DSP 14h).
; "SBT P" does the same from a 16-bit DPMI client (to test SBEMU16).
        cpu     586
        bits    16
        org     100h
start:  mov     bx, 1000h
        mov     ah, 4Ah
        int     21h
        ; buffer: 22050 bytes inside one 64K page -> use segment-aligned buffer after code
        mov     ax, cs
        add     ax, (buf - start + 100h + 15) / 16
        add     ax, 0FFFh
        and     ax, 0F000h              ; 64K aligned segment
        mov     [bseg], ax
        mov     es, ax
        xor     di, di
        mov     cx, 22050
        xor     bx, bx
.f:     mov     al, 40h
        cmp     bx, 25
        jb      .lo
        mov     al, 0C0h
.lo:    stosb
        inc     bx
        cmp     bx, 50
        jb      .n
        xor     bx, bx
.n:     loop    .f
        cmp     byte [80h], 0
        je      .rm
        mov     ax, 1687h
        int     2Fh
        or      ax, ax
        jnz     fail
        mov     [entry], di
        mov     [entry + 2], es
        or      si, si
        jz      .np
        mov     bx, si
        mov     ah, 48h
        int     21h
        mov     es, ax
.np:    xor     ax, ax
        call    far [entry]
        jc      fail
        mov     [dsel], ds
.rm:    ; DSP reset
        mov     dx, 226h
        mov     al, 1
        out     dx, al
        mov     cx, 100
.d:     in      al, dx
        loop    .d
        xor     al, al
        out     dx, al
        mov     dx, 22Eh
        mov     cx, 0
.w:     in      al, dx
        test    al, 80h
        jnz     .ok
        loop    .w
        jmp     fail
.ok:    mov     dx, 22Ah
        in      al, dx
        cmp     al, 0AAh
        jne     fail
        call    irqtest
        mov     al, 0D1h
        call    dsp
        mov     al, 40h
        call    dsp
        mov     al, 211                 ; 256 - 1e6/22050
        call    dsp
        ; DMA 1
        mov     al, 5
        out     0Ah, al
        out     0Ch, al
        mov     al, 49h
        out     0Bh, al
        mov     ax, [bseg]
        mov     bx, ax
        shl     ax, 4
        out     02h, al
        mov     al, ah
        out     02h, al
        shr     bx, 12
        mov     al, bl
        out     83h, al
        mov     ax, 22049
        out     03h, al
        mov     al, ah
        out     03h, al
        mov     al, 1
        out     0Ah, al
        mov     al, 14h
        call    dsp
        mov     al, 0E1h
        call    dsp
        mov     al, 55h
        call    dsp
        ; wait ~1.5 s (BIOS ticks)
        push    ds
        mov     ax, 40h
        cmp     byte [80h], 0
        je      .t
        mov     ax, 2
        mov     bx, 40h
        int     31h
.t:     mov     es, ax
        pop     ds
        mov     bx, [es:6Ch]
.tw:    mov     ax, [es:6Ch]
        sub     ax, bx
        cmp     ax, 27
        jb      .tw
        mov     dx, ok
        mov     ah, 9
        int     21h
        mov     ax, 4C00h
        int     21h
fail:   mov     dx, msgf
        mov     ah, 9
        int     21h
        mov     ax, 4C01h
        int     21h
; hook INT 0Fh (IRQ 7), unmask, DSP F2h, wait up to 5 ticks, print count
irqtest:
        cmp     byte [80h], 0
        jne     .pm
        mov     ax, 350Fh
        int     21h
        mov     [oldo], bx
        mov     [olds], es
        mov     ax, 250Fh
        mov     dx, isr
        int     21h
        jmp     .go
.pm:    mov     ax, 0204h
        mov     bl, 0Fh
        int     31h
        mov     [oldo], dx
        mov     [olds], cx
        mov     ax, 0205h
        mov     bl, 0Fh
        mov     cx, cs
        mov     dx, isr
        int     31h
.go:    in      al, 21h
        and     al, 7Fh
        out     21h, al
        sti
        mov     al, 0F2h
        call    dsp
        mov     cx, 0
.w:     cmp     byte [irqs], 0
        jne     .d
        push    cx
        mov     cx, 20
.w2:    in      al, 61h
        loop    .w2
        pop     cx
        loop    .w
.d:     mov     al, [irqs]
        add     al, '0'
        mov     [msgi + 4], al
        mov     dx, msgi
        mov     ah, 9
        int     21h
        mov     byte [irqs], 0
        cmp     byte [80h], 0
        jne     .pr
        push    ds
        mov     dx, [oldo]
        mov     ds, [olds]
        mov     ax, 250Fh
        int     21h
        pop     ds
        ret
.pr:    mov     ax, 0205h
        mov     bl, 0Fh
        mov     cx, [olds]
        mov     dx, [oldo]
        int     31h
        ret
isr:    push    ax
        push    dx
        push    ds
        mov     ax, cs
        cmp     byte [cs:80h], 0
        je      .r
        mov     ax, [cs:dsel]
.r:     mov     ds, ax
        inc     byte [irqs]
        mov     dx, 22Eh
        in      al, dx
        mov     al, 20h
        out     20h, al
        pop     ds
        pop     dx
        pop     ax
        iret
dsp:    push    dx
        push    ax
        mov     dx, 22Ch
.b:     in      al, dx
        test    al, 80h
        jnz     .b
        pop     ax
        out     dx, al
        pop     dx
        ret
entry   dd      0
oldo    dw      0
olds    dw      0
dsel    dw      0
irqs    db      0
msgi    db      'irq ? ', 13, 10, '$'
bseg    dw      0
msgf    db      'SB fail', 13, 10, '$'
ok      db      'played', 13, 10, '$'
buf:
