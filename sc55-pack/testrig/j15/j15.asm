; J15.COM - INT 15h AH=84h (BIOS joystick) test: prints the INT 15h vector, CF + AL for DX=0, AX BX CX DX + CF for DX=1.
; nasm -f bin -o J15.COM j15.asm
org 100h
        push ds
        xor ax, ax
        mov ds, ax
        mov ax, [0x56]
        mov bx, [0x54]
        pop ds
        push bx
        call hex16
        pop ax
        call hex16
        mov ah, 84h
        mov dx, 0
        stc
        int 15h
        mov bl, '0'
        adc bl, 0
        push ax
        mov dl, 'C'
        call putc
        mov dl, bl
        call putc
        mov dl, ' '
        call putc
        pop ax
        call hex8
        mov dl, ' '
        call putc
        mov ah, 84h
        mov dx, 1
        stc
        int 15h
        pushf
        push dx
        push cx
        push bx
        call hex16
        pop ax
        call hex16
        pop ax
        call hex16
        pop ax
        call hex16
        popf
        mov dl, '0'
        adc dl, 0
        call putc
        mov ah, 4Ch
        int 21h
hex16:  push ax
        mov al, ah
        call hex8
        pop ax
        call hex8
        mov dl, ' '
        call putc
        ret
hex8:   push ax
        shr al, 4
        call nib
        pop ax
        and al, 0Fh
nib:    add al, '0'
        cmp al, '9'
        jbe .o
        add al, 7
.o:     mov dl, al
putc:   push ax
        mov ah, 2
        int 21h
        pop ax
        ret
