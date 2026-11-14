#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;

void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
int   memcmp(const void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
size_t strlen(const char *);

void kprintf(const char *fmt, ...);
int  ksnprintf(char *buf, size_t n, const char *fmt, ...);
void *kmalloc(size_t n);
void kpanic(const char *msg) __attribute__((noreturn));

/* ---- info de arranque ---- */
struct boot_info {
    u64 fb_addr; u32 fb_pitch, fb_w, fb_h; u8 fb_bpp, fb_type; /* 1=RGB, 2=texto EGA */
    u64 mod_start, mod_end;                  /* modulo 0 (imagen FAT32) */
    u64 mem_bytes;
};
void mb2_parse(u64 info, struct boot_info *bi);

/* ---- E/S de puertos ---- */
static inline void outb(u16 p, u8 v)  { __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline u8   inb(u16 p)         { u8 v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void outw(u16 p, u16 v) { __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
static inline void outl(u16 p, u32 v) { __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p)); }
static inline u32  inl(u16 p)         { u32 v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p)); return v; }

/* ---- dispositivos de bloque ---- */
struct blkdev {
    const char *name; u32 secsz; u64 nsec; void *priv;
    int (*read)(struct blkdev *, u64 lba, u32 n, void *buf);
};
void ramdisk_init(struct blkdev *d, void *base, u64 bytes);

/* ---- FAT32 ---- */
struct fat32 {
    struct blkdev *dev;
    u32 spc, rsv, nfats, fatsz, root, ncl;
    u64 part_lba, data_lba;
};
struct fat32_file { u32 first, cur, cur_idx, size, pos; int isdir; };
typedef int (*fat32_cb)(void *ctx, const char *name, u32 cluster, u32 size, int isdir);

int  fat32_mount(struct fat32 *fs, struct blkdev *dev);
int  fat32_iterate(struct fat32 *fs, u32 dircl, fat32_cb cb, void *ctx);
int  fat32_open(struct fat32 *fs, const char *path, struct fat32_file *f);
long fat32_read(struct fat32 *fs, struct fat32_file *f, void *buf, u32 n);

/* ---- GPU ---- */
struct gpu {
    const char *name; int id;
    u8 *fb; u32 w, h, pitch;                 /* 32 bpp */
    void (*fill)(struct gpu *, int x, int y, int w, int h, u32 color);
};
#define MAX_GPUS 8
extern struct gpu *gpus[MAX_GPUS];
extern int ngpus;
void gpu_register(struct gpu *g);
void gpu_init_firmware_fb(struct boot_info *bi);
void gpu_pci_found(u8 bus, u8 dev, u8 fn, u16 vendor, u16 device, u32 bar0);
void pci_scan(void);

/* ---- GUI ---- */
void gui_render(struct gpu *g);
void klog_push(char c);
const char *klog_line(int i);
#define KLOG_LINES 40

/* ---- syscalls estilo Unix (numeracion de Linux x86_64) ---- */
#define SYS_read 0
#define SYS_write 1
#define SYS_open 2
#define SYS_close 3
#define SYS_exit 60
void  vfs_set_root(struct fat32 *fs);
long  ksyscall(long no, long a1, long a2, long a3);
