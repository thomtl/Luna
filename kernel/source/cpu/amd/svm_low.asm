bits 64
section .text

global svm_vmrun
svm_vmrun:
    push rax
    push rbx
    push rcx
    push rdx
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
    push rsi

    ; Restore guest GPR state
    mov rbx, qword [rdi + 0x0]
    mov rcx, qword [rdi + 0x8]
    mov rdx, qword [rdi + 0x10]
    mov rsi, qword [rdi + 0x18]
    mov rbp, qword [rdi + 0x28]
    mov r8, qword [rdi + 0x30]
    mov r9, qword [rdi + 0x38]
    mov r10, qword [rdi + 0x40]
    mov r11, qword [rdi + 0x48]
    mov r12, qword [rdi + 0x50]
    mov r13, qword [rdi + 0x58]
    mov r14, qword [rdi + 0x60]
    mov r15, qword [rdi + 0x68]

    push rdi
    mov rdi, qword [rdi + 0x20]

    mov rax, qword [rsp + 8] ; Mov VMCB into rax

    vmload
    vmrun
    vmsave

    push rdi
    mov rdi, qword [rsp + 0x8] ; Restore rdi from stack

    mov qword [rdi + 0x0], rbx
    mov qword [rdi + 0x8], rcx
    mov qword [rdi + 0x10], rdx
    mov qword [rdi + 0x18], rsi
    mov qword [rdi + 0x28], rbp
    mov qword [rdi + 0x30], r8
    mov qword [rdi + 0x38], r9
    mov qword [rdi + 0x40], r10
    mov qword [rdi + 0x48], r11
    mov qword [rdi + 0x50], r12
    mov qword [rdi + 0x58], r13
    mov qword [rdi + 0x60], r14
    mov qword [rdi + 0x68], r15

    pop r8
    pop r9
    mov qword [r9 + 0x20], r8

    pop rsi
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
    pop rdx
    pop rcx
    pop rbx
    pop rax
    
    ret
