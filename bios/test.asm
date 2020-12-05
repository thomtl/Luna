bits 16

mov eax, 0x69696969
vmcall

mov eax, 0
vmcall

times 510 - ($-$$) db 0
dw 0xAA55