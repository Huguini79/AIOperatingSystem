/* Capa estilo Unix: descriptores de fichero + llamadas read/write/open/close/exit
 * (numeros de Linux x86_64). Aun se invoca directamente; falta enlazarla a la
 * instruccion SYSCALL (MSR STAR/LSTAR) cuando existan IDT y modo usuario. */
#include "kernel.h"

static struct fat32 *root_fs;
#define NFD 32
static struct { int used; struct fat32_file f; } fds[NFD];

void vfs_set_root(struct fat32 *fs) { root_fs = fs; }

long ksyscall(long no, long a1, long a2, long a3) {
    switch (no) {
    case SYS_write:
        if (a1 == 1 || a1 == 2) { const char *s = (const char *)a2; for (long i = 0; i < a3; i++) kprintf("%c", s[i]); return a3; }
        return -9;                                              /* EBADF */
    case SYS_open:
        if (!root_fs) return -2;
        for (int i = 3; i < NFD; i++) if (!fds[i].used) {
            if (fat32_open(root_fs, (const char *)a1, &fds[i].f)) return -2;   /* ENOENT */
            fds[i].used = 1; return i;
        }
        return -24;                                             /* EMFILE */
    case SYS_read:
        if (a1 < 3 || a1 >= NFD || !fds[a1].used) return -9;
        return fat32_read(root_fs, &fds[a1].f, (void *)a2, (u32)a3);
    case SYS_close:
        if (a1 < 3 || a1 >= NFD || !fds[a1].used) return -9;
        fds[a1].used = 0; return 0;
    case SYS_exit: kprintf("exit(%ld)\n", a1); return 0;
    }
    return -38;                                                 /* ENOSYS */
}
