; ESSTAT: find the ES1371/ES1373 (1274:xxxx) on PCI bus 0, print BAR0, CONTROL, STATUS, SERIAL CONTROL, DAC1 frame
cpu 386
org 100h
  xor bx,bx            ; device*8
.d: mov eax,80000000h
  movzx ecx,bx
  shl ecx,8
  or eax,ecx
  mov dx,0CF8h
  out dx,eax
  mov dx,0CFCh
  in eax,dx
  cmp ax,1274h
  je .f
  add bx,8
  cmp bx,256
  jb .d
  mov dx,nf
  mov ah,9
  int 21h
  jmp .x
.f: mov eax,80000010h
  movzx ecx,bx
  shl ecx,8
  or eax,ecx
  mov dx,0CF8h
  out dx,eax
  mov dx,0CFCh
  in eax,dx
  and ax,0FFFCh
  mov si,ax
  call ph32
  mov dx,si
  in eax,dx            ; control
  call ph32
  lea dx,[si+4]
  in eax,dx            ; status
  call ph32
  lea dx,[si+20h]
  in eax,dx            ; serial control
  call ph32
  lea dx,[si+0Ch]      ; memory page = 0Ch selects DAC1/DAC2 frame regs
  mov eax,0Ch
  out dx,eax
  lea dx,[si+34h]      ; DAC1 frame size/count (page C, offset 34h... DAC2 is 3Ch)
  in eax,dx
  call ph32
  lea dx,[si+3Ch]
  in eax,dx
  call ph32
.x: mov ax,4C00h
  int 21h
ph32: push eax
  shr eax,16
  call phw
  pop eax
  call phw
  mov dl,' '
  mov ah,2
  int 21h
  ret
phw: push ax
  mov al,ah
  call phb
  pop ax
phb: push ax
  shr al,4
  call nib
  pop ax
  and al,0Fh
nib: add al,'0'
  cmp al,'9'
  jbe .p
  add al,7
.p: mov dl,al
  mov ah,2
  int 21h
  ret
nf db 'no 1274 card',13,10,'$'
