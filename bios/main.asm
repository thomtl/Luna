bits 16
org 0x0

jmp 0xF000:start
start:
    mov ax, 0
    mov ss, ax
    mov sp, 0x1000

    call fill_ivt

    jmp jump_to_mbr

fill_ivt:
    mov si, 0

    mov bx, 0
    mov es, bx

    .loop:
        mov word [es:bx], .handler_stub
        add bx, 2
        mov word [es:bx], cs
        add bx, 2

        inc si
        cmp si, 0xFF
        jne .loop

    ret

    .handler_stub:
        mov eax, 0
        vmcall

jump_to_mbr:
    mov dl, 0
    .find_disk:
        clc

        mov ecx, 0x00000200 ; 32bit, Read, Disk 0, 512 bytes
        movzx edx, dl
        shl edx, 16
        or ecx, edx

        mov eax, 1 ; OP: Disk IO
        mov edi, 0x7C00 ; Destination
        mov ebx, 0x0 ; Disk offset
        vmcall

        jc .fail

        mov bx, 0x7C0
        mov es, bx
        mov bx, 0x1FE ; Boot signature
        mov ax, word [es:bx] ; 0x1FE: Boot signature
        cmp ax, 0xAA55
        je .done

        inc dl
        jmp .find_disk
    .done:
    ; DL was already set by the earlier code
    jmp 0:0x7C00
    .fail:
    mov eax, 0 ; OP: Exit to VMM
    vmcall