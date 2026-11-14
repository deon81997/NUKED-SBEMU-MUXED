; DTRACE.COM  - INT 21h call logger (TSR). DTRACE      : install (logging off)
;               DTRACE ON / OFF / DUMP   (DUMP prints the log: AH AL CF AXret  name/regs)
cpu 386
org 100h
start: jmp main
old21 dd 0
on db 0
cnt dw 0
ENT equ 32
MAXE equ 600
int21:
  cmp ah,0F0h
  jne .go
  mov ax,cs
  mov es,ax
  mov bx,[cs:cnt]
  mov cl,[cs:on]
  mov dx,ring
  iret
.go:
  cmp ah,0F1h
  jne .go2
  mov [cs:on],al
  or al,al
  jz .gi
  mov word [cs:cnt],0
.gi:
  iret
.go2:
  cmp byte [cs:on],0
  je .pass
  cmp ah,4Ch
  je .pass
  cmp ah,31h
  je .pass
  cmp ah,2
  je .pass
  cmp ah,6
  je .pass
  cmp ah,9
  je .pass
  cmp ah,40h       ; writes: log but no name
  cmp ah,2Ch
  je .pass
  cmp ah,2Ah
  je .pass
  cmp ah,0Bh
  je .pass
  ; record entry
  push di
  push si
  push cx
  push es
  push ax
  mov di,[cs:cnt]
  cmp di,MAXE
  jae .full
  imul di,di,ENT
  add di,ring
  push cs
  pop es
  mov [es:di],ax          ; al, ah
  mov word [es:di+2],0
  mov [es:di+4],bx
  mov [es:di+6],cx
  mov [es:di+8],dx
  ; name source: DS:DX, or DS:SI for 6Ch/60h/71xx
  mov si,dx
  cmp ah,6Ch
  jne .s1
  pop ax
  push ax
  mov si,[esp+6]     ; original si (pushed: di, si, cx, es, ax -> si at sp+6)
.s1:
  cmp ah,60h
  jne .s2
  mov si,[esp+6]
.s2:
  lea di,[di+10]
  mov cx,22
  call isname
  jnc .cp
  mov byte [es:di],0
  jmp .done
.cp:
  mov al,[ds:si]
  mov [es:di],al
  inc si
  inc di
  or al,al
  jz .done
  loop .cp
.done:
  inc word [cs:cnt]
.full:
  pop ax
  pop es
  pop cx
  pop si
  pop di
  ; call the old handler, then record the result
  push bp
  mov bp,[cs:cnt]
  pushf
  call far [cs:old21]
  pushf
  push di
  push ax
  dec bp
  cmp bp,MAXE
  jae .r
  imul di,bp,ENT
  add di,ring
  mov [cs:di+2],ax
  mov ax,[esp+4]      ; flags
  and al,1
  or byte [cs:di+3],al
  or byte [cs:di+3],80h
.r:
  pop ax
  pop di
  popf
  pop bp
  sti
  retf 2
.pass:
  jmp far [cs:old21]
isname:     ; CF=0 if AH (in [es:di-10+1]) takes a name
  push ax
  mov al,[es:di-9]
  cmp al,39h
  jb .no
  cmp al,3Dh
  jbe .yes
  cmp al,41h
  je .yes
  cmp al,43h
  je .yes
  cmp al,4Bh
  je .yes
  cmp al,4Eh
  je .yes
  cmp al,56h
  je .yes
  cmp al,5Ah
  je .yes
  cmp al,5Bh
  je .yes
  cmp al,60h
  je .yes
  cmp al,6Ch
  je .yes
.no: pop ax
  stc
  ret
.yes: pop ax
  clc
  ret
ring:
; ---- transient part ----
main:
  mov si,81h
.sk: lodsb
  cmp al,' '
  je .sk
  cmp al,13
  je install
  and al,0DFh
  cmp al,'D'
  je dump
  mov ah,[si]
  and ah,0DFh
  cmp ah,'N'
  je .on
  mov ax,0F100h
  int 21h
  jmp exit
.on: mov ax,0F101h
  int 21h
  jmp exit
install:
  mov ax,3521h
  int 21h
  mov [old21],bx
  mov [old21+2],es
  mov ax,2521h
  mov dx,int21
  int 21h
  mov dx,ring+ENT*MAXE+15
  shr dx,4
  mov ax,3100h
  int 21h
dump:
  mov ah,0F0h
  int 21h
  mov [dseg],es
  mov [dcnt],bx
  mov si,dx
.l: cmp word [dcnt],0
  je exit
  dec word [dcnt]
  push ds
  mov ds,[dseg]
  mov ax,[si]
  mov bx,[si+2]
  pop ds
  push si
  push ax
  mov al,ah
  call phb
  pop ax
  call phb
  mov al,' '
  call pc
  mov al,bh
  and al,1
  add al,'0'
  test bh,80h
  jnz .k
  mov al,'?'
.k: call pc
  mov al,' '
  call pc
  mov ax,bx
  call phb
  mov al,' '
  call pc
  pop si
  push si
  push ds
  mov ds,[cs:dseg]
  mov ax,[si+4]
  mov cx,[si+6]
  mov dx,[si+8]
  pop ds
  push dx
  push cx
  call phw
  mov al,' '
  call pc
  pop ax
  call phw
  mov al,' '
  call pc
  pop ax
  call phw
  mov al,' '
  call pc
  pop si
  push si
  add si,10
  mov cx,22
.n: push ds
  mov ds,[cs:dseg]
  mov al,[si]
  pop ds
  or al,al
  jz .ne
  call pc
  inc si
  loop .n
.ne: mov al,13
  call pc
  mov al,10
  call pc
  pop si
  add si,ENT
  jmp .l
exit:
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
dseg dw 0
dcnt dw 0
