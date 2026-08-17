.model tiny
.code
org 100h

start:
    mov dx, offset message
    mov ah, 09h
    int 21h
    mov ax, 4c00h
    int 21h

message db 'DOS_AGENT_JWASM', 13, 10, '$'
end start
