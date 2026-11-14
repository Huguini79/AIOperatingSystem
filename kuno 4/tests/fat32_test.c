/* Prueba en el host del driver kernel/fat32.c contra una imagen creada con mkfat32.py */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel.h"

static FILE *img;
static int file_read(struct blkdev *d, u64 lba, u32 n, void *buf) {
    (void)d; fseek(img, lba * 512, SEEK_SET); return fread(buf, 512, n, img) == n ? 0 : -1;
}
static int ls(void *c, const char *n, u32 cl, u32 sz, int dir) { (void)c; printf("  %c %6u cl=%-4u '%s'\n", dir ? 'd' : '-', sz, cl, n); return 0; }
#define CHECK(x) do { if (!(x)) { printf("FALLO: %s (linea %d)\n", #x, __LINE__); exit(1); } } while (0)

int main(int argc, char **argv) {
    img = fopen(argv[1], "rb"); CHECK(img);
    struct blkdev d = { "file", 512, 1 << 30, 0, file_read };
    struct fat32 fs; CHECK(fat32_mount(&fs, &d) == 0);
    printf("montado: spc=%u raiz=%u clusters=%u\n/:\n", fs.spc, fs.root, fs.ncl);
    fat32_iterate(&fs, fs.root, ls, 0);

    struct fat32_file f; char buf[64] = {0};
    CHECK(fat32_open(&fs, "/HELLO.TXT", &f) == 0);                     /* insensible a mayusculas */
    CHECK(fat32_read(&fs, &f, buf, 63) == 17 && !strcmp(buf, "Hola desde FAT32!"));
    memset(buf, 0, 64);
    CHECK(fat32_open(&fs, "/archivo con nombre largo.txt", &f) == 0);  /* LFN */
    CHECK(fat32_read(&fs, &f, buf, 63) == 6 && !strcmp(buf, "LFN ok"));
    CHECK(fat32_open(&fs, "/sub/inner.txt", &f) == 0);                 /* subdirectorio */
    CHECK(fat32_read(&fs, &f, buf, 63) == 5);
    CHECK(fat32_open(&fs, "/no/existe", &f) != 0);

    CHECK(fat32_open(&fs, "/big.bin", &f) == 0 && f.size == 5000);     /* cadena de 10 clusters */
    u8 *big = malloc(5000), *p = big; long n, tot = 0;
    while ((n = fat32_read(&fs, &f, p, 333)) > 0) { p += n; tot += n; } /* lecturas desalineadas */
    CHECK(tot == 5000);
    for (int i = 0; i < 5000; i++) CHECK(big[i] == ((i * 7 + 3) & 0xFF));
    printf("TODAS LAS PRUEBAS FAT32 OK\n"); return 0;
}
