/* Gestor de ventanas: terminal (shell), explorador de archivos, visor de texto y panel de sistema.
 * Todo se dibuja en un buffer por GPU (doble buffer) y se copia al framebuffer; el puntero va
 * en el buffer, asi no hay parpadeo. Se maneja con teclado (Alt+...) y con raton. */
#include "kernel.h"

extern const u8 font8x8[95][8];

struct surf { u32 *px; int w, h; };
static struct surf surfs[MAX_GPUS];

#define TCOLS   78
#define TLINES  100
#define HIST    16
#define MAXWIN  10
#define TITLEH  20
#define TASKH   28
enum { W_NONE, W_TERM, W_FILES, W_VIEW, W_ABOUT };

struct window {
    int kind, x, y, w, h, maxed, rx, ry, rw, rh;
    char title[48]; u32 accent;
    /* terminal */
    char lines[TLINES][TCOLS + 1]; int row, col, scroll;
    char in[128]; int inlen; char hist[HIST][128]; int nhist, hpos; struct shell sh;
    /* explorador */
    struct { char name[48]; int isdir; u32 size; } ent[64]; int nent, sel, top; char path[128];
    /* visor */
    char text[4096]; int tlen;
};
static struct window wins[MAXWIN], *z[MAXWIN];
static int nz, dirty = 1, term_count;
static struct fat32 *g_fs;
static int mx = 200, my = 200, mbtn, drag_dx, drag_dy;
static struct window *dragging;

static void scopy(char *d, const char *s, size_t n) { size_t i = 0; for (; s[i] && i + 1 < n; i++) d[i] = s[i]; d[i] = 0; }

/* ---------- dibujo ---------- */
static void rect(struct surf *s, int x, int y, int w, int h, u32 c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s->w) w = s->w - x;
    if (y + h > s->h) h = s->h - y;
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) { u32 *r = s->px + (size_t)(y + j) * s->w + x; for (int i = 0; i < w; i++) r[i] = c; }
}
static void frame(struct surf *s, int x, int y, int w, int h, u32 c) {
    rect(s, x, y, w, 1, c); rect(s, x, y + h - 1, w, 1, c); rect(s, x, y, 1, h, c); rect(s, x + w - 1, y, 1, h, c);
}
static void glyph(struct surf *s, int x, int y, int ch, u32 col) {
    if (ch < 32 || ch > 126) ch = '?';
    if (x < 0 || y < 0 || x + 8 > s->w || y + 8 > s->h) return;
    for (int r = 0; r < 8; r++) {
        u32 *row = s->px + (size_t)(y + r) * s->w + x; u8 bits = font8x8[ch - 32][r];
        for (int b = 0; b < 8; b++) if (bits >> b & 1) row[b] = col;
    }
}
static void text(struct surf *s, int x, int y, const char *str, u32 col, int max) {
    for (; *str && max > 0; str++, max--, x += 8) glyph(s, x, y, *str, col);
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

/* ---------- ventanas ---------- */
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
    w->kind = W_NONE; dirty = 1;
}
static struct window *new_win(int kind) {
    for (int i = 0; i < MAXWIN; i++) if (wins[i].kind == W_NONE) {
        struct window *w = &wins[i]; memset(w, 0, sizeof *w); w->kind = kind;
        w->x = 30 + 28 * (nz % 8); w->y = 24 + 24 * (nz % 8);
        switch (kind) {
        case W_TERM:  w->w = 640; w->h = 340; w->accent = 0x00305090; break;
        case W_FILES: w->w = 420; w->h = 300; w->accent = 0x00306040; break;
        case W_VIEW:  w->w = 560; w->h = 320; w->accent = 0x00704030; break;
        default:      w->w = 400; w->h = 250; w->accent = 0x00805020; break;
        }
        z[nz++] = w; dirty = 1; return w;
    }
    return 0;
}

/* ---- terminal ---- */
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
    ksnprintf(w->title, sizeof w->title, "TERMINAL %d", ++term_count);
    shell_init(&w->sh, g_fs);
    t_puts(w, "Kuno 0.2 - shell. Escribe 'help' para ver los comandos.\n");
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
        if (k == 'l') { t_clear(w); }
        else if (k == 'c') { t_puts(w, "kuno$ ^C\n"); w->inlen = 0; w->in[0] = 0; }
        return;
    }
    if (k == KEY_PGUP) { w->scroll += 5; if (w->scroll > w->row) w->scroll = w->row; }
    else if (k == KEY_PGDN) { w->scroll -= 5; if (w->scroll < 0) w->scroll = 0; }
    else if (k == KEY_UP) { if (w->hpos < w->nhist) w->hpos++; if (w->hpos) { scopy(w->in, w->hist[w->nhist - w->hpos], 128); w->inlen = (int)strlen(w->in); } }
    else if (k == KEY_DOWN) {
        if (w->hpos > 0) w->hpos--;
        if (w->hpos) scopy(w->in, w->hist[w->nhist - w->hpos], 128); else w->in[0] = 0;
        w->inlen = (int)strlen(w->in);
    }
    else if (k == '\n') term_run(w);
    else if (k == '\b') { if (w->inlen) w->in[--w->inlen] = 0; }
    else if (k >= 32 && k < 127 && w->inlen < 100) { w->in[w->inlen++] = (char)k; w->in[w->inlen] = 0; w->scroll = 0; }
}
static void draw_term(struct surf *s, struct window *w, int focused) {
    int x = w->x + 8, y = w->y + TITLEH + 6, vis = (w->h - TITLEH - 10) / 10, R = w->row + 1;
    int start = R - vis - w->scroll; if (start < 0) start = 0;
    for (int i = start, l = 0; i < R && l < vis; i++, l++, y += 10) {
        if (i == w->row) {
            char pr[160]; int n = ksnprintf(pr, sizeof pr, "kuno:%s$ ", w->sh.cwd); if (n > 60) n = 60;
            text(s, x, y, pr, 0x0070C8FF, 60);
            text(s, x + 8 * n, y, w->in, 0x00FFFFFF, TCOLS - n);
            if (focused && !w->scroll) rect(s, x + 8 * (n + w->inlen), y, 8, 9, 0x00FFFFFF);
        } else text(s, x, y, w->lines[i], 0x0090FF90, TCOLS);
    }
}

/* ---- explorador ---- */
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
    ksnprintf(w->title, sizeof w->title, "ARCHIVOS: %s", w->path);
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
    ksnprintf(w->title, sizeof w->title, "VER: %s", base);
}
static void files_activate(struct window *w) {
    if (w->sel >= w->nent) return;
    char p[160]; if (path_resolve(w->path, w->ent[w->sel].name, p, sizeof p)) return;
    if (w->ent[w->sel].isdir) { scopy(w->path, p, sizeof w->path); files_reload(w); } else view_open(p);
}
static void files_key(struct window *w, const struct event *e) {
    int vis = (w->h - TITLEH - 12) / 10, k = e->key;
    if (k == KEY_UP) { if (w->sel > 0) w->sel--; }
    else if (k == KEY_DOWN) { if (w->sel + 1 < w->nent) w->sel++; }
    else if (k == KEY_PGUP) { w->sel -= 10; if (w->sel < 0) w->sel = 0; }
    else if (k == KEY_PGDN) { w->sel += 10; if (w->sel >= w->nent) w->sel = w->nent ? w->nent - 1 : 0; }
    else if (k == KEY_HOME) w->sel = 0;
    else if (k == KEY_END) w->sel = w->nent ? w->nent - 1 : 0;
    else if (k == '\n') files_activate(w);
    else if (k == '\b' || k == KEY_LEFT) { char p[160]; if (!path_resolve(w->path, "..", p, sizeof p)) { scopy(w->path, p, sizeof w->path); files_reload(w); } }
    else if (k == KEY_RIGHT) files_activate(w);
    if (w->sel < w->top) w->top = w->sel;
    if (w->sel >= w->top + vis) w->top = w->sel - vis + 1;
}
static void draw_files(struct surf *s, struct window *w) {
    int vis = (w->h - TITLEH - 12) / 10, x = w->x + 8, y = w->y + TITLEH + 6;
    if (!w->nent) text(s, x, y, "(vacio)", 0x00A0A0A0, 40);
    for (int i = w->top, l = 0; i < w->nent && l < vis; i++, l++, y += 10) {
        if (i == w->sel) rect(s, w->x + 2, y - 1, w->w - 4, 10, 0x00305090);
        char b[64];
        if (w->ent[i].isdir) { ksnprintf(b, sizeof b, "[%s]", w->ent[i].name); text(s, x, y, b, 0x00FFE070, 36); }
        else { text(s, x + 16, y, w->ent[i].name, 0x00FFFFFF, 34); ksnprintf(b, sizeof b, "%u", w->ent[i].size); text(s, w->x + w->w - 8 - 8 * (int)strlen(b), y, b, 0x00A0C0FF, 12); }
    }
}

/* ---- visor ---- */
static void view_key(struct window *w, const struct event *e) {
    if (e->key == KEY_UP && w->scroll > 0) w->scroll--;
    else if (e->key == KEY_DOWN) w->scroll++;
    else if (e->key == KEY_PGUP) { w->scroll -= 10; if (w->scroll < 0) w->scroll = 0; }
    else if (e->key == KEY_PGDN) w->scroll += 10;
    else if (e->key == KEY_HOME) w->scroll = 0;
    else if (e->key == 27) close_win(w);
}
static void draw_view(struct surf *s, struct window *w) {
    int cols = (w->w - 16) / 8, vis = (w->h - TITLEH - 10) / 10, line = 0, col = 0, x = w->x + 8, y0 = w->y + TITLEH + 6;
    for (int i = 0; i <= w->tlen; i++) {
        char c = w->text[i];
        if (line >= w->scroll && line < w->scroll + vis && c >= 32) glyph(s, x + 8 * col, y0 + (line - w->scroll) * 10, c, 0x00E0E0E0);
        if (c == '\n' || c == '\t' || col + 1 >= cols) { if (c == '\t') col += 4 - (col & 3); if (c == '\n' || col + 1 >= cols) { line++; col = 0; } else if (c >= 32) col++; }
        else col++;
    }
}

/* ---- sistema ---- */
static void draw_about(struct surf *s, struct window *w) {
    char b[96]; int x = w->x + 12, y = w->y + TITLEH + 10;
    text(s, x, y, "KUNO 0.2 - KERNEL ESTILO UNIX X86_64", 0x00FFFFFF, 44); y += 16;
    ksnprintf(b, sizeof b, "MEMORIA: %llu MIB   GPUS: %d   RATON: %s", kmem_bytes >> 20, ngpus, mouse_present ? "SI" : "NO");
    text(s, x, y, b, 0x00A0C0FF, 46); y += 16;
    static const char *help[] = { "ALT+TAB   cambiar de ventana", "ALT+T     nueva terminal", "ALT+F     explorador de archivos",
        "ALT+I     este panel", "ALT+M     maximizar / restaurar", "ALT+F4    cerrar ventana", "ALT+FLECHAS mover ventana",
        "RATON     arrastrar titulo, [X] cierra" };
    for (unsigned i = 0; i < sizeof help / sizeof *help; i++, y += 12) text(s, x, y, help[i], 0x00C8C8C8, 46);
}

/* ---------- render ---------- */
static void draw_window(struct surf *s, struct window *w, int focused) {
    rect(s, w->x + 4, w->y + 4, w->w, w->h, 0x00080C14);
    rect(s, w->x, w->y, w->w, w->h, 0x00141A22);
    rect(s, w->x, w->y, w->w, TITLEH, focused ? w->accent : 0x00404850);
    frame(s, w->x, w->y, w->w, w->h, focused ? 0x0080A0FF : 0x00505860);
    text(s, w->x + 8, w->y + 6, w->title, 0x00FFFFFF, (w->w - 40) / 8);
    rect(s, w->x + w->w - 17, w->y + 4, 12, 12, 0x00D04040); text(s, w->x + w->w - 15, w->y + 6, "X", 0x00FFFFFF, 1);
    switch (w->kind) {
    case W_TERM: draw_term(s, w, focused); break;
    case W_FILES: draw_files(s, w); break;
    case W_VIEW: draw_view(s, w); break;
    case W_ABOUT: draw_about(s, w); break;
    }
}
static void render(struct surf *s) {
    for (int y = 0; y < s->h; y += 4) { u32 t = (u32)y * 255 / (u32)s->h; rect(s, 0, y, s->w, 4, (0x10 + t / 6) << 16 | (0x20 + t / 4) << 8 | (0x50 + t / 2)); }
    for (int i = 0; i < nz; i++) draw_window(s, z[i], i == nz - 1);
    int ty = s->h - TASKH;
    rect(s, 0, ty, s->w, TASKH, 0x00181820); rect(s, 0, ty, s->w, 1, 0x00405080);
    static const struct { int x, w; const char *l; } btn[] = { { 6, 60, "TERM" }, { 72, 60, "FILES" }, { 138, 44, "SYS" } };
    for (int i = 0; i < 3; i++) { rect(s, btn[i].x, ty + 4, btn[i].w, 20, 0x00305090); text(s, btn[i].x + 8, ty + 10, btn[i].l, 0x00FFFFFF, 8); }
    int bx = 196;
    for (int i = 0; i < MAXWIN; i++) if (wins[i].kind != W_NONE) {
        rect(s, bx, ty + 4, 108, 20, (nz && z[nz - 1] == &wins[i]) ? 0x00506CA8 : 0x00303844);
        text(s, bx + 6, ty + 10, wins[i].title, 0x00FFFFFF, 12); bx += 112;
    }
    draw_cursor(s);
}

void gui_frame(void) {
    if (!dirty) return;
    dirty = 0;
    for (int i = 0; i < ngpus; i++) {
        struct gpu *g = gpus[i]; struct surf *s = &surfs[i];
        if (!s->px) { s->px = kmalloc((size_t)g->w * g->h * 4); s->w = (int)g->w; s->h = (int)g->h; }
        render(s);
        for (int y = 0; y < s->h; y++) memcpy(g->fb + (size_t)y * g->pitch, s->px + (size_t)y * s->w, (size_t)s->w * 4);
    }
}

/* ---------- entrada ---------- */
static void toggle_max(struct window *w) {
    int sw = (int)gpus[0]->w, sh = (int)gpus[0]->h;
    if (!w->maxed) { w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h; w->x = w->y = 0; w->w = sw; w->h = sh - TASKH; w->maxed = 1; }
    else { w->x = w->rx; w->y = w->ry; w->w = w->rw; w->h = w->rh; w->maxed = 0; }
}
static int wm_key(const struct event *e) {
    struct window *top = nz ? z[nz - 1] : 0;
    switch (e->key) {
    case '\t': if (nz > 1) { struct window *b = z[0]; for (int i = 0; i + 1 < nz; i++) z[i] = z[i + 1]; z[nz - 1] = b; } return 1;
    case 't': term_open(); return 1;
    case 'f': files_open_win(); return 1;
    case 'i': { int have = 0; for (int i = 0; i < MAXWIN; i++) if (wins[i].kind == W_ABOUT) { raise_win(&wins[i]); have = 1; }
                if (!have) { struct window *w = new_win(W_ABOUT); if (w) scopy(w->title, "SISTEMA", sizeof w->title); } return 1; }
    case 'm': if (top) toggle_max(top); return 1;
    case KEY_F1 + 3: if (top) close_win(top); return 1;
    case KEY_LEFT:  if (top && !top->maxed) top->x -= 16; return 1;
    case KEY_RIGHT: if (top && !top->maxed) top->x += 16; return 1;
    case KEY_UP:    if (top && !top->maxed) top->y -= 16; return 1;
    case KEY_DOWN:  if (top && !top->maxed) top->y += 16; return 1;
    }
    return 0;
}
static void taskbar_click(int x) {
    if (x >= 6 && x < 66) term_open();
    else if (x >= 72 && x < 132) files_open_win();
    else if (x >= 138 && x < 182) { struct event e = { EV_KEY, 1, 0, MOD_ALT, 'i', 0, 0 }; wm_key(&e); }
    else if (x >= 196) {
        int idx = (x - 196) / 112;
        for (int i = 0; i < MAXWIN; i++) if (wins[i].kind != W_NONE && idx-- == 0) { raise_win(&wins[i]); break; }
    }
}
static void mouse_event(const struct event *e) {
    int sw = (int)gpus[0]->w, sh = (int)gpus[0]->h;
    mx += e->dx; my += e->dy;
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx >= sw) mx = sw - 1;
    if (my >= sh) my = sh - 1;
    int down = e->buttons & 1, pressed = down && !(mbtn & 1); mbtn = e->buttons;
    if (dragging) { if (down) { dragging->x = mx - drag_dx; dragging->y = my - drag_dy; if (dragging->y < 0) dragging->y = 0; } else dragging = 0; return; }
    if (!pressed) return;
    if (my >= sh - TASKH) { taskbar_click(mx); return; }
    for (int i = nz - 1; i >= 0; i--) {
        struct window *w = z[i];
        if (mx < w->x || mx >= w->x + w->w || my < w->y || my >= w->y + w->h) continue;
        raise_win(w);
        if (my < w->y + TITLEH) {
            if (mx >= w->x + w->w - 20) close_win(w);
            else if (!w->maxed) { dragging = w; drag_dx = mx - w->x; drag_dy = my - w->y; }
        } else if (w->kind == W_FILES) {
            int row = (my - (w->y + TITLEH + 6)) / 10 + w->top;
            if (row >= 0 && row < w->nent) { if (row == w->sel) files_activate(w); else w->sel = row; }
        }
        return;
    }
}
void gui_handle(const struct event *e) {
    dirty = 1;
    if (e->type == EV_MOUSE) { if (ngpus) mouse_event(e); return; }
    if (!e->down) return;
    if ((e->mods & (MOD_ALT | MOD_SUPER)) && wm_key(e)) return;
    if (!nz) return;
    struct window *w = z[nz - 1];
    switch (w->kind) { case W_TERM: term_key(w, e); break; case W_FILES: files_key(w, e); break; case W_VIEW: view_key(w, e); break; default: break; }
}
void gui_init(struct fat32 *fs) {
    g_fs = fs;
    mx = (int)gpus[0]->w / 2; my = (int)gpus[0]->h / 2;
    struct window *a = new_win(W_ABOUT); if (a) { scopy(a->title, "SISTEMA", sizeof a->title); a->x = (int)gpus[0]->w - 420; a->y = 40; }
    files_open_win(); term_open();
    dirty = 1;
}
