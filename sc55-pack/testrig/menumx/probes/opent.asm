; OPENT file : try 3Dxx open modes, 6C00 extended open, 4E findfirst, 60 truename; print CF/AX each
org 100h
  mov si,81h
.sk: lodsb
  cmp al,' '
  je .sk
  dec si
  mov di,nbuf
  mov [fn],di
.f: lodsb
  cmp al,13
  je .g
  mov [di],al
  inc di
  jmp .f
.g: mov byte [di],0
  mov si,modes
.m: lodsb
  cmp al,0FFh
  je .x
  push si
  mov ah,3Dh
  mov dx,[fn]
  push ax
  call phb
  mov al,'='
  call pc
  pop ax
  int 21h
  call res
  jc .n
  mov bx,ax
  mov ah,3Eh
  int 21h
.n: pop si
  jmp .m
.x: mov al,'X'
  call pc
  mov ax,6C00h
  mov bx,0040h
  mov cx,0
  mov dx,1
  mov si,[fn]
  int 21h
  call res
  jc .y
  mov bx,ax
  mov ah,3Eh
  int 21h
.y: mov al,'F'
  call pc
  mov ah,4Eh
  mov cx,27h
  mov dx,[fn]
  int 21h
  call res
  mov al,'T'
  call pc
  mov ah,60h
  mov si,[fn]
  mov di,tbuf
  push ds
  pop es
  int 21h
  call res
  mov si,tbuf
.t: lodsb
  or al,al
  jz .e
  call pc
  jmp .t
.e: mov ax,4C00h
  int 21h
res: pushf
  push ax
  jc .c
  mov al,'+'
  jmp .o
.c: mov al,'-'
.o: call pc
  pop ax
  push ax
  call phw
  mov al,' '
  call pc
  pop ax
  popf
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
modes db 00h,20h,40h,10h,30h,12h,01h,02h,0FFh
fn dw 0
tbuf times 128 db 0
nbuf times 128 db 0
