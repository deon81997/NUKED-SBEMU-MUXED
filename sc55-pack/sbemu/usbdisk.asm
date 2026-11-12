; USB disk block device stub for SBEMU (USB disks session 4). nasm -f bin; bytes go to usbdiskbin.h
; (tools/mkusbdisk.sh). A DOS block device header + strategy / interrupt routines; the interrupt routine
; far-calls SBEMU's protected-mode handler (DPMI real-mode callback, RETF style) with ES:BX = request header.
; cbvec 0 (SBEMU gone or not set up yet): every request -> "drive not ready".
; Layout used by usbdisk.c: 00h header, 12h request pointer, 16h callback, 1Ah volume id, 30h code.
bits 16
org 0
next:   dd -1               ; 00 next device (DOS fills it when linked)
attr:   dw 0x0802           ; 04 block, open/close/removable (bit 11), 32-bit sectors (bit 1)
        dw strat            ; 06
        dw intr             ; 08
units:  db 0                ; 0A number of units (usbdisk.c)
        db 0,0,0,0,0,0,0    ; 0B
reqp:   dd 0                ; 12 request header from the strategy call
cbvec:  dd 0                ; 16 SBEMU callback, 0 = none
volid:  db "NO NAME    ",0  ; 1A volume id for media check "changed"
        times 0x30-($-$$) db 0
strat:  mov [cs:reqp], bx
        mov [cs:reqp+2], es
        retf
intr:   pushf
        push ds
        push es
        push fs
        push gs
        pushad
        cld
        les bx, [cs:reqp]
        cmp dword [cs:cbvec], 0
        je .nr
        call far [cs:cbvec]
        jmp .out
.nr:    mov word [es:bx+3], 0x8102  ; error, done, not ready
.out:   popad
        pop gs
        pop fs
        pop es
        pop ds
        popf
        retf
