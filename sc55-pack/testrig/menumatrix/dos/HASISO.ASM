; SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
; HASISO.COM X - errorlevel 0 if X:\*.ISO exists, 1 if not or the drive is not ready. Own INT 24h handler (Fail):
; MS-DOS 7.1 asks "Not ready reading drive F, Abort, Retry, Fail?" for an empty spare letter, which stops the rig.
org 100h
        mov al, [82h]                   ; drive letter (first char after the space)
        mov [pat], al
        mov ax, 2524h                   ; INT 24h -> fail (DOS restores it from the PSP on exit)
        mov dx, i24
        int 21h
        mov ah, 1Ah                     ; DTA at 0x80 is fine (command line already read)
        mov dx, 80h
        int 21h
        mov ah, 4Eh
        xor cx, cx
        mov dx, pat
        int 21h
        mov al, 1
        jc done
        xor al, al
done:   mov ah, 4Ch
        int 21h
i24:    mov al, 3
        iret
pat:    db "X:\*.ISO", 0
