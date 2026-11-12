; SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
; JOY16.COM - test for SBEMU16: (1) real mode: SBEMU's info port 206h after a write to 201h;
; (2) as a 16-bit DPMI client (HDPMI16): Jazz Jackrabbit's gameport loop, counts printed 5 times.
        cpu     586
        bits    16
        org     100h
start:
        mov     dx, 201h
        mov     al, 0FFh
        out     dx, al
        mov     dx, 206h
        out     dx, al
        mov     cx, 10
.i:     in      al, dx
        call    hexb
        loop    .i
        call    crlf
        ; DPMI 16-bit
        mov     bx, 1000h
        mov     ah, 4Ah
        int     21h
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
        mov     word [rounds], 5
.round: xor     bx, bx
        xor     bp, bp
        xor     cx, cx
        cli
        mov     dx, 201h
        mov     al, 0FFh
        out     dx, al
        in      al, dx
        mov     [first], al
        and     al, 3
        jz      .end
.l:     in      al, dx
        and     ax, 1
        add     bx, ax
        in      al, dx
        and     ax, 2
        add     bp, ax
        inc     cx
        js      .end
        in      al, dx
        and     al, 3
        jnz     .l
.end:   sti
        shr     bp, 1
        push    cx
        mov     al, [first]
        call    hexb
        mov     ax, bx
        call    hexw
        mov     ax, bp
        call    hexw
        pop     ax
        call    hexw
        call    crlf
        mov     cx, 0
.w:     loop    .w
        dec     word [rounds]
        jnz     .round
        mov     ax, 4C00h
        int     21h
fail:   mov     dx, msgf
        mov     ah, 9
        int     21h
        mov     ax, 4C01h
        int     21h
hexw:   push    ax
        mov     al, ah
        call    hex2
        pop     ax
hexb:   call    hex2
        push    ax
        push    dx
        mov     dl, ' '
        mov     ah, 2
        int     21h
        pop     dx
        pop     ax
        ret
hex2:   push    ax
        shr     al, 4
        call    nib
        pop     ax
        push    ax
        and     al, 15
        call    nib
        pop     ax
        ret
nib:    push    ax
        push    dx
        add     al, '0'
        cmp     al, '9'
        jbe     .p
        add     al, 7
.p:     mov     dl, al
        mov     ah, 2
        int     21h
        pop     dx
        pop     ax
        ret
crlf:   push    ax
        push    dx
        mov     dl, 13
        mov     ah, 2
        int     21h
        mov     dl, 10
        int     21h
        pop     dx
        pop     ax
        ret
entry   dd      0
rounds  dw      0
first   db      0
msgf    db      'no DPMI', 13, 10, '$'
