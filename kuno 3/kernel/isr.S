/* Stubs de interrupcion y cambio de contexto para multitarea preemptiva.
 * Convencion: al entrar a un IRQ, la CPU ya apilo SS,RSP,RFLAGS,CS,RIP.
 * Nosotros apilamos los GPRs en el orden que el planificador espera. */

.section .text
.code64

/* Orden del frame en la pila (desde RSP hacia arriba):
 *   r15 r14 r13 r12 r11 r10 r9 r8 rbp rdi rsi rdx rcx rbx rax
 *   rip cs rflags rsp ss   (puestos por la CPU)
 * Total GPRs: 15 * 8 = 120 bytes antes del frame de hardware.
 */

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

/* ---- IRQ0: timer ---- */
.global isr_timer
isr_timer:
    SAVE_GPRS
    mov %rsp, %rdi          /* primer arg: puntero al frame (rsp actual) */
    call timer_interrupt_c  /* en sched.c: guarda rsp, elige siguiente, pone nuevo rsp */
    /* rax = nuevo rsp de la tarea elegida */
    mov %rax, %rsp
    RESTORE_GPRS
    iretq

/* ---- IRQs genericas (1-15): solo EOI ---- */
.global isr_spurious
isr_spurious:
    SAVE_GPRS
    /* vector no lo tenemos facil sin stub por IRQ; EOI master siempre */
    mov $0, %edi
    call irq_spurious_handler
    RESTORE_GPRS
    iretq

/* ---- excepciones genericas (sin codigo de error para la mayoria) ----
 * Para simplificar, no distinguimos las que empujan error code.
 * En un kernel serio habria stubs separados. */
.global isr_exception
isr_exception:
    SAVE_GPRS
    xor %rdi, %rdi          /* vec = 0 (desconocido en este stub unico) */
    xor %rsi, %rsi          /* err */
    mov 120(%rsp), %rdx     /* rip aproximado (tras 15 pushes) */
    call exception_handler
    RESTORE_GPRS
    iretq

/* ---- arranque de la primera tarea (nunca vuelve) ----
 * rdi = rsp inicial de la tarea (ya con frame falso) */
.global task_enter
task_enter:
    mov %rdi, %rsp
    RESTORE_GPRS
    iretq

.section .note.GNU-stack,"",@progbits
