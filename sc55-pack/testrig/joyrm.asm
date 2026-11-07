; JOYRM.COM - real-mode gameport read (test rig): prints loop counts for axes 0/1 and the button nibble.
; "nasm -f bin joyrm.asm -o JOYRM.COM". A count of FFFF = the axis never timed out.
        org 100h
        mov dx, 201h
        cli
        out dx, al
        xor bx, bx              ; axis 0 count
        xor si, si              ; axis 1 count
        mov cx, 0FFFFh
.l:     in al, dx
        test al, 1
        jz .a0
        inc bx
.a0:    test al, 2
        jz .a1
        inc si
.a1:    test al, 3
        jz .done
        loop .l
.done:  in al, dx
        sti
        mov [btn], al
        mov ax, bx
        call hex
        mov ax, si
        call hex
        mov al, [btn]
        xor ah, ah
        call hex
        mov dx, crlf
        mov ah, 9
        int 21h
        mov ax, 4C00h
        int 21h
hex:    mov cx, 4
.h:     rol ax, 4
        push ax
        and al, 0Fh
        add al, '0'
        cmp al, '9'
        jbe .p
        add al, 7
.p:     mov dl, al
        mov ah, 2
        int 21h
        pop ax
        loop .h
        mov dl, ' '
        mov ah, 2
        int 21h
        ret
btn     db 0
crlf    db 13, 10, '$'
