; sc55w.asm - SC55W.VXD shell: device declaration, control dispatch,
; I/O trap stub, timer, IRQ and DMA glue, and cdecl wrappers around the
; VMM/VxD services used by the C core (sbcore.c).
;
; Dynamic VxD, loaded by SC55W.EXE with CreateFile("\\.\SC55W.VXD").
; Assemble with JWasm; link with wlink "system win_vxd dynamic".

        .386p
        .model flat
        option casemap:none
        include vxd98.inc

; services not in vxd98.inc
@_LinPageLock               equ 0063h
@Hook_V86_Int_Chain         equ 0041h
@Unhook_V86_Int_Chain       equ 0118h
@_LinPageUnLock             equ 0064h
@Begin_Nest_V86_Exec        equ 0082h
@Exec_Int                   equ 0084h
@End_Nest_Exec              equ 0086h
@Save_Client_State          equ 008Dh
@Restore_Client_State       equ 008Eh
VKD_DEVICE_ID               equ 000Dh
@VKD_Force_Keys             equ 0007h
@VKD_Get_Kbd_Owner          equ 0008h
Create_VM_Msg               equ 0007h
Destroy_VM_Msg              equ 000Ch
@VTD_Get_Real_Time          equ 0007h
@VTD_Begin_Min_Int_Period   equ 0003h
@VTD_End_Min_Int_Period     equ 0004h
@VDMAD_Set_Channel_Callbacks equ 001Fh
VDMAD_Callbacks_ReadNotify  equ 0001h
PAGEMAPGLOBAL               equ 40000000h

; C core
extern _sc_in:near, _sc_out:near, _sc_tick:near, _sc_dma_changed:near
extern _sc_eoi:near, _sc_ioctl:near, _sc_exit:near, _sc_api_midi:near
extern _sc_int15:near, _sc_api_volume:near
extern _sc_int21:near, _sc_vm_gone:near

public _vxd_time_us, _vxd_muldiv, _vxd_raise_irq, _vxd_lower_irq
public _vxd_dma_get, _vxd_dma_set, _vxd_timer_arm
public _vxd_install_io, _vxd_remove_io, _vxd_virt_irq, _vxd_unvirt_irq
public _vxd_virt_dma, _vxd_unvirt_dma, _vxd_map_block, _vxd_unmap_block
public _vxd_min_period
public _vxd_sys_vm, _vxd_port_in, _vxd_port_out, _vxd_hook_int15
public _vxd_hook_int21, _vxd_kbd_owner, _vxd_force_keys
public _vxd_nest_begin, _vxd_nest_int21, _vxd_nest_end
public SC55W_DDB

_LDATA  segment dword public 'LCODE'

SC55W_DDB VxD_Desc_Block <0, DDK_VERSION, UNDEFINED_DEVICE_ID, 1, 0, 0, \
        <'S','C','5','5','W',' ',' ',' '>, UNDEFINED_INIT_ORDER, offset SC55W_Control, \
        0, offset PM_API, 0, 0, 0, 0, 0, 0, 'Prev', sizeof VxD_Desc_Block, 'Rsv1', 'Rsv2', 'Rsv3'>

irq_desc VPICD_IRQ_Descriptor <0, VPICD_OPT_CAN_SHARE, offset Hw_Int, 0, offset EOI_Proc, 0, 0, 500, 0>

irq_handle   dd 0
dma_handle   dd 0, 0
timer_handle dd 0
period_on    dd 0
map_pages    dd 0

_LDATA  ends

_LTEXT  segment dword public 'LCODE'
        assume cs:flat, ds:flat, es:flat, ss:flat

;---------------------------------------------------------------------------
; control dispatch: EAX = message
;---------------------------------------------------------------------------
SC55W_Control proc near
        cmp     eax, W32_DEVICEIOCONTROL
        je      ctl_ioctl
        cmp     eax, Sys_Dynamic_Device_Exit
        je      ctl_exit
        cmp     eax, Create_VM_Msg
        je      ctl_vm
        cmp     eax, Destroy_VM_Msg
        je      ctl_vm
        clc
        ret
ctl_vm:                                 ; forget a VM's program list (EBX = VM)
        pushad
        push    ebx
        call    _sc_vm_gone
        add     esp, 4
        popad
        clc
        ret
ctl_exit:
        pushad
        call    _sc_exit
        xor     esi, esi
        xchg    esi, timer_handle
        or      esi, esi
        jz      @F
        int 20h
        dd (VMM_DEVICE_ID shl 16) + @Cancel_Time_Out
@@:     popad
        clc
        ret

ctl_ioctl:
        ; ESI -> DIOCParams
        push    ebx
        push    esi
        push    edi
        push    ebp
        mov     ebp, esi
        push    [ebp].DIOCParams.cbOutBuffer
        push    [ebp].DIOCParams.lpvOutBuffer
        push    [ebp].DIOCParams.cbInBuffer
        push    [ebp].DIOCParams.lpvInBuffer
        push    [ebp].DIOCParams.dwIoControlCode
        call    _sc_ioctl
        add     esp, 20
        or      eax, eax
        js      ioctl_bad
        mov     edi, [ebp].DIOCParams.lpcbBytesReturned
        or      edi, edi
        jz      @F
        mov     [edi], eax
@@:     xor     eax, eax
        jmp     ioctl_done
ioctl_bad:
        mov     eax, 1                  ; ERROR_INVALID_FUNCTION
ioctl_done:
        pop     ebp
        pop     edi
        pop     esi
        pop     ebx
        clc
        ret
SC55W_Control endp

;---------------------------------------------------------------------------
; protected-mode API for the 16-bit MIDI driver (INT 2Fh AX=1684h by name)
;   client AX = 0001h: send CX bytes (1-3) from DL, DH, BL
;   client AX = 0000h: version -> AX = 0100h
;---------------------------------------------------------------------------
PM_API proc near
        movzx   eax, word ptr [ebp+Client_EAX]
        cmp     eax, 1
        je      api_send
        cmp     eax, 2
        je      api_volume
        or      eax, eax
        jne     api_bad
        mov     word ptr [ebp+Client_EAX], 0100h
        and     dword ptr [ebp+Client_EFlags], not 1
        ret
api_send:
        movzx   ecx, word ptr [ebp+Client_ECX]
        cmp     ecx, 3
        ja      api_bad
        jecxz   api_ok
        movzx   eax, byte ptr [ebp+Client_EDX]
        push    ecx
        push    eax
        call    _sc_api_midi
        add     esp, 4
        pop     ecx
        dec     ecx
        jz      api_ok
        movzx   eax, byte ptr [ebp+Client_EDX+1]
        push    ecx
        push    eax
        call    _sc_api_midi
        add     esp, 4
        pop     ecx
        dec     ecx
        jz      api_ok
        movzx   eax, byte ptr [ebp+Client_EBX]
        push    eax
        call    _sc_api_midi
        add     esp, 4
api_ok:
        and     dword ptr [ebp+Client_EFlags], not 1
        ret
api_volume:                             ; AX=2: MIDI volume, DX = left, CX = right
        movzx   eax, word ptr [ebp+Client_ECX]
        shl     eax, 16
        mov     ax, word ptr [ebp+Client_EDX]
        push    eax
        call    _sc_api_volume
        add     esp, 4
        jmp     api_ok
api_bad:
        or      dword ptr [ebp+Client_EFlags], 1
        ret
PM_API endp

;---------------------------------------------------------------------------
; I/O trap: EBX = VM, ECX = I/O type, EDX = port, EAX = data (output)
;---------------------------------------------------------------------------
IO_Stub proc near
        cmp     ecx, Byte_Output
        je      io_out
        cmp     ecx, Byte_Input
        je      io_in
        int 20h                                     ; word/dword/string I/O:
        dd (VMM_DEVICE_ID shl 16) + @Simulate_IO + 8000h   ; split into bytes
io_in:
        push    edx
        push    ebx
        call    _sc_in
        add     esp, 8
        ret
io_out:
        push    eax
        push    edx
        push    ebx
        call    _sc_out
        add     esp, 12
        ret
IO_Stub endp

;---------------------------------------------------------------------------
; time-out (event time): run the transfer engine
;---------------------------------------------------------------------------
Timer_CB proc near
        mov     timer_handle, 0
        pushad
        call    _sc_tick
        popad
        ret
Timer_CB endp

;---------------------------------------------------------------------------
; VPICD callbacks
;---------------------------------------------------------------------------
Hw_Int proc near
        stc                             ; never ours: let other sharers have it
        ret
Hw_Int endp

EOI_Proc proc near                      ; EAX = IRQ handle, EBX = VM
        pushad
        push    ebx
        call    _sc_eoi
        add     esp, 4
        popad
        ret
EOI_Proc endp

;---------------------------------------------------------------------------
; VDMAD callbacks: EAX = DMA handle, EBX = VM
;---------------------------------------------------------------------------
DMA_CB0 proc near
        pushad
        push    edx                     ; 1/2/3 = count/page/address read, else a change
        push    ebx
        push    0
        call    _sc_dma_changed
        add     esp, 12
        popad
        ret
DMA_CB0 endp

DMA_CB1 proc near
        pushad
        push    edx                     ; 1/2/3 = count/page/address read, else a change
        push    ebx
        push    1
        call    _sc_dma_changed
        add     esp, 12
        popad
        ret
DMA_CB1 endp

;===========================================================================
; cdecl wrappers for the C core. EBX, ESI, EDI, EBP are preserved.
;===========================================================================

; u32 vxd_time_us(void): VTD real time (1.193182 MHz ticks) -> microseconds
_vxd_time_us proc near
        push    ebx
        push    esi
        push    edi
        int 20h
        dd (VTD_DEVICE_ID shl 16) + @VTD_Get_Real_Time
        ; EDX:EAX = ticks. 96-bit product ticks * 1000000 in EDI:EDX:EAX
        mov     ecx, eax
        mov     eax, edx
        mov     ebx, 1000000
        mul     ebx
        mov     esi, eax
        mov     edi, edx
        mov     eax, ecx
        mul     ebx
        add     edx, esi
        adc     edi, 0
        ; divide by 1193182, keep the low 32 bits of the quotient
        mov     ebx, 1193182
        push    eax
        mov     ecx, edx
        xor     edx, edx
        mov     eax, edi
        div     ebx
        mov     eax, ecx
        div     ebx
        pop     eax
        div     ebx
        pop     edi
        pop     esi
        pop     ebx
        ret
_vxd_time_us endp

; u32 vxd_muldiv(u32 a, u32 b, u32 c) = a*b/c (saturates)
_vxd_muldiv proc near
        mov     eax, [esp+4]
        mul     dword ptr [esp+8]
        mov     ecx, [esp+12]
        jecxz   md_sat
        cmp     edx, ecx
        jae     md_sat
        div     ecx
        ret
md_sat: mov     eax, 0FFFFFFFFh
        ret
_vxd_muldiv endp

; helper: EBX = VM from [esp+arg] or current VM if 0
get_vm macro arg
        mov     ebx, arg
        or      ebx, ebx
        jnz     @F
        int 20h
        dd (VMM_DEVICE_ID shl 16) + @Get_Cur_VM_Handle
@@:
endm

; void vxd_raise_irq(u32 vm)
_vxd_raise_irq proc near
        push    ebx
        mov     eax, irq_handle
        or      eax, eax
        jz      ri_done
        get_vm  [esp+8]
        mov     eax, irq_handle
        int 20h
        dd (VPICD_DEVICE_ID shl 16) + @VPICD_Set_Int_Request
ri_done:
        pop     ebx
        ret
_vxd_raise_irq endp

; void vxd_lower_irq(u32 vm)
_vxd_lower_irq proc near
        push    ebx
        mov     eax, irq_handle
        or      eax, eax
        jz      li_done
        get_vm  [esp+8]
        mov     eax, irq_handle
        int 20h
        dd (VPICD_DEVICE_ID shl 16) + @VPICD_Clear_Int_Request
li_done:
        pop     ebx
        ret
_vxd_lower_irq endp

; int vxd_dma_get(int idx, u32 vm, u32 *addr, u32 *count, u32 *mode)
_vxd_dma_get proc near
        push    ebx
        push    esi
        push    edi
        mov     eax, [esp+16]
        mov     eax, dma_handle[eax*4]
        or      eax, eax
        jz      dg_fail
        get_vm  [esp+20]
        mov     edi, [esp+16]
        mov     eax, dma_handle[edi*4]
        int 20h
        dd (VDMAD_DEVICE_ID shl 16) + @VDMAD_Get_Virt_State
        mov     edi, [esp+24]
        mov     [edi], esi
        mov     edi, [esp+28]
        mov     [edi], ecx
        mov     edi, [esp+32]
        movzx   edx, dx                 ; DL = mode, DH = extended mode (16-bit flag)
        mov     [edi], edx
        mov     eax, 1
        jmp     dg_done
dg_fail:
        xor     eax, eax
dg_done:
        pop     edi
        pop     esi
        pop     ebx
        ret
_vxd_dma_get endp

; void vxd_dma_set(int idx, u32 vm, u32 addr, u32 count, u32 mode)
_vxd_dma_set proc near
        push    ebx
        push    esi
        push    edi
        mov     eax, [esp+16]
        mov     eax, dma_handle[eax*4]
        or      eax, eax
        jz      ds_done
        get_vm  [esp+20]
        mov     edi, [esp+16]
        mov     eax, dma_handle[edi*4]
        mov     esi, [esp+24]
        mov     ecx, [esp+28]
        mov     edx, [esp+32]
        and     edx, 0FFFFh             ; keep DH: VDMAD halves the count for word channels
        int 20h
        dd (VDMAD_DEVICE_ID shl 16) + @VDMAD_Set_Virt_State
ds_done:
        pop     edi
        pop     esi
        pop     ebx
        ret
_vxd_dma_set endp

; void vxd_timer_arm(u32 ms) - one outstanding global time-out at most
_vxd_timer_arm proc near
        cmp     timer_handle, 0
        jne     ta_done
        push    esi
        mov     eax, [esp+8]
        xor     edx, edx
        mov     esi, offset Timer_CB
        int 20h
        dd (VMM_DEVICE_ID shl 16) + @Set_Global_Time_Out
        mov     timer_handle, esi
        pop     esi
ta_done:
        ret
_vxd_timer_arm endp

; int vxd_install_io(u32 port): 0 = ok
_vxd_install_io proc near
        push    esi
        mov     edx, [esp+8]
        mov     esi, offset IO_Stub
        int 20h
        dd (VMM_DEVICE_ID shl 16) + @Install_IO_Handler
        sbb     eax, eax
        neg     eax
        pop     esi
        ret
_vxd_install_io endp

; void vxd_remove_io(u32 port)
_vxd_remove_io proc near
        mov     edx, [esp+4]
        int 20h
        dd (VMM_DEVICE_ID shl 16) + @Remove_IO_Handler
        ret
_vxd_remove_io endp

; int vxd_virt_irq(u32 irq): 0 = ok
_vxd_virt_irq proc near
        push    edi
        mov     eax, [esp+8]
        mov     irq_desc.VID_IRQ_Number, ax
        mov     edi, offset irq_desc
        int 20h
        dd (VPICD_DEVICE_ID shl 16) + @VPICD_Virtualize_IRQ
        jc      vi_fail
        mov     irq_handle, eax
        xor     eax, eax
        pop     edi
        ret
vi_fail:
        mov     eax, 1
        pop     edi
        ret
_vxd_virt_irq endp

; void vxd_unvirt_irq(void)
_vxd_unvirt_irq proc near
        xor     eax, eax
        xchg    eax, irq_handle
        or      eax, eax
        jz      @F
        int 20h
        dd (VPICD_DEVICE_ID shl 16) + @VPICD_Force_Default_Behavior
@@:     ret
_vxd_unvirt_irq endp

; int vxd_virt_dma(int idx, u32 chan): 0 = ok
_vxd_virt_dma proc near
        push    esi
        mov     eax, [esp+12]
        mov     esi, offset DMA_CB0
        cmp     dword ptr [esp+8], 0
        je      @F
        mov     esi, offset DMA_CB1
@@:     int 20h
        dd (VDMAD_DEVICE_ID shl 16) + @VDMAD_Virtualize_Channel
        jc      vd_fail
        mov     ecx, [esp+8]
        mov     dma_handle[ecx*4], eax
        push    ebx                     ; ask for count/address read notifications
        mov     ebx, 0FFFFh
        mov     ecx, VDMAD_Callbacks_ReadNotify
        int 20h
        dd (VDMAD_DEVICE_ID shl 16) + @VDMAD_Set_Channel_Callbacks
        pop     ebx
        xor     eax, eax
        pop     esi
        ret
vd_fail:
        mov     eax, 1
        pop     esi
        ret
_vxd_virt_dma endp

; void vxd_unvirt_dma(int idx)
_vxd_unvirt_dma proc near
        mov     ecx, [esp+4]
        xor     eax, eax
        xchg    eax, dma_handle[ecx*4]
        or      eax, eax
        jz      @F
        int 20h
        dd (VDMAD_DEVICE_ID shl 16) + @VDMAD_Unvirtualize_Channel
@@:     ret
_vxd_unvirt_dma endp

; u32 vxd_map_block(u32 addr, u32 size): lock + global alias of the
; caller's buffer (must be called in the caller's context: DeviceIoControl)
_vxd_map_block proc near
        mov     eax, [esp+4]
        and     eax, 0FFFh
        add     eax, [esp+8]
        add     eax, 0FFFh
        shr     eax, 12                 ; pages
        mov     map_pages, eax
        push    PAGEMAPGLOBAL
        push    eax
        mov     eax, [esp+12]
        shr     eax, 12
        push    eax
        int 20h
        dd (VMM_DEVICE_ID shl 16) + @_LinPageLock
        add     esp, 12
        or      eax, eax
        jz      mb_done
        mov     ecx, [esp+4]
        and     ecx, 0FFFh
        add     eax, ecx
mb_done:
        ret
_vxd_map_block endp

; void vxd_unmap_block(u32 alias, u32 size)
_vxd_unmap_block proc near
        push    PAGEMAPGLOBAL
        push    map_pages
        mov     eax, [esp+12]
        shr     eax, 12
        push    eax
        int 20h
        dd (VMM_DEVICE_ID shl 16) + @_LinPageUnLock
        add     esp, 12
        ret
_vxd_unmap_block endp

;---------------------------------------------------------------------------
; INT 15h hook (V86 chain), for AH=84h (BIOS joystick) in DOS boxes.
; EAX = 15h, EBX = VM, EBP = client registers. CF clear = handled.
;---------------------------------------------------------------------------
Int15_Hook proc near
        cmp     byte ptr [ebp+Client_EAX+1], 84h
        jne     i15_pass
        pushad
        push    ebp
        push    ebx
        call    _sc_int15
        add     esp, 8
        or      eax, eax                ; popad keeps the flags
        popad
        jz      i15_pass
        clc
        ret
i15_pass:
        stc
        ret
Int15_Hook endp

; int vxd_hook_int15(int on): 0 = ok
_vxd_hook_int15 proc near
        push    esi
        mov     eax, 15h
        mov     esi, offset Int15_Hook
        cmp     dword ptr [esp+8], 0
        je      i15_off
        int 20h
        dd (VMM_DEVICE_ID shl 16) + @Hook_V86_Int_Chain
        jmp     i15_ret
i15_off:
        int 20h
        dd (VMM_DEVICE_ID shl 16) + @Unhook_V86_Int_Chain
i15_ret:
        sbb     eax, eax
        neg     eax
        pop     esi
        ret
_vxd_hook_int15 endp

;---------------------------------------------------------------------------
; INT 21h hook (V86 chain): notes which program each DOS box runs (AX=4B00h
; EXEC; AH=4Ch / 31h / 00h end one), so the helper can pick the game pad
; layout. Never handles the call itself (always passes it on).
;---------------------------------------------------------------------------
Int21_Hook proc near
        cmp     byte ptr [ebp+Client_EAX+1], 4Bh
        je      i21_note
        cmp     byte ptr [ebp+Client_EAX+1], 4Ch
        je      i21_note
        cmp     byte ptr [ebp+Client_EAX+1], 31h
        je      i21_note
        cmp     byte ptr [ebp+Client_EAX+1], 0
        jne     i21_pass
i21_note:
        pushad
        push    ebp
        push    ebx
        call    _sc_int21
        add     esp, 8
        popad
i21_pass:
        stc
        ret
Int21_Hook endp

; int vxd_hook_int21(int on): 0 = ok
_vxd_hook_int21 proc near
        push    esi
        mov     eax, 21h
        mov     esi, offset Int21_Hook
        cmp     dword ptr [esp+8], 0
        je      i21_off
        int 20h
        dd (VMM_DEVICE_ID shl 16) + @Hook_V86_Int_Chain
        jmp     i21_ret
i21_off:
        int 20h
        dd (VMM_DEVICE_ID shl 16) + @Unhook_V86_Int_Chain
i21_ret:
        sbb     eax, eax
        neg     eax
        pop     esi
        ret
_vxd_hook_int21 endp

; nested INT 21h calls in the current VM, from inside the INT 21h hook:
;   vxd_nest_begin(save) ... vxd_nest_int21() (client registers in, out) ... vxd_nest_end(save)
; save = buffer for the client state (256 bytes)
_vxd_nest_begin proc near
        push    edi
        mov     edi, [esp+8]
        int 20h
        dd (VMM_DEVICE_ID shl 16) + @Save_Client_State
        int 20h
        dd (VMM_DEVICE_ID shl 16) + @Begin_Nest_V86_Exec
        pop     edi
        ret
_vxd_nest_begin endp

_vxd_nest_int21 proc near
        mov     eax, 21h
        int 20h
        dd (VMM_DEVICE_ID shl 16) + @Exec_Int
        ret
_vxd_nest_int21 endp

_vxd_nest_end proc near
        push    esi
        int 20h
        dd (VMM_DEVICE_ID shl 16) + @End_Nest_Exec
        mov     esi, [esp+8]
        int 20h
        dd (VMM_DEVICE_ID shl 16) + @Restore_Client_State
        pop     esi
        ret
_vxd_nest_end endp

; u32 vxd_kbd_owner(void): the VM that has the keyboard
_vxd_kbd_owner proc near
        push    ebx
        int 20h
        dd (VKD_DEVICE_ID shl 16) + @VKD_Get_Kbd_Owner
        mov     eax, ebx
        pop     ebx
        ret
_vxd_kbd_owner endp

; int vxd_force_keys(const u8 *codes, u32 n): scan codes into the keyboard input; 0 = ok
_vxd_force_keys proc near
        push    esi
        mov     esi, [esp+8]
        mov     ecx, [esp+12]
        int 20h
        dd (VKD_DEVICE_ID shl 16) + @VKD_Force_Keys
        sbb     eax, eax
        neg     eax
        pop     esi
        ret
_vxd_force_keys endp

; u32 vxd_sys_vm(void)
_vxd_sys_vm proc near
        push    ebx
        int 20h
        dd (VMM_DEVICE_ID shl 16) + @Get_Sys_VM_Handle
        mov     eax, ebx
        pop     ebx
        ret
_vxd_sys_vm endp

; u32 vxd_port_in(u32 port) / void vxd_port_out(u32 port, u32 val): the real port
_vxd_port_in proc near
        mov     edx, [esp+4]
        xor     eax, eax
        in      al, dx
        ret
_vxd_port_in endp

_vxd_port_out proc near
        mov     edx, [esp+4]
        mov     eax, [esp+8]
        out     dx, al
        ret
_vxd_port_out endp

; void vxd_min_period(int on): 1 ms timer resolution while attached
_vxd_min_period proc near
        mov     ecx, [esp+4]
        or      ecx, ecx
        jz      mp_off
        cmp     period_on, 0
        jne     mp_done
        mov     eax, 1
        int 20h
        dd (VTD_DEVICE_ID shl 16) + @VTD_Begin_Min_Int_Period
        jc      mp_done
        mov     period_on, 1
        jmp     mp_done
mp_off: cmp     period_on, 0
        je      mp_done
        mov     eax, 1
        int 20h
        dd (VTD_DEVICE_ID shl 16) + @VTD_End_Min_Int_Period
        mov     period_on, 0
mp_done:
        ret
_vxd_min_period endp

_LTEXT  ends
        end
