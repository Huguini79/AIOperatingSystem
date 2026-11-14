/* Sustitutos de las piezas del kernel que los tests del host no enlazan */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "kernel.h"
struct gpu *gpus[MAX_GPUS]; int ngpus; u64 kmem_bytes = 256ull << 20;
int ksnprintf(char *b, size_t n, const char *f, ...) { va_list a; va_start(a, f); int r = vsnprintf(b, n, f, a); va_end(a); return r; }
void kprintf(const char *f, ...) { (void)f; }
const char *klog_line(int i) { (void)i; return i == KLOG_LINES - 1 ? "[test] linea de log" : ""; }
void *kmalloc(size_t n) { return calloc(1, n); }
void kpanic(const char *m) { fprintf(stderr, "panic: %s\n", m); exit(2); }

/* multitarea: stubs para tests en host */
volatile u64 jiffies;
volatile u64 irq0_count;
void idt_init(void) {}
void irq_init(void) {}
void pit_init(void) {}
void pic_eoi(int irq) { (void)irq; }
void sched_init(void) {}
void sched_start(void) {}
int  task_create(const char *n, void (*e)(void *), void *a) { (void)n; (void)e; (void)a; return -1; }
void sched_yield(void) {}
int  sched_ntasks(void) { return 0; }
void sched_task_info(int i, char *name_out, int nlen, int *state, u64 *ticks, u64 *sw) {
    (void)i; if (name_out && nlen) name_out[0] = 0; if (state) *state = -1;
    if (ticks) *ticks = 0; if (sw) *sw = 0;
}
const char *sched_state_str(int st) { (void)st; return "n/a"; }
u64 sched_switches(void) { return 0; }
u64 sched_jiffies(void) { return 0; }
