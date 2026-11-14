; SBIRQ - time from DSP 14h (128 bytes, 4000 Hz, 8-bit single-cycle) to IRQ 7, like God of Thunder's check
; prints: ms (TSC, calibrated on the BIOS clock). Real SB: ~32 ms.
cpu 586
org 100h
start:
  mov si,81h
  xor ebx,ebx
.ph: lodsb
  cmp al,' '
  je .ph
  cmp al,13
  je .pd
  or al,20h
  sub al,'0'
  cmp al,9
  jbe .pn
  sub al,'a'-'0'-10
.pn: shl ebx,4
  movzx eax,al
  or ebx,eax
  jmp .ph
.pd: mov [want],ebx
  mov ah,4Ah          ; shrink to 4 KB, then grab the biggest block
  mov bx,100h
  push cs
  pop es
  int 21h
  mov ah,48h
  mov bx,0FFFFh
  int 21h
  mov ah,48h
  int 21h
  jc nomem
  movzx eax,ax
  shl eax,4
  mov [blo],eax
  movzx ebx,bx
  shl ebx,4
  add eax,ebx
  mov [bhi],eax
  ; calibrate TSC: cycles in 9 BIOS ticks
  push ds
  xor ax,ax
  mov es,ax
  mov eax,[es:46Ch]
.w0: cmp eax,[es:46Ch]
  je .w0
  rdtsc
  mov [t0],eax
  mov [t0+4],edx
  mov ebx,[es:46Ch]
  add ebx,9
.w1: cmp ebx,[es:46Ch]
  ja .w1
  rdtsc
  sub eax,[t0]
  sbb edx,[t0+4]
  ; cycles per ms = cyc / (9*54.9254)= cyc*1000/494329
  mov ecx,494329
  div ecx           ; eax = cycles / 494.329 ms*1000 ... eax = cyc/494329
  imul eax,eax,1000
  mov [cpms],eax
  pop ds
  ; hook INT 0Fh
  mov ax,350Fh
  int 21h
  mov [oldo],bx
  mov [olds],es
  mov ax,250Fh
  mov dx,isr
  int 21h
  in al,21h
  mov [oldmask],al
  and al,7Fh
  out 21h,al
  ; DSP reset
  mov dx,226h
  mov al,1
  out dx,al
  mov cx,100
.d: in al,dx
  loop .d
  xor al,al
  out dx,al
  mov cx,0FFFFh
.r: mov dx,22Eh
  in al,dx
  test al,80h
  jnz .rd
  loop .r
.rd: mov dx,22Ah
  in al,dx
  mov al,0D1h
  call dspw
  mov al,40h
  call dspw
  mov al,06h
  call dspw
  ; DMA buffer: phys of buf, 128 bytes
  mov ax,ds
  movzx eax,ax
  shl eax,4
  mov eax,[want]
  cmp eax,[blo]
  jb nomem
  add eax,80h
  cmp eax,[bhi]
  ja nomem
  sub eax,80h
  mov [phys],eax
  mov al,5
  out 0Ah,al
  xor al,al
  out 0Ch,al
  mov al,49h
  out 0Bh,al
  mov eax,[phys]
  out 02h,al
  mov al,ah
  out 02h,al
  shr eax,16
  out 83h,al
  mov al,7Fh
  out 03h,al
  xor al,al
  out 03h,al
  mov al,1
  out 0Ah,al
  mov byte [flag],0
  rdtsc
  mov [t0],eax
  mov [t0+4],edx
  mov al,14h
  call dspw
  mov al,7Fh
  call dspw
  xor al,al
  call dspw
  ; wait for the IRQ, up to 2 s
.wt: cmp byte [flag],0
  jne .got
  rdtsc
  sub eax,[t0]
  sbb edx,[t0+4]
  div dword [cpms]
  cmp eax,2000
  jb .wt
  mov dx,msgno
  mov ah,9
  int 21h
  jmp .done
.got:
  mov eax,[t1]
  mov edx,[t1+4]
  sub eax,[t0]
  sbb edx,[t0+4]
  div dword [cpms]
  call pdec
  mov dx,msgms
  mov ah,9
  int 21h
.done:
  mov al,[oldmask]
  out 21h,al
  push ds
  mov dx,[oldo]
  mov ds,[olds]
  mov ax,250Fh
  int 21h
  pop ds
  mov ax,4C00h
  int 21h

dspw:
  mov ah,al
  mov dx,22Ch
.w: in al,dx
  test al,80h
  jnz .w
  mov al,ah
  out dx,al
  ret

isr:
  push eax
  push edx
  rdtsc
  mov [cs:t1],eax
  mov [cs:t1+4],edx
  mov dx,22Eh
  in al,dx
  mov byte [cs:flag],1
  mov al,20h
  out 20h,al
  pop edx
  pop eax
  iret

pdec:          ; eax decimal
  mov ebx,10
  xor cx,cx
.a: xor edx,edx
  div ebx
  push dx
  inc cx
  test eax,eax
  jnz .a
.b: pop dx
  add dl,'0'
  mov ah,2
  int 21h
  loop .b
  ret

nomem:
  mov dx,msgmem
  mov ah,9
  int 21h
  mov ax,4C01h
  int 21h
want dd 0
blo dd 0
bhi dd 0
msgmem db 'address not in free memory',13,10,'$'
msgno db 'no IRQ in 2000 ms',13,10,'$'
msgms db ' ms to IRQ 7',13,10,'$'
flag db 0
oldmask db 0
oldo dw 0
olds dw 0
cpms dd 0
phys dd 0
t0 dd 0,0
t1 dd 0,0
align 256
buf_p:
  times 128 db 80h
