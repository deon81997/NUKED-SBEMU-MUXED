; nukedjoy.asm - NUKEDJOY.VXD: VJoyD joystick minidriver (original interface, Win95 DDK
; msjstick sample). VJoyD loads it when a joystick's type names it (OEMCallout =
; NUKEDJOY.VXD); on that first load it registers its poll, hw caps and joystick id
; routines. The values come from SC55W.EXE through SC55W.VXD (nukedjoy.c).
; Never open this VxD from a program: VJoyD must be the one that loads it first.

        .386p
        .model flat
        option casemap:none
        include ../vxd/vxd98.inc

VJOYD_DEVICE_ID     equ 0449h       ; MULTIMEDIA_OEM_ID + 9
@VJOYD_Register     equ 0000h       ; service 0 (VJOYD has no Get_Version): EAX poll, EBX cfg, ECX hw caps, EDX joystick id
@Get_DDB            equ 0146h       ; EAX device id, EDI name -> ECX DDB or 0

extern _nj_poll@8:near, _nj_hwcaps@8:near, _nj_joyid@8:near, _nj_register:near
VJOYD_REINIT_CONTROL equ 70000000h  ; VJoyD asks an already loaded minidriver to register
@_RegCreateKey      equ 014Ah
@_RegSetValueEx     equ 0152h
@_RegCloseKey       equ 0149h

public _vxd_get_ddb, _vxd_note, _vxd_vjoyd_version, _vxd_vjoyd_register_dx5, _vxd_vjoyd_register_old
public NUKEDJOY_DDB

_LDATA  segment dword public 'LCODE'
NUKEDJOY_DDB VxD_Desc_Block <0, DDK_VERSION, UNDEFINED_DEVICE_ID, 1, 0, 0, \
        <'N','U','K','E','D','J','O','Y'>, UNDEFINED_INIT_ORDER, offset NJ_Control, \
        0, 0, 0, 0, 0, 0, 0, 0, 'Prev', sizeof VxD_Desc_Block, 'Rsv1', 'Rsv2', 'Rsv3'>
registered dd 0
note_key db 'Software\NUKED SB-EMU\NukedJoy', 0
note_hk  dd 0
note_val dd 0
_LDATA  ends

_LTEXT  segment dword public 'LCODE'
        assume cs:flat, ds:flat, es:flat, ss:flat

NJ_Control proc near
        cmp     eax, Sys_Dynamic_Device_Init
        je      nj_init
        cmp     eax, VJOYD_REINIT_CONTROL
        je      nj_init
        clc
        ret
nj_init:
        pushad
        ; only when VJoyD is there (it is the one loading us)
        mov     eax, VJOYD_DEVICE_ID
        xor     edi, edi
        int 20h
        dd (VMM_DEVICE_ID shl 16) + @Get_DDB
        or      ecx, ecx
        jz      @F
        push    dword ptr [esp+28]      ; the control message (EAX saved by pushad)
        call    _nj_register            ; nukedjoy.c: DX5 interface if VJoyD has it, else the old one
        add     esp, 4
        mov     registered, eax
@@:     popad
        clc
        ret
NJ_Control endp

; u32 __cdecl vxd_get_ddb(u32 id, const char *name8)
_vxd_get_ddb proc near
        push    edi
        push    ebx
        mov     eax, [esp+12]
        mov     edi, [esp+16]
        int 20h
        dd (VMM_DEVICE_ID shl 16) + @Get_DDB
        mov     eax, ecx
        pop     ebx
        pop     edi
        ret
_vxd_get_ddb endp

; void __cdecl vxd_note(const char *name, u32 value): HKLM\Software\NUKED SB-EMU\NukedJoy,
; a REG_DWORD, for checking what VJoyD did with us (regedit)
_vxd_note proc near
        pushad
        mov     eax, [esp+40]
        mov     note_val, eax
        push    offset note_hk
        push    offset note_key
        push    80000002h                   ; HKEY_LOCAL_MACHINE
        int 20h
        dd (VMM_DEVICE_ID shl 16) + @_RegCreateKey
        add     esp, 12
        or      eax, eax
        jnz     nn_done
        push    4
        push    offset note_val
        push    4                           ; REG_DWORD
        push    0
        push    dword ptr [esp+36+16]       ; name
        push    note_hk
        int 20h
        dd (VMM_DEVICE_ID shl 16) + @_RegSetValueEx
        add     esp, 24
        push    note_hk
        int 20h
        dd (VMM_DEVICE_ID shl 16) + @_RegCloseKey
        add     esp, 4
nn_done:
        popad
        ret
_vxd_note endp

; u32 __cdecl vxd_vjoyd_version(void)
_vxd_vjoyd_version proc near           ; VMM version: AH.AL (4.10 = Windows 98)
        push    ebx
        push    ecx
        xor     eax, eax
        int 20h
        dd (VMM_DEVICE_ID shl 16) + 0       ; Get_VMM_Version
        movzx   eax, ax
        pop     ecx
        pop     ebx
        ret
_vxd_vjoyd_version endp

; void __cdecl vxd_vjoyd_register_dx5(void *VJREGDRVINFO)
_vxd_vjoyd_register_dx5 proc near
        pushad
        mov     eax, 0FFFFFFFFh         ; as MSANALOG.VXD does it (Windows 98):
        mov     edx, [esp+36]           ; EDX -> VJREGDRVINFO, EBX = ECX = 0
        xor     ebx, ebx
        xor     ecx, ecx
        int 20h
        dd (VJOYD_DEVICE_ID shl 16) + @VJOYD_Register
        popad
        ret
_vxd_vjoyd_register_dx5 endp

; void __cdecl vxd_vjoyd_register_old(poll, cfg, hwcaps, joyid)
_vxd_vjoyd_register_old proc near
        pushad
        mov     eax, [esp+36]
        mov     ebx, [esp+40]
        mov     ecx, [esp+44]
        mov     edx, [esp+48]
        int 20h
        dd (VJOYD_DEVICE_ID shl 16) + @VJOYD_Register
        popad
        ret
_vxd_vjoyd_register_old endp

_LTEXT  ends
        end
