/* Shell de comandos. Independiente de la GUI: escribe a traves de un callback,
 * asi la usan igual las ventanas de terminal y la consola de texto. */
#include "kernel.h"

static void say(shell_out o, void *c, const char *s) { o(c, s); }
static void sayf(shell_out o, void *c, const char *fmt, const char *a) {
    char b[200]; ksnprintf(b, sizeof b, fmt, a); o(c, b);
}

void shell_init(struct shell *sh, struct fat32 *fs) {
    memset(sh, 0, sizeof *sh); sh->fs = fs; sh->cwd[0] = '/';
}

int path_resolve(const char *cwd, const char *arg, char *out, size_t n) {
    char comb[320]; size_t c = 0;
    if (n < 2) return -1;
    if (arg[0] != '/') { for (size_t i = 0; cwd[i] && c < sizeof comb - 2; i++) comb[c++] = cwd[i]; comb[c++] = '/'; }
    for (size_t i = 0; arg[i] && c < sizeof comb - 1; i++) comb[c++] = arg[i];
    comb[c] = 0;
    size_t o = 1; out[0] = '/'; out[1] = 0;
    const char *p = comb;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *e = p; while (*e && *e != '/') e++;
        size_t l = (size_t)(e - p);
        if (l == 1 && p[0] == '.') { /* nada */ }
        else if (l == 2 && p[0] == '.' && p[1] == '.') { while (o > 1 && out[o - 1] != '/') o--; if (o > 1) o--; out[o] = 0; }
        else {
            if (o + 1 + l >= n) return -1;
            if (o > 1) out[o++] = '/';
            for (size_t i = 0; i < l; i++) out[o++] = p[i];
            out[o] = 0;
        }
        p = e;
    }
    return 0;
}

struct lsctx { shell_out o; void *c; };
static int ls_cb(void *ctx, const char *name, u32 cl, u32 size, int isdir) {
    struct lsctx *l = ctx; (void)cl;
    if (name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2]))) return 0;
    char b[300]; ksnprintf(b, sizeof b, "%s %8u  %s\n", isdir ? "d" : "-", size, name); l->o(l->c, b);
    return 0;
}

static int split(char *line, char **argv, int max) {
    int n = 0;
    while (*line && n < max) {
        while (*line == ' ') line++;
        if (!*line) break;
        if (*line == '"') { argv[n++] = ++line; while (*line && *line != '"') line++; }
        else { argv[n++] = line; while (*line && *line != ' ') line++; }
        if (*line) *line++ = 0;
    }
    return n;
}

static int open_arg(struct shell *sh, const char *arg, struct fat32_file *f, shell_out o, void *c) {
    char p[160];
    if (!sh->fs) { say(o, c, "no hay disco montado\n"); return -1; }
    if (path_resolve(sh->cwd, arg, p, sizeof p) || fat32_open(sh->fs, p, f)) { sayf(o, c, "%s: no existe\n", arg); return -1; }
    return 0;
}

void shell_exec(struct shell *sh, const char *line, shell_out o, void *c) {
    char buf[160], *av[8]; size_t n = 0;
    while (line[n] && n < sizeof buf - 1) { buf[n] = line[n]; n++; }
    buf[n] = 0;
    int ac = split(buf, av, 8);
    if (!ac) return;
    const char *cmd = av[0];
    struct fat32_file f;

    if (!strcmp(cmd, "help")) {
        say(o, c, "Comandos: help ls cd pwd cat hexdump echo clear uname mem gpus dmesg\n"
                  "          ps uptime reboot halt exit\n"
                  "Teclas:   Alt+Tab ventana  Alt+T terminal  Alt+F archivos  Alt+I sistema\n"
                  "          Alt+M maximizar  Alt+F4 cerrar  Alt+flechas mover  Ctrl+L limpiar\n");
    } else if (!strcmp(cmd, "ls")) {
        if (open_arg(sh, ac > 1 ? av[1] : ".", &f, o, c)) return;
        if (!f.isdir) { sayf(o, c, "%s\n", ac > 1 ? av[1] : "."); return; }
        struct lsctx l = { o, c }; fat32_iterate(sh->fs, f.first, ls_cb, &l);
    } else if (!strcmp(cmd, "cd")) {
        char p[160];
        if (!sh->fs) { say(o, c, "no hay disco montado\n"); return; }
        if (path_resolve(sh->cwd, ac > 1 ? av[1] : "/", p, sizeof p) || fat32_open(sh->fs, p, &f)) { say(o, c, "cd: no existe\n"); return; }
        if (!f.isdir) { say(o, c, "cd: no es un directorio\n"); return; }
        for (size_t i = 0; ; i++) { sh->cwd[i] = p[i]; if (!p[i]) break; }
    } else if (!strcmp(cmd, "pwd")) {
        sayf(o, c, "%s\n", sh->cwd);
    } else if (!strcmp(cmd, "cat")) {
        if (ac < 2) { say(o, c, "uso: cat ARCHIVO\n"); return; }
        if (open_arg(sh, av[1], &f, o, c)) return;
        if (f.isdir) { say(o, c, "cat: es un directorio\n"); return; }
        char chunk[257]; long r; u32 total = 0;
        while (total < 65536 && (r = fat32_read(sh->fs, &f, chunk, 256)) > 0) {
            char out[257]; int m = 0;
            for (long i = 0; i < r; i++) {
                char ch = chunk[i];
                if (ch == '\r') continue;
                out[m++] = (ch == '\n' || ch == '\t' || (ch >= 32 && ch < 127)) ? ch : '.';
            }
            out[m] = 0; say(o, c, out); total += (u32)r;
        }
        say(o, c, "\n");
    } else if (!strcmp(cmd, "hexdump")) {
        if (ac < 2) { say(o, c, "uso: hexdump ARCHIVO\n"); return; }
        if (open_arg(sh, av[1], &f, o, c)) return;
        u8 d[256]; long r = f.isdir ? 0 : fat32_read(sh->fs, &f, d, 256);
        for (long i = 0; i < r; i += 16) {
            char l[100]; int m = ksnprintf(l, sizeof l, "%08x  ", (u32)i);
            for (long j = 0; j < 16; j++) m += (i + j < r) ? ksnprintf(l + m, sizeof l - m, "%02x ", d[i + j]) : ksnprintf(l + m, sizeof l - m, "   ");
            l[m++] = ' ';
            for (long j = 0; j < 16 && i + j < r; j++) l[m++] = (d[i + j] >= 32 && d[i + j] < 127) ? d[i + j] : '.';
            l[m++] = '\n'; l[m] = 0; say(o, c, l);
        }
    } else if (!strcmp(cmd, "echo")) {
        for (int i = 1; i < ac; i++) { say(o, c, av[i]); say(o, c, i + 1 < ac ? " " : ""); }
        say(o, c, "\n");
    } else if (!strcmp(cmd, "clear")) {
        sh->want_clear = 1;
    } else if (!strcmp(cmd, "uname")) {
        say(o, c, "Kuno 0.3 x86_64 preempt\n");
    } else if (!strcmp(cmd, "mem")) {
        char b[64]; ksnprintf(b, sizeof b, "%llu KiB utilizables\n", kmem_bytes >> 10); say(o, c, b);
    } else if (!strcmp(cmd, "gpus")) {
        if (!ngpus) say(o, c, "sin GPU (modo texto)\n");
        for (int i = 0; i < ngpus; i++) {
            char b[96]; ksnprintf(b, sizeof b, "gpu%d: %s %ux%u\n", i, gpus[i]->name, gpus[i]->w, gpus[i]->h); say(o, c, b);
        }
    } else if (!strcmp(cmd, "ps")) {
        char b[96];
        ksnprintf(b, sizeof b, "PID  STATE   TICKS    SW  NAME\n"); say(o, c, b);
        for (int i = 0; i < 8; i++) {
            char name[24]; int st; u64 tk, sw;
            sched_task_info(i, name, sizeof name, &st, &tk, &sw);
            if (st < 0) continue;
            ksnprintf(b, sizeof b, "%3d  %-6s %7llu %4llu  %s\n",
                      i, sched_state_str(st), tk, sw, name);
            say(o, c, b);
        }
        ksnprintf(b, sizeof b, "cambios de contexto: %llu\n", sched_switches());
        say(o, c, b);
    } else if (!strcmp(cmd, "uptime")) {
        char b[64];
        u64 sec = sched_jiffies() / 100;
        ksnprintf(b, sizeof b, "uptime %llu s  (jiffies %llu)\n", sec, sched_jiffies());
        say(o, c, b);
    } else if (!strcmp(cmd, "dmesg")) {
        for (int i = 0; i < KLOG_LINES; i++) { const char *l = klog_line(i); if (l[0]) { say(o, c, l); say(o, c, "\n"); } }
    } else if (!strcmp(cmd, "exit")) {
        sh->want_exit = 1;
    } else if (!strcmp(cmd, "reboot")) {
        say(o, c, "reiniciando...\n"); outb(0x64, 0xFE);
        for (;;) __asm__ volatile("hlt");
    } else if (!strcmp(cmd, "halt")) {
        say(o, c, "sistema detenido\n");
        for (;;) __asm__ volatile("cli; hlt");
    } else if (!strcmp(cmd, "mkdir") || !strcmp(cmd, "touch") || !strcmp(cmd, "rm") || !strcmp(cmd, "cp") || !strcmp(cmd, "mv")) {
        say(o, c, "FAT32 es de solo lectura por ahora\n");
    } else {
        sayf(o, c, "%s: comando no encontrado (prueba 'help')\n", cmd);
    }
}
