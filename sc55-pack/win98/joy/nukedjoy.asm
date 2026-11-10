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
@VJOYD_Register     equ 0001h       ; EAX poll, EBX cfg, ECX hw caps, EDX joystick id
@Get_DDB            equ 0146h       ; EAX device id, EDI name -> ECX DDB or 0

extern _nj_poll@8:near, _nj_hwcaps@8:near, _nj_joyid@8:near
public _vxd_get_ddb, NUKEDJOY_DDB

_LDATA  segment dword public 'LCODE'
NUKEDJOY_DDB VxD_Desc_Block <0, DDK_VERSION, UNDEFINED_DEVICE_ID, 1, 0, 0, \
        <'N','U','K','E','D','J','O','Y'>, UNDEFINED_INIT_ORDER, offset NJ_Control, \
        0, 0, 0, 0, 0, 0, 0, 0, 'Prev', sizeof VxD_Desc_Block, 'Rsv1', 'Rsv2', 'Rsv3'>
registered dd 0
_LDATA  ends

_LTEXT  segment dword public 'LCODE'
        assume cs:flat, ds:flat, es:flat, ss:flat

NJ_Control proc near
        cmp     eax, Sys_Dynamic_Device_Init
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
        mov     eax, offset _nj_poll@8
        xor     ebx, ebx
        mov     ecx, offset _nj_hwcaps@8
        mov     edx, offset _nj_joyid@8
        int 20h
        dd (VJOYD_DEVICE_ID shl 16) + @VJOYD_Register
        mov     registered, 1
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

_LTEXT  ends
        end
