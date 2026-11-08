; SBEMU16.COM - SBEMU for 16-bit protected-mode games (Borland RTM / 16PMODE extenders: Jazz Jackrabbit,
; ...). Such games run as clients of HDPMI16, and port accesses from there never reach SBEMU (it sees real
; mode through JEMM/QPIEMU and HDPMI32 clients only). This helper is a resident HDPMI16 client that traps the
; ports SBEMU emulates - in HDPMI16i built from SBEMU's HX fork (crazii/HX: same trap API as its HDPMI32i) -
; and repeats each access from real mode (DPMI 0301h -> a far proc doing the IN/OUT in v86 mode), where SBEMU's
; QEMM trap handles it exactly as for a real-mode game. Ports SBEMU doesn't use just reach the hardware.
;
;   C:\HDPMI\HDPMI32I.EXE -r -x
;   C:\HDPMI\HDPMI16I.EXE -r        (the one in this pack: v3.20 like HDPMI32I, crazii/HX)
;   SBEMU ...
;   SBEMU16                          after SBEMU (reads BLASTER for the SB base and MPU ports)
;
; Ports (8 ranges, HDPMI16's limit): 00-21 (DMA 1, PIC 1), 42-43 (PIT 2), 61 (speaker), 81-8F (DMA pages),
; A0-DF (PIC 2, DMA 2), SB base+0..F, 201 (joystick), MPU (BLASTER P, 330) .. 38B (OPL). Ports in between that SBEMU
; doesn't use are repeated from real mode too, which just reaches the hardware.
; Build: nasm -f bin -o SBEMU16.COM sbemu16.asm
        cpu     586
        bits    16
        org     100h

start:
        mov     dx, msg_hello
        mov     ah, 9
        int     21h
        ; ---- BLASTER: Axxx base, Pxxx MPU (real mode: environment segment at PSP:2Ch)
        mov     es, [2Ch]
        xor     di, di
.envloop:
        cmp     byte [es:di], 0
        je      .envdone
        cmp     dword [es:di], 'BLAS'
        jne     .envskip
        cmp     dword [es:di+4], 'TER='
        jne     .envskip
        add     di, 8
.blchar:
        mov     al, [es:di]
        or      al, al
        jz      .envdone
        inc     di
        and     al, 0DFh
        cmp     al, 'A'
        jne     .notA
        call    hexnum
        mov     [sbbase], ax
        jmp     .blchar
.notA:  cmp     al, 'P'
        jne     .blchar
        call    hexnum
        mov     [mpubase], ax
        jmp     .blchar
.envskip:
        cmp     byte [es:di], 0
        je      .envnext
        inc     di
        jmp     .envskip
.envnext:
        inc     di
        jmp     .envloop
.envdone:
        mov     ax, [sbbase]
        mov     [ranges + SBR], ax
        mov     ax, [mpubase]
        mov     [ranges + MPUR], ax
        mov     cx, 38Ch                        ; MPU .. OPL in one range
        sub     cx, ax
        cmp     cx, 100h
        jb      .mpuok
        mov     cx, 2                           ; MPU far from 388h: only its own two ports
.mpuok: mov     [ranges + MPUR + 2], cx
        ; ---- TSC ticks per microsecond (4 BIOS timer ticks = 219702 us)
        push    ds
        xor     ax, ax
        mov     ds, ax
        mov     bx, [46Ch]
.t0:    cmp     bx, [46Ch]
        je      .t0
        rdtsc
        mov     esi, eax
        mov     bx, [46Ch]
        add     bx, 4
.t1:    cmp     bx, [46Ch]
        jne     .t1
        rdtsc
        pop     ds
        sub     eax, esi
        xor     edx, edx
        mov     ecx, 219702
        div     ecx
        or      eax, eax
        jnz     .tok
        inc     eax
.tok:   mov     [tpu], eax

        ; ---- shrink to what we keep, then DPMI
        mov     ax, cs
        mov     es, ax
        mov     bx, (prog_end - start + 100h + 15 + 1024) / 16 ; + room for the stack
        mov     [keep], bx
        mov     ah, 4Ah
        int     21h
        mov     sp, (prog_end - start + 100h + 1024) & 0FFFEh
        mov     ax, 1687h
        int     2Fh
        or      ax, ax
        jnz     no_dpmi
        mov     [dpmi_entry], di
        mov     [dpmi_entry + 2], es
        mov     [rmseg], cs
        or      si, si
        jz      .nopriv
        mov     bx, si
        mov     ah, 48h
        int     21h
        jc      no_mem
        mov     es, ax
.nopriv:
        xor     ax, ax                          ; 16-bit client
        call    far [dpmi_entry]
        jc      no_pm
        ; ---- protected mode (HDPMI16)
        mov     [datasel], ds
        mov     ax, 168Ah
        mov     si, hdpmi_str
        int     2Fh
        or      al, al
        jnz     no_api
        mov     [api], di
        mov     [api + 2], es
        mov     si, ranges
.inst:
        mov     dx, [si]
        mov     cx, [si + 2]
        or      cx, cx
        jz      .instdone
        push    si
        movzx   esi, dx                         ; ESI start, EDI count, CX:EDX handler, BX handler DS
        movzx   edi, cx
        mov     cx, cs
        mov     edx, trap
        mov     bx, ds
        mov     eax, 6
        call    far [api]
        pop     si
        jc      no_trap
        add     si, 4
        jmp     .inst
.instdone:
        mov     dx, msg_ok
        mov     ah, 9
        int     21h
        ; ---- stay resident: DOS keeps the program (real-mode int 21h 31h), the host keeps the client
        mov     di, rmr
        mov     word [di + 28], 3100h           ; EAX
        mov     ax, [keep]
        mov     [di + 20], ax                   ; EDX = paragraphs
        mov     dword [di + 46], 0
        push    ds
        pop     es
        mov     ax, 0300h
        mov     bx, 0021h
        xor     cx, cx
        int     31h
        mov     ax, 4C01h                       ; (does not come back)
        int     21h

no_dpmi: mov    dx, msg_nodpmi
        jmp     rmfail
no_mem: mov     dx, msg_nomem
        jmp     rmfail
no_pm:  mov     dx, msg_nopm
rmfail: mov     ah, 9
        int     21h
        mov     ax, 4C01h
        int     21h
no_api: mov     dx, msg_noapi
        jmp     pmfail
no_trap: mov    dx, msg_notrap
pmfail: mov     ah, 9
        int     21h
        mov     ax, 4C01h
        int     21h

; hex number at es:di -> ax
hexnum:
        xor     ax, ax
.h:     mov     bl, [es:di]
        cmp     bl, '0'
        jb      .e
        cmp     bl, '9'
        jbe     .d
        and     bl, 0DFh
        cmp     bl, 'A'
        jb      .e
        cmp     bl, 'F'
        ja      .e
        sub     bl, 7
.d:     sub     bl, '0'
        shl     ax, 4
        or      al, bl
        inc     di
        jmp     .h
.e:     ret

; ====================================================================== resident part
; real mode: the access itself (v86 under JEMM: SBEMU's QEMM trap sees it). BL: bit 0 out, bits 1-2 size
; (0 byte, 1 word, 3 dword); DX port; EAX value / result
rm_io:
        test    bl, 8
        jnz     rm_joy
        test    bl, 1
        jnz     .out
        test    bl, 6
        jz      .inb
        test    bl, 4
        jnz     .ind
        in      ax, dx
        retf
.inb:   in      al, dx
        retf
.ind:   in      eax, dx
        retf
.out:   test    bl, 6
        jz      .outb
        test    bl, 4
        jnz     .outd
        out     dx, ax
        retf
.outb:  out     dx, al
        retf
.outd:  out     dx, eax
        retf
; game wrote 201h: pass it on, then ask SBEMU (port 206h) how long each axis has left -> jbuf
rm_joy: out     dx, al
        mov     dx, 206h
        out     dx, al
        mov     dx, 201h
        in      al, dx
        mov     [cs:jbuf], al
        mov     dx, 206h
        mov     di, jbuf + 1
        mov     cx, 10
.j:     in      al, dx
        mov     [cs:di], al
        inc     di
        loop    .j
        retf

; protected mode: one access through rm_io. in: BX op, DX port, EAX value; out: EAX. DS = our data
rmio:
        push    es
        push    di
        push    cx
        mov     di, [depth]
        cmp     di, 8
        jb      .ok
        xor     di, di                          ; (nesting deeper than 8: reuse - should not happen)
.ok:    imul    di, di, 50
        add     di, rmr
        inc     word [depth]
        mov     [di + 28], eax
        mov     dword [di + 20], 0
        mov     [di + 20], dx
        mov     dword [di + 16], 0
        mov     [di + 16], bx
        mov     word [di + 32], 0
        mov     ax, [rmseg]
        mov     [di + 34], ax
        mov     [di + 36], ax
        mov     [di + 38], ax
        mov     [di + 40], ax
        mov     word [di + 42], rm_io
        mov     [di + 44], ax
        mov     dword [di + 46], 0
        push    ds
        pop     es
        push    bx
        mov     ax, 0301h
        xor     bx, bx
        xor     cx, cx
        int     31h
        pop     bx
        mov     eax, [di + 28]
        dec     word [depth]
        pop     cx
        pop     di
        pop     es
        ret

; trap proc (SBEMU's HDPMI, crazii/HX: the same interface SBEMU's own traps use). HDPMI16i calls it once per
; byte (a word access comes as two calls, port and port+1) through an IRETD to it, on the game's stack, with
; interrupts off, DS = ES = our data; EDX = port, ECX = 1 for OUT, EAX = value. Return: EAX (IN: the byte read),
; every other register kept, 32-bit far return.
trap:
        push    ebx
        push    ecx
        push    edx
        push    esi
        push    edi
        push    ebp
        push    ds
        push    es
        push    fs                              ; HDPMI16's trip to real mode does not keep the client's FS/GS
        push    gs                              ; (Jazz Jackrabbit's mixer counts its loops in FS:)
        mov     ds, [cs:datasel]
        mov     bx, cx
        and     bx, 1                           ; byte access, in / out
        cmp     dx, 201h
        jne     .plain
        call    joy
        jmp     .ret
.plain: call    rmio
.ret:
%ifdef LOG
        mov     si, [logi]
        cmp     si, 1C00h
        jae     .nolog
        cmp     dx, 20h
        je      .nolog
        cmp     dx, 61h
        je      .nolog
        cmp     dx, 3
        je      .nolog
        cmp     dx, 0Ch
        je      .nolog
        shl     si, 3
        mov     [logb + si], dx
        mov     [logb + si + 2], bl
        mov     [logb + si + 3], al
        mov     bp, sp
        mov     cx, [ss:bp + 100]
        mov     [logb + si + 4], cx
        mov     cx, [ss:bp + 104]
        mov     [logb + si + 6], cx
        inc     word [logi]
.nolog:
%endif
        pop     gs
        pop     fs
        pop     es
        pop     ds
        pop     ebp
        pop     edi
        pop     esi
        pop     edx
        pop     ecx
        pop     ebx
        o32 retf

;   gameport: the game times its reads of 201h against its write, and every trip to real mode takes longer
; than a stick's whole time-out. So on the write SBEMU gives each axis' time-out (rm_joy), and reads are answered
; here with no trip at all, on a clock that moves JSTEP us per read: the same stick position always gives a game
; the same count, however long its traps take (QEMU: ~30 us each; real hardware: a few us). GrIP (bit stream
; stepped per read), button-only polling (no write for 20 ms) and an SBEMU without port 206h go the slow way.
JSTEP   equ     4
joy:
        test    bl, 1
        jz      .in
        or      bl, 8
        call    rmio                            ; out 201h + info
        rdtsc
        mov     [t0], eax
        mov     [t0 + 4], edx
        mov     byte [jvalid], 0
        mov     word [jreads], 0
        cmp     byte [jbuf + 10], 0A5h
        jne     .outx
        test    byte [jbuf + 9], 1
        jnz     .outx                           ; GrIP: every read to SBEMU
        mov     al, [jbuf]
        and     al, 0F0h
        mov     [jbtn], al
        mov     byte [jvalid], 1
.outx:  ret
.in:    cmp     byte [jvalid], 0
        je      .slow
        rdtsc
        sub     eax, [t0]
        sbb     edx, [t0 + 4]
        jnz     .stale
        mov     ecx, eax
        mov     eax, [tpu]
        mov     edx, 20000
        mul     edx
        cmp     ecx, eax
        jae     .stale
        mov     ax, [jreads]
        cmp     ax, 0FFF0h
        jae     .nostep
        add     ax, JSTEP
        mov     [jreads], ax
.nostep:
        mov     al, [jbtn]
        or      al, 0Fh                         ; then clear the axes that have run out
        mov     dx, [jreads]
        xor     bx, bx
.cmp:   mov     cx, [jbuf + 1 + bx]
        cmp     cx, 0FFFFh
        je      .run                            ; nothing connected: never runs out
        cmp     dx, cx
        jb      .run
        mov     cl, bl
        shr     cl, 1
        mov     ah, 1
        shl     ah, cl
        not     ah
        and     al, ah
.run:   add     bx, 2
        cmp     bx, 8
        jb      .cmp
        movzx   eax, al
        ret
.stale: mov     byte [jvalid], 0
.slow:  xor     bx, bx
        jmp     rmio

; ---------------------------------------------------------------------- data
datasel dw      0
tpu     dd      1                               ; TSC ticks per us
t0      dd      0, 0                            ; TSC at the game's last write to 201h
jreads  dw      0                               ; us on the per-read clock since that write
jbtn    db      0                               ; button bits (4-7) at that write
jvalid  db      0
jbuf    times 11 db 0                           ; from rm_joy: 201h value, 4 x us, flags, A5h
rmseg   dw      0
depth   dw      0
api     dd      0
sbbase  dw      220h
mpubase dw      330h
keep    dw      0
dpmi_entry dd   0
hdpmi_str db    'HDPMI', 0
; port ranges: start, count (HDPMI16: 8 at most)
ranges:
        dw      00h, 22h                        ; DMA 1 + PIC 1 (00-21)
        dw      42h, 2                          ; PIT 2 (not 40h, 60h: timer reads and keyboard stay fast)
        dw      61h, 1                          ; speaker
        dw      81h, 0Fh                        ; DMA pages (81-8F)
        dw      0A0h, 40h                       ; PIC 2 + DMA 2 (A0-DF)
SBR     equ     $ - ranges
        dw      220h, 16                        ; SB
        dw      201h, 1                         ; joystick
MPUR    equ     $ - ranges
        dw      330h, 5Ch                       ; MPU 330-331 ... OPL 388-38B
        dw      0, 0
msg_hello  db   'SBEMU16: SBEMU for 16-bit protected-mode games (HDPMI16)', 13, 10, '$'
msg_ok     db   'SBEMU16: resident - sound, OPL, MPU and joystick ports of 16-bit DPMI games go to SBEMU.', 13, 10, '$'
msg_nodpmi db   'SBEMU16: no DPMI host. Load C:\HDPMI\HDPMI16I.EXE -r and HDPMI32I.EXE -r -x first.', 13, 10, '$'
msg_nomem  db   'SBEMU16: not enough memory.', 13, 10, '$'
msg_nopm   db   'SBEMU16: no 16-bit DPMI host. Load C:\HDPMI\HDPMI16I.EXE -r (before HDPMI32I).', 13, 10, '$'
msg_noapi  db   'SBEMU16: the 16-bit DPMI host is not HDPMI. Load C:\HDPMI\HDPMI16I.EXE -r.', 13, 10, '$'
msg_notrap db   'SBEMU16: HDPMI16 cannot trap ports - use the HDPMI16I.EXE that comes with SBEMU (C:\HDPMI).', 13, 10, '$'
rmr:    times   8*50 db 0
%ifdef LOG
logsig  db      'S16LOG'
logi    dw      0
logb    times   1C00h*8 db 0
%endif
prog_end:
