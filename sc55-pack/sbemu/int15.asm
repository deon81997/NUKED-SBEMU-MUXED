; SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
; INT 15h stub for SBEMU (beta.76, mouse delivery beta.78). Data at 0..1Fh, INT 15h entry at 20h, deliver at 24h.
bits 16
org 0
oldvec: dd 0            ; 0  previous INT 15h
cbvec:  dd 0            ; 4  SBEMU callback (AH=84h)
joyon:  db 0            ; 8  1 = answer AH=84h DX=0/1
mon:    db 0            ; 9  1 = the BIOS mouse is enabled (C200h BH=1 succeeded)
psize:  db 0            ; 10 C205h packet size (BH), 0 = not given
mres:   db 2            ; 11 C203h resolution (0..3), 2 after reset
sal:    db 0            ; 12 scratch: caller's AL
sbh:    db 0            ; 13 scratch: caller's BH
ncall:  db 0            ; 14 C2xxh calls seen (wraps)
busy:   db 0            ; 15 the mouse driver's handler is running (called by the BIOS or by deliver)
drvh:   dd 0            ; 16 the mouse driver's own handler (C207h ES:BX), 0 = none
pst:    dw 0            ; 20 packet for deliver: status (PS/2 first byte)
px:     dw 0            ; 22 X (low byte)
py:     dw 0            ; 24 Y (low byte)
ndel:   db 0            ; 26 packets delivered (wraps)
nskip:  db 0            ; 27 deliver calls refused (busy / no handler / mouse off)
tmph:   dd 0            ; 28 scratch: C207h ES:BX while the BIOS is called
        jmp near i15    ; 20h INT 15h
        nop
        jmp near deliver ; 24h far call from SBEMU (DPMI 0301h): packet at 20..25 to the driver's handler
                        ;     AX = 1 delivered, 0 not (busy, no handler, mouse disabled)
i15:
        cmp ah, 0C2h
        je mouse
        cmp ah, 84h
        jne old
        test byte [cs:joyon], 1
        jz old
        cmp dx, 1
        ja old
        push bp
        mov bp, sp
        and byte [bp+6], 0FEh
        pop bp
        jmp far [cs:cbvec]
old:    jmp far [cs:oldvec]
mouse:
        mov [cs:sal], al
        mov [cs:sbh], bh
        inc byte [cs:ncall]
        cmp al, 7                       ; set handler: the BIOS gets ours (bioscb), which calls the driver's
        jne .call
        mov [cs:tmph], bx
        mov [cs:tmph+2], es
        cmp bx, 0
        jne .sub
        cmp word [cs:tmph+2], 0
        je .call                        ; 0:0 = remove: as it is
.sub:        push cs
        pop es
        mov bx, bioscb
.call:  pushf
        call far [cs:oldvec]
        jc .fail
        push ax
        mov al, [cs:sal]
        cmp al, 7
        jne .n0
        mov ax, [cs:tmph]               ; success: note the driver's handler, give it back its ES:BX
        mov bx, ax
        mov [cs:drvh], ax
        mov ax, [cs:tmph+2]
        mov es, ax
        mov [cs:drvh+2], ax
        jmp .pop
.n0:    cmp al, 0
        jne .n1
        mov al, [cs:sbh]
        and al, 1
        mov [cs:mon], al
        jmp .pop
.n1:    cmp al, 1                       ; reset: disabled, defaults
        je .off
        cmp al, 5                       ; initialise: packet size, disabled, defaults
        jne .n3
        mov al, [cs:sbh]
        mov [cs:psize], al
.off:   mov byte [cs:mon], 0
        mov byte [cs:mres], 2
        jmp .pop
.n3:    cmp al, 3
        jne .pop
        mov al, [cs:sbh]
        and al, 3
        mov [cs:mres], al
.pop:   pop ax
        clc                             ; success (the compares above changed CF)
        retf 2
.fail:  cmp byte [cs:sal], 7            ; refused: the caller's ES:BX back, flags as the BIOS left them
        jne .ret
        mov bx, [cs:tmph]
        mov es, [cs:tmph+2]
.ret:   retf 2

; the BIOS's IRQ 12 handler calls this with status, X, Y, Z pushed: pass them on to the driver's handler
bioscb:
        push bp
        mov bp, sp
        inc byte [cs:busy]
        push word [bp+12]
        push word [bp+10]
        push word [bp+8]
        push word [bp+6]
        call far [cs:drvh]
        add sp, 8
        dec byte [cs:busy]
        pop bp
        retf

; SBEMU: a whole USB mouse packet at once, like the BIOS hands one over (the bytes one at a time through
; the keyboard controller broke on BIOSes that read the whole packet in one interrupt: Award, Dell's)
deliver:
        xor ax, ax
        cmp byte [cs:busy], 0
        jne .no
        test byte [cs:mon], 1
        jz .no
        cmp word [cs:drvh+2], 0
        je .no
        inc byte [cs:busy]
        push word [cs:pst]
        push word [cs:px]
        push word [cs:py]
        push ax
        call far [cs:drvh]
        add sp, 8
        dec byte [cs:busy]
        inc byte [cs:ndel]
        mov ax, 1
        retf
.no:    inc byte [cs:nskip]
        retf
