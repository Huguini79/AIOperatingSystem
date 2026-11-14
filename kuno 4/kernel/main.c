#include "kernel.h"

static struct boot_info bi;
static struct blkdev rd;
static struct fat32 fs, *fs_ptr;
u64 kmem_bytes;

/* tarea de fondo: contador visible (prueba de preemption) */
static volatile u64 worker_counter;
static void worker_task(void *arg) {
    (void)arg;
    for (;;) {
        worker_counter++;
        /* trabajo "pesado" minimo para no saturar sin ceder siempre */
        for (volatile int i = 0; i < 5000; i++)
            __asm__ volatile("pause");
    }
}

/* segunda tarea: solo gasta ticks para demostrar el RR */
static volatile u64 spinner_counter;
static void spinner_task(void *arg) {
    (void)arg;
    for (;;) {
        spinner_counter++;
        __asm__ volatile("pause");
    }
}

u64 kmain_worker_count(void) { return worker_counter; }
u64 kmain_spinner_count(void) { return spinner_counter; }

void kmain(u32 magic, u64 info) {
    kprintf("Kuno 0.3 (x86_64) - multitarea preemptiva\n");
    if (magic != 0x36D76289) kpanic("no arrancado por un cargador Multiboot2");
    mb2_parse(info, &bi);
    kmem_bytes = bi.mem_bytes;
    kprintf("[mem] %llu MiB utilizables\n", bi.mem_bytes >> 20);

    /* interrupciones y planificador antes de la GUI */
    sched_init();
    idt_init();
    irq_init();

    gpu_init_firmware_fb(&bi);
    pci_scan();

    if (bi.mod_start) {
        ramdisk_init(&rd, (void *)bi.mod_start, bi.mod_end - bi.mod_start);
        if (fat32_mount(&fs, &rd) == 0) {
            fs_ptr = &fs; vfs_set_root(&fs);
            kprintf("[fat32] montado: %u sectores/cluster, raiz=cluster %u\n", fs.spc, fs.root);
        } else kprintf("[fat32] el modulo no es FAT32\n");
    } else kprintf("[fat32] sin modulo de disco (usa module2 en grub.cfg)\n");

    ps2_init();

    if (!ngpus) {
        static struct shell sh; shell_init(&sh, fs_ptr);
        /* en modo texto tambien arrancamos el planificador */
        task_create("worker", worker_task, 0);
        task_create("spinner", spinner_task, 0);
        sched_start();
        textcon_run(&sh);
    }

    gui_init(fs_ptr);

    /* tareas de demostracion */
    task_create("worker", worker_task, 0);
    task_create("spinner", spinner_task, 0);

    sched_start();   /* sti + tarea 0 = este hilo (gui) */

    kprintf("Listo. %d GPU(s). Multitarea ON. Alt+T = terminal.\n", ngpus);
    for (;;) {
        input_poll();
        struct event e;
        while (ev_pop(&e)) gui_handle(&e);
        gui_frame();
        __asm__ volatile("pause");
    }
}
