/* Gestor de ventanas estilo Windows XP + juegos 3D (raycasting), Paint, explorador moderno.
 * Punto fijo 16.16 (sin FPU). Entrada por IRQ; juegos sincronizados a jiffies. */
#include "kernel.h"

extern const u8 font8x8[95][8];

struct surf { u32 *px; int w, h; };
static struct surf surfs[MAX_GPUS];

#define TCOLS   78
#define TLINES  100
#define HIST    16
#define MAXWIN  14
#define TITLEH  22
#define TASKH   30
#define ICON_W  48
#define ICON_H  56
#define PW      240   /* paint canvas */
#define PH      160
#define MAPW    16
#define MAPH    16

enum {
    W_NONE, W_TERM, W_FILES, W_VIEW, W_ABOUT, W_CALC,
    W_SNAKE, W_TETRIS, W_RACE, W_CTRL, W_PAINT, W_MAZE, W_CLOCK
};

struct window {
    int kind, x, y, w, h, maxed, rx, ry, rw, rh;
    char title[48]; u32 accent;
    char lines[TLINES][TCOLS + 1]; int row, col, scroll;
    char in[128]; int inlen; char hist[HIST][128]; int nhist, hpos; struct shell sh;
    struct { char name[48]; int isdir; u32 size; } ent[64]; int nent, sel, top; char path[128];
    char text[4096]; int tlen;
    char cdisp[32]; long cval, cacc; char cop; int cnew;
    int sx[256], sy[256], sn, sdir, sfoodx, sfoody, sscore, sdead, stick;
    int tboard[20][10], tcur[4][4], tpx, tpy, tscore, tlines, tlevel, ttick, tnext, tgameover;
    /* race 3D */
    int rangle, rpx, rpy, rscore, rspeed, rtick, rlives, rdead;
    /* paint */
    u32 *canvas; int pcolor, pbrush, pdrawing;
    /* maze raycaster: pos/dir en fixed 16.16 */
    int mx_fp, my_fp, mdir; /* dir 0..255 */
    int mscore, mwon, mtick;
};

static struct window wins[MAXWIN], *z[MAXWIN];
static int nz, dirty = 1, term_count, tick;
static struct fat32 *g_fs;
static int mx = 200, my = 200, mbtn, drag_dx, drag_dy;
static struct window *dragging;
static int start_open;

/* ---- fixed-point 16.16 + sin table (256 = full circle) ---- */
typedef int fp;
#define ONE 65536
static const int sintab[64] = { /* 0..90 degrees in 16.16, 64 steps */
    0,1608,3216,4821,6424,8022,9616,11204,12785,14359,15924,17479,19024,20557,22078,23586,
    25080,26558,28020,29466,30893,32303,33692,35062,36410,37736,39040,40320,41576,42806,44011,45189,
    46341,47464,48559,49624,50660,51665,52639,53581,54491,55368,56212,57022,57798,58538,59243,59914,
    60547,61145,61705,62228,62714,63162,63572,63944,64277,64571,64827,65043,65220,65358,65457,65516
};
static fp fsin(int a) { /* a in 0..255 */
    a &= 255;
    int q = a >> 6, r = a & 63;
    int v = sintab[r];
    if (q == 1) v = sintab[63 - r];
    else if (q == 2) v = -sintab[r];
    else if (q == 3) v = -sintab[63 - r];
    return v;
}
static fp fcos(int a) { return fsin(a + 64); }
static int fmul(fp a, fp b) { return (int)(((long long)a * b) >> 16); }

/* maze map: 1=wall */
static const u8 mazemap[MAPH][MAPW] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1},
    {1,0,1,1,0,1,0,1,0,1,1,1,0,1,0,1},
    {1,0,1,0,0,1,0,0,0,0,0,1,0,1,0,1},
    {1,0,1,0,1,1,1,1,1,1,0,1,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,1,0,1,1,1,0,1},
    {1,0,1,1,1,1,0,1,0,1,0,0,0,0,0,1},
    {1,0,0,0,0,1,0,1,0,1,1,1,1,1,0,1},
    {1,1,1,1,0,1,0,0,0,0,0,0,0,1,0,1},
    {1,0,0,0,0,1,1,1,1,1,1,1,0,1,0,1},
    {1,0,1,1,0,0,0,0,0,0,0,1,0,0,0,1},
    {1,0,1,0,0,1,1,0,1,1,0,1,1,1,0,1},
    {1,0,1,0,1,1,0,0,0,1,0,0,0,1,0,1},
    {1,0,0,0,0,0,0,1,0,0,0,1,0,0,0,1},
    {1,0,1,1,1,1,0,1,1,1,0,1,0,1,2,1}, /* 2 = exit */
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};

struct desk_icon { int x, y; const char *label; int kind; };
static const struct desk_icon icons[] = {
    {  24,  24, "Terminal",   W_TERM },
    {  24, 100, "Archivos",   W_FILES },
    {  24, 176, "Paint",      W_PAINT },
    {  24, 252, "Snake",      W_SNAKE },
    {  24, 328, "Tetris",     W_TETRIS },
    {  24, 404, "Rally 3D",   W_RACE },
    { 100,  24, "Laberinto",  W_MAZE },
    { 100, 100, "Calculadora",W_CALC },
    { 100, 176, "Reloj",      W_CLOCK },
    { 100, 252, "Sistema",    W_ABOUT },
    { 100, 328, "Control",    W_CTRL },
};
#define NICONS (int)(sizeof icons / sizeof icons[0])

static void scopy(char *d, const char *s, size_t n) {
    size_t i = 0; for (; s[i] && i + 1 < n; i++) d[i] = s[i]; d[i] = 0;
}

static void rect(struct surf *s, int x, int y, int w, int h, u32 c) {
    if (x < 0) { w += x; x = 0; } if (y < 0) { h += y; y = 0; }
    if (x + w > s->w) w = s->w - x; if (y + h > s->h) h = s->h - y;
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) {
        u32 *r = s->px + (size_t)(y + j) * s->w + x;
        for (int i = 0; i < w; i++) r[i] = c;
    }
}
static void frame(struct surf *s, int x, int y, int w, int h, u32 c) {
    rect(s, x, y, w, 1, c); rect(s, x, y + h - 1, w, 1, c);
    rect(s, x, y, 1, h, c); rect(s, x + w - 1, y, 1, h, c);
}
static void glyph(struct surf *s, int x, int y, int ch, u32 col) {
    if (ch < 32 || ch > 126) ch = '?';
    if (x < 0 || y < 0 || x + 8 > s->w || y + 8 > s->h) return;
    for (int r = 0; r < 8; r++) {
        u32 *row = s->px + (size_t)(y + r) * s->w + x;
        u8 bits = font8x8[ch - 32][r];
        for (int b = 0; b < 8; b++) if (bits >> b & 1) row[b] = col;
    }
}
static void text(struct surf *s, int x, int y, const char *str, u32 col, int max) {
    for (; *str && max > 0; str++, max--, x += 8) glyph(s, x, y, *str, col);
}
static void text_shadow(struct surf *s, int x, int y, const char *str, u32 col, int max) {
    text(s, x + 1, y + 1, str, 0x00000000, max); text(s, x, y, str, col, max);
}
static void vline(struct surf *s, int x, int y0, int y1, u32 c) {
    if (x < 0 || x >= s->w) return;
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (y0 < 0) y0 = 0; if (y1 >= s->h) y1 = s->h - 1;
    for (int y = y0; y <= y1; y++) s->px[(size_t)y * s->w + x] = c;
}

static const char *cursor_img[19] = {
    "X           ", "XX          ", "X.X         ", "X..X        ", "X...X       ", "X....X      ", "X.....X     ",
    "X......X    ", "X.......X   ", "X........X  ", "X.....XXXXX ", "X..X..X     ", "X.X X..X    ", "XX  X..X    ",
    "X    X..X   ", "     X..X   ", "      X..X  ", "      X..X  ", "       XX   " };
static void draw_cursor(struct surf *s) {
    for (int j = 0; j < 19; j++)
        for (int i = 0; cursor_img[j][i]; i++) {
            char c = cursor_img[j][i]; int x = mx + i, y = my + j;
            if (c == ' ' || x < 0 || y < 0 || x >= s->w || y >= s->h) continue;
            s->px[(size_t)y * s->w + x] = c == 'X' ? 0x00000000 : 0x00FFFFFF;
        }
}

static void raise_win(struct window *w) {
    int i = 0; while (i < nz && z[i] != w) i++;
    if (i == nz) return;
    for (; i + 1 < nz; i++) z[i] = z[i + 1];
    z[nz - 1] = w; dirty = 1;
}
static void close_win(struct window *w) {
    int i = 0; while (i < nz && z[i] != w) i++;
    for (; i + 1 < nz; i++) z[i] = z[i + 1];
    if (nz) nz--;
    if (dragging == w) dragging = 0;
    if (w->canvas) { /* bump allocator: no free */ w->canvas = 0; }
    w->kind = W_NONE; dirty = 1;
}
static struct window *new_win(int kind) {
    for (int i = 0; i < MAXWIN; i++) if (wins[i].kind == W_NONE) {
        struct window *w = &wins[i]; memset(w, 0, sizeof *w); w->kind = kind;
        w->x = 40 + 24 * (nz % 8); w->y = 30 + 20 * (nz % 8);
        switch (kind) {
        case W_TERM:   w->w = 660; w->h = 360; break;
        case W_FILES:  w->w = 520; w->h = 360; break;
        case W_VIEW:   w->w = 560; w->h = 340; break;
        case W_CALC:   w->w = 220; w->h = 280; break;
        case W_SNAKE:  w->w = 420; w->h = 360; break;
        case W_TETRIS: w->w = 280; w->h = 420; break;
        case W_RACE:   w->w = 520; w->h = 380; break;
        case W_PAINT:  w->w = 280; w->h = 260; break;
        case W_MAZE:   w->w = 480; w->h = 340; break;
        case W_CLOCK:  w->w = 220; w->h = 140; break;
        case W_CTRL:   w->w = 420; w->h = 340; break;
        default:       w->w = 420; w->h = 260; break;
        }
        w->accent = 0x00005AA0;
        z[nz++] = w; dirty = 1; return w;
    }
    return 0;
}

/* ---- terminal (compact) ---- */
static void t_newline(struct window *w) {
    w->col = 0;
    if (w->row < TLINES - 1) w->row++;
    else { memmove(w->lines[0], w->lines[1], (TLINES - 1) * (TCOLS + 1)); memset(w->lines[TLINES - 1], 0, TCOLS + 1); }
}
static void t_put(struct window *w, char c) {
    if (c == '\n') { t_newline(w); return; }
    if (c == '\r') return;
    if (c == '\t') { do t_put(w, ' '); while (w->col & 3); return; }
    if (w->col >= TCOLS) t_newline(w);
    w->lines[w->row][w->col++] = c;
}
static void t_puts(struct window *w, const char *s) { for (; *s; s++) t_put(w, *s); }
static void term_out(void *ctx, const char *s) { t_puts(ctx, s); }
static void t_clear(struct window *w) { memset(w->lines, 0, sizeof w->lines); w->row = w->col = w->scroll = 0; }
static void term_open(void) {
    struct window *w = new_win(W_TERM); if (!w) return;
    ksnprintf(w->title, sizeof w->title, "Terminal %d", ++term_count);
    shell_init(&w->sh, g_fs);
    t_puts(w, "Kuno 0.3 - shell. help / write / mkdir / touch\n");
}
static void term_run(struct window *w) {
    char pr[160];
    ksnprintf(pr, sizeof pr, "kuno:%s$ ", w->sh.cwd);
    t_puts(w, pr); t_puts(w, w->in); t_put(w, '\n');
    if (w->inlen) {
        if (w->nhist == HIST) { memmove(w->hist[0], w->hist[1], (HIST - 1) * 128); w->nhist--; }
        scopy(w->hist[w->nhist++], w->in, 128);
    }
    shell_exec(&w->sh, w->in, term_out, w);
    if (w->sh.want_clear) { t_clear(w); w->sh.want_clear = 0; }
    if (w->col) t_newline(w);
    w->inlen = 0; w->in[0] = 0; w->hpos = 0; w->scroll = 0;
    if (w->sh.want_exit) close_win(w);
}
static void term_key(struct window *w, const struct event *e) {
    int k = e->key;
    if (e->mods & MOD_CTRL) {
        if (k == 'l') t_clear(w);
        else if (k == 'c') { t_puts(w, "^C\n"); w->inlen = 0; w->in[0] = 0; }
        return;
    }
    if (k == KEY_PGUP) { w->scroll += 5; if (w->scroll > w->row) w->scroll = w->row; }
    else if (k == KEY_PGDN) { w->scroll -= 5; if (w->scroll < 0) w->scroll = 0; }
    else if (k == KEY_UP) {
        if (w->hpos < w->nhist) w->hpos++;
        if (w->hpos) { scopy(w->in, w->hist[w->nhist - w->hpos], 128); w->inlen = (int)strlen(w->in); }
    } else if (k == KEY_DOWN) {
        if (w->hpos > 0) w->hpos--;
        if (w->hpos) scopy(w->in, w->hist[w->nhist - w->hpos], 128); else w->in[0] = 0;
        w->inlen = (int)strlen(w->in);
    } else if (k == '\n') term_run(w);
    else if (k == '\b') { if (w->inlen) w->in[--w->inlen] = 0; }
    else if (k >= 32 && k < 127 && w->inlen < 100) { w->in[w->inlen++] = (char)k; w->in[w->inlen] = 0; }
}
static void draw_term(struct surf *s, struct window *w, int focused) {
    int x = w->x + 8, y = w->y + TITLEH + 6, vis = (w->h - TITLEH - 10) / 10, R = w->row + 1;
    int start = R - vis - w->scroll; if (start < 0) start = 0;
    rect(s, w->x + 2, w->y + TITLEH + 2, w->w - 4, w->h - TITLEH - 4, 0x00001018);
    for (int i = start, l = 0; i < R && l < vis; i++, l++, y += 10) {
        if (i == w->row) {
            char pr[160]; int n = ksnprintf(pr, sizeof pr, "kuno:%s$ ", w->sh.cwd); if (n > 50) n = 50;
            text(s, x, y, pr, 0x0070C8FF, 50);
            text(s, x + 8 * n, y, w->in, 0x00FFFFFF, TCOLS - n);
            if (focused && !w->scroll) rect(s, x + 8 * (n + w->inlen), y, 8, 9, 0x00FFFFFF);
        } else text(s, x, y, w->lines[i], 0x0090FF90, TCOLS);
    }
}

/* ---- explorador estilo XP ---- */
struct lctx { struct window *w; int dirs; };
static int list_cb(void *c, const char *name, u32 cl, u32 size, int isdir) {
    struct lctx *l = c; struct window *w = l->w; (void)cl;
    if (name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2]))) return 0;
    if (isdir != l->dirs) return 0;
    if (w->nent >= 64) return 1;
    scopy(w->ent[w->nent].name, name, 48); w->ent[w->nent].isdir = isdir; w->ent[w->nent].size = size; w->nent++;
    return 0;
}
static void files_reload(struct window *w) {
    struct fat32_file f; struct lctx l = { w, 1 };
    w->nent = w->sel = w->top = 0;
    ksnprintf(w->title, sizeof w->title, "Explorador - %s", w->path);
    if (!g_fs || fat32_open(g_fs, w->path, &f) || !f.isdir) return;
    fat32_iterate(g_fs, f.first, list_cb, &l); l.dirs = 0; fat32_iterate(g_fs, f.first, list_cb, &l);
}
static void files_open_win(void) {
    struct window *w = new_win(W_FILES); if (!w) return;
    scopy(w->path, "/", sizeof w->path); files_reload(w);
}
static void view_open(const char *path) {
    struct fat32_file f; if (!g_fs || fat32_open(g_fs, path, &f) || f.isdir) return;
    struct window *w = new_win(W_VIEW); if (!w) return;
    long n = fat32_read(g_fs, &f, w->text, sizeof w->text - 1); if (n < 0) n = 0;
    int m = 0;
    for (long i = 0; i < n; i++) {
        char c = w->text[i]; if (c == '\r') continue;
        w->text[m++] = (c == '\n' || c == '\t' || (c >= 32 && c < 127)) ? c : '.';
    }
    w->text[m] = 0; w->tlen = m;
    const char *base = path; for (const char *p = path; *p; p++) if (*p == '/') base = p + 1;
    ksnprintf(w->title, sizeof w->title, "Ver: %s", base);
}
static void files_activate(struct window *w) {
    if (w->sel >= w->nent) return;
    char p[160]; if (path_resolve(w->path, w->ent[w->sel].name, p, sizeof p)) return;
    if (w->ent[w->sel].isdir) { scopy(w->path, p, sizeof w->path); files_reload(w); }
    else view_open(p);
}
static void files_key(struct window *w, const struct event *e) {
    int vis = (w->h - TITLEH - 40) / 18, k = e->key;
    if (k == KEY_UP) { if (w->sel > 0) w->sel--; }
    else if (k == KEY_DOWN) { if (w->sel + 1 < w->nent) w->sel++; }
    else if (k == KEY_PGUP) { w->sel -= 8; if (w->sel < 0) w->sel = 0; }
    else if (k == KEY_PGDN) { w->sel += 8; if (w->sel >= w->nent) w->sel = w->nent ? w->nent - 1 : 0; }
    else if (k == '\n') files_activate(w);
    else if (k == '\b' || k == KEY_LEFT) {
        char p[160]; if (!path_resolve(w->path, "..", p, sizeof p)) {
            scopy(w->path, p, sizeof w->path); files_reload(w);
        }
    } else if (k == KEY_RIGHT) files_activate(w);
    if (w->sel < w->top) w->top = w->sel;
    if (w->sel >= w->top + vis) w->top = w->sel - vis + 1;
}
static void draw_files(struct surf *s, struct window *w) {
    int tb = w->y + TITLEH + 2;
    rect(s, w->x + 2, w->y + TITLEH + 2, w->w - 4, w->h - TITLEH - 4, 0x00FFFFFF);
    /* toolbar XP */
    rect(s, w->x + 2, tb, w->w - 4, 28, 0x00ECE9D8);
    frame(s, w->x + 6, tb + 4, 56, 20, 0x00A0A0A0);
    text(s, w->x + 14, tb + 9, "Subir", 0x00000000, 6);
    frame(s, w->x + 68, tb + 4, 56, 20, 0x00A0A0A0);
    text(s, w->x + 76, tb + 9, "Abrir", 0x00000000, 6);
    text(s, w->x + 140, tb + 9, w->path, 0x00000080, 40);
    /* header */
    int hy = tb + 30;
    rect(s, w->x + 2, hy, w->w - 4, 18, 0x00D4D0C8);
    text(s, w->x + 12, hy + 5, "Nombre", 0x00000000, 10);
    text(s, w->x + w->w - 140, hy + 5, "Tamano", 0x00000000, 8);
    text(s, w->x + w->w - 70, hy + 5, "Tipo", 0x00000000, 6);
    int vis = (w->h - TITLEH - 56) / 18, x = w->x + 8, y = hy + 20;
    if (!w->nent) text(s, x, y, "(vacio)", 0x00808080, 20);
    for (int i = w->top, l = 0; i < w->nent && l < vis; i++, l++, y += 18) {
        if (i == w->sel) rect(s, w->x + 4, y - 2, w->w - 8, 17, 0x003168C4);
        u32 col = i == w->sel ? 0x00FFFFFF : 0x00000000;
        if (w->ent[i].isdir) {
            rect(s, x, y, 12, 10, 0x00E8C040);
            text(s, x + 18, y + 1, w->ent[i].name, col, 28);
            text(s, w->x + w->w - 70, y + 1, "Carpeta", col, 8);
        } else {
            rect(s, x, y, 10, 12, 0x00F0F0F0);
            frame(s, x, y, 10, 12, 0x00808080);
            text(s, x + 18, y + 1, w->ent[i].name, col, 28);
            char b[24]; ksnprintf(b, sizeof b, "%u", w->ent[i].size);
            text(s, w->x + w->w - 140, y + 1, b, col, 10);
            text(s, w->x + w->w - 70, y + 1, "Archivo", col, 8);
        }
    }
    /* status */
    char st[48]; ksnprintf(st, sizeof st, "%d elementos", w->nent);
    text(s, w->x + 8, w->y + w->h - 16, st, 0x00404040, 20);
}

static void view_key(struct window *w, const struct event *e) {
    if (e->key == KEY_UP && w->scroll > 0) w->scroll--;
    else if (e->key == KEY_DOWN) w->scroll++;
    else if (e->key == KEY_PGUP) { w->scroll -= 10; if (w->scroll < 0) w->scroll = 0; }
    else if (e->key == KEY_PGDN) w->scroll += 10;
    else if (e->key == 27) close_win(w);
}
static void draw_view(struct surf *s, struct window *w) {
    int cols = (w->w - 16) / 8, vis = (w->h - TITLEH - 10) / 10, line = 0, col = 0, x = w->x + 8, y0 = w->y + TITLEH + 6;
    rect(s, w->x + 2, w->y + TITLEH + 2, w->w - 4, w->h - TITLEH - 4, 0x00FFFFF0);
    for (int i = 0; i <= w->tlen; i++) {
        char c = w->text[i];
        if (line >= w->scroll && line < w->scroll + vis && c >= 32)
            glyph(s, x + 8 * col, y0 + (line - w->scroll) * 10, c, 0x00000000);
        if (c == '\n' || c == '\t' || col + 1 >= cols) {
            if (c == '\t') col += 4 - (col & 3);
            if (c == '\n' || col + 1 >= cols) { line++; col = 0; }
            else if (c >= 32) col++;
        } else col++;
    }
}

/* ---- calculadora ---- */
static void calc_open(void) {
    struct window *w = new_win(W_CALC); if (!w) return;
    scopy(w->title, "Calculadora", sizeof w->title);
    scopy(w->cdisp, "0", sizeof w->cdisp);
}
static void calc_set_disp(struct window *w, long v) { ksnprintf(w->cdisp, sizeof w->cdisp, "%ld", v); }
static void calc_apply(struct window *w) {
    if (!w->cop) { w->cacc = w->cval; return; }
    switch (w->cop) {
    case '+': w->cacc += w->cval; break;
    case '-': w->cacc -= w->cval; break;
    case '*': w->cacc *= w->cval; break;
    case '/': if (w->cval) w->cacc /= w->cval; else w->cacc = 0; break;
    }
    w->cval = w->cacc; calc_set_disp(w, w->cval);
}
static long calc_parse(const char *s) {
    long v = 0; int neg = 0, i = 0;
    if (s[0] == '-') { neg = 1; i = 1; }
    while (s[i] >= '0' && s[i] <= '9') v = v * 10 + (s[i++] - '0');
    return neg ? -v : v;
}
static void calc_key(struct window *w, const struct event *e) {
    int k = e->key;
    if (k >= '0' && k <= '9') {
        if (w->cnew) { w->cdisp[0] = 0; w->cnew = 0; }
        int n = (int)strlen(w->cdisp);
        if (n < 14) { w->cdisp[n] = (char)k; w->cdisp[n + 1] = 0; }
        w->cval = calc_parse(w->cdisp);
    } else if (k == '+' || k == '-' || k == '*' || k == '/') {
        calc_apply(w); w->cop = (char)k; w->cnew = 1;
    } else if (k == '\n' || k == '=') { calc_apply(w); w->cop = 0; w->cnew = 1; }
    else if (k == 'c' || k == 'C' || k == 27) {
        w->cval = w->cacc = 0; w->cop = 0; w->cnew = 1; scopy(w->cdisp, "0", sizeof w->cdisp);
    } else if (k == '\b') {
        int n = (int)strlen(w->cdisp);
        if (n > 1) { w->cdisp[n - 1] = 0; w->cval = calc_parse(w->cdisp); }
        else { scopy(w->cdisp, "0", sizeof w->cdisp); w->cval = 0; w->cnew = 1; }
    }
}
static void draw_calc(struct surf *s, struct window *w) {
    int bx = w->x + 10, by = w->y + TITLEH + 8;
    rect(s, w->x + 2, w->y + TITLEH + 2, w->w - 4, w->h - TITLEH - 4, 0x00D4D0C8);
    rect(s, bx, by, w->w - 20, 28, 0x00FFFFFF);
    frame(s, bx, by, w->w - 20, 28, 0x00808080);
    int len = (int)strlen(w->cdisp);
    text(s, bx + w->w - 28 - 8 * len, by + 10, w->cdisp, 0x00000000, 16);
    static const char *labs[] = { "7","8","9","/","4","5","6","*","1","2","3","-","0","C","=","+" };
    for (int i = 0; i < 16; i++) {
        int col = i % 4, row = i / 4;
        int x = bx + col * 48, y = by + 40 + row * 42;
        rect(s, x, y, 44, 36, 0x00E8E4D8); frame(s, x, y, 44, 36, 0x00A0A0A0);
        text(s, x + 16, y + 14, labs[i], 0x00000000, 2);
    }
}

/* ---- Snake ---- */
static void snake_place_food(struct window *w) {
    for (int tries = 0; tries < 200; tries++) {
        int fx = (tick * 7 + tries * 13) % 18 + 1;
        int fy = (tick * 11 + tries * 17) % 14 + 1;
        int ok = 1;
        for (int i = 0; i < w->sn; i++) if (w->sx[i] == fx && w->sy[i] == fy) { ok = 0; break; }
        if (ok) { w->sfoodx = fx; w->sfoody = fy; return; }
    }
    w->sfoodx = 10; w->sfoody = 8;
}
static void snake_reset(struct window *w) {
    w->sn = 3; w->sx[0] = 5; w->sy[0] = 7; w->sx[1] = 4; w->sy[1] = 7; w->sx[2] = 3; w->sy[2] = 7;
    w->sdir = 1; w->sscore = 0; w->sdead = 0; w->stick = 0; snake_place_food(w);
}
static void snake_open(void) {
    struct window *w = new_win(W_SNAKE); if (!w) return;
    scopy(w->title, "Snake", sizeof w->title); snake_reset(w);
}
static void snake_step(struct window *w) {
    if (w->sdead) return;
    int nx = w->sx[0], ny = w->sy[0];
    if (w->sdir == 0) ny--; else if (w->sdir == 1) nx++; else if (w->sdir == 2) ny++; else nx--;
    if (nx < 0 || nx >= 20 || ny < 0 || ny >= 16) { w->sdead = 1; return; }
    for (int i = 0; i < w->sn; i++) if (w->sx[i] == nx && w->sy[i] == ny) { w->sdead = 1; return; }
    for (int i = w->sn; i > 0; i--) { w->sx[i] = w->sx[i - 1]; w->sy[i] = w->sy[i - 1]; }
    w->sx[0] = nx; w->sy[0] = ny;
    if (nx == w->sfoodx && ny == w->sfoody) {
        w->sn++; if (w->sn > 250) w->sn = 250; w->sscore += 10; snake_place_food(w);
    }
}
static void snake_key(struct window *w, const struct event *e) {
    int k = e->key;
    if (w->sdead && (k == '\n' || k == 'r' || k == 'R')) { snake_reset(w); return; }
    if (k == KEY_UP && w->sdir != 2) w->sdir = 0;
    else if (k == KEY_RIGHT && w->sdir != 3) w->sdir = 1;
    else if (k == KEY_DOWN && w->sdir != 0) w->sdir = 2;
    else if (k == KEY_LEFT && w->sdir != 1) w->sdir = 3;
}
static void draw_snake(struct surf *s, struct window *w) {
    int cell = 18, ox = w->x + 20, oy = w->y + TITLEH + 20;
    rect(s, w->x + 2, w->y + TITLEH + 2, w->w - 4, w->h - TITLEH - 4, 0x00102010);
    for (int y = 0; y < 16; y++) for (int x = 0; x < 20; x++)
        rect(s, ox + x * cell, oy + y * cell, cell - 1, cell - 1, 0x00203020);
    rect(s, ox + w->sfoodx * cell, oy + w->sfoody * cell, cell - 1, cell - 1, 0x00E04040);
    for (int i = 0; i < w->sn; i++)
        rect(s, ox + w->sx[i] * cell, oy + w->sy[i] * cell, cell - 1, cell - 1, i ? 0x0020A040 : 0x0040E060);
    char b[32]; ksnprintf(b, sizeof b, "Puntos: %d", w->sscore);
    text(s, w->x + 12, w->y + w->h - 18, b, 0x00FFFFFF, 16);
    if (w->sdead) text_shadow(s, w->x + 80, w->y + TITLEH + 140, "GAME OVER - R reinicia", 0x00FF6060, 24);
}

/* ---- Tetris ---- */
static const int tetrominoes[7][4][4] = {
    {{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}},
    {{1,1,0,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
    {{0,1,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
    {{0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
    {{1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
    {{1,0,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
    {{0,0,1,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
};
static const u32 tcolors[8] = {0,0x00F0F000,0x00F0A000,0x00A000F0,0x0000F000,0x00F00000,0x000000F0,0x00F08000};
static void tetris_new_piece(struct window *w) {
    w->tnext = (tick * 3 + w->tscore) % 7;
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++)
        w->tcur[r][c] = tetrominoes[w->tnext][r][c] ? (w->tnext + 1) : 0;
    w->tpx = 3; w->tpy = 0;
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++)
        if (w->tcur[r][c] && w->tboard[w->tpy + r][w->tpx + c]) { w->tgameover = 1; return; }
}
static void tetris_reset(struct window *w) {
    memset(w->tboard, 0, sizeof w->tboard);
    w->tscore = 0; w->tlines = 0; w->tlevel = 1; w->ttick = 0; w->tgameover = 0;
    tetris_new_piece(w);
}
static void tetris_open(void) {
    struct window *w = new_win(W_TETRIS); if (!w) return;
    scopy(w->title, "Tetris", sizeof w->title); tetris_reset(w);
}
static int tetris_valid(struct window *w, int px, int py, int rot[4][4]) {
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) {
        if (!rot[r][c]) continue;
        int x = px + c, y = py + r;
        if (x < 0 || x >= 10 || y >= 20) return 0;
        if (y >= 0 && w->tboard[y][x]) return 0;
    }
    return 1;
}
static void tetris_rotate(struct window *w) {
    int tmp[4][4];
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) tmp[c][3 - r] = w->tcur[r][c];
    if (tetris_valid(w, w->tpx, w->tpy, tmp))
        for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) w->tcur[r][c] = tmp[r][c];
}
static void tetris_lock(struct window *w) {
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++)
        if (w->tcur[r][c] && w->tpy + r >= 0) w->tboard[w->tpy + r][w->tpx + c] = w->tcur[r][c];
    int cleared = 0;
    for (int y = 19; y >= 0; y--) {
        int full = 1;
        for (int x = 0; x < 10; x++) if (!w->tboard[y][x]) { full = 0; break; }
        if (full) {
            cleared++;
            for (int yy = y; yy > 0; yy--) for (int x = 0; x < 10; x++) w->tboard[yy][x] = w->tboard[yy - 1][x];
            for (int x = 0; x < 10; x++) w->tboard[0][x] = 0;
            y++;
        }
    }
    if (cleared) {
        w->tlines += cleared; w->tscore += cleared * cleared * 100 * w->tlevel;
        w->tlevel = 1 + w->tlines / 10;
    }
    tetris_new_piece(w);
}
static void tetris_step(struct window *w) {
    if (w->tgameover) return;
    if (tetris_valid(w, w->tpx, w->tpy + 1, w->tcur)) w->tpy++;
    else tetris_lock(w);
}
static void tetris_key(struct window *w, const struct event *e) {
    int k = e->key;
    if (w->tgameover && (k == '\n' || k == 'r' || k == 'R')) { tetris_reset(w); return; }
    if (k == KEY_LEFT && tetris_valid(w, w->tpx - 1, w->tpy, w->tcur)) w->tpx--;
    else if (k == KEY_RIGHT && tetris_valid(w, w->tpx + 1, w->tpy, w->tcur)) w->tpx++;
    else if (k == KEY_DOWN) tetris_step(w);
    else if (k == KEY_UP || k == 'x') tetris_rotate(w);
    else if (k == ' ') { while (tetris_valid(w, w->tpx, w->tpy + 1, w->tcur)) w->tpy++; tetris_lock(w); }
}
static void draw_tetris(struct surf *s, struct window *w) {
    int cell = 16, ox = w->x + 20, oy = w->y + TITLEH + 12;
    rect(s, w->x + 2, w->y + TITLEH + 2, w->w - 4, w->h - TITLEH - 4, 0x00101820);
    frame(s, ox - 2, oy - 2, 10 * cell + 4, 20 * cell + 4, 0x006080A0);
    for (int y = 0; y < 20; y++) for (int x = 0; x < 10; x++)
        rect(s, ox + x * cell, oy + y * cell, cell - 1, cell - 1,
             w->tboard[y][x] ? tcolors[w->tboard[y][x]] : 0x00081828);
    if (!w->tgameover)
        for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++)
            if (w->tcur[r][c] && w->tpy + r >= 0)
                rect(s, ox + (w->tpx + c) * cell, oy + (w->tpy + r) * cell, cell - 1, cell - 1, tcolors[w->tcur[r][c]]);
    char b[40];
    ksnprintf(b, sizeof b, "Pts %d  Lin %d  Nv %d", w->tscore, w->tlines, w->tlevel);
    text(s, ox, oy + 20 * cell + 8, b, 0x00FFFFFF, 28);
    if (w->tgameover) text_shadow(s, ox + 8, oy + 140, "GAME OVER", 0x00FF4040, 12);
}

/* ---- Rally 3D con proyeccion perspectiva (punto fijo) ---- */
static void race_reset(struct window *w) {
    w->rpx = 8 * ONE; w->rpy = 14 * ONE; w->rangle = 192; /* looking up */
    w->rscore = 0; w->rspeed = 0; w->rtick = 0; w->rlives = 3; w->rdead = 0;
}
static void race_open(void) {
    struct window *w = new_win(W_RACE); if (!w) return;
    scopy(w->title, "Rally 3D", sizeof w->title); race_reset(w);
}
static void race_step(struct window *w) {
    if (w->rdead) return;
    w->rtick++;
    if (w->rspeed) {
        fp dx = fmul(fsin(w->rangle), w->rspeed * (ONE / 32));
        fp dy = fmul(fcos(w->rangle), w->rspeed * (ONE / 32));
        int nx = (w->rpx + dx) >> 16, ny = (w->rpy - dy) >> 16;
        if (nx >= 1 && nx < MAPW - 1 && ny >= 1 && ny < MAPH - 1 && !mazemap[ny][nx]) {
            w->rpx += dx; w->rpy -= dy;
            w->rscore += w->rspeed;
        }
        if (mazemap[ny][nx] == 2) { w->rscore += 1000; /* loop */ }
    }
}
static void race_key(struct window *w, const struct event *e) {
    int k = e->key;
    if (w->rdead && (k == 'r' || k == '\n')) { race_reset(w); return; }
    if (k == KEY_LEFT) w->rangle = (w->rangle - 4) & 255;
    else if (k == KEY_RIGHT) w->rangle = (w->rangle + 4) & 255;
    else if (k == KEY_UP) { if (w->rspeed < 12) w->rspeed++; }
    else if (k == KEY_DOWN) { if (w->rspeed > 0) w->rspeed--; }
}
static void draw_race(struct surf *s, struct window *w) {
    int ox = w->x + 4, oy = w->y + TITLEH + 4;
    int rw = w->w - 8, rh = w->h - TITLEH - 28;
    int horizon = oy + rh / 3;
    /* cielo */
    rect(s, ox, oy, rw, horizon - oy, 0x0058A8E0);
    /* suelo con perspectiva por scanline */
    for (int y = horizon; y < oy + rh; y++) {
        int row = y - horizon;
        int dist = 1 + row * 2;
        u32 grass = (row & 4) ? 0x0038A038 : 0x00289028;
        rect(s, ox, y, rw, 1, grass);
        /* carretera trapezoidal */
        int half = rw / 2;
        int road_w = half * 8 / (4 + dist / 8);
        if (road_w > half - 4) road_w = half - 4;
        int cx = ox + half + ((w->rangle - 192) * road_w) / 64;
        rect(s, cx - road_w, y, road_w * 2, 1, 0x00404040);
        if ((row + w->rtick * 2) % 16 < 4)
            rect(s, cx - 2, y, 4, 1, 0x00E0E0A0);
    }
    /* mini HUD */
    char b[48]; ksnprintf(b, sizeof b, "Pts %d  Vel %d  Ang %d", w->rscore, w->rspeed, w->rangle);
    text(s, ox + 8, oy + rh + 6, b, 0x00FFFFFF, 40);
    text(s, ox + 8, oy + 8, "Flechas: girar/acelerar", 0x00FFFFFF, 28);
}

/* ---- Laberinto 3D raycasting (Wolfenstein-style) ---- */
static void maze_reset(struct window *w) {
    w->mx_fp = 3 * ONE + ONE / 2;
    w->my_fp = 3 * ONE + ONE / 2;
    w->mdir = 0;
    w->mscore = 0; w->mwon = 0; w->mtick = 0;
}
static void maze_open(void) {
    struct window *w = new_win(W_MAZE); if (!w) return;
    scopy(w->title, "Laberinto 3D", sizeof w->title); maze_reset(w);
}
static void maze_move(struct window *w, int forward) {
    fp dist = forward ? (ONE / 6) : -(ONE / 6);
    fp dx = fmul(fsin(w->mdir), dist);
    fp dy = fmul(fcos(w->mdir), dist);
    int nx = (w->mx_fp + dx) >> 16, ny = (w->my_fp - dy) >> 16;
    if (nx >= 0 && nx < MAPW && ny >= 0 && ny < MAPH) {
        if (mazemap[ny][nx] == 0) {
            w->mx_fp += dx; w->my_fp -= dy;
        } else if (mazemap[ny][nx] == 2) {
            w->mx_fp += dx; w->my_fp -= dy; w->mwon = 1; w->mscore += 500;
        }
    }
}
static void maze_key(struct window *w, const struct event *e) {
    int k = e->key;
    if (w->mwon && (k == 'r' || k == '\n')) { maze_reset(w); return; }
    if (k == KEY_LEFT) w->mdir = (w->mdir - 6) & 255;
    else if (k == KEY_RIGHT) w->mdir = (w->mdir + 6) & 255;
    else if (k == KEY_UP) maze_move(w, 1);
    else if (k == KEY_DOWN) maze_move(w, 0);
}
static void draw_maze(struct surf *s, struct window *w) {
    int ox = w->x + 4, oy = w->y + TITLEH + 4;
    int rw = w->w - 8, rh = w->h - TITLEH - 28;
    int fov = 64; /* ~90 degrees in 256-circle units */
    for (int col = 0; col < rw; col++) {
        int ra = (w->mdir - fov / 2 + (col * fov) / rw) & 255;
        fp rx = w->mx_fp, ry = w->my_fp;
        fp rdx = fsin(ra) / 16; /* step */
        fp rdy = -fcos(ra) / 16;
        int hit = 0, side = 0, steps = 0;
        u8 cell = 0;
        while (!hit && steps < 400) {
            rx += rdx; ry += rdy; steps++;
            int mx = rx >> 16, my = ry >> 16;
            if (mx < 0 || my < 0 || mx >= MAPW || my >= MAPH) { hit = 1; cell = 1; break; }
            cell = mazemap[my][mx];
            if (cell) {
                hit = 1;
                /* crude side: which delta larger this step */
                side = (rdx < 0 ? -rdx : rdx) > (rdy < 0 ? -rdy : rdy);
            }
        }
        int dist = steps;
        if (dist < 1) dist = 1;
        int wall_h = (rh * 12) / dist;
        if (wall_h > rh) wall_h = rh;
        int y0 = oy + (rh - wall_h) / 2;
        int y1 = y0 + wall_h;
        /* cielo / suelo */
        vline(s, ox + col, oy, y0, 0x00306090);
        vline(s, ox + col, y1, oy + rh, 0x00404030);
        /* muro */
        u32 colr = cell == 2 ? 0x00E0C020 : (side ? 0x007070A0 : 0x009090C0);
        if (dist > 80) colr = (colr >> 1) & 0x007F7F7F;
        vline(s, ox + col, y0, y1, colr);
    }
    char b[48];
    ksnprintf(b, sizeof b, "Laberinto 3D  pts %d  %s", w->mscore, w->mwon ? "SALISTE! R=otra" : "busca la salida dorada");
    text(s, ox + 4, oy + rh + 6, b, 0x00FFFFFF, 50);
}

/* ---- Paint ---- */
static const u32 palette[8] = {
    0x00000000, 0x00FFFFFF, 0x00E00000, 0x0000A000,
    0x000000E0, 0x00E0E000, 0x00E08000, 0x008000E0
};
static void paint_open(void) {
    struct window *w = new_win(W_PAINT); if (!w) return;
    scopy(w->title, "Paint", sizeof w->title);
    w->canvas = kmalloc((size_t)PW * PH * 4);
    if (w->canvas) {
        for (int i = 0; i < PW * PH; i++) w->canvas[i] = 0x00FFFFFF;
    }
    w->pcolor = 0; w->pbrush = 2; w->pdrawing = 0;
}
static void paint_plot(struct window *w, int cx, int cy) {
    if (!w->canvas) return;
    int r = w->pbrush;
    for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
        int x = cx + dx, y = cy + dy;
        if (x >= 0 && x < PW && y >= 0 && y < PH)
            w->canvas[y * PW + x] = palette[w->pcolor & 7];
    }
}
static void draw_paint(struct surf *s, struct window *w) {
    int ox = w->x + 8, oy = w->y + TITLEH + 8;
    rect(s, w->x + 2, w->y + TITLEH + 2, w->w - 4, w->h - TITLEH - 4, 0x00D4D0C8);
    /* canvas */
    frame(s, ox - 1, oy - 1, PW + 2, PH + 2, 0x00808080);
    if (w->canvas) {
        for (int y = 0; y < PH; y++) {
            u32 *src = w->canvas + y * PW;
            u32 *dst = s->px + (size_t)(oy + y) * s->w + ox;
            if (oy + y >= 0 && oy + y < s->h)
                for (int x = 0; x < PW && ox + x < s->w; x++)
                    if (ox + x >= 0) dst[x] = src[x];
        }
    }
    /* palette */
    int py = oy + PH + 8;
    for (int i = 0; i < 8; i++) {
        rect(s, ox + i * 28, py, 24, 16, palette[i]);
        if (i == w->pcolor) frame(s, ox + i * 28, py, 24, 16, 0x00FF0000);
        else frame(s, ox + i * 28, py, 24, 16, 0x00404040);
    }
}

/* ---- clock ---- */
static void clock_open(void) {
    struct window *w = new_win(W_CLOCK); if (!w) return;
    scopy(w->title, "Reloj", sizeof w->title);
}
static void draw_clock(struct surf *s, struct window *w) {
    int ox = w->x + 12, oy = w->y + TITLEH + 16;
    rect(s, w->x + 2, w->y + TITLEH + 2, w->w - 4, w->h - TITLEH - 4, 0x00001020);
    u64 sec = sched_jiffies() / 100;
    u32 h = (u32)((sec / 3600) % 24), m = (u32)((sec / 60) % 60), s2 = (u32)(sec % 60);
    char clk[24];
    ksnprintf(clk, sizeof clk, "%02u:%02u:%02u", h, m, s2);
    text_shadow(s, ox + 20, oy + 20, clk, 0x0040FF80, 12);
    ksnprintf(clk, sizeof clk, "uptime %llu s", sec);
    text(s, ox + 20, oy + 50, clk, 0x00A0C0FF, 18);
    ksnprintf(clk, sizeof clk, "jiffies %llu", sched_jiffies());
    text(s, ox + 20, oy + 66, clk, 0x008090A0, 20);
}

/* ---- about / control ---- */
static void draw_about(struct surf *s, struct window *w) {
    int x = w->x + 14, y = w->y + TITLEH + 12;
    rect(s, w->x + 2, w->y + TITLEH + 2, w->w - 4, w->h - TITLEH - 4, 0x00ECE9D8);
    text(s, x, y, "KUNO 0.3 - Windows XP style + 3D", 0x00000080, 40); y += 16;
    char b[80];
    ksnprintf(b, sizeof b, "Mem %llu MiB  GPUs %d  Mouse %s", kmem_bytes >> 20, ngpus, mouse_present ? "Si" : "No");
    text(s, x, y, b, 0x00000000, 48); y += 14;
    static const char *h[] = {
        "Iconos del escritorio o Menu Inicio",
        "Alt+T Term  Alt+F Archivos  Alt+P Paint",
        "Alt+S Snake  Alt+R Tetris  Alt+G Rally",
        "Alt+Z Laberinto 3D  Alt+O Reloj",
        "Juegos: flechas, R reinicia",
        "Laberinto: busca la salida dorada"
    };
    for (unsigned i = 0; i < sizeof h / sizeof *h; i++, y += 13) text(s, x, y, h[i], 0x00202020, 48);
}
static void draw_ctrl(struct surf *s, struct window *w) {
    int x = w->x + 14, y = w->y + TITLEH + 12;
    rect(s, w->x + 2, w->y + TITLEH + 2, w->w - 4, w->h - TITLEH - 4, 0x00ECE9D8);
    text(s, x, y, "Panel de control", 0x00000080, 24); y += 16;
    char b[80];
    ksnprintf(b, sizeof b, "Uptime %llu s  jiffies %llu", sched_jiffies() / 100, sched_jiffies());
    text(s, x, y, b, 0x00000000, 44); y += 12;
    ksnprintf(b, sizeof b, "Context switches %llu", sched_switches());
    text(s, x, y, b, 0x00000000, 40); y += 14;
    text(s, x, y, "Tareas:", 0x00000080, 12); y += 12;
    for (int i = 0; i < 8; i++) {
        char name[20]; int st; u64 tk, sw;
        sched_task_info(i, name, sizeof name, &st, &tk, &sw);
        if (st < 0) continue;
        ksnprintf(b, sizeof b, " %d %-6s t=%llu %s", i, sched_state_str(st), tk, name);
        text(s, x, y, b, 0x00000000, 44); y += 12;
    }
    if ((sched_jiffies() & 7) == 0) dirty = 1;
}

/* ---- icons / open ---- */
static void draw_icon(struct surf *s, int ix, int iy, const char *label, int kind) {
    u32 col = 0x00C0C0C0;
    if (kind == W_TERM) col = 0x002060A0;
    else if (kind == W_FILES) col = 0x00E0C040;
    else if (kind == W_PAINT) col = 0x00E080A0;
    else if (kind == W_SNAKE) col = 0x0020A040;
    else if (kind == W_TETRIS) col = 0x00A040C0;
    else if (kind == W_RACE) col = 0x00C04020;
    else if (kind == W_MAZE) col = 0x004060A0;
    else if (kind == W_CALC) col = 0x00A0A0A0;
    else if (kind == W_CLOCK) col = 0x0020C0C0;
    else if (kind == W_ABOUT) col = 0x004080C0;
    else if (kind == W_CTRL) col = 0x00808060;
    rect(s, ix + 8, iy + 4, 32, 28, col);
    frame(s, ix + 8, iy + 4, 32, 28, 0x00000000);
    glyph(s, ix + 20, iy + 12, label[0], 0x00FFFFFF);
    int len = (int)strlen(label);
    int tx = ix + 24 - 4 * len; if (tx < ix) tx = ix;
    text_shadow(s, tx, iy + 38, label, 0x00FFFFFF, 12);
}
static void open_by_kind(int kind) {
    switch (kind) {
    case W_TERM: term_open(); break;
    case W_FILES: files_open_win(); break;
    case W_CALC: calc_open(); break;
    case W_SNAKE: snake_open(); break;
    case W_TETRIS: tetris_open(); break;
    case W_RACE: race_open(); break;
    case W_PAINT: paint_open(); break;
    case W_MAZE: maze_open(); break;
    case W_CLOCK: clock_open(); break;
    case W_ABOUT: {
        for (int i = 0; i < MAXWIN; i++) if (wins[i].kind == W_ABOUT) { raise_win(&wins[i]); return; }
        struct window *w = new_win(W_ABOUT);
        if (w) scopy(w->title, "Acerca de Kuno", sizeof w->title);
        break;
    }
    case W_CTRL: {
        for (int i = 0; i < MAXWIN; i++) if (wins[i].kind == W_CTRL) { raise_win(&wins[i]); return; }
        struct window *w = new_win(W_CTRL);
        if (w) scopy(w->title, "Panel de control", sizeof w->title);
        break;
    }
    }
}

static void draw_window(struct surf *s, struct window *w, int focused) {
    u32 title_col = focused ? 0x00005AA0 : 0x007A96B8;
    u32 border = focused ? 0x00003C78 : 0x006080A0;
    rect(s, w->x + 3, w->y + 3, w->w, w->h, 0x00000040);
    rect(s, w->x, w->y, w->w, w->h, 0x00ECE9D8);
    rect(s, w->x, w->y, w->w, TITLEH, title_col);
    frame(s, w->x, w->y, w->w, w->h, border);
    text(s, w->x + 8, w->y + 7, w->title, 0x00FFFFFF, (w->w - 40) / 8);
    rect(s, w->x + w->w - 20, w->y + 4, 14, 14, 0x00C04040);
    text(s, w->x + w->w - 17, w->y + 7, "X", 0x00FFFFFF, 1);
    switch (w->kind) {
    case W_TERM: draw_term(s, w, focused); break;
    case W_FILES: draw_files(s, w); break;
    case W_VIEW: draw_view(s, w); break;
    case W_ABOUT: draw_about(s, w); break;
    case W_CALC: draw_calc(s, w); break;
    case W_SNAKE: draw_snake(s, w); break;
    case W_TETRIS: draw_tetris(s, w); break;
    case W_RACE: draw_race(s, w); break;
    case W_PAINT: draw_paint(s, w); break;
    case W_MAZE: draw_maze(s, w); break;
    case W_CLOCK: draw_clock(s, w); break;
    case W_CTRL: draw_ctrl(s, w); break;
    }
}
static void draw_desktop(struct surf *s) {
    for (int y = 0; y < s->h - TASKH; y++) {
        int t = y * 255 / (s->h - TASKH ? s->h - TASKH : 1);
        u32 c = t < 140
            ? ((0x30 + t / 4) << 16) | ((0x80 + t / 3) << 8) | (0xC8 + t / 8)
            : ((0x20 + (t - 140) / 6) << 16) | ((0x70 + (t - 140) / 4) << 8) | 0x20;
        rect(s, 0, y, s->w, 1, c);
    }
    for (int i = 0; i < NICONS; i++)
        draw_icon(s, icons[i].x, icons[i].y, icons[i].label, icons[i].kind);
}
static void draw_taskbar(struct surf *s) {
    int ty = s->h - TASKH;
    rect(s, 0, ty, s->w, TASKH, 0x00208030);
    rect(s, 0, ty, s->w, 2, 0x0040B050);
    rect(s, 4, ty + 4, 70, 22, 0x0030A040);
    text(s, 14, ty + 10, "Inicio", 0x00FFFFFF, 8);
    static const struct { int x, w; const char *l; } btn[] = {
        {82,40,"Term"},{126,40,"Files"},{170,40,"Paint"},{214,40,"Snake"},
        {258,44,"Tetris"},{306,40,"Rally"},{350,40,"Maze"},{394,40,"Clock"}
    };
    for (int i = 0; i < 8; i++) {
        rect(s, btn[i].x, ty + 5, btn[i].w, 20, 0x00287030);
        text(s, btn[i].x + 4, ty + 10, btn[i].l, 0x00FFFFFF, 6);
    }
    int bx = 450;
    for (int i = 0; i < MAXWIN; i++) if (wins[i].kind != W_NONE) {
        int active = nz && z[nz - 1] == &wins[i];
        rect(s, bx, ty + 4, 90, 22, active ? 0x0040A050 : 0x00286028);
        text(s, bx + 4, ty + 10, wins[i].title, 0x00FFFFFF, 10);
        bx += 94; if (bx > s->w - 120) break;
    }
    u64 sec = sched_jiffies() / 100;
    char clk[16];
    ksnprintf(clk, sizeof clk, "%02u:%02u:%02u",
              (u32)((sec / 3600) % 24), (u32)((sec / 60) % 60), (u32)(sec % 60));
    rect(s, s->w - 88, ty + 4, 82, 22, 0x00186020);
    text(s, s->w - 80, ty + 10, clk, 0x00FFFFFF, 10);
}
static void render(struct surf *s) {
    draw_desktop(s);
    for (int i = 0; i < nz; i++) draw_window(s, z[i], i == nz - 1);
    draw_taskbar(s);
    if (start_open) {
        int sx = 4, sy = s->h - TASKH - 280;
        rect(s, sx, sy, 180, 276, 0x00ECE9D8);
        frame(s, sx, sy, 180, 276, 0x00005AA0);
        rect(s, sx, sy, 180, 24, 0x00005AA0);
        text(s, sx + 8, sy + 8, "Menu Inicio", 0x00FFFFFF, 14);
        static const char *items[] = {
            "Terminal","Archivos","Paint","Snake","Tetris","Rally 3D",
            "Laberinto 3D","Calculadora","Reloj","Sistema","Panel control"
        };
        for (int i = 0; i < 11; i++)
            text(s, sx + 12, sy + 32 + i * 22, items[i], 0x00000000, 18);
    }
    draw_cursor(s);
}

void gui_frame(void) {
    tick++;
    static u64 last_jiff;
    u64 j = sched_jiffies();
    int any = 0;
    if (j != last_jiff) {
        last_jiff = j;
        for (int i = 0; i < MAXWIN; i++) {
            if (wins[i].kind == W_SNAKE && !wins[i].sdead) {
                wins[i].stick++;
                if (wins[i].stick >= 8) { wins[i].stick = 0; snake_step(&wins[i]); any = 1; }
            }
            if (wins[i].kind == W_TETRIS && !wins[i].tgameover) {
                wins[i].ttick++;
                int speed = 14 - wins[i].tlevel; if (speed < 4) speed = 4;
                if (wins[i].ttick >= speed) { wins[i].ttick = 0; tetris_step(&wins[i]); any = 1; }
            }
            if (wins[i].kind == W_RACE && !wins[i].rdead) {
                if ((j & 1) == 0) { race_step(&wins[i]); any = 1; }
            }
            if (wins[i].kind == W_CLOCK || wins[i].kind == W_CTRL) any = 1;
        }
        if ((j % 25) == 0) any = 1;
    }
    if (any) dirty = 1;
    if (!dirty) return;
    dirty = 0;
    for (int i = 0; i < ngpus; i++) {
        struct gpu *g = gpus[i]; struct surf *s = &surfs[i];
        if (!s->px) { s->px = kmalloc((size_t)g->w * g->h * 4); s->w = (int)g->w; s->h = (int)g->h; }
        render(s);
        for (int y = 0; y < s->h; y++)
            memcpy(g->fb + (size_t)y * g->pitch, s->px + (size_t)y * s->w, (size_t)s->w * 4);
    }
}

static void toggle_max(struct window *w) {
    int sw = (int)gpus[0]->w, sh = (int)gpus[0]->h;
    if (!w->maxed) {
        w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h;
        w->x = w->y = 0; w->w = sw; w->h = sh - TASKH; w->maxed = 1;
    } else { w->x = w->rx; w->y = w->ry; w->w = w->rw; w->h = w->rh; w->maxed = 0; }
}
static int wm_key(const struct event *e) {
    struct window *top = nz ? z[nz - 1] : 0;
    switch (e->key) {
    case '\t':
        if (nz > 1) {
            struct window *b = z[0];
            for (int i = 0; i + 1 < nz; i++) z[i] = z[i + 1];
            z[nz - 1] = b;
        }
        return 1;
    case 't': case 'T': term_open(); return 1;
    case 'f': case 'F': files_open_win(); return 1;
    case 'c': case 'C': calc_open(); return 1;
    case 's': case 'S': snake_open(); return 1;
    case 'r': case 'R': tetris_open(); return 1;
    case 'g': case 'G': race_open(); return 1;
    case 'p': case 'P': paint_open(); return 1;
    case 'z': case 'Z': maze_open(); return 1;
    case 'o': case 'O': clock_open(); return 1;
    case 'i': case 'I': open_by_kind(W_ABOUT); return 1;
    case 'm': case 'M': if (top) toggle_max(top); return 1;
    case KEY_F1 + 3: if (top) close_win(top); return 1;
    case KEY_LEFT:  if (top && !top->maxed) top->x -= 16; return 1;
    case KEY_RIGHT: if (top && !top->maxed) top->x += 16; return 1;
    case KEY_UP:    if (top && !top->maxed) top->y -= 16; return 1;
    case KEY_DOWN:  if (top && !top->maxed) top->y += 16; return 1;
    }
    return 0;
}
static void taskbar_click(int x) {
    if (x >= 4 && x < 74) { start_open = !start_open; dirty = 1; return; }
    if (x >= 82 && x < 122) term_open();
    else if (x >= 126 && x < 166) files_open_win();
    else if (x >= 170 && x < 210) paint_open();
    else if (x >= 214 && x < 254) snake_open();
    else if (x >= 258 && x < 302) tetris_open();
    else if (x >= 306 && x < 346) race_open();
    else if (x >= 350 && x < 390) maze_open();
    else if (x >= 394 && x < 434) clock_open();
    else if (x >= 450) {
        int idx = (x - 450) / 94;
        for (int i = 0; i < MAXWIN; i++) if (wins[i].kind != W_NONE && idx-- == 0) { raise_win(&wins[i]); break; }
    }
    start_open = 0;
}
static void desktop_click(int x, int y) {
    for (int i = 0; i < NICONS; i++) {
        int ix = icons[i].x, iy = icons[i].y;
        if (x >= ix && x < ix + ICON_W && y >= iy && y < iy + ICON_H) {
            open_by_kind(icons[i].kind); start_open = 0; return;
        }
    }
    if (start_open) {
        int sx = 4, sy = (int)gpus[0]->h - TASKH - 280;
        if (x >= sx && x < sx + 180 && y >= sy + 28 && y < sy + 270) {
            int row = (y - (sy + 28)) / 22;
            static const int kinds[] = {
                W_TERM, W_FILES, W_PAINT, W_SNAKE, W_TETRIS, W_RACE,
                W_MAZE, W_CALC, W_CLOCK, W_ABOUT, W_CTRL
            };
            if (row >= 0 && row < 11) open_by_kind(kinds[row]);
        }
        start_open = 0;
    }
}
static void mouse_event(const struct event *e) {
    int sw = (int)gpus[0]->w, sh = (int)gpus[0]->h;
    mx += e->dx; my += e->dy;
    if (mx < 0) mx = 0; if (my < 0) my = 0;
    if (mx >= sw) mx = sw - 1; if (my >= sh) my = sh - 1;
    int down = e->buttons & 1, pressed = down && !(mbtn & 1);
    int released = !down && (mbtn & 1);
    mbtn = e->buttons;

    /* paint drag */
    if (nz && z[nz - 1]->kind == W_PAINT && down) {
        struct window *w = z[nz - 1];
        int ox = w->x + 8, oy = w->y + TITLEH + 8;
        if (mx >= ox && mx < ox + PW && my >= oy && my < oy + PH) {
            paint_plot(w, mx - ox, my - oy);
            w->pdrawing = 1; dirty = 1; return;
        }
    }
    if (released && nz && z[nz - 1]->kind == W_PAINT) z[nz - 1]->pdrawing = 0;

    if (dragging) {
        if (down) {
            dragging->x = mx - drag_dx; dragging->y = my - drag_dy;
            if (dragging->y < 0) dragging->y = 0;
        } else dragging = 0;
        return;
    }
    if (!pressed) return;
    if (my >= sh - TASKH) { taskbar_click(mx); return; }
    for (int i = nz - 1; i >= 0; i--) {
        struct window *w = z[i];
        if (mx < w->x || mx >= w->x + w->w || my < w->y || my >= w->y + w->h) continue;
        raise_win(w);
        if (my < w->y + TITLEH) {
            if (mx >= w->x + w->w - 22) close_win(w);
            else if (!w->maxed) { dragging = w; drag_dx = mx - w->x; drag_dy = my - w->y; }
        } else if (w->kind == W_FILES) {
            int tb = w->y + TITLEH + 2;
            if (my >= tb && my < tb + 28) {
                if (mx < w->x + 66) {
                    char p[160]; if (!path_resolve(w->path, "..", p, sizeof p)) {
                        scopy(w->path, p, sizeof w->path); files_reload(w);
                    }
                } else if (mx < w->x + 128) files_activate(w);
            } else {
                int row = (my - (tb + 48)) / 18 + w->top;
                if (row >= 0 && row < w->nent) {
                    if (row == w->sel) files_activate(w); else w->sel = row;
                }
            }
        } else if (w->kind == W_CALC) {
            int bx = w->x + 10, by = w->y + TITLEH + 48;
            int col = (mx - bx) / 48, row = (my - by) / 42;
            if (col >= 0 && col < 4 && row >= 0 && row < 4) {
                static const char map[] = "789/456*123-0C=+";
                char ch = map[row * 4 + col];
                struct event ke = { EV_KEY, 1, 0, 0, ch == '=' ? '\n' : ch, 0, 0 };
                calc_key(w, &ke);
            }
        } else if (w->kind == W_PAINT) {
            int ox = w->x + 8, oy = w->y + TITLEH + 8;
            int py = oy + PH + 8;
            if (my >= py && my < py + 16) {
                int ci = (mx - ox) / 28;
                if (ci >= 0 && ci < 8) w->pcolor = ci;
            } else if (mx >= ox && mx < ox + PW && my >= oy && my < oy + PH) {
                paint_plot(w, mx - ox, my - oy);
            }
        }
        start_open = 0;
        return;
    }
    desktop_click(mx, my);
}

void gui_handle(const struct event *e) {
    dirty = 1;
    if (e->type == EV_MOUSE) { if (ngpus) mouse_event(e); return; }
    if (!e->down) return;
    if ((e->mods & (MOD_ALT | MOD_SUPER)) && wm_key(e)) return;
    if (!nz) return;
    struct window *w = z[nz - 1];
    switch (w->kind) {
    case W_TERM: term_key(w, e); break;
    case W_FILES: files_key(w, e); break;
    case W_VIEW: view_key(w, e); break;
    case W_CALC: calc_key(w, e); break;
    case W_SNAKE: snake_key(w, e); break;
    case W_TETRIS: tetris_key(w, e); break;
    case W_RACE: race_key(w, e); break;
    case W_MAZE: maze_key(w, e); break;
    default: break;
    }
}

void gui_init(struct fat32 *fs) {
    g_fs = fs;
    mx = (int)gpus[0]->w / 2; my = (int)gpus[0]->h / 2;
    struct window *a = new_win(W_ABOUT);
    if (a) {
        scopy(a->title, "Bienvenido a Kuno 0.3", sizeof a->title);
        a->x = (int)gpus[0]->w - 460; a->y = 40;
    }
    dirty = 1;
}
