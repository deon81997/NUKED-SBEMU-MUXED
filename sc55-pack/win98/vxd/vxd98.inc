; vxd98.inc - minimal Windows 98 VxD definitions for JWasm (flat model).
; Numbers and layouts taken from the Windows 98 DDK (vmm.inc, vpicd.inc,
; vdmad.inc, vwin32.inc, shell.inc) and cross-checked against hte's tables.

; ---- device ids ----
VMM_DEVICE_ID      equ 0001h
VPICD_DEVICE_ID    equ 0003h
VDMAD_DEVICE_ID    equ 0004h
VTD_DEVICE_ID      equ 0005h
SHELL_DEVICE_ID    equ 0017h
VWIN32_DEVICE_ID   equ 002Ah
UNDEFINED_DEVICE_ID  equ 0
UNDEFINED_INIT_ORDER equ 80000000h
DDK_VERSION        equ 40Ah

; ---- control messages ----
Sys_Critical_Init        equ 00h
Device_Init              equ 01h
Init_Complete            equ 02h
Sys_VM_Init              equ 03h
Sys_VM_Terminate         equ 04h
System_Exit              equ 05h
Sys_Critical_Exit        equ 06h
Create_VM                equ 07h
VM_Critical_Init         equ 08h
VM_Init                  equ 09h
VM_Terminate             equ 0Ah
VM_Not_Executeable       equ 0Bh
Destroy_VM               equ 0Ch
Sys_Dynamic_Device_Init  equ 1Bh
Sys_Dynamic_Device_Exit  equ 1Ch
W32_DEVICEIOCONTROL      equ 23h

; ---- I/O trap types (ECX in an I/O handler) ----
Byte_Input   equ 000h
Byte_Output  equ 004h

; ---- VDMAD mode flags (DL from Get_Virt_State) ----
DMA_masked     equ 01h
DMA_type_verify equ 00h
DMA_type_write equ 04h
DMA_type_read  equ 08h
DMA_AutoInit   equ 10h
DMA_AdrDec     equ 20h

; ---- service call macros: INT 20h + dword (device<<16 | service) ----
VxDCall_ macro dev, svc
        int 20h
        dd (dev shl 16) + svc
endm
VxDJmp_ macro dev, svc
        int 20h
        dd (dev shl 16) + svc + 8000h
endm

; VMM
@Get_Cur_VM_Handle      equ 0001h
@Get_Sys_VM_Handle      equ 0003h
@Schedule_Global_Event  equ 000Eh
@Call_Global_Event      equ 0010h
@Cancel_Global_Event    equ 0012h
@Call_Priority_VM_Event equ 0014h
@Set_Global_Time_Out    equ 003Ch
@Cancel_Time_Out        equ 003Eh
@Get_System_Time        equ 003Fh
@_HeapAllocate          equ 004Fh
@_HeapFree              equ 0051h
@_PageAllocate          equ 0053h
@_PageFree              equ 0055h
@Simulate_IO            equ 0094h
@Install_IO_Handler     equ 0096h
@Enable_Global_Trapping equ 0097h
@Disable_Global_Trapping equ 0099h
@Out_Debug_String       equ 00C2h
@Get_Last_Updated_System_Time equ 00CFh
@Set_Async_Time_Out     equ 00FEh
@Remove_IO_Handler      equ 0116h
; VPICD
@VPICD_Virtualize_IRQ   equ 0001h
@VPICD_Set_Int_Request  equ 0002h
@VPICD_Clear_Int_Request equ 0003h
@VPICD_Phys_EOI         equ 0004h
@VPICD_Get_Status       equ 0006h
@VPICD_Force_Default_Behavior equ 0011h
; VDMAD
@VDMAD_Virtualize_Channel   equ 0001h
@VDMAD_Get_Virt_State       equ 0004h
@VDMAD_Set_Virt_State       equ 0005h
@VDMAD_Default_Handler      equ 0012h
@VDMAD_Unvirtualize_Channel equ 001Ah
; VWIN32
@_VWIN32_SetWin32Event   equ 000Eh
@_VWIN32_CloseVxDHandle  equ 0014h
; SHELL
@SHELL_SYSMODAL_Message  equ 0003h

; ---- DDB (VxD_Desc_Block) ----
VxD_Desc_Block struct
DDB_Next                dd ?
DDB_SDK_Version         dw ?
DDB_Req_Device_Number   dw ?
DDB_Dev_Major_Version   db ?
DDB_Dev_Minor_Version   db ?
DDB_Flags               dw ?
DDB_Name                db 8 dup (?)
DDB_Init_Order          dd ?
DDB_Control_Proc        dd ?
DDB_V86_API_Proc        dd ?
DDB_PM_API_Proc         dd ?
DDB_V86_API_CSIP        dd ?
DDB_PM_API_CSIP         dd ?
DDB_Reference_Data      dd ?
DDB_Service_Table_Ptr   dd ?
DDB_Service_Table_Size  dd ?
DDB_Win32_Service_Table dd ?
DDB_Prev                dd ?
DDB_Size                dd ?
DDB_Reserved1           dd ?
DDB_Reserved2           dd ?
DDB_Reserved3           dd ?
VxD_Desc_Block ends

; ---- DeviceIoControl parameter block (ESI on W32_DEVICEIOCONTROL) ----
DIOCParams struct
Internal1         dd ?
VMHandle          dd ?
Internal2         dd ?
dwIoControlCode   dd ?
lpvInBuffer       dd ?
cbInBuffer        dd ?
lpvOutBuffer      dd ?
cbOutBuffer       dd ?
lpcbBytesReturned dd ?
lpoOverlapped     dd ?
hDevice           dd ?
tagProcess        dd ?
DIOCParams ends

; ---- VPICD IRQ descriptor ----
VPICD_IRQ_Descriptor struct
VID_IRQ_Number      dw ?
VID_Options         dw ?
VID_Hw_Int_Proc     dd ?
VID_Virt_Int_Proc   dd ?
VID_EOI_Proc        dd ?
VID_Mask_Change_Proc dd ?
VID_IRET_Proc       dd ?
VID_IRET_Time_Out   dd ?
VID_Hw_Int_Ref      dd ?
VPICD_IRQ_Descriptor ends
VPICD_OPT_READ_HW_IRR   equ 01h
VPICD_OPT_CAN_SHARE     equ 02h
VPICD_OPT_REF_DATA      equ 04h
VPICD_OPT_VIRT_INT_REJECT equ 10h
VPICD_OPT_SHARE_PMODE_ONLY equ 20h

; ---- client register structure offsets (EBP in handlers) ----
Client_EDI   equ 00h
Client_ESI   equ 04h
Client_EBP   equ 08h
Client_EBX   equ 10h
Client_EDX   equ 14h
Client_ECX   equ 18h
Client_EAX   equ 1Ch
Client_EFlags equ 2Ch
