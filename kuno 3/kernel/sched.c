/* Planificador preemptivo round-robin (tareas de kernel). */
#include "kernel.h"

#define MAX_TASKS       8
#define TASK_STACK_SIZE (16 * 1024)

enum { TS_UNUSED = 0, TS_READY, TS_RUNNING, TS_BLOCKED, TS_ZOMBIE };

struct task {
    u64  rsp;
    u8  *stack;
    int  id;
    int  state;
    char name[20];
    u64  ticks;
    u64  switches;
    void (*entry)(void *);
    void *arg;
};

static struct task tasks[MAX_TASKS];
static struct task *current;
static int ntasks;
static u64 schedule_count;

extern volatile u64 jiffies;

struct trap_frame {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
    u64 rip, cs, rflags, rsp, ss;
};

static void set_name(struct task *t, const char *n) {
    size_t i = 0;
    for (; n && n[i] && i + 1 < sizeof t->name; i++) t->name[i] = n[i];
    t->name[i] = 0;
}

static void task_trampoline(void) {
    void (*fn)(void *) = current->entry;
    void *arg = current->arg;
    fn(arg);
    current->state = TS_ZOMBIE;
    for (;;) {
        __asm__ volatile("sti; hlt");
    }
}

int task_create(const char *name, void (*entry)(void *), void *arg) {
    struct task *t = 0;
    for (int i = 0; i < MAX_TASKS; i++)
        if (tasks[i].state == TS_UNUSED) { t = &tasks[i]; t->id = i; break; }
    if (!t) return -1;

    t->stack = kmalloc(TASK_STACK_SIZE);
    if (!t->stack) return -1;
    memset(t->stack, 0, TASK_STACK_SIZE);

    u8 *top = t->stack + TASK_STACK_SIZE;
    top = (u8 *)((u64)top & ~0xFUL);
    struct trap_frame *f = (struct trap_frame *)(top - sizeof(struct trap_frame));
    memset(f, 0, sizeof *f);

    f->rip = (u64)task_trampoline;
    f->cs  = 0x08;
    f->rflags = 0x202;       /* IF=1 */
    f->rsp = (u64)top;
    f->ss  = 0x10;

    t->rsp = (u64)f;
    t->entry = entry;
    t->arg = arg;
    t->state = TS_READY;
    t->ticks = 0;
    t->switches = 0;
    set_name(t, name);
    ntasks++;
    kprintf("[sched] tarea '%s' id=%d\n", t->name, t->id);
    return t->id;
}

struct task *sched_current(void) { return current; }
int sched_ntasks(void) { return ntasks; }

void sched_task_info(int i, char *name_out, int nlen, int *state, u64 *ticks, u64 *sw) {
    if (i < 0 || i >= MAX_TASKS || tasks[i].state == TS_UNUSED) {
        if (name_out && nlen) name_out[0] = 0;
        if (state) *state = -1;
        return;
    }
    if (name_out && nlen) {
        size_t j = 0;
        for (; tasks[i].name[j] && j + 1 < (size_t)nlen; j++) name_out[j] = tasks[i].name[j];
        name_out[j] = 0;
    }
    if (state) *state = tasks[i].state;
    if (ticks) *ticks = tasks[i].ticks;
    if (sw) *sw = tasks[i].switches;
}

static struct task *pick_next(void) {
    int start = current ? current->id : -1;
    for (int n = 1; n <= MAX_TASKS; n++) {
        int i = (start + n + MAX_TASKS) % MAX_TASKS;
        if (tasks[i].state == TS_READY)
            return &tasks[i];
    }
    if (current && (current->state == TS_RUNNING || current->state == TS_READY))
        return current;
    for (int i = 0; i < MAX_TASKS; i++)
        if (tasks[i].state == TS_READY || tasks[i].state == TS_RUNNING)
            return &tasks[i];
    return current;
}

/* rdi = rsp del frame guardado por el stub */
u64 timer_interrupt_c(u64 rsp) {
    extern volatile u64 irq0_count;
    extern void pic_eoi(int irq);
    irq0_count++;
    jiffies++;
    pic_eoi(0);

    if (current) {
        current->rsp = rsp;
        current->ticks++;
        if (current->state == TS_RUNNING)
            current->state = TS_READY;
    }
    struct task *next = pick_next();
    if (!next) next = current;
    if (next && next != current) {
        schedule_count++;
        next->switches++;
    }
    if (next) {
        current = next;
        current->state = TS_RUNNING;
        return current->rsp;
    }
    return rsp;
}

void sched_on_timer(void) {
    /* el cambio real ocurre en timer_interrupt_c */
}

void sched_yield(void) {
    u64 t = jiffies;
    while (jiffies == t)
        __asm__ volatile("pause");
}

void sched_start(void) {
    /* hilo de arranque = tarea 0 (GUI) */
    struct task *t0 = &tasks[0];
    t0->id = 0;
    t0->state = TS_RUNNING;
    t0->stack = 0;
    t0->rsp = 0;
    t0->ticks = 0;
    t0->switches = 0;
    set_name(t0, "gui");
    if (ntasks < 1) ntasks = 1;
    current = t0;
    kprintf("[sched] planificador activo, actual='%s'\n", current->name);
    __asm__ volatile("sti");
}

void sched_init(void) {
    memset(tasks, 0, sizeof tasks);
    current = 0;
    ntasks = 0;
    schedule_count = 0;
    kprintf("[sched] max %d tareas\n", MAX_TASKS);
}

u64 sched_switches(void) { return schedule_count; }
u64 sched_jiffies(void) { return jiffies; }

/* estados legibles para la shell / panel */
const char *sched_state_str(int st) {
    switch (st) {
    case TS_UNUSED:  return "unused";
    case TS_READY:   return "ready";
    case TS_RUNNING: return "run";
    case TS_BLOCKED: return "block";
    case TS_ZOMBIE:  return "zombie";
    default:         return "?";
    }
}
