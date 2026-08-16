bits 16
org 0x100

mov ah, 0x3c
xor cx, cx
mov dx, filename
int 0x21
jc failed
mov bx, ax
mov ah, 0x40
mov cx, payload_end - payload
mov dx, payload
int 0x21
jc failed
cmp ax, cx
jne failed
mov ah, 0x3e
int 0x21
mov dx, success
mov ah, 0x09
int 0x21
xor al, al
jmp exit

failed:
mov dx, failure
mov ah, 0x09
int 0x21
mov al, 1

exit:
mov ah, 0x4c
int 0x21

filename db 'C:\TMP\BINARY.DAT', 0
success db 'DOS_AGENT_BINARY_CREATED', 13, 10, '$'
failure db 'DOS_AGENT_BINARY_FAILED', 13, 10, '$'
payload db 0x00, 0x01, 0x02, 0x0a, 0x0d, 0x1a, 0x7f, 0x80, 0xfe, 0xff
        db 'DOS_AGENT_BINARY', 0x00
payload_end:
