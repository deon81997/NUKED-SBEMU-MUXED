; PITMAX: highest PIT ch0 count seen in 30000 latched reads (= reload value) + INT 8 vector
org 100h
  xor bx,bx
  mov cx,30000
.l: mov al,0
  out 43h,al
  in al,40h
  mov ah,al
  in al,40h
  xchg al,ah
  cmp ax,bx
  jbe .n
  mov bx,ax
.n: loop .l
  mov ax,bx
  call phw
  mov al,' '
  call pc
  push ds
  xor ax,ax
  mov ds,ax
  mov ax,[22h]
  mov bx,[20h]
  pop ds
  call phw
  mov al,':'
  call pc
  mov ax,bx
  call phw
  mov ax,4C00h
  int 21h
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
