bits 16

mov al, ':'
mov dx, 0x3F8
out dx, al

mov al, ')'
out dx, al

mov al, 10
out dx, al

vmmcall

times 510-($-$$) db 0
dw 0xAA55

times (512 * 1024 / 8) dq 0