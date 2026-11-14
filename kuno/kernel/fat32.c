/* Driver FAT32 (solo lectura): BPB, cadenas de clusters, nombres largos (LFN),
 * rutas con '/', superfloppy o particion MBR. Sin asignacion dinamica. */
#include "kernel.h"

#define EOC 0x0FFFFFF8u
static u16 rd16(const u8 *p) { return p[0] | p[1] << 8; }
static u32 rd32(const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static int try_mount(struct fat32 *fs, u64 lba) {
    u8 s[512];
    if (fs->dev->read(fs->dev, lba, 1, s) || s[510] != 0x55 || s[511] != 0xAA) return -1;
    if (rd16(s + 11) != 512 || !s[13] || rd16(s + 22) != 0 || !rd32(s + 36)) return -1; /* no es FAT32 */
    fs->spc = s[13]; fs->rsv = rd16(s + 14); fs->nfats = s[16];
    fs->fatsz = rd32(s + 36); fs->root = rd32(s + 44); fs->part_lba = lba;
    fs->data_lba = lba + fs->rsv + (u64)fs->nfats * fs->fatsz;
    u32 total = rd16(s + 19) ? rd16(s + 19) : rd32(s + 32);
    fs->ncl = (total - fs->rsv - fs->nfats * fs->fatsz) / fs->spc;
    return 0;
}

int fat32_mount(struct fat32 *fs, struct blkdev *dev) {
    fs->dev = dev;
    if (try_mount(fs, 0) == 0) return 0;
    u8 s[512];                                        /* MBR: probar particiones */
    if (dev->read(dev, 0, 1, s) || s[510] != 0x55) return -1;
    for (int i = 0; i < 4; i++) {
        u8 *e = s + 446 + 16 * i;
        if ((e[4] == 0x0B || e[4] == 0x0C) && try_mount(fs, rd32(e + 8)) == 0) return 0;
    }
    return -1;
}

static u32 fat_next(struct fat32 *fs, u32 c) {
    u8 s[512];
    if (fs->dev->read(fs->dev, fs->part_lba + fs->rsv + (c * 4) / 512, 1, s)) return EOC;
    return rd32(s + (c * 4) % 512) & 0x0FFFFFFF;
}
static u64 cl_lba(struct fat32 *fs, u32 c) { return fs->data_lba + (u64)(c - 2) * fs->spc; }

int fat32_iterate(struct fat32 *fs, u32 cl, fat32_cb cb, void *ctx) {
    char lfn[256]; lfn[0] = 0;
    for (; cl >= 2 && cl < EOC; cl = fat_next(fs, cl)) {
        for (u32 sc = 0; sc < fs->spc; sc++) {
            u8 s[512];
            if (fs->dev->read(fs->dev, cl_lba(fs, cl) + sc, 1, s)) return -1;
            for (int i = 0; i < 512; i += 32) {
                u8 *e = s + i;
                if (e[0] == 0x00) return 0;
                if (e[0] == 0xE5) { lfn[0] = 0; continue; }
                if (e[11] == 0x0F) {                                  /* entrada LFN */
                    int ord = e[0] & 0x3F, base = (ord - 1) * 13;
                    static const u8 off[13] = {1,3,5,7,9,14,16,18,20,22,24,28,30};
                    if (e[0] & 0x40) memset(lfn, 0, sizeof lfn);
                    for (int k = 0; k < 13 && base + k < 255; k++) {
                        u16 ch = rd16(e + off[k]);
                        if (ch == 0 || ch == 0xFFFF) break;
                        lfn[base + k] = ch < 128 ? (char)ch : '?';
                    }
                    continue;
                }
                if (e[11] & 0x08) { lfn[0] = 0; continue; }          /* etiqueta de volumen */
                char name[256];
                if (lfn[0]) { int n = 0; while (lfn[n]) { name[n] = lfn[n]; n++; } name[n] = 0; }
                else {
                    int n = 0;
                    for (int k = 0; k < 8 && e[k] != ' '; k++) name[n++] = e[k];
                    if (e[8] != ' ') { name[n++] = '.'; for (int k = 8; k < 11 && e[k] != ' '; k++) name[n++] = e[k]; }
                    name[n] = 0;
                }
                lfn[0] = 0;
                u32 c = (u32)rd16(e + 20) << 16 | rd16(e + 26);
                int r = cb(ctx, name, c, rd32(e + 28), !!(e[11] & 0x10));
                if (r) return r;
            }
        }
    }
    return 0;
}

struct find { const char *want; size_t len; u32 cl, size; int isdir; };
static int find_cb(void *ctx, const char *name, u32 cl, u32 size, int isdir) {
    struct find *f = ctx;
    if (strlen(name) != f->len) return 0;
    for (size_t i = 0; i < f->len; i++) if (lower(name[i]) != lower(f->want[i])) return 0;
    f->cl = cl; f->size = size; f->isdir = isdir;
    return 1;
}

int fat32_open(struct fat32 *fs, const char *path, struct fat32_file *f) {
    u32 cl = fs->root, size = 0; int isdir = 1;
    while (*path) {
        while (*path == '/') path++;
        if (!*path) break;
        const char *e = path; while (*e && *e != '/') e++;
        if (!isdir) return -1;
        struct find fd = { path, (size_t)(e - path), 0, 0, 0 };
        if (fat32_iterate(fs, cl, find_cb, &fd) != 1) return -1;
        cl = fd.cl ? fd.cl : fs->root; size = fd.size; isdir = fd.isdir;       /* ".." al raiz => cl 0 */
        path = e;
    }
    f->first = f->cur = cl; f->cur_idx = 0; f->size = size; f->pos = 0; f->isdir = isdir;
    return 0;
}

long fat32_read(struct fat32 *fs, struct fat32_file *f, void *buf, u32 n) {
    u8 *out = buf; u32 done = 0, csz = fs->spc * 512;
    if (n > f->size - f->pos) n = f->size - f->pos;
    while (n) {
        u32 idx = f->pos / csz;
        if (idx < f->cur_idx) { f->cur = f->first; f->cur_idx = 0; }
        while (f->cur_idx < idx) { f->cur = fat_next(fs, f->cur); f->cur_idx++; if (f->cur >= EOC) return done; }
        u32 in_cl = f->pos % csz, sc = in_cl / 512, so = in_cl % 512;
        u8 s[512];
        if (fs->dev->read(fs->dev, cl_lba(fs, f->cur) + sc, 1, s)) return -1;
        u32 chunk = 512 - so; if (chunk > n) chunk = n;
        memcpy(out + done, s + so, chunk);
        done += chunk; f->pos += chunk; n -= chunk;
    }
    return done;
}
