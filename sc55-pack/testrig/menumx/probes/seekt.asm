; SEEKT file : seek -22 from end, print new pos + 22 bytes hex; then seek to 0 from end; print pos
org 100h
  mov si,81h
.sk: lodsb
  cmp al,' '
  je .sk
  dec si
  mov dx,si
.f: lodsb
  cmp al,13
  jne .f
  mov byte [si-1],0
  mov ax,3D00h
  int 21h
  jc err
  mov [h],ax
  mov bx,ax
  mov ax,4202h
  mov cx,0FFFFh
  mov dx,-22
  int 21h
  jc err
  call phw32
  mov ah,3Fh
  mov bx,[h]
  mov cx,22
  mov dx,buf
  int 21h
  jc err
  push ax
  mov al,'n'
  call pc
  pop ax
  push ax
  call phb
  mov al,':'
  call pc
  pop cx
  mov si,buf
.p: lodsb
  call phb
  loop .p
  call nl
  ; seek to 1000 from start, read 4
  mov ax,4200h
  mov bx,[h]
  xor cx,cx
  mov dx,1000
  int 21h
  call phw32
  mov ah,3Fh
  mov bx,[h]
  mov cx,4
  mov dx,buf
  int 21h
  mov si,buf
  mov cx,4
.q: lodsb
  call phb
  loop .q
  call nl
  ; seek cur +0
  mov ax,4201h
  mov bx,[h]
  xor cx,cx
  xor dx,dx
  int 21h
  call phw32
  call nl
  mov ax,4C00h
  int 21h
err: push ax
  mov al,'E'
  call pc
  pop ax
  call phb
  call nl
  mov ax,4C01h
  int 21h
phw32: push ax
  mov ax,dx
  call phw
  pop ax
  call phw
  mov al,' '
  jmp pc
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
  jbe pc
  add al,7
pc: push ax
  push dx
  mov dl,al
  mov ah,2
  int 21h
  pop dx
  pop ax
  ret
nl: mov al,13
  call pc
  mov al,10
  jmp pc
h dw 0
buf times 32 db 0
