; INT 21h stub for SBEMU (beta.79): which program is running, for the pad layout per game (padmap.c).
; nasm -f bin int21.asm -o int21.bin; the bytes are in main.c (MAIN_I21Stub). Data at 0..BFh, entry at C0h.
;
; Only AX=4B00h (load and execute) is looked at; everything else goes straight on to DOS. For an EXEC, before
; DOS is entered (so nested DOS calls are fine), the stub
;   - gets the full name (TRUENAME 60h) and the size (FINDFIRST 4Eh, into its own DTA; the caller's DTA is
;     put back afterwards),
;   - tells SBEMU (callback, AL=1, EBX = the file's id, 0 = not found): SBEMU answers AL=1 when the program
;     itself has a layout, AL=0 to have the files in its folder tried,
;   - if asked, goes through the folder (FINDFIRST/FINDNEXT *.*, at most 1024 files) calling back with AL=2,
;     EBX = each file's id, until SBEMU answers AL=1,
;   - calls back with AL=3 (that program's look-up is done),
; then runs the EXEC itself (pushf / call far: DOS returns here when the program has ended, however it ended:
; exit, Ctrl+C, critical error, or staying resident) and calls back with AL=4 (it has ended). So SBEMU keeps a
; stack of the programs running, and no exit function has to be hooked.
; Id = FNV-1 32 (multiply, then xor) over the upper-case 8.3 name, xor (size << 3), as PADMAP.TXT's Id= lines.
bits 16
cpu 386
org 0
oldvec: dd 0            ; 0   previous INT 21h
cbvec:  dd 0            ; 4   SBEMU callback (IRET type)
on:     db 0            ; 8   1 = look EXECs up
nexec:  db 0            ; 9   EXECs seen (wraps)
nfail:  db 0            ; 10  TRUENAME / FINDFIRST failed (wraps)
        db 0
dta:    dd 0            ; 12  the caller's DTA while ours is set
path:   times 128 db 0  ; 16  TRUENAME of the program, then <folder>\*.* for the scan
dtabuf: times 44 db 0   ; 144 our DTA: size at 170 (144+1Ah), name at 174 (144+1Eh)
        times 192-($-$$) db 0
        jmp near i21    ; C0h INT 21h

i21:
        cmp ax, 4B00h
        je exec
old:    jmp far [cs:oldvec]
exec:
        test byte [cs:on], 1
        jz old
        sti
        push ds
        push es
        pushad
        inc byte [cs:nexec]
        mov si, dx                      ; TRUENAME caller's DS:DX -> path
        push cs
        pop es
        mov di, path
        mov ah, 60h
        pushf
        call far [cs:oldvec]
        push cs
        pop ds
        jc .noname
        mov ah, 2Fh                     ; keep the caller's DTA, use ours
        pushf
        call far [cs:oldvec]
        mov [dta], bx
        mov [dta+2], es
        mov dx, dtabuf
        mov ah, 1Ah
        pushf
        call far [cs:oldvec]
        mov dx, path
        mov cx, 7
        mov ah, 4Eh
        pushf
        call far [cs:oldvec]
        jc .nofile
        call hash
        mov al, 1
        call cb
        cmp al, 1
        je .lookedup
        mov si, path                    ; the folder: what follows the last backslash -> *.*
        mov di, si
.f:     lodsb
        test al, al
        jz .fe
        cmp al, '\'
        jne .f
        mov di, si
        jmp .f
.fe:    mov dword [di], 002A2E2Ah       ; "*.*", 0
        mov dx, path
        mov cx, 7
        mov ah, 4Eh
        pushf
        call far [cs:oldvec]
        mov bp, 1024
.next:  jc .scanned
        call hash
        mov al, 2
        call cb
        cmp al, 1
        je .scanned
        dec bp
        jz .scanned
        mov ah, 4Fh
        pushf
        call far [cs:oldvec]
        jmp .next
.nofile:
        inc byte [nfail]
        xor ebx, ebx
        mov al, 1
        call cb
.scanned:
.lookedup:
        mov al, 3
        call cb
        push ds                         ; the caller's DTA back
        lds dx, [cs:dta]
        mov ah, 1Ah
        pushf
        call far [cs:oldvec]
        pop ds
        jmp .run
.noname:
        inc byte [nfail]
        xor ebx, ebx
        mov al, 1
        call cb
        mov al, 3
        call cb
.run:
        popad
        pop es
        pop ds
        pushf
        call far [cs:oldvec]            ; the EXEC: back here when the program has ended
        pushf
        push ds
        push es
        pushad
        mov al, 4
        call cb
        popad
        pop es
        pop ds
        popf
        push bp                         ; DOS's carry (EXEC failed) into the caller's flags, IF as the caller had it
        mov bp, sp
        jc .err
        and byte [bp+6], 0FEh
        pop bp
        iret
.err:   or byte [bp+6], 1
        pop bp
        iret

cb:     pushf                           ; AL = what, EBX = id -> AL = answer
        call far [cs:cbvec]
        ret

hash:   mov si, dtabuf+1Eh              ; name from our DTA -> EBX = id
        mov ebx, 811C9DC5h
.l:     lodsb
        test al, al
        jz .e
        cmp al, 'a'
        jb .u
        cmp al, 'z'
        ja .u
        sub al, 20h
.u:     imul ebx, ebx, 01000193h
        xor bl, al
        jmp .l
.e:     mov eax, [dtabuf+1Ah]
        shl eax, 3
        xor ebx, eax
        ret
