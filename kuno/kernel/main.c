#include "kernel.h"

static struct boot_info bi;
static struct blkdev rd;
static struct fat32 fs, *fs_ptr;
u64 kmem_bytes;

void kmain(u32 magic, u64 info) {
    kprintf("Kuno 0.2 (x86_64) - arrancando...\n");
    if (magic != 0x36D76289) kpanic("no arrancado por un cargador Multiboot2");
    mb2_parse(info, &bi);
    kmem_bytes = bi.mem_bytes;
    kprintf("[mem] %llu MiB utilizables\n", bi.mem_bytes >> 20);

    gpu_init_firmware_fb(&bi);
    pci_scan();                                            /* detecta y registra mas GPUs */

    if (bi.mod_start) {
        ramdisk_init(&rd, (void *)bi.mod_start, bi.mod_end - bi.mod_start);
        if (fat32_mount(&fs, &rd) == 0) {
            fs_ptr = &fs; vfs_set_root(&fs);
            kprintf("[fat32] montado: %u sectores/cluster, raiz=cluster %u\n", fs.spc, fs.root);
        } else kprintf("[fat32] el modulo no es FAT32\n");
    } else kprintf("[fat32] sin modulo de disco (usa module2 en grub.cfg)\n");

    ps2_init();                                            /* teclado + raton */

    if (!ngpus) {                                          /* sin framebuffer: consola de texto VGA */
        static struct shell sh; shell_init(&sh, fs_ptr);
        textcon_run(&sh);                                  /* no vuelve */
    }

    gui_init(fs_ptr);
    kprintf("Listo. %d GPU(s). Alt+T = terminal.\n", ngpus);
    for (;;) {
        input_poll();
        struct event e; int any = 0;
        while (ev_pop(&e)) { gui_handle(&e); any = 1; }
        (void)any;
        gui_frame();                                       /* solo redibuja si hubo cambios */
        __asm__ volatile("pause");
    }
}
