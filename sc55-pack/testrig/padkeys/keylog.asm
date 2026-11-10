; KEYLOG (NASM) - every keyboard byte (port 60h, seen by INT 9) in hex for N seconds, then exit.
;   KEYLOG [seconds]   (default 25). nasm -f bin keylog.asm -o KEYLOG.COM
; Wrapped by mkkeen.py (win98/test/keys) into a fake KEEN4E.EXE whose id is Commander Keen 4's.
bits 16
org 100h
start:  mov si, 81h                     ; seconds from the command line
        xor cx, cx
.p:     lodsb
        cmp al, ' '
        je .p
        sub al, '0'
        cmp al, 9
        ja .pd
        imul cx, cx, 10
        xor ah, ah
        add cx, ax
        jmp .p
.pd:    test cx, cx
        jnz .s
        mov cx, 25
.s:     mov ax, 18
        mul cx
        mov [limit], ax
        mov ax, 3509h
        int 21h
        mov [old9], bx
        mov [old9+2], es
        mov dx, new9
        mov ax, 2509h
        int 21h
        xor ah, ah
        int 1Ah
        mov [t0], dx
lp:     cli
        mov si, [tail]
        cmp si, [head]
        sti
        je chk
        mov al, [buf+si]
        inc si
        and si, 63
        mov [tail], si
        call hex
        jmp lp
chk:    xor ah, ah
        int 1Ah
        sub dx, [t0]
        cmp dx, [limit]
        jb lp
        push ds
        lds dx, [old9]
        mov ax, 2509h
        int 21h
        pop ds
        mov ax, 0C00h                   ; flush the BIOS key buffer
        int 21h
        mov dl, 13
        mov ah, 2
        int 21h
        mov dl, 10
        int 21h
        mov ax, 4C00h
        int 21h
hex:    push ax
        shr al, 4
        call nib
        pop ax
        call nib
        mov dl, ' '
        mov ah, 2
        int 21h
        ret
nib:    and al, 15
        add al, '0'
        cmp al, '9'
        jbe .o
        add al, 7
.o:     mov dl, al
        mov ah, 2
        int 21h
        ret
new9:   push ax
        push bx
        in al, 60h
        mov bx, [cs:head]
        mov [cs:buf+bx], al
        inc bx
        and bx, 63
        mov [cs:head], bx
        pop bx
        pop ax
        jmp far [cs:old9]
old9:   dd 0
t0:     dw 0
limit:  dw 0
head:   dw 0
tail:   dw 0
buf:    times 64 db 0
