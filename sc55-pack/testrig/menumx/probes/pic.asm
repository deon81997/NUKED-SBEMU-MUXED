org 100h
  in al,21h
  mov bl,al
  call hex
  mov dl,' '
  mov ah,2
  int 21h
  in al,0A1h
  mov bl,al
  call hex
  mov dl,' '
  mov ah,2
  int 21h
  cli
  mov al,0Bh
  out 20h,al
  in al,20h
  mov bh,al
  mov al,0Bh
  out 0A0h,al
  in al,0A0h
  mov cl,al
  mov al,0Ah
  out 20h,al
  out 0A0h,al
  sti
  mov bl,bh
  push cx
  call hex
  pop cx
  mov dl,' '
  mov ah,2
  int 21h
  mov bl,cl
  call hex
  mov dl,13
  mov ah,2
  int 21h
  mov dl,10
  int 21h
  mov ax,4c00h
  int 21h
hex:
  mov al,bl
  shr al,4
  call nib
  mov al,bl
  and al,0fh
nib:
  add al,'0'
  cmp al,'9'
  jbe .p
  add al,7
.p:
  mov dl,al
  mov ah,2
  int 21h
  ret
