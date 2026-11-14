/* Consola de texto VGA (0xB8000) con la misma shell: respaldo cuando no hay framebuffer grafico. */
#include "kernel.h"
static volatile u16 *vga = (volatile u16 *)0xB8000;
static int row, col;

static void clr(void) { for (int i = 0; i < 80 * 25; i++) vga[i] = 0x0720; row = col = 0; }
static void tput(char c) {
    if (c == '\n') { col = 0; row++; }
    else if (c == '\b') { if (col) { col--; vga[row * 80 + col] = 0x0720; } }
    else { vga[row * 80 + col] = 0x0700 | (u8)c; if (++col >= 80) { col = 0; row++; } }
    if (row >= 25) {
        for (int i = 0; i < 80 * 24; i++) vga[i] = vga[i + 80];
        for (int i = 0; i < 80; i++) vga[24 * 80 + i] = 0x0720;
        row = 24;
    }
}
static void tout(void *c, const char *s) { (void)c; for (; *s; s++) tput(*s); }

void textcon_run(struct shell *sh) {
    clr(); tout(0, "Kuno 0.2 - consola de texto. Escribe 'help'.\n");
    for (;;) {
        char p[160], in[128]; int n = 0;
        ksnprintf(p, sizeof p, "kuno:%s$ ", sh->cwd); tout(0, p);
        for (;;) {
            input_poll();
            struct event e;
            if (!ev_pop(&e)) { __asm__ volatile("pause"); continue; }
            if (e.type != EV_KEY || !e.down) continue;
            if (e.key == '\n') { tput('\n'); break; }
            if (e.key == '\b') { if (n) { n--; tput('\b'); } }
            else if (e.key >= 32 && e.key < 127 && n < 126) { in[n++] = (char)e.key; tput((char)e.key); }
        }
        in[n] = 0; shell_exec(sh, in, tout, 0);
        if (sh->want_clear) { clr(); sh->want_clear = 0; }
        sh->want_exit = 0;
    }
}
