bits 64

HOST_RSP equ 0x6C14
HOST_RIP equ 0x6C16

section .text

%macro VMX_RUN_FUNC 2
%1:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rax, dr0
    push rax
    mov rax, dr1
    push rax
    mov rax, dr2
    push rax
    mov rax, dr3
    push rax
    mov rax, dr6
    push rax

    push rdi
    mov rcx, HOST_RSP
    mov rdx, HOST_RIP

    vmwrite rcx, rsp
    mov rdi, .payload_end
    vmwrite rdx, rdi

    mov rdi, qword [rsp]

    mov rax, qword [rdi + (15 * 8)]
    mov dr0, rax
    mov rax, qword [rdi + (16 * 8)]
    mov dr1, rax
    mov rax, qword [rdi + (17 * 8)]
    mov dr2, rax
    mov rax, qword [rdi + (18 * 8)]
    mov dr3, rax
    mov rax, qword [rdi + (19 * 8)]
    mov dr6, rax

    mov rax, qword [rdi]
    mov rbx, qword [rdi + (1 * 8)]
    mov rcx, qword [rdi + (2 * 8)]
    mov rdx, qword [rdi + (3 * 8)]
    mov rsi, qword [rdi + (5 * 8)]
    mov rbp, qword [rdi + (6 * 8)]
    mov r8, qword [rdi + (7 * 8)]
    mov r9, qword [rdi + (8 * 8)]
    mov r10, qword [rdi + (9 * 8)]
    mov r11, qword [rdi + (10 * 8)]
    mov r12, qword [rdi + (11 * 8)]
    mov r13, qword [rdi + (12 * 8)]
    mov r14, qword [rdi + (13 * 8)]
    mov r15, qword [rdi + (14 * 8)]
    mov rdi, qword [rdi + (4 * 8)]

    sti
    %2
.payload_end:
    push rdi
    mov rdi, qword [rsp + 8]

    mov qword [rdi], rax

    mov rax, dr0
    mov qword [rdi + (15 * 8)], rax
    mov rax, dr1
    mov qword [rdi + (16 * 8)], rax
    mov rax, dr2
    mov qword [rdi + (17 * 8)], rax
    mov rax, dr3
    mov qword [rdi + (18 * 8)], rax
    mov rax, dr6
    mov qword [rdi + (19 * 8)], rax

    mov qword [rdi + (1 * 8)], rbx
    mov qword [rdi + (2 * 8)], rcx
    mov qword [rdi + (3 * 8)], rdx
    mov qword [rdi + (5 * 8)], rsi
    mov qword [rdi + (6 * 8)], rbp
    mov qword [rdi + (7 * 8)], r8
    mov qword [rdi + (8 * 8)], r9
    mov qword [rdi + (9 * 8)], r10
    mov qword [rdi + (10 * 8)], r11
    mov qword [rdi + (11 * 8)], r12
    mov qword [rdi + (12 * 8)], r13
    mov qword [rdi + (13 * 8)], r14
    mov qword [rdi + (14 * 8)], r15
    pop r8 ; Guest RDI
    pop r9 ; Host RDI
    mov qword [r9 + (4 * 8)], r8

    pop rax
    mov dr6, rax
    pop rax
    mov dr3, rax
    pop rax
    mov dr2, rax
    pop rax
    mov dr1, rax
    pop rax
    mov dr0, rax

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    pushfq ; mov, push, and pop don't modify flags so the original op flags are still intact, return these
    pop rax
    ret
%endmacro

global vmx_vmlaunch
global vmx_vmresume

VMX_RUN_FUNC vmx_vmlaunch, vmlaunch
VMX_RUN_FUNC vmx_vmresume, vmresume