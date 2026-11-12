; SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
; USB exFAT / NTFS network-redirector stub for SBEMU (docs/USB-STORAGE.md 5b). nasm -f bin -> usbredirbin.h
; (makefile). INT 2Fh handler: AH=11h calls for one of our drive letters far-call SBEMU's protected-mode handler
; (DPMI real-mode callback, RETF style, registers in and out through it); everything else chains at once.
; Which drive a call is for, by subfunction (cls table, filled by usbredir.c):
;   1 = path in SDA first filename buffer ("X:\..."), 2 = SFT at ES:DI (device info bit 15 + drive),
;   3 = CDS at ES:DI, 4 = search data block in the current DTA (bit 7 + drive, our magic at +0Fh).
; Layout used by usbredir.c: 00h old INT 2Fh, 04h callback (0 = chain all), 08h SDA, 0Ch drive mask, 10h cls, 40h code.
bits 16
org 0
old:    dd 0
cbvec:  dd 0
sda:    dd 0
mask:   dd 0
cls:    times 0x30 db 0
entry:  cmp ah, 0x11
        jne chain
        cmp dword [cs:cbvec], 0
        je chain
        cmp al, 0
        jne .sub
        mov al, 0xFF                ; installation check: a redirector is here
        iret
.sub:   cmp al, 0x2F
        ja chain
        push ebx
        push si
        push ds
        xor bx, bx
        mov bl, al
        mov bl, [cs:cls+bx]
        cmp bl, 1
        je .path
        cmp bl, 2
        je .sft
        cmp bl, 3
        je .cds
        cmp bl, 4
        je .dta
        jmp .no
.path:  lds si, [cs:sda]
        cmp byte [si+0x9F], ':'
        jne .no
        mov bl, [si+0x9E]
        sub bl, 'A'
        jmp .chk
.sft:   test word [es:di+5], 0x8000
        jz .no
        mov bl, [es:di+5]
        and bl, 0x3F
        jmp .chk
.cds:   mov bl, [es:di]
        sub bl, 'A'
        jmp .chk
.dta:   lds si, [cs:sda]
        lds si, [si+0x0C]
        mov bl, [si]
        test bl, 0x80
        jz .no
        cmp word [si+0x0F], 0x5553
        jne .no
        and bl, 0x3F
.chk:   cmp bl, 31
        ja .no
        movzx ebx, bl
        bt dword [cs:mask], ebx
        jnc .no
        pop ds
        pop si
        pop ebx
        call far [cs:cbvec]         ; registers and CF come back from the handler
        push bp
        mov bp, sp
        jc .err
        and byte [bp+6], 0xFE
        pop bp
        iret
.err:   or byte [bp+6], 1
        pop bp
        iret
.no:    pop ds
        pop si
        pop ebx
chain:  jmp far [cs:old]
