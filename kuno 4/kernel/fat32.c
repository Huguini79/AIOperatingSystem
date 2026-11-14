/* Driver FAT32: lectura + escritura (ramdisk). LFN en lectura; escritura en 8.3. */
#include "kernel.h"

#define EOC 0x0FFFFFF8u
static u16 rd16(const u8 *p) { return p[0] | p[1] << 8; }
static u32 rd32(const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
static void wr16(u8 *p, u16 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void wr32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }
static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int upper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

static int try_mount(struct fat32 *fs, u64 lba) {
    u8 s[512];
    if (fs->dev->read(fs->dev, lba, 1, s) || s[510] != 0x55 || s[511] != 0xAA) return -1;
    if (rd16(s + 11) != 512 || !s[13] || rd16(s + 22) != 0 || !rd32(s + 36)) return -1;
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
    u8 s[512];
    if (dev->read(dev, 0, 1, s) || s[510] != 0x55) return -1;
    for (int i = 0; i < 4; i++) {
        u8 *e = s + 446 + 16 * i;
        if ((e[4] == 0x0B || e[4] == 0x0C) && try_mount(fs, rd32(e + 8)) == 0) return 0;
    }
    return -1;
}

static int dev_write(struct fat32 *fs, u64 lba, u32 n, const void *buf) {
    if (!fs->dev->write) return -1;
    return fs->dev->write(fs->dev, lba, n, buf);
}

static u32 fat_next(struct fat32 *fs, u32 c) {
    u8 s[512];
    if (fs->dev->read(fs->dev, fs->part_lba + fs->rsv + (c * 4) / 512, 1, s)) return EOC;
    return rd32(s + (c * 4) % 512) & 0x0FFFFFFF;
}

static int fat_set(struct fat32 *fs, u32 c, u32 val) {
    u64 off = (u64)c * 4;
    u64 sec = fs->part_lba + fs->rsv + off / 512;
    u32 o = (u32)(off % 512);
    u8 s[512];
    if (fs->dev->read(fs->dev, sec, 1, s)) return -1;
    u32 old = rd32(s + o) & 0xF0000000u;
    wr32(s + o, old | (val & 0x0FFFFFFF));
    /* escribir en todas las FATs */
    for (u32 f = 0; f < fs->nfats; f++) {
        if (dev_write(fs, sec + (u64)f * fs->fatsz, 1, s)) return -1;
    }
    return 0;
}

static u32 fat_alloc(struct fat32 *fs) {
    for (u32 c = 2; c < fs->ncl + 2; c++) {
        if (fat_next(fs, c) == 0) {
            if (fat_set(fs, c, EOC)) return 0;
            /* borrar cluster */
            u8 z[512]; memset(z, 0, 512);
            u64 lba = fs->data_lba + (u64)(c - 2) * fs->spc;
            for (u32 s = 0; s < fs->spc; s++)
                if (dev_write(fs, lba + s, 1, z)) return 0;
            return c;
        }
    }
    return 0;
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
                if (e[11] == 0x0F) {
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
                if (e[11] & 0x08) { lfn[0] = 0; continue; }
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
        cl = fd.cl ? fd.cl : fs->root; size = fd.size; isdir = fd.isdir;
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

/* ---- escritura ---- */

/* parent dir cluster + leaf name from path */
static int path_split(struct fat32 *fs, const char *path, u32 *dircl, char *leaf, size_t nleaf) {
    char parent[160]; size_t plen = 0;
    const char *last = path, *p = path;
    while (*p) { if (*p == '/') last = p + 1; p++; }
    while (path < last && plen + 1 < sizeof parent) parent[plen++] = *path++;
    parent[plen] = 0;
    if (!parent[0]) { parent[0] = '/'; parent[1] = 0; }
    struct fat32_file df;
    if (fat32_open(fs, parent, &df) || !df.isdir) return -1;
    *dircl = df.first;
    size_t i = 0;
    for (; last[i] && i + 1 < nleaf; i++) leaf[i] = last[i];
    leaf[i] = 0;
    return leaf[0] ? 0 : -1;
}

static void to_83(const char *name, u8 *out11) {
    memset(out11, ' ', 11);
    int i = 0, j = 0;
    while (name[i] && name[i] != '.' && j < 8) {
        char c = (char)upper(name[i++]);
        if (c == ' ') continue;
        out11[j++] = c;
    }
    while (name[i] && name[i] != '.') i++;
    if (name[i] == '.') {
        i++; j = 8;
        while (name[i] && j < 11) {
            char c = (char)upper(name[i++]);
            if (c == ' ') continue;
            out11[j++] = c;
        }
    }
}

/* buscar hueco libre en directorio; devuelve cluster+offset sector interno */
struct dirslot {
    u32 cl; u32 sec_in_cl; int off; /* byte offset in sector */
    u8 sector[512];
    int found;
};
static int find_free_slot(struct fat32 *fs, u32 dircl, struct dirslot *ds) {
    u32 cl = dircl, prev = 0;
    for (; cl >= 2 && cl < EOC; prev = cl, cl = fat_next(fs, cl)) {
        for (u32 sc = 0; sc < fs->spc; sc++) {
            if (fs->dev->read(fs->dev, cl_lba(fs, cl) + sc, 1, ds->sector)) return -1;
            for (int i = 0; i < 512; i += 32) {
                u8 *e = ds->sector + i;
                if (e[0] == 0x00 || e[0] == 0xE5) {
                    ds->cl = cl; ds->sec_in_cl = sc; ds->off = i; ds->found = 1;
                    return 0;
                }
            }
        }
    }
    /* ampliar directorio con un cluster nuevo */
    if (!prev) prev = dircl;
    u32 nc = fat_alloc(fs);
    if (!nc) return -1;
    if (fat_set(fs, prev, nc)) return -1;
    memset(ds->sector, 0, 512);
    ds->cl = nc; ds->sec_in_cl = 0; ds->off = 0; ds->found = 1;
    return 0;
}

static int write_dir_entry(struct fat32 *fs, u32 dircl, const char *name, u32 first_cl, u32 size, int isdir) {
    struct dirslot ds; memset(&ds, 0, sizeof ds);
    if (find_free_slot(fs, dircl, &ds) || !ds.found) return -1;
    u8 *e = ds.sector + ds.off;
    memset(e, 0, 32);
    to_83(name, e);
    e[11] = isdir ? 0x10 : 0x20;
    wr16(e + 20, (u16)(first_cl >> 16));
    wr16(e + 26, (u16)(first_cl & 0xFFFF));
    wr32(e + 28, size);
    return dev_write(fs, cl_lba(fs, ds.cl) + ds.sec_in_cl, 1, ds.sector);
}

int fat32_create(struct fat32 *fs, const char *path, int isdir) {
    if (!fs || !fs->dev->write) return -1;
    /* ya existe? */
    struct fat32_file f;
    if (fat32_open(fs, path, &f) == 0) return -2;
    u32 dircl; char leaf[64];
    if (path_split(fs, path, &dircl, leaf, sizeof leaf)) return -1;
    u32 cl = fat_alloc(fs);
    if (!cl) return -1;
    if (isdir) {
        /* . y .. */
        u8 s[512]; memset(s, 0, 512);
        memset(s, ' ', 11); s[0] = '.'; s[11] = 0x10;
        wr16(s + 26, (u16)(cl & 0xFFFF)); wr16(s + 20, (u16)(cl >> 16));
        memset(s + 32, ' ', 11); s[32] = '.'; s[33] = '.'; s[32 + 11] = 0x10;
        wr16(s + 32 + 26, (u16)(dircl & 0xFFFF)); wr16(s + 32 + 20, (u16)(dircl >> 16));
        if (dev_write(fs, cl_lba(fs, cl), 1, s)) return -1;
    }
    return write_dir_entry(fs, dircl, leaf, cl, 0, isdir);
}

/* actualizar size en la entrada de directorio del archivo abierto (busca por first cluster) */
static int update_size(struct fat32 *fs, u32 dircl, u32 first, u32 size) {
    for (u32 cl = dircl; cl >= 2 && cl < EOC; cl = fat_next(fs, cl)) {
        for (u32 sc = 0; sc < fs->spc; sc++) {
            u8 s[512];
            if (fs->dev->read(fs->dev, cl_lba(fs, cl) + sc, 1, s)) return -1;
            for (int i = 0; i < 512; i += 32) {
                u8 *e = s + i;
                if (e[0] == 0 || e[0] == 0xE5 || e[11] == 0x0F || (e[11] & 0x08)) continue;
                u32 c = (u32)rd16(e + 20) << 16 | rd16(e + 26);
                if (c == first) {
                    wr32(e + 28, size);
                    return dev_write(fs, cl_lba(fs, cl) + sc, 1, s);
                }
            }
        }
    }
    return -1;
}

long fat32_write(struct fat32 *fs, struct fat32_file *f, const void *buf, u32 n) {
    if (!fs || !fs->dev->write || f->isdir) return -1;
    const u8 *in = buf;
    u32 done = 0, csz = fs->spc * 512;
    while (n) {
        u32 idx = f->pos / csz;
        if (idx < f->cur_idx) { f->cur = f->first; f->cur_idx = 0; }
        while (f->cur_idx < idx) {
            u32 next = fat_next(fs, f->cur);
            if (next >= EOC) {
                u32 nc = fat_alloc(fs);
                if (!nc) return done ? (long)done : -1;
                if (fat_set(fs, f->cur, nc)) return -1;
                f->cur = nc;
            } else f->cur = next;
            f->cur_idx++;
        }
        if (f->cur < 2 || f->cur >= EOC) {
            u32 nc = fat_alloc(fs);
            if (!nc) return done ? (long)done : -1;
            if (f->first < 2) f->first = nc;
            else if (fat_set(fs, f->cur, nc)) return -1;
            f->cur = nc;
        }
        u32 in_cl = f->pos % csz, sc = in_cl / 512, so = in_cl % 512;
        u8 s[512];
        if (so || n < 512 - so) {
            if (fs->dev->read(fs->dev, cl_lba(fs, f->cur) + sc, 1, s)) return -1;
        } else memset(s, 0, 512);
        u32 chunk = 512 - so; if (chunk > n) chunk = n;
        memcpy(s + so, in + done, chunk);
        if (dev_write(fs, cl_lba(fs, f->cur) + sc, 1, s)) return -1;
        done += chunk; f->pos += chunk; n -= chunk;
        if (f->pos > f->size) f->size = f->pos;
    }
    /* intentar actualizar size en raiz (simplificado: solo si archivo en root) */
    update_size(fs, fs->root, f->first, f->size);
    return done;
}

int fat32_unlink(struct fat32 *fs, const char *path) {
    if (!fs || !fs->dev->write) return -1;
    struct fat32_file f;
    if (fat32_open(fs, path, &f)) return -1;
    u32 dircl; char leaf[64];
    if (path_split(fs, path, &dircl, leaf, sizeof leaf)) return -1;
    /* marcar entrada E5 y liberar clusters */
    for (u32 cl = dircl; cl >= 2 && cl < EOC; cl = fat_next(fs, cl)) {
        for (u32 sc = 0; sc < fs->spc; sc++) {
            u8 s[512];
            if (fs->dev->read(fs->dev, cl_lba(fs, cl) + sc, 1, s)) return -1;
            for (int i = 0; i < 512; i += 32) {
                u8 *e = s + i;
                if (e[0] == 0 || e[0] == 0xE5 || e[11] == 0x0F) continue;
                u32 c = (u32)rd16(e + 20) << 16 | rd16(e + 26);
                if (c == f.first) {
                    e[0] = 0xE5;
                    if (dev_write(fs, cl_lba(fs, cl) + sc, 1, s)) return -1;
                    while (c >= 2 && c < EOC) {
                        u32 n = fat_next(fs, c);
                        fat_set(fs, c, 0);
                        c = n;
                    }
                    return 0;
                }
            }
        }
    }
    return -1;
}
