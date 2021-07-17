bits 64
section .text

global thread_invoke
thread_invoke:
    mov rax, qword [rdi + 0 * 8]
    mov rbx, qword [rdi + 1 * 8]
    mov rcx, qword [rdi + 2 * 8]
    mov rdx, qword [rdi + 3 * 8]
    mov rsi, qword [rdi + 4 * 8]
    mov rbp, qword [rdi + 6 * 8]

    mov r8, qword [rdi + 7 * 8]
    mov r9, qword [rdi + 8 * 8]
    mov r10, qword [rdi + 9 * 8]
    mov r11, qword [rdi + 10 * 8]
    mov r12, qword [rdi + 11 * 8]
    mov r13, qword [rdi + 12 * 8]
    mov r14, qword [rdi + 13 * 8]
    mov r15, qword [rdi + 14 * 8]

    mov rsp, qword [rdi + 15 * 8]

    push 0x10 ; SS
    push qword [rdi + 15 * 8] ; RSP
    push qword [rdi + 17 * 8] ; RFLAGS
    push 0x8 ; CS
    push qword [rdi + 16 * 8] ; RIP

    mov rdi, qword [rdi + 5 * 8]

    iretq
