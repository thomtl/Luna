bits 64
section .text

global svm_vmrun
svm_vmrun:
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

    push rsi

    ;mov rax, qword [rdi + (14 * 8)]
    ;mov dr0, rax
    ;mov rax, qword [rdi + (15 * 8)]
    ;mov dr1, rax
    ;mov rax, qword [rdi + (16 * 8)]
    ;mov dr2, rax
    ;mov rax, qword [rdi + (17 * 8)]
    ;mov dr3, rax

    ; Restore guest GPR state
    mov rbx, qword [rdi + (0 * 8)]
    mov rcx, qword [rdi + (1 * 8)]
    mov rdx, qword [rdi + (2 * 8)]
    mov rsi, qword [rdi + (3 * 8)]
    mov rbp, qword [rdi + (5 * 8)]
    mov r8, qword [rdi + (6 * 8)]
    mov r9, qword [rdi + (7 * 8)]
    mov r10, qword [rdi + (8 * 8)]
    mov r11, qword [rdi + (9 * 8)]
    mov r12, qword [rdi + (10 * 8)]
    mov r13, qword [rdi + (11 * 8)]
    mov r14, qword [rdi + (12 * 8)]
    mov r15, qword [rdi + (13 * 8)]

    push rdi
    mov rdi, qword [rdi + 0x20]

    mov rax, qword [rsp + 8] ; Mov VMCB into rax

    vmload
    vmrun
    vmsave

    push rdi
    mov rdi, qword [rsp + 0x8] ; Restore rdi from stack

    mov qword [rdi + (0 * 8)], rbx
    mov qword [rdi + (1 * 8)], rcx
    mov qword [rdi + (2 * 8)], rdx
    mov qword [rdi + (3 * 8)], rsi
    mov qword [rdi + (5 * 8)], rbp
    mov qword [rdi + (6 * 8)], r8
    mov qword [rdi + (7 * 8)], r9
    mov qword [rdi + (8 * 8)], r10
    mov qword [rdi + (9 * 8)], r11
    mov qword [rdi + (10 * 8)], r12
    mov qword [rdi + (11 * 8)], r13
    mov qword [rdi + (12 * 8)], r14
    mov qword [rdi + (13 * 8)], r15

    ;mov rax, dr0
    ;mov qword [rdi + (14 * 8)], rax
    ;mov rax, dr1
    ;mov qword [rdi + (15 * 8)], rax
    ;mov rax, dr2
    ;mov qword [rdi + (16 * 8)], rax
    ;mov rax, dr3
    ;mov qword [rdi + (17 * 8)], rax

    pop r8
    pop r9
    mov qword [r9 + 0x20], r8

    pop rsi

    ;pop rax
    ;mov dr3, rax
    ;pop rax
    ;mov dr2, rax
    ;pop rax
    ;mov dr1, rax
    ;pop rax
    ;mov dr0, rax

    pop r15 ; Restore Callee Saved Registers
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    
    ret
