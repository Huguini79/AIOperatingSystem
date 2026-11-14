#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "kernel.h"
#define CHECK(x) do { if (!(x)) { printf("FALLO: %s (linea %d)\n", #x, __LINE__); exit(1); } } while (0)
static struct kbd k; static struct event e;
static int feed(u8 b) { return kbd_decode(&k, b, &e); }
int main(void) {
    CHECK(feed(0x1E) && e.key == 'a' && e.down && !e.mods);          /* a */
    CHECK(feed(0x9E) && e.key == 'a' && !e.down);                    /* suelta a */
    CHECK(!feed(0x2A) && feed(0x1E) && e.key == 'A' && (e.mods & MOD_SHIFT)); feed(0x9E); feed(0xAA);
    CHECK(feed(0x02) && e.key == '1'); feed(0x82);
    CHECK(!feed(0x2A) && feed(0x02) && e.key == '!'); feed(0x82); feed(0xAA);   /* Shift+1 */
    CHECK(!feed(0x3A) && feed(0x1E) && e.key == 'A'); feed(0x9E); feed(0xBA);    /* Caps Lock */
    CHECK(!feed(0x2A) && feed(0x1E) && e.key == 'a'); feed(0x9E); feed(0xAA); feed(0x3A); feed(0xBA);
    CHECK(feed(0x1C) && e.key == '\n'); CHECK(feed(0x0E) && e.key == '\b'); CHECK(feed(0x39) && e.key == ' ');
    CHECK(feed(0x35) && e.key == '/'); CHECK(feed(0x33) && e.key == ',');
    CHECK(!feed(0xE0) && feed(0x48) && e.key == KEY_UP);             /* flecha arriba */
    CHECK(!feed(0xE0) && feed(0x50) && e.key == KEY_DOWN);
    CHECK(!feed(0xE0) && feed(0x49) && e.key == KEY_PGUP);
    CHECK(feed(0x3B) && e.key == KEY_F1); CHECK(feed(0x3E) && e.key == KEY_F1 + 3); CHECK(feed(0x57) && e.key == KEY_F1 + 10);
    CHECK(!feed(0x38) && feed(0x0F) && e.key == '\t' && (e.mods & MOD_ALT)); feed(0x8F); feed(0xB8);  /* Alt+Tab */
    CHECK(!feed(0x1D) && feed(0x26) && e.key == 'l' && (e.mods & MOD_CTRL)); feed(0xA6); feed(0x9D);  /* Ctrl+L */
    CHECK(!feed(0xE0) && !feed(0x5B) && feed(0x14) && (e.mods & MOD_SUPER)); /* Super+T */
    CHECK(feed(0x94) && (e.mods & MOD_SUPER)); feed(0xE0); feed(0xDB);
    CHECK(feed(0x14) && !(e.mods & MOD_SUPER));
    CHECK(!feed(0xE1) && !feed(0x1D) && !feed(0x45) && !feed(0xE1) && !feed(0x9D) && !feed(0xC5)); /* Pause ignorada */
    CHECK(feed(0x1E) && e.key == 'a');

    struct mouse m = {0};                                            /* paquetes de raton */
    CHECK(!mouse_decode(&m, 0x09, &e) && !mouse_decode(&m, 5, &e) && mouse_decode(&m, 3, &e));
    CHECK(e.type == EV_MOUSE && e.dx == 5 && e.dy == -3 && e.buttons == 1);    /* izq, derecha-arriba */
    mouse_decode(&m, 0x08 | 0x10 | 0x20, &e); mouse_decode(&m, 0xFB, &e);     /* dx=-5, dy=-? */
    CHECK(mouse_decode(&m, 0xFE, &e) && e.dx == -5 && e.dy == 2 && e.buttons == 0);
    CHECK(!mouse_decode(&m, 0x00, &e) && m.n == 0);                  /* byte desincronizado descartado */
    printf("TODAS LAS PRUEBAS DE ENTRADA OK\n"); return 0;
}
