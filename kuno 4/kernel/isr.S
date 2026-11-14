/* Stubs de interrupcion y cambio de contexto. */
.section .text
.code64

.macro SAVE_GPRS
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15
.endm

.macro RESTORE_GPRS
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax
.endm

/* IRQ0 timer — puede cambiar de tarea */
.global isr_timer
isr_timer:
    SAVE_GPRS
    mov %rsp, %rdi
    call timer_interrupt_c
    mov %rax, %rsp
    RESTORE_GPRS
    iretq

/* IRQ1 teclado — no cambia de tarea, solo encola eventos */
.global isr_kbd
isr_kbd:
    SAVE_GPRS
    call kbd_irq_handler
    mov $1, %edi
    call pic_eoi
    RESTORE_GPRS
    iretq

/* IRQ12 raton */
.global isr_mouse
isr_mouse:
    SAVE_GPRS
    call mouse_irq_handler
    mov $12, %edi
    call pic_eoi
    RESTORE_GPRS
    iretq

.global isr_spurious
isr_spurious:
    SAVE_GPRS
    xor %edi, %edi
    call irq_spurious_handler
    RESTORE_GPRS
    iretq

.global isr_exception
isr_exception:
    SAVE_GPRS
    xor %rdi, %rdi
    xor %rsi, %rsi
    mov 120(%rsp), %rdx
    call exception_handler
    RESTORE_GPRS
    iretq

.global task_enter
task_enter:
    mov %rdi, %rsp
    RESTORE_GPRS
    iretq

.section .note.GNU-stack,"",@progbits
