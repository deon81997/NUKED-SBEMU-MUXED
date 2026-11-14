; WAIT1.COM: about 1 s of busy waiting outside DOS (18 BIOS ticks), like a game sitting at a prompt
org 100h
  xor ax,ax
  mov es,ax
  mov cx,18
.l: mov ax,[es:46Ch]
.w: cmp ax,[es:46Ch]
  je .w
  loop .l
  int 20h
