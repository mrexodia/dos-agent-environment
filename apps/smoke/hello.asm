bits 16
org 0x100

mov dx, message
mov ah, 0x09
int 0x21
ret

message db 'DOS_AGENT_HELLO', 13, 10, '$'
