bits 64

section .text

extern vmx_do_host_rsp_update ; RDI: VM*, RSI: New RSP

global vmx_vmenter
global vmx_do_vmexit
vmx_vmenter: ; RDI: VM*, RSI: Guest GPRs*, RDX: 1 = Resume, 0 = launch
    push rbp ; Save Callee Saved Registers
    push rbx
    push r12
    push r13
    push r14
    push r15

    ;mov rax, dr0
    ;push rax
    ;mov rax, dr1
    ;push rax
    ;mov rax, dr2
    ;push rax
    ;mov rax, dr3
    ;push rax
    ;mov rax, dr6
    ;push rax

    mov rbx, rdx ; RBX is preserved by functions

    push rsi

    mov rsi, rsp ; From this point until vmx_do_vmexit we cannot push or pop
    call vmx_do_host_rsp_update
    mov rsi, qword [rsp]

    cmp rbx, 1 ; mov doesn't modify flags

    ;mov rax, qword [rdi + (15 * 8)]
    ;mov dr0, rax
    ;mov rax, qword [rdi + (16 * 8)]
    ;mov dr1, rax
    ;mov rax, qword [rdi + (17 * 8)]
    ;mov dr2, rax
    ;mov rax, qword [rdi + (18 * 8)]
    ;mov dr3, rax
    ;mov rax, qword [rdi + (19 * 8)]
    ;mov dr6, rax

    mov rax, qword [rsi]
    mov rbx, qword [rsi + (1 * 8)]
    mov rcx, qword [rsi + (2 * 8)]
    mov rdx, qword [rsi + (3 * 8)]
    mov rdi, qword [rsi + (4 * 8)]
    mov rbp, qword [rsi + (6 * 8)]
    mov r8, qword [rsi + (7 * 8)]
    mov r9, qword [rsi + (8 * 8)]
    mov r10, qword [rsi + (9 * 8)]
    mov r11, qword [rsi + (10 * 8)]
    mov r12, qword [rsi + (11 * 8)]
    mov r13, qword [rsi + (12 * 8)]
    mov r14, qword [rsi + (13 * 8)]
    mov r15, qword [rsi + (14 * 8)]
    mov rsi, qword [rsi + (5 * 8)]

    je .do_resume

    vmlaunch
    jmp vmx_do_vmexit

.do_resume:
    vmresume

vmx_do_vmexit:
    push rsi
    mov rsi, qword [rsp + 8]

    mov qword [rsi], rax
    mov qword [rsi + (1 * 8)], rbx
    mov qword [rsi + (2 * 8)], rcx
    mov qword [rsi + (3 * 8)], rdx
    mov qword [rsi + (4 * 8)], rdi
    mov qword [rsi + (6 * 8)], rbp
    mov qword [rsi + (7 * 8)], r8
    mov qword [rsi + (8 * 8)], r9
    mov qword [rsi + (9 * 8)], r10
    mov qword [rsi + (10 * 8)], r11
    mov qword [rsi + (11 * 8)], r12
    mov qword [rsi + (12 * 8)], r13
    mov qword [rsi + (13 * 8)], r14
    mov qword [rsi + (14 * 8)], r15
    pop r8 ; Guest RSI
    pop r9 ; Host RSI
    mov qword [r9 + (5 * 8)], r8

    ;mov rax, dr0
    ;mov qword [rdi + (15 * 8)], rax
    ;mov rax, dr1
    ;mov qword [rdi + (16 * 8)], rax
    ;mov rax, dr2
    ;mov qword [rdi + (17 * 8)], rax
    ;mov rax, dr3
    ;mov qword [rdi + (18 * 8)], rax
    ;mov rax, dr6
    ;mov qword [rdi + (19 * 8)], rax

    ;pop rax
    ;mov dr6, rax
    ;pop rax
    ;mov dr3, rax
    ;pop rax
    ;mov dr2, rax
    ;pop rax
    ;mov dr1, rax
    ;pop rax
    ;mov dr0, rax

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp 

    pushfq ; mov, push, and pop don't modify flags so the original op flags are still intact, return these
    pop rax
    ret