; SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED
org 100h
        xor ah, ah
        int 1Ah
        mov bx, dx
        add bx, 182          ; 10 s
w:      xor ah, ah
        int 1Ah
        cmp dx, bx
        jb w
        ret
