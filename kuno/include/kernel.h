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
int   strcmp(const char *, const char *);

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

/* ---- entrada: teclado y raton PS/2 ---- */
#define MOD_SHIFT 1
#define MOD_CTRL  2
#define MOD_ALT   4
#define MOD_SUPER 8
enum { KEY_UP = 0x101, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_HOME, KEY_END, KEY_PGUP, KEY_PGDN,
       KEY_DEL, KEY_INS, KEY_F1 = 0x110 /* ..KEY_F1+11 = F12 */ };
enum { EV_KEY, EV_MOUSE };
struct event { u8 type, down, buttons; u16 mods; int key, dx, dy; };
struct kbd   { u8 ext, skip, held, caps; };
struct mouse { u8 n; u8 pkt[3]; };
int  kbd_decode(struct kbd *k, u8 byte, struct event *ev);      /* 1 si genera evento */
int  mouse_decode(struct mouse *m, u8 byte, struct event *ev);
void ps2_init(void);
void input_poll(void);
int  ev_pop(struct event *e);
extern int mouse_present;

/* ---- shell ---- */
struct shell { struct fat32 *fs; char cwd[128]; int want_clear, want_exit; };
typedef void (*shell_out)(void *ctx, const char *s);
void shell_init(struct shell *sh, struct fat32 *fs);
void shell_exec(struct shell *sh, const char *line, shell_out out, void *ctx);
int  path_resolve(const char *cwd, const char *arg, char *out, size_t n);
extern u64 kmem_bytes;

/* ---- GUI y consola de texto ---- */
void gui_init(struct fat32 *fs);
void gui_handle(const struct event *e);
void gui_frame(void);
void textcon_run(struct shell *sh);
