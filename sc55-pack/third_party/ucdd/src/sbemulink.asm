; SPDX-License-Identifier: GPL-3.0-only
; NUKED SB-EMU addition to uCDD (mount-only build, SBEMU_LINK=1): the drive's own CD-Audio, like a real drive.
;
; A real CD-ROM drive plays CD-Audio by itself: PLAY starts it, the position moves on, the drive is busy until
; the end of the range, Q-channel / audio status / head location follow the playing position, STOP pauses,
; RESUME continues. This module does the same for the mounted image without any sound card code:
;  - the position comes from the BIOS timer (75 sectors a second); nothing runs between requests (no CPU use);
;  - the range and the commands are written to the "SBCDA1" mailbox, version 1, which SBEMU /VCDA reads: SBEMU
;    then reads the audio sectors from the image and mixes them, and its position (pos) is used while it plays.
; Without SBEMU the music is silent, but games see a drive that plays and finishes the track on time (no
; "general failure" from PLAY, no endless wait for the music to end).
;
; Called by the driver (driver.asm) with DS=CS, SI = unit, FS:BP = request, ES:DI = IOCTL control block,
; CX = control block size (already checked). Returns the request status in AX.

%define VA_TICKS_PER_DAY 1573040

; ---- the mailbox (SBEMU's cdlink.h CDLINK_Box, 32 bytes, found by its signature + version) ----
    align 4
va_box:
va_box_sig      db 'SBCDA1', 0, 0
va_box_state    db 0        ; 0 idle, 1 playing, 2 paused (SBEMU writes 0 at the end of the range)
va_box_flags    db 0        ; bit 0: SBEMU attached (set by SBEMU), other bits: SBEMU diagnostics
va_box_cmd      db 0        ; 1 play, 2 pause, 3 resume, 4 stop
va_box_unitno   db 0
va_box_start    dd 0        ; file sector (2352-byte raw sectors of the image file)
va_box_end      dd 0        ; one past the last
va_box_pos      dd 0        ; SBEMU advances it while it plays
va_box_gen      dw 0        ; +1 on every command
va_box_vol      db 255, 255
va_box_unitp    dw 0        ; unit offset in this segment (SBEMU reads the image path at +IMAGE_PATH)
va_box_ver      dw 1

va_unit     dw 0            ; unit of the current range, 0 = none
va_playing  db 0
va_paused   db 0
va_start    dd 0            ; image sectors (LBA + ORIGIN)
va_end      dd 0
va_base     dd 0            ; image sector at va_t0 (and the paused position)
va_t0       dd 0            ; BIOS ticks at play / resume
va_gap      dd 0            ; synthetic gap (PREGAP) before the start track: image sector - gap = file sector
va_head     dd 0            ; image sector of the head when nothing plays
va_stat_start dd 0          ; MSF for IOCTL 15
va_stat_end   dd 0

; ---- helpers ----
va_ticks:                   ; EAX = BIOS ticks since midnight
    push gs
    xor ax, ax
    mov gs, ax
    mov eax, [gs:46ch]
    pop gs
    ret

va_cmd:                     ; AL = command, AH = state: post it to the mailbox
    mov [va_box_cmd], al
    mov [va_box_state], ah
    inc word [va_box_gen]
    ret

; Current image sector of the playing range; ends the range when it is over. Keeps all registers but EAX.
va_pos:
    cmp byte [va_playing], 0
    jne .playing
    mov eax, [va_head]
    cmp byte [va_paused], 0
    je .ret
    mov eax, [va_base]
.ret:
    ret
.playing:
    push ebx
    push ecx
    push edx
    call va_ticks
    sub eax, [va_t0]
    jnc .elapsed
    add eax, VA_TICKS_PER_DAY
.elapsed:
    mov ecx, 4119           ; 75 sectors a second / 18.2065 ticks a second = 4.119 sectors a tick
    mul ecx
    mov ecx, 1000
    div ecx
    add eax, [va_base]
    mov ebx, eax            ; timer position
    test byte [va_box_flags], 1
    jz .timer
    ; SBEMU plays it: its position is what is heard. It ends the range by writing state 0.
    cmp byte [va_box_state], 0
    je .ended
    mov eax, [va_box_pos]
    add eax, [va_gap]
    ; safety: SBEMU could not read the image (or is stalled): end 3 s after the timer says so
    mov edx, [va_end]
    add edx, 225
    cmp ebx, edx
    jae .ended
    cmp eax, [va_end]
    jb .done
    mov eax, [va_end]
    dec eax
    jmp .done
.timer:
    mov eax, ebx
    cmp eax, [va_end]
    jb .done
.ended:
    mov byte [va_playing], 0
    mov byte [va_paused], 0
    mov eax, [va_end]
    mov [va_head], eax
    mov byte [va_box_state], 0
.done:
    pop edx
    pop ecx
    pop ebx
    ret

; Stop and forget the range (a second STOP, SEEK, reset, eject).
va_reset:
    push eax
    call va_pos
    mov [va_head], eax
    mov byte [va_playing], 0
    mov byte [va_paused], 0
    mov dword [va_stat_start], 0
    mov dword [va_stat_end], 0
    mov ax, 0004h
    call va_cmd
    pop eax
    ret

; The driver calls this from clear_unit (eject, unmount, new image): stop that unit's music.
va_unit_gone:
    cmp si, [va_unit]
    jne .ret
    call va_reset
    mov word [va_unit], 0
    mov dword [va_head], 0
.ret:
    ret

; FS:BP+13 addressing mode, EAX = address -> EAX = image sector, CF on error
va_address:
    cmp byte [fs:bp+13], 0
    je .lba
    cmp byte [fs:bp+13], 1
    jne .bad
    test eax, 0ff000000h
    jnz .bad
    movzx edx, al
    cmp dl, 75
    jae .bad
    movzx ecx, ah
    cmp cl, 60
    jae .bad
    shr eax, 16
    imul eax, 60
    add eax, ecx
    imul eax, 75
    add eax, edx
    sub eax, 150
    jc .bad
.lba:
    add eax, [si+ORIGIN]
    clc
    ret
.bad:
    stc
    ret

; EAX = image sector -> BX = track entry (unit offset, relative to SI), DL = track number. Uses INDEX 00.
va_track:
    push cx
    movzx cx, byte [si+TRACK_COUNT]
    lea bx, [si+TRACKS]
    mov dl, 1
.next:
    cmp cx, 1
    jbe .found
    cmp eax, [bx+TRACK_SIZE+TRACK_INDEX0]
    jb .found
    add bx, TRACK_SIZE
    inc dl
    dec cx
    jmp .next
.found:
    pop cx
    ret

; EAX frames -> EAX = M<<16 | S<<8 | F (binary)
va_frames_msf:
    push ebx
    push ecx
    push edx
    xor edx, edx
    mov ecx, 4500
    div ecx
    mov ebx, eax
    shl ebx, 16
    mov eax, edx
    xor edx, edx
    mov ecx, 75
    div ecx
    shl eax, 8
    or eax, ebx
    or eax, edx
    pop edx
    pop ecx
    pop ebx
    ret

va_msf:                     ; EAX image sector -> disc time MSF
    sub eax, [si+ORIGIN]
    add eax, 150
    jmp va_frames_msf

; ---- the request entry ----
va_request:
    cmp word [si+HANDLE], 0ffffh
    je .not_ready
    mov al, [fs:bp+2]
    cmp al, 83h
    je .seek
    cmp al, 84h
    je .play
    cmp al, 85h
    je .stop
    cmp al, 88h
    je .resume
    cmp al, 3
    je .input
    cmp al, 0ch
    je .output
.unknown:
    mov ax, 8103h
    ret
.not_ready:
    mov ax, 8102h
    ret
.error:
    mov ax, 810ch
    ret
.ok:
    mov ax, 0100h
    ret

.output:
    cmp byte [es:di], 3
    jne .unknown
    cmp cx, 9
    jb .error
    mov al, [es:di+2]       ; output 0 volume
    mov [va_box_vol], al
    mov al, [es:di+4]       ; output 1 volume
    mov [va_box_vol+1], al
    jmp .ok

.input:
    mov al, [es:di]
    cmp al, 1
    je .head
    cmp al, 4
    je .volume
    cmp al, 12
    je .q_channel
    cmp al, 15
    je .audio_status
    jmp .unknown

.head:
    cmp cx, 6
    jb .error
    cmp byte [es:di+1], 1
    ja .unknown
    call va_pos
    cmp byte [es:di+1], 0
    je .head_lba
    call va_msf
    jmp .head_store
.head_lba:
    sub eax, [si+ORIGIN]
.head_store:
    mov [es:di+2], eax
    jmp .ok

.volume:
    cmp cx, 9
    jb .error
    mov byte [es:di+1], 0
    mov al, [va_box_vol]
    mov [es:di+2], al
    mov byte [es:di+3], 1
    mov al, [va_box_vol+1]
    mov [es:di+4], al
    mov word [es:di+5], 2
    mov word [es:di+7], 3
    jmp .ok

.audio_status:
    cmp cx, 11
    jb .error
    call va_pos             ; may end the range
    movzx ax, byte [va_paused]
    mov [es:di+1], ax
    mov eax, [va_stat_start]
    mov [es:di+3], eax
    mov eax, [va_stat_end]
    mov [es:di+7], eax
    jmp .ok

.q_channel:
    cmp cx, 11
    jb .error
    call va_pos
    push eax                ; position
    call va_track           ; BX = track entry, DL = number
    mov al, [bx+TRACK_CONTROL]
    or al, 1                ; ADR 1 = position
    mov [es:di+1], al
    mov al, dl
    aam                     ; track number in BCD
    shl ah, 4
    or al, ah
    mov [es:di+2], al
    mov byte [es:di+3], 1   ; index 1, or 0 in the pregap
    pop eax
    push eax
    sub eax, [bx+TRACK_START]
    jnc .q_relative
    neg eax
    mov byte [es:di+3], 0
.q_relative:
    call va_frames_msf
    mov [es:di+6], al
    mov [es:di+5], ah
    shr eax, 16
    mov [es:di+4], al
    mov byte [es:di+7], 0
    pop eax
    call va_msf
    mov [es:di+10], al
    mov [es:di+9], ah
    shr eax, 16
    mov [es:di+8], al
    jmp .ok

.seek:
    cmp bp, 10000h-24
    ja .error
    mov eax, [fs:bp+20]
    call va_address
    jc .error
    cmp eax, [si+DISC_SECTORS]
    jae .error
    call va_reset
    mov [va_head], eax
    mov [va_unit], si
    jmp .ok

.stop:
    cmp byte [va_playing], 0
    je .stop_paused
    call va_pos             ; may have ended just now
    cmp byte [va_playing], 0
    je .stop_paused
    mov [va_base], eax      ; first STOP pauses
    mov byte [va_playing], 0
    mov byte [va_paused], 1
    call va_msf
    mov [va_stat_start], eax
    mov ax, 0202h
    call va_cmd
    jmp .ok
.stop_paused:               ; a STOP while paused (or stopped) clears the resume point
    call va_reset
    jmp .ok

.resume:
    cmp byte [va_paused], 1
    jne .error
    call va_ticks
    mov [va_t0], eax
    mov byte [va_paused], 0
    mov byte [va_playing], 1
    mov ax, 0103h
    call va_cmd
    jmp .ok

.play:
    ; Tunnel B1 sends a length of 0 with a complete request (uCDD driver note).
    cmp bp, 10000h-22
    ja .error
    mov eax, [fs:bp+14]
    call va_address
    jc .error
    mov edx, [fs:bp+18]
    test edx, edx
    jz .error
    ; drives stop at the lead-out (Angst sends a count past the end of the disc)
    add edx, eax
    jc .clamp
    cmp edx, [si+DISC_SECTORS]
    jbe .end_ok
.clamp:
    mov edx, [si+DISC_SECTORS]
.end_ok:
    cmp eax, edx
    jae .error
    push edx
    call va_track           ; start track: no audio from a data track
    pop edx
    test byte [bx+TRACK_CONTROL], 40h
    jnz .error
    ; the range ends where a later data track starts
    movzx cx, byte [si+TRACK_COUNT]
    lea di, [si+TRACKS]     ; (ES:DI is not used by PLAY)
.range:
    cmp di, bx
    jbe .range_next
    test byte [di+TRACK_CONTROL], 40h
    jz .range_next
    cmp edx, [di+TRACK_INDEX0]
    jbe .range_next
    mov edx, [di+TRACK_INDEX0]
.range_next:
    add di, TRACK_SIZE
    loop .range
    cmp eax, edx
    jae .error
    ; the new range
    mov [va_start], eax
    mov [va_base], eax
    mov [va_end], edx
    mov [va_unit], si
    mov [va_box_unitp], si
    mov cl, [fs:bp+1]
    mov [va_box_unitno], cl
    mov ecx, [bx+TRACK_CONTROL]
    shr ecx, 8
    mov [va_gap], ecx
    push eax
    sub eax, ecx
    mov [va_box_start], eax
    mov [va_box_pos], eax
    mov eax, edx
    sub eax, ecx
    mov [va_box_end], eax
    pop eax
    call va_msf
    mov [va_stat_start], eax
    mov eax, [va_end]
    call va_msf
    mov [va_stat_end], eax
    call va_ticks
    mov [va_t0], eax
    mov byte [va_paused], 0
    mov byte [va_playing], 1
    mov ax, 0101h
    call va_cmd
    jmp .ok

; For the driver's status word: CF = audio is playing (the busy bit of a real drive).
va_busy:
    cmp byte [va_playing], 0
    je .no
    push eax
    call va_pos
    pop eax
    cmp byte [va_playing], 0
    je .no
    stc
    ret
.no:
    clc
    ret
