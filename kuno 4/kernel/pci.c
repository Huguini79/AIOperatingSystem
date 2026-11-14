#include "kernel.h"
static u32 rd(u8 b, u8 d, u8 f, u8 off) {
    outl(0xCF8, 0x80000000u | (u32)b << 16 | (u32)d << 11 | (u32)f << 8 | (off & 0xFC));
    return inl(0xCFC);
}
void pci_scan(void) {
    for (int b = 0; b < 8; b++)
        for (int d = 0; d < 32; d++)
            for (int f = 0; f < 8; f++) {
                u32 id = rd(b, d, f, 0);
                if ((id & 0xFFFF) == 0xFFFF) { if (!f) break; continue; }
                if ((rd(b, d, f, 8) >> 24) == 3)                       /* clase 0x03: pantalla */
                    gpu_pci_found(b, d, f, id & 0xFFFF, id >> 16, rd(b, d, f, 0x10));
                if (!f && !((rd(b, d, f, 0x0C) >> 16) & 0x80)) break; /* no multifuncion */
            }
}
