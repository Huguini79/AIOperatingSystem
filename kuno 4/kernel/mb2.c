#include "kernel.h"
struct tag { u32 type, size; };
struct mmap_entry { u64 base, len; u32 type, rsv; };

void mb2_parse(u64 info, struct boot_info *bi) {
    memset(bi, 0, sizeof *bi);
    u8 *p = (u8 *)info + 8;
    for (;;) {
        struct tag *t = (struct tag *)p;
        if (t->type == 0) break;
        if (t->type == 3 && !bi->mod_start) {               /* modulo */
            bi->mod_start = *(u32 *)(p + 8); bi->mod_end = *(u32 *)(p + 12);
        } else if (t->type == 6) {                          /* mapa de memoria */
            u32 esz = *(u32 *)(p + 8);
            for (u8 *e = p + 16; e < p + t->size; e += esz) {
                struct mmap_entry *m = (void *)e;
                if (m->type == 1) bi->mem_bytes += m->len;
            }
        } else if (t->type == 8) {                          /* framebuffer */
            bi->fb_addr = *(u64 *)(p + 8);   bi->fb_pitch = *(u32 *)(p + 16);
            bi->fb_w = *(u32 *)(p + 20);     bi->fb_h = *(u32 *)(p + 24);
            bi->fb_bpp = p[28];              bi->fb_type = p[29];
        }
        p += (t->size + 7) & ~7u;
    }
}
