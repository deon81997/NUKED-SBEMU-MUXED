; INT 15h stub for SBEMU (beta.76). Data at 0..15, entry at 10h.
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
        db 0            ; 15
entry:                  ; 10h
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
        pushf
        call far [cs:oldvec]
        jc .ret                         ; BIOS refused: record nothing, return its flags
        push ax
        mov al, [cs:sal]
        cmp al, 0
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
.ret:   retf 2
