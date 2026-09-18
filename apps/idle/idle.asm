; IDLE.COM — tiny int 28h HLT TSR for DOS Agent Environment
;
; DOS calls INT 28h ("DOS idle") whenever the kernel waits for console input.
; The stock Win98 real-mode kernel busy-loops there, burning a full CPU core
; even under KVM. This TSR hooks INT 28h and executes STI;HLT so the vCPU
; sleeps until the next hardware interrupt (the 18 Hz timer at the latest),
; then chains to the previous handler.
;
; Assemble: nasm -f bin -o IDLE.COM idle.asm

        org 0x100

start:
        ; remember previous INT 28h vector
        xor     ax, ax
        mov     es, ax
        mov     ax, [es:28h*4]
        mov     [old28], ax
        mov     ax, [es:28h*4+2]
        mov     [old28+2], ax

        ; install our handler
        mov     word [es:28h*4], handler
        mov     [es:28h*4+2], cs

        mov     dx, hello
        mov     ah, 9
        int     21h

        ; terminate and stay resident: keep everything up to 'end'
        mov     ax, 3100h
        mov     dx, (end - start + 15) / 16 + 16
        int     21h

handler:
        sti                     ; never halt with interrupts masked
        hlt                     ; sleep until the next IRQ (timer at worst)
        jmp     far [cs:old28]  ; chain to the previous handler

hello:  db 'IDLE: int 28h halt installed', 13, 10, '$'
old28:  dd 0

end:
