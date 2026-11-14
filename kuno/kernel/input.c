/* Teclado y raton PS/2 por sondeo (polling) del controlador i8042: sin IDT todavia.
 * El decodificador de scancodes (set 1) y el de paquetes de raton son funciones puras. */
#include "kernel.h"

int mouse_present;

#define QN 128
static struct event q[QN];
static int qh, qt;
static void push(const struct event *e) { int n = (qt + 1) % QN; if (n != qh) { q[qt] = *e; qt = n; } }
int ev_pop(struct event *e) { if (qh == qt) return 0; *e = q[qh]; qh = (qh + 1) % QN; return 1; }

/* Distribucion US (set 1, make codes 0x00..0x39) */
static const char k_norm[] = "\0\033" "1234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
static const char k_shft[] = "\0\033" "!@#$%^&*()_+\b\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";

#define H_LSHIFT 0x01
#define H_RSHIFT 0x02
#define H_LCTRL  0x04
#define H_RCTRL  0x08
#define H_LALT   0x10
#define H_RALT   0x20
#define H_LSUPER 0x40
#define H_RSUPER 0x80

static void hold(struct kbd *k, u8 bit, int down) { if (down) k->held |= bit; else k->held &= ~bit; }

int kbd_decode(struct kbd *k, u8 b, struct event *ev) {
    if (k->skip) { k->skip--; return 0; }
    if (b == 0xE0) { k->ext = 1; return 0; }
    if (b == 0xE1) { k->skip = 5; return 0; }                       /* tecla Pause */
    if (b == 0xFA || b == 0xFE) return 0;                         /* ACK / reenvio (0xAA es Shift izq. soltado) */
    int down = !(b & 0x80), ext = k->ext, key = 0; u8 c = b & 0x7F; k->ext = 0;

    if (!ext && c == 0x2A) hold(k, H_LSHIFT, down);
    else if (!ext && c == 0x36) hold(k, H_RSHIFT, down);
    else if (c == 0x1D) hold(k, ext ? H_RCTRL : H_LCTRL, down);
    else if (c == 0x38) hold(k, ext ? H_RALT : H_LALT, down);
    else if (ext && c == 0x5B) hold(k, H_LSUPER, down);
    else if (ext && c == 0x5C) hold(k, H_RSUPER, down);
    else if (!ext && c == 0x3A) { if (down) k->caps ^= 1; }
    else {
        u16 mods = ((k->held & 3) ? MOD_SHIFT : 0);
        if (ext) {
            switch (c) {
            case 0x48: key = KEY_UP; break;    case 0x50: key = KEY_DOWN; break;
            case 0x4B: key = KEY_LEFT; break;  case 0x4D: key = KEY_RIGHT; break;
            case 0x47: key = KEY_HOME; break;  case 0x4F: key = KEY_END; break;
            case 0x49: key = KEY_PGUP; break;  case 0x51: key = KEY_PGDN; break;
            case 0x53: key = KEY_DEL; break;   case 0x52: key = KEY_INS; break;
            case 0x1C: key = '\n'; break;      case 0x35: key = '/'; break;     /* keypad */
            }
        } else if (c < sizeof k_norm - 1) {
            key = (mods & MOD_SHIFT) ? k_shft[c] : k_norm[c];
            if (k->caps) {
                if (key >= 'a' && key <= 'z') key -= 32;
                else if (key >= 'A' && key <= 'Z') key += 32;
            }
        } else if (c >= 0x3B && c <= 0x44) key = KEY_F1 + (c - 0x3B);
        else if (c == 0x57 || c == 0x58) key = KEY_F1 + 10 + (c - 0x57);
        if (!key) return 0;
        ev->type = EV_KEY; ev->down = down; ev->key = key; ev->dx = ev->dy = 0; ev->buttons = 0;
        ev->mods = mods | ((k->held & (H_LCTRL | H_RCTRL)) ? MOD_CTRL : 0) |
                          ((k->held & (H_LALT | H_RALT)) ? MOD_ALT : 0) |
                          ((k->held & (H_LSUPER | H_RSUPER)) ? MOD_SUPER : 0);
        return 1;
    }
    return 0;
}

int mouse_decode(struct mouse *m, u8 b, struct event *ev) {
    if (m->n == 0 && !(b & 8)) return 0;                            /* resincronizar */
    m->pkt[m->n++] = b;
    if (m->n < 3) return 0;
    m->n = 0;
    u8 b0 = m->pkt[0];
    if (b0 & 0xC0) return 0;                                        /* desbordamiento */
    ev->type = EV_MOUSE; ev->down = 0; ev->key = 0; ev->mods = 0;
    ev->dx = m->pkt[1] - ((b0 << 4) & 0x100);
    ev->dy = -(m->pkt[2] - ((b0 << 3) & 0x100));                    /* PS/2: Y hacia arriba */
    ev->buttons = b0 & 7;
    return 1;
}

/* ---- controlador i8042 ---- */
static int wait_in(void)  { for (int i = 0; i < 100000; i++) if (!(inb(0x64) & 2)) return 1; return 0; }
static int wait_out(void) { for (int i = 0; i < 100000; i++) if (inb(0x64) & 1) return 1; return 0; }
static void ctl(u8 c) { wait_in(); outb(0x64, c); }
static int dev_cmd(int aux, u8 c) {
    if (aux) ctl(0xD4);
    wait_in(); outb(0x60, c);
    return wait_out() && inb(0x60) == 0xFA;
}

void ps2_init(void) {
    while (inb(0x64) & 1) inb(0x60);                                /* vaciar buffer */
    ctl(0xA8);                                                      /* habilitar puerto auxiliar */
    ctl(0x20); u8 cfg = wait_out() ? inb(0x60) : 0;
    cfg &= ~0x33; cfg |= 0x40;                                      /* sin IRQ, relojes on, traduccion a set 1 */
    ctl(0x60); wait_in(); outb(0x60, cfg);
    dev_cmd(0, 0xF4);                                               /* teclado: activar */
    dev_cmd(1, 0xF6);                                               /* raton: valores por defecto */
    mouse_present = dev_cmd(1, 0xF4);                               /* raton: activar streaming */
    kprintf("[input] teclado PS/2 ok, raton: %s\n", mouse_present ? "si" : "no");
}

void input_poll(void) {
    static struct kbd k; static struct mouse m;
    u8 st; int guard = 64;
    while (guard-- && ((st = inb(0x64)) & 1)) {
        u8 d = inb(0x60); struct event e;
        if (st & 0x20) { if (mouse_decode(&m, d, &e)) push(&e); }
        else if (kbd_decode(&k, d, &e)) push(&e);
    }
}
