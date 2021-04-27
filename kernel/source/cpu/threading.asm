bits 64
section .text

global do_yield
do_yield: ; RDI = Curr Ctx, RSI = Curr state ptr, RDX = New Ctx, RCX = New State
    mov qword [rdi + 8 * 0], rbx
    mov qword [rdi + 8 * 1], rbp
    mov qword [rdi + 8 * 3], r12
    mov qword [rdi + 8 * 4], r13
    mov qword [rdi + 8 * 5], r14
    mov qword [rdi + 8 * 6], r15

    pushf
    pop qword [rdi + 8 * 9]

    pop r8
    mov qword [rdi + 8 * 8], r8
    mov qword [rdi + 8 * 2], rsp

    mov qword [rsi], rcx ; This thread is Idle now

    mov rdi, rdx
    jmp thread_invoke

global thread_invoke
thread_invoke:
    mov rbx, qword [rdi + 8 * 0] ; Restore caller saved regs
    mov rbp, qword [rdi + 8 * 1]
    mov r12, qword [rdi + 8 * 3]
    mov r13, qword [rdi + 8 * 4]
    mov r14, qword [rdi + 8 * 5]
    mov r15, qword [rdi + 8 * 6]

    mov rsp, qword [rdi + 8 * 2] ; RSP
    mov rcx,  qword [rdi + 8 * 8] ; RIP

    push qword [rdi + 8 * 9] ; We are now on the new stack so this should be fine
    popf

    mov rdi, qword [rdi + 8 * 7] ; Needed for passing an argument to the function

    jmp rcx
    