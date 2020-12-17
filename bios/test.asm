bits 16
org 0

mov ax, 0x7C0
mov ds, ax

mov dx, 0xE9
mov si, msg
.loop:
    lodsb
    cmp al, 0
    je .done

    out dx, al
    jmp .loop
.done:


mov eax, 0
vmcall

msg: db 'Hello Luna World', 0

times 510 - ($-$$) db 0
dw 0xAA55