bits 64

%macro ISR_NOERROR 1
isr%1:
    push 0
    push %1
    jmp isr_stub
%endmacro

%macro ISR_ERROR 1
isr%1:
    push %1
    jmp isr_stub
%endmacro

%macro ISR_ARRAY_ENTRY 1
dq isr%1
%endmacro

global isr_array_begin
global isr_array_end

isr_array_begin:
%assign j 0
%rep 256
ISR_ARRAY_ENTRY j
%assign j (j + 1)
%endrep
isr_array_end:

ISR_NOERROR 0
ISR_NOERROR 1
ISR_NOERROR 2
ISR_NOERROR 3
ISR_NOERROR 4
ISR_NOERROR 5
ISR_NOERROR 6
ISR_NOERROR 7
ISR_ERROR 8
ISR_NOERROR 9
ISR_ERROR 10
ISR_ERROR 11
ISR_ERROR 12
ISR_ERROR 13
ISR_ERROR 14
ISR_NOERROR 15
ISR_NOERROR 16
ISR_ERROR 17
ISR_NOERROR 18
ISR_NOERROR 19
ISR_NOERROR 20
ISR_ERROR 21
ISR_NOERROR 22
ISR_NOERROR 23
ISR_NOERROR 24
ISR_NOERROR 25
ISR_NOERROR 26
ISR_NOERROR 27
ISR_NOERROR 28
ISR_NOERROR 29
ISR_ERROR 30
ISR_NOERROR 31

; Normal IRQs
%assign i 32
%rep (256 - 32 - 1)
ISR_NOERROR i
%assign i (i + 1)
%endrep

; IRQ255 is reserved for the APIC Spurious
isr255:
    iretq

isr_stub:
    push rax
    push rbx
    push rcx
    push rdx
    push rdi
    push rsi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    xor rax, rax
    mov ax, ds
    push rax

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    
    cld
    mov rdi, rsp

    extern isr_handler
    call isr_handler

    pop rax
    mov ds, ax
    mov es, ax

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rsi
    pop rdi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16
    iretq