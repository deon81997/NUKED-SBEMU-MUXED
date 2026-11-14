; SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
; RMPLAY.COM - test rig only: a real-mode MPU-401 writer (SBEMU's real-mode trap, QPIEMU/JEMM) for gamecap.sh.
; Plays C:\T\RM.BIN = pairs (type, value): 0 data byte, 1 command byte (both wait for DRR like a game, bounded),
; 2 read an answer (waits for DSR, bounded), 3 wait value ms. Build: nasm -f bin -o RMPLAY.COM rmplay.asm
org 100h
        mov ax, 3D00h
        mov dx, fname
        int 21h
        jc fail
        mov bx, ax
        mov ah, 3Fh
        mov cx, 60000
        mov dx, buf
        int 21h
        jc fail
        mov cx, ax
        shr cx, 1
        mov si, buf
next:   jcxz done
        dec cx
        lodsw                   ; al = type, ah = value
        cmp al, 2
        je  rd
        cmp al, 3
        je  wt
        push ax
        call drr
        pop ax
        mov dx, 330h
        add dl, al              ; 0 -> 330h, 1 -> 331h
        mov al, ah
        out dx, al
        jmp next
rd:     mov dx, 331h
        xor di, di
rdw:    in al, dx
        test al, 80h
        jz rdok
        dec di
        jnz rdw
        jmp next
rdok:   mov dx, 330h
        in al, dx
        jmp next
wt:     push cx
        mov al, ah
        xor ah, ah
        mov dx, 1000
        mul dx                  ; dx:ax = ms * 1000 us
        mov cx, dx
        mov dx, ax
        mov ah, 86h
        int 15h
        pop cx
        jmp next
drr:    push cx
        mov dx, 331h
        mov cx, 0FFFFh
drw:    in al, dx
        test al, 40h
        jz drok
        loop drw
drok:   pop cx
        ret
done:   mov dx, okmsg
        mov ah, 9
        int 21h
        mov ax, 4C00h
        int 21h
fail:   mov dx, errmsg
        mov ah, 9
        int 21h
        mov ax, 4C01h
        int 21h
fname   db 'C:\T\RM.BIN', 0
okmsg   db 'RMPLAY done', 13, 10, '$'
errmsg  db 'RMPLAY: no C:\T\RM.BIN', 13, 10, '$'
buf:
