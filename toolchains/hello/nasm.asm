bits 16
org 100h

mov dx, message
mov ah, 09h
int 21h
mov ax, 4c00h
int 21h

message db 'DOS_AGENT_NASM', 13, 10, '$'
