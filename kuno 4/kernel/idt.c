/* IDT, PIC 8259A y PIT (IRQ0) para multitarea preemptiva. */
#include "kernel.h"

#define IDT_ENTRIES 256
#define IRQ_BASE    0x20

struct idt_entry {
    u16 offset_lo;
    u16 selector;
    u8  ist;
    u8  type_attr;   /* 0x8E = interrupt gate present, DPL0 */
    u16 offset_mid;
    u32 offset_hi;
    u32 zero;
} __attribute__((packed));

struct idt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed));

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr idtr;

/* contadores visibles para depuracion / panel de control */
volatile u64 jiffies;
volatile u64 irq0_count;
volatile u32 nested_irq;

/* stubs de interrupcion en isr.S */

/* ---- PIC 8259A ---- */
static void pic_remap(void) {
    /* ICW1: iniciar secuencia, cascade, ICW4 */
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    /* ICW2: vector base */
    outb(0x21, IRQ_BASE);       /* master -> 0x20 */
    outb(0xA1, IRQ_BASE + 8);   /* slave  -> 0x28 */
    /* ICW3: cascada */
    outb(0x21, 0x04);           /* slave en IRQ2 */
    outb(0xA1, 0x02);
    /* ICW4: modo 8086 */
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    /* IRQ0 timer, IRQ1 kbd, IRQ2 cascade; slave: IRQ12 mouse (bit4) */
    outb(0x21, 0xF8);           /* 1111 1000 -> 0,1,2 */
    outb(0xA1, 0xEF);           /* 1110 1111 -> permitir IRQ12 */
}

void pic_eoi(int irq) {
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

/* ---- PIT canal 0: ~100 Hz ---- */
#define PIT_FREQ 1193182
#define TIMER_HZ 100

void pit_init(void) {
    u32 div = PIT_FREQ / TIMER_HZ;
    outb(0x43, 0x36);                 /* canal 0, lobyte/hibyte, modo 3 */
    outb(0x40, (u8)(div & 0xFF));
    outb(0x40, (u8)((div >> 8) & 0xFF));
    kprintf("[pit] %d Hz (div=%u)\n", TIMER_HZ, div);
}

/* ---- IDT ---- */
static void idt_set(int vec, void *handler, u8 type) {
    u64 addr = (u64)handler;
    idt[vec].offset_lo  = (u16)(addr & 0xFFFF);
    idt[vec].selector   = 0x08;       /* kernel code */
    idt[vec].ist        = 0;
    idt[vec].type_attr  = type;
    idt[vec].offset_mid = (u16)((addr >> 16) & 0xFFFF);
    idt[vec].offset_hi  = (u32)(addr >> 32);
    idt[vec].zero       = 0;
}

extern void isr_timer(void);
extern void isr_kbd(void);
extern void isr_mouse(void);
extern void isr_spurious(void);
extern void isr_exception(void);

void idt_init(void) {
    memset(idt, 0, sizeof idt);
    for (int i = 0; i < 32; i++)
        idt_set(i, isr_exception, 0x8E);
    idt_set(IRQ_BASE + 0, isr_timer, 0x8E);
    idt_set(IRQ_BASE + 1, isr_kbd, 0x8E);
    for (int i = 2; i < 12; i++)
        idt_set(IRQ_BASE + i, isr_spurious, 0x8E);
    idt_set(IRQ_BASE + 12, isr_mouse, 0x8E);
    for (int i = 13; i < 16; i++)
        idt_set(IRQ_BASE + i, isr_spurious, 0x8E);

    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (u64)idt;
    __asm__ volatile("lidt %0" : : "m"(idtr));
    kprintf("[idt] cargada (timer+kbd+mouse)\n");
}

void irq_init(void) {
    pic_remap();
    pit_init();
    kprintf("[irq] PIC: IRQ0 timer, IRQ1 kbd, IRQ12 mouse\n");
}

/* llamados desde asm */
void irq_spurious_handler(int irq) {
    pic_eoi(irq);
}

void exception_handler(u64 vec, u64 err, u64 rip) {
    kprintf("\n*** excepcion #%llu err=%llx rip=%llx\n", vec, err, rip);
    if (vec == 8 || vec == 13 || vec == 14)
        kpanic("excepcion fatal");
}
