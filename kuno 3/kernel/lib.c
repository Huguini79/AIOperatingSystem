#include "kernel.h"

void *memcpy(void *d, const void *s, size_t n) {
    void *r = d; size_t q = n >> 3; n &= 7;
    __asm__ volatile("rep movsq" : "+D"(d), "+S"(s), "+c"(q) :: "memory");
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) :: "memory");
    return r;
}
void *memmove(void *d, const void *s, size_t n) {
    u8 *a = d; const u8 *b = s;
    if (a < b) while (n--) *a++ = *b++; else { a += n; b += n; while (n--) *--a = *--b; }
    return d;
}
void *memset(void *d, int c, size_t n) {
    void *r = d; __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory"); return r;
}
int memcmp(const void *x, const void *y, size_t n) {
    const u8 *a = x, *b = y;
    for (; n; n--, a++, b++) if (*a != *b) return *a - *b;
    return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (u8)*a - (u8)*b; }

/* ---- puerto serie COM1 ---- */
static void serial_putc(char c) {
    while (!(inb(0x3F8 + 5) & 0x20)) ;
    outb(0x3F8, (u8)c);
}
static void serial_init(void) {
    outb(0x3F8 + 1, 0); outb(0x3F8 + 3, 0x80); outb(0x3F8, 1); outb(0x3F8 + 1, 0);
    outb(0x3F8 + 3, 3); outb(0x3F8 + 2, 0xC7); outb(0x3F8 + 4, 0x0B);
}

/* ---- log en anillo (lo muestra la ventana "terminal" de la GUI) ---- */
#define KLOG_W 96
static char klog[KLOG_LINES][KLOG_W];
static int klog_row, klog_col, serial_ready;
void klog_push(char c) {
    if (c == '\n' || klog_col >= KLOG_W - 1) {
        klog_row = (klog_row + 1) % KLOG_LINES; klog_col = 0;
        memset(klog[klog_row], 0, KLOG_W);
        if (c == '\n') return;
    }
    klog[klog_row][klog_col++] = c;
}
const char *klog_line(int i) { return klog[(klog_row + 1 + i) % KLOG_LINES]; }

static void kputc(char c) {
    if (!serial_ready) { serial_init(); serial_ready = 1; }
    if (c == '\n') serial_putc('\r');
    serial_putc(c);
    klog_push(c);
}

/* ---- printf minimo: %s %c %d %u %x %p con modificadores l / ll y ancho 0N ---- */
struct out { char *buf; size_t n, len; int to_console; };
static void oc(struct out *o, char c) {
    if (o->to_console) kputc(c);
    else if (o->len + 1 < o->n) o->buf[o->len] = c;
    o->len++;
}
static void onum(struct out *o, u64 v, int base, int neg, int width, char pad) {
    char t[24]; int i = 0;
    if (!v) t[i++] = '0';
    while (v) { t[i++] = "0123456789abcdef"[v % base]; v /= base; }
    if (neg) t[i++] = '-';
    while (i < width) t[i++] = pad;
    while (i) oc(o, t[--i]);
}
static void vfmt(struct out *o, const char *f, va_list ap) {
    for (; *f; f++) {
        if (*f != '%') { oc(o, *f); continue; }
        f++;
        char pad = ' '; int width = 0, lng = 0;
        if (*f == '0') { pad = '0'; f++; }
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        while (*f == 'l') { lng++; f++; }
        switch (*f) {
        case 's': { const char *s = va_arg(ap, const char *); if (!s) s = "(null)"; while (*s) oc(o, *s++); break; }
        case 'c': oc(o, (char)va_arg(ap, int)); break;
        case 'd': { long long v = lng ? va_arg(ap, long long) : va_arg(ap, int);
                    onum(o, v < 0 ? -v : v, 10, v < 0, width, pad); break; }
        case 'u': onum(o, lng ? va_arg(ap, u64) : va_arg(ap, u32), 10, 0, width, pad); break;
        case 'x': onum(o, lng ? va_arg(ap, u64) : va_arg(ap, u32), 16, 0, width, pad); break;
        case 'p': oc(o, '0'); oc(o, 'x'); onum(o, (u64)va_arg(ap, void *), 16, 0, 16, '0'); break;
        case '%': oc(o, '%'); break;
        default: oc(o, '?');
        }
    }
}
void kprintf(const char *fmt, ...) {
    struct out o = { 0, 0, 0, 1 }; va_list ap; va_start(ap, fmt); vfmt(&o, fmt, ap); va_end(ap);
}
int ksnprintf(char *buf, size_t n, const char *fmt, ...) {
    struct out o = { buf, n, 0, 0 }; va_list ap; va_start(ap, fmt); vfmt(&o, fmt, ap); va_end(ap);
    if (n) buf[o.len < n ? o.len : n - 1] = 0;
    return (int)o.len;
}

/* ---- asignador "bump" (sin free; suficiente para la fase 1) ---- */
static u8 heap[32 << 20] __attribute__((aligned(4096)));
static size_t heap_off;
void *kmalloc(size_t n) {
    n = (n + 15) & ~15UL;
    if (heap_off + n > sizeof heap) kpanic("kmalloc: sin memoria");
    void *p = heap + heap_off; heap_off += n; return p;
}
void kpanic(const char *msg) {
    kprintf("\n*** KERNEL PANIC: %s\n", msg);
    for (;;) __asm__ volatile("cli; hlt");
}
