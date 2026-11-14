#include "kernel.h"

static struct boot_info bi;
static struct blkdev rd;
static struct fat32 fs;

static int ls_cb(void *ctx, const char *name, u32 cl, u32 size, int isdir) {
    (void)ctx; (void)cl;
    kprintf("  %s %8u  %s\n", isdir ? "d" : "-", size, name);
    return 0;
}

void kmain(u32 magic, u64 info) {
    kprintf("Kuno 0.1 (x86_64) - arrancando...\n");
    if (magic != 0x36D76289) kpanic("no arrancado por un cargador Multiboot2");
    mb2_parse(info, &bi);
    kprintf("[mem] %llu MiB utilizables\n", bi.mem_bytes >> 20);

    gpu_init_firmware_fb(&bi);
    pci_scan();                                            /* detecta y registra mas GPUs */

    if (bi.mod_start) {
        ramdisk_init(&rd, (void *)bi.mod_start, bi.mod_end - bi.mod_start);
        if (fat32_mount(&fs, &rd) == 0) {
            kprintf("[fat32] montado: %u sectores/cluster, raiz=cluster %u\n", fs.spc, fs.root);
            vfs_set_root(&fs);
            fat32_iterate(&fs, fs.root, ls_cb, 0);
            long fd = ksyscall(SYS_open, (long)"/hello.txt", 0, 0);
            if (fd >= 0) {
                char buf[128]; long n = ksyscall(SYS_read, fd, (long)buf, sizeof buf - 1);
                buf[n > 0 ? n : 0] = 0; kprintf("[hello.txt] %s\n", buf); ksyscall(SYS_close, fd, 0, 0);
            }
        } else kprintf("[fat32] el modulo no es FAT32\n");
    } else kprintf("[fat32] sin modulo de disco (usa module2 en grub.cfg)\n");

    kprintf("Listo. %d GPU(s).\n", ngpus);
    for (int i = 0; i < ngpus; i++) gui_render(gpus[i]);   /* una GUI por pantalla */
    for (;;) __asm__ volatile("hlt");
}
