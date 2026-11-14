/* Capa de GPUs: cada tarjeta es un 'struct gpu' con su propio framebuffer y operaciones.
 *  - Driver 0: framebuffer del firmware (GOP en UEFI / VBE en BIOS, via GRUB).
 *  - Driver BGA (Bochs/QEMU std-vga): modo nativo en tarjetas SECUNDARIAS.
 *  - Intel/AMD/NVIDIA/virtio/vmware: detectadas por PCI, aun sin driver nativo. */
#include "kernel.h"

struct gpu *gpus[MAX_GPUS];
int ngpus;

void gpu_register(struct gpu *g) {
    if (ngpus >= MAX_GPUS) return;
    g->id = ngpus; gpus[ngpus++] = g;
    kprintf("[gpu%d] %s %ux%u fb=%p\n", g->id, g->name, g->w, g->h, g->fb);
}

static void linear_fill(struct gpu *g, int x, int y, int w, int h, u32 c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)g->w) w = g->w - x;
    if (y + h > (int)g->h) h = g->h - y;
    for (int j = 0; j < h; j++) {
        u32 *row = (u32 *)(g->fb + (size_t)(y + j) * g->pitch) + x;
        for (int i = 0; i < w; i++) row[i] = c;
    }
}

static struct gpu fw_gpu;
void gpu_init_firmware_fb(struct boot_info *bi) {
    if (bi->fb_type != 1 || bi->fb_bpp != 32) { kprintf("[gpu] sin framebuffer 32bpp RGB (tipo %d, %d bpp)\n", bi->fb_type, bi->fb_bpp); return; }
    fw_gpu = (struct gpu){ "firmware-fb", 0, (u8 *)bi->fb_addr, bi->fb_w, bi->fb_h, bi->fb_pitch, linear_fill };
    gpu_register(&fw_gpu);
}

/* ---- Bochs VBE Extensions (BGA) ---- */
static void bga_w(u16 idx, u16 v) { outw(0x1CE, idx); outw(0x1CF, v); }
static struct gpu bga_gpus[MAX_GPUS];
static void bga_attach(u32 bar0) {
    static const u16 W = 1024, H = 768;
    bga_w(4, 0); bga_w(1, W); bga_w(2, H); bga_w(3, 32); bga_w(4, 0x41);   /* habilitar + LFB */
    struct gpu *g = &bga_gpus[ngpus];
    *g = (struct gpu){ "bochs-bga", 0, (u8 *)(u64)(bar0 & ~0xFu), W, H, W * 4u, linear_fill };
    gpu_register(g);
}

void gpu_pci_found(u8 bus, u8 dev, u8 fn, u16 ven, u16 did, u32 bar0) {
    const char *n = ven == 0x8086 ? "Intel" : ven == 0x1002 ? "AMD" : ven == 0x10DE ? "NVIDIA" :
                    ven == 0x1234 ? "Bochs/QEMU" : ven == 0x1AF4 ? "virtio" : ven == 0x15AD ? "VMware" : "desconocido";
    kprintf("[pci] %02x:%02x.%x GPU %s %04x:%04x\n", bus, dev, fn, n, ven, did);
    int is_primary = ngpus > 0 && (bar0 & ~0xFu) == (u32)(u64)gpus[0]->fb;
    if (ven == 0x1234 && did == 0x1111 && !is_primary) bga_attach(bar0);
    else if (is_primary) kprintf("       (es la tarjeta del firmware; se usa su framebuffer)\n");
    else kprintf("       (sin driver nativo todavia)\n");
}
