/* Sustitutos de las piezas del kernel que los tests del host no enlazan */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "kernel.h"
struct gpu *gpus[MAX_GPUS]; int ngpus; u64 kmem_bytes = 256ull << 20;
int ksnprintf(char *b, size_t n, const char *f, ...) { va_list a; va_start(a, f); int r = vsnprintf(b, n, f, a); va_end(a); return r; }
void kprintf(const char *f, ...) { (void)f; }
const char *klog_line(int i) { (void)i; return i == KLOG_LINES - 1 ? "[test] linea de log" : ""; }
void *kmalloc(size_t n) { return calloc(1, n); }
void kpanic(const char *m) { fprintf(stderr, "panic: %s\n", m); exit(2); }
