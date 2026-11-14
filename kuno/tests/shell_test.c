#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "kernel.h"
#define CHECK(x) do { if (!(x)) { printf("FALLO: %s (linea %d)\n", #x, __LINE__); exit(1); } } while (0)
static FILE *img; static char out[8192];
static int file_read(struct blkdev *d, u64 lba, u32 n, void *buf) { (void)d; fseek(img, lba * 512, SEEK_SET); return fread(buf, 512, n, img) == n ? 0 : -1; }
static void cap(void *c, const char *s) { (void)c; strncat(out, s, sizeof out - strlen(out) - 1); }
static struct shell sh;
static const char *run(const char *l) { out[0] = 0; shell_exec(&sh, l, cap, 0); return out; }
int main(int argc, char **argv) {
    (void)argc; img = fopen(argv[1], "rb"); CHECK(img);
    struct blkdev d = { "file", 512, 1 << 30, 0, file_read }; struct fat32 fs; CHECK(!fat32_mount(&fs, &d));
    shell_init(&sh, &fs);
    char p[64];                                                       /* resolucion de rutas */
    path_resolve("/a/b", "../c", p, sizeof p); CHECK(!strcmp(p, "/a/c"));
    path_resolve("/", "..", p, sizeof p);      CHECK(!strcmp(p, "/"));
    path_resolve("/a", "/x/./y//z", p, sizeof p); CHECK(!strcmp(p, "/x/y/z"));
    path_resolve("/a", ".", p, sizeof p);      CHECK(!strcmp(p, "/a"));
    path_resolve("/a/b", "../..", p, sizeof p); CHECK(!strcmp(p, "/"));

    CHECK(strstr(run("ls"), "HELLO.TXT") && strstr(out, "Archivo con nombre largo.txt") && strstr(out, "d "));
    CHECK(!strstr(out, " .\n"));
    CHECK(!strcmp(run("cat hello.txt"), "Hola desde FAT32!\n"));
    CHECK(!strcmp(run("cat \"archivo con nombre largo.txt\""), "LFN ok\n"));
    CHECK(strstr(run("cd sub"), "") && !strcmp(run("pwd"), "/sub\n"));
    CHECK(!strcmp(run("cat inner.txt"), "hello\n"));
    CHECK(strstr(run("ls /"), "big.bin"));
    CHECK(!strcmp(run("cd .."), "") && !strcmp(run("pwd"), "/\n"));
    CHECK(strstr(run("cat /nope"), "no existe"));
    CHECK(strstr(run("cd HELLO.TXT"), "no es un directorio"));
    CHECK(strstr(run("cd nada"), "no existe") && !strcmp(run("pwd"), "/\n"));
    CHECK(strstr(run("hexdump HELLO.TXT"), "00000000  48 6f 6c 61") && strstr(out, "Hola desde FAT"));
    CHECK(!strcmp(run("echo hola   mundo"), "hola mundo\n"));
    CHECK(strstr(run("foo"), "foo: comando no encontrado"));
    CHECK(strstr(run("mkdir x"), "solo lectura"));
    CHECK(strstr(run("mem"), "262144 KiB") && strstr(run("uname"), "Kuno"));
    CHECK(strstr(run("dmesg"), "[test]"));
    run("clear"); CHECK(sh.want_clear); run("exit"); CHECK(sh.want_exit);
    CHECK(!strcmp(run(""), "") && !strcmp(run("   "), ""));
    printf("TODAS LAS PRUEBAS DE SHELL OK\n"); return 0;
}
