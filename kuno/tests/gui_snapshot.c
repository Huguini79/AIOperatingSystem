/* Ejecuta la GUI real en el host sobre un framebuffer falso, simula entrada y vuelca PPM. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel.h"
static FILE *img;
static int file_read(struct blkdev *d, u64 lba, u32 n, void *buf) { (void)d; fseek(img, lba * 512, SEEK_SET); return fread(buf, 512, n, img) == n ? 0 : -1; }
static void key(int k, int mods) { struct event e = { EV_KEY, 1, 0, (u16)mods, k, 0, 0 }; gui_handle(&e); }
static void typ(const char *s) { for (; *s; s++) key(*s, 0); key('\n', 0); }
static void mouse(int dx, int dy, int btn) { struct event e = { EV_MOUSE, 0, (u8)btn, 0, 0, dx, dy }; gui_handle(&e); }
static void dump(struct gpu *g, const char *name) {
    gui_frame(); FILE *f = fopen(name, "wb"); fprintf(f, "P6\n%u %u\n255\n", g->w, g->h);
    for (u32 y = 0; y < g->h; y++) for (u32 x = 0; x < g->w; x++) { u32 p = ((u32 *)(g->fb + y * g->pitch))[x]; fputc(p >> 16, f); fputc(p >> 8, f); fputc(p, f); }
    fclose(f);
}
int main(int argc, char **argv) {
    (void)argc; img = fopen(argv[1], "rb");
    struct blkdev d = { "file", 512, 1 << 30, 0, file_read }; static struct fat32 fs; if (fat32_mount(&fs, &d)) return 1;
    static struct gpu g; g = (struct gpu){ "fake", 0, calloc(1024 * 768, 4), 1024, 768, 4096, 0 }; gpus[0] = &g; ngpus = 1;
    gui_init(&fs);
    typ("help"); typ("ls"); typ("cd sub"); typ("cat inner.txt"); typ("cd /"); typ("hexdump HELLO.TXT"); typ("foo");
    key(KEY_UP, 0); key(KEY_UP, 0);                                  /* historial */
    key('f', MOD_ALT);                                               /* nuevo explorador */
    key(KEY_DOWN, 0); key(KEY_DOWN, 0);
    key('\n', 0);                                                    /* entra en 'sub' (o abre visor) */
    key('t', MOD_ALT); typ("echo segunda terminal");                 /* otra terminal */
    key('\t', MOD_ALT);                                              /* Alt+Tab */
    key('\t', MOD_ALT);
    for (int i = 0; i < 8; i++) mouse(10, 5, 0);                     /* mover raton */
    dump(&g, "build/snap1.ppm");
    mouse(-5000, -5000, 0); mouse(440, 754, 0); mouse(0, 0, 1); mouse(0, 0, 0);   /* clic en "TERMINAL 1" */
    key('m', MOD_ALT);                                               /* maximizar */
    mouse(-300, -300, 0);
    dump(&g, "build/snap2.ppm");
    printf("ventanas=%s\n", "ok"); return 0;
}
