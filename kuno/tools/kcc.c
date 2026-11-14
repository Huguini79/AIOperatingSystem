/* kcc - compilador de un subconjunto de C ("KC") a ensamblador x86-64 (AT&T, ABI System V).
 * Soporta: funciones (hasta 6 params enteros), int locales, + - * / %, comparaciones,
 * !, - unario, asignacion, if/else, while, return, llamadas (propias o externas).
 * Una sola pasada, sin AST: el resultado de cada expresion queda en %rax.
 * Uso: kcc fuente.kc > salida.s */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

enum { T_EOF, T_NUM, T_ID, T_INT, T_IF, T_ELSE, T_WHILE, T_RETURN, T_PUNCT };
typedef struct { int k; long v; char s[64]; } Tok;
static const char *src; static Tok t; static int line = 1;
static const char *regs[6] = { "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9" };

static void die(const char *m) { fprintf(stderr, "kcc: linea %d: %s (cerca de '%s')\n", line, m, t.s); exit(1); }

static void next(void) {
    for (;;) {
        while (isspace((unsigned char)*src)) { if (*src == '\n') line++; src++; }
        if (src[0] == '/' && src[1] == '/') { while (*src && *src != '\n') src++; continue; }
        break;
    }
    t.s[0] = 0;
    if (!*src) { t.k = T_EOF; return; }
    if (isdigit((unsigned char)*src)) { t.v = strtol(src, (char **)&src, 0); t.k = T_NUM; sprintf(t.s, "%ld", t.v); return; }
    if (isalpha((unsigned char)*src) || *src == '_') {
        int n = 0; while (isalnum((unsigned char)*src) || *src == '_') { if (n < 63) t.s[n++] = *src; src++; }
        t.s[n] = 0; t.k = T_ID;
        if (!strcmp(t.s, "int")) t.k = T_INT; else if (!strcmp(t.s, "if")) t.k = T_IF;
        else if (!strcmp(t.s, "else")) t.k = T_ELSE; else if (!strcmp(t.s, "while")) t.k = T_WHILE;
        else if (!strcmp(t.s, "return")) t.k = T_RETURN;
        return;
    }
    t.k = T_PUNCT; t.s[0] = *src++; t.s[1] = 0;
    if ((t.s[0] == '=' || t.s[0] == '!' || t.s[0] == '<' || t.s[0] == '>') && *src == '=') { t.s[1] = '='; t.s[2] = 0; src++; }
}
static int is(const char *p) { return t.k == T_PUNCT && !strcmp(t.s, p); }
static int accept(const char *p) { if (is(p)) { next(); return 1; } return 0; }
static void expect(const char *p) { if (!accept(p)) { char m[64]; snprintf(m, sizeof m, "se esperaba '%s'", p); die(m); } }

/* ---- variables locales y control de alineacion de pila ---- */
static char names[64][64]; static int nloc, depth, lbl; static char fname[64];
static int declare(const char *n) {
    if (nloc >= 64) die("demasiadas variables");
    strcpy(names[nloc], n); return -8 * ++nloc;
}
static int loc(const char *n) {
    for (int i = 0; i < nloc; i++) if (!strcmp(names[i], n)) return -8 * (i + 1);
    die("variable no declarada"); return 0;
}
static void push(void) { printf("  push %%rax\n"); depth++; }
static void pop(const char *r) { printf("  pop %s\n", r); depth--; }

/* ---- expresiones ---- */
static void expr(void);
static void primary(void) {
    if (t.k == T_NUM) { printf("  mov $%ld,%%rax\n", t.v); next(); return; }
    if (t.k == T_ID) {
        char name[64]; strcpy(name, t.s); next();
        if (accept("(")) {
            int n = 0;
            if (!is(")")) do { expr(); push(); n++; } while (accept(","));
            expect(")"); if (n > 6) die("maximo 6 argumentos");
            for (int i = n - 1; i >= 0; i--) pop(regs[i]);
            int pad = depth & 1;                              /* mantener %rsp alineado a 16 en call */
            if (pad) printf("  sub $8,%%rsp\n");
            printf("  xor %%eax,%%eax\n  call %s\n", name);
            if (pad) printf("  add $8,%%rsp\n");
            return;
        }
        printf("  mov %d(%%rbp),%%rax\n", loc(name)); return;
    }
    if (accept("(")) { expr(); expect(")"); return; }
    die("se esperaba una expresion");
}
static void unary(void) {
    if (accept("-")) { unary(); printf("  neg %%rax\n"); return; }
    if (accept("!")) { unary(); printf("  cmp $0,%%rax\n  sete %%al\n  movzb %%al,%%eax\n"); return; }
    primary();
}
static const char *lv[][5] = { { "==", "!=", 0 }, { "<", ">", "<=", ">=", 0 }, { "+", "-", 0 }, { "*", "/", "%", 0 } };
static void emitop(const char *op) {
    if (!strcmp(op, "+")) printf("  add %%rcx,%%rax\n");
    else if (!strcmp(op, "-")) printf("  sub %%rcx,%%rax\n");
    else if (!strcmp(op, "*")) printf("  imul %%rcx,%%rax\n");
    else if (!strcmp(op, "/")) printf("  cqo\n  idiv %%rcx\n");
    else if (!strcmp(op, "%")) printf("  cqo\n  idiv %%rcx\n  mov %%rdx,%%rax\n");
    else {
        const char *cc = !strcmp(op, "==") ? "e" : !strcmp(op, "!=") ? "ne" : !strcmp(op, "<") ? "l" :
                         !strcmp(op, ">") ? "g" : !strcmp(op, "<=") ? "le" : "ge";
        printf("  cmp %%rcx,%%rax\n  set%s %%al\n  movzb %%al,%%eax\n", cc);
    }
}
static void binary(int l) {
    if (l == 4) { unary(); return; }
    binary(l + 1);
    for (;;) {
        int i; for (i = 0; lv[l][i] && !is(lv[l][i]); i++) ;
        if (!lv[l][i]) return;
        char op[3]; strcpy(op, t.s); next();
        push(); binary(l + 1); printf("  mov %%rax,%%rcx\n"); pop("%rax"); emitop(op);
    }
}
static void expr(void) {                                       /* asignacion: ID '=' expr */
    if (t.k == T_ID) {
        const char *ss = src; Tok st = t; int sl = line; next();
        if (is("=")) { next(); expr(); printf("  mov %%rax,%d(%%rbp)\n", loc(st.s)); return; }
        src = ss; t = st; line = sl;
    }
    binary(0);
}

/* ---- sentencias ---- */
static void stmt(void) {
    if (accept("{")) { while (!is("}") && t.k != T_EOF) stmt(); expect("}"); return; }
    if (t.k == T_INT) {
        next(); if (t.k != T_ID) die("se esperaba un nombre"); int o = declare(t.s); next();
        if (accept("=")) { expr(); printf("  mov %%rax,%d(%%rbp)\n", o); }
        else printf("  movq $0,%d(%%rbp)\n", o);
        expect(";"); return;
    }
    if (t.k == T_IF) {
        next(); expect("("); expr(); expect(")"); int a = lbl++, b = lbl++;
        printf("  cmp $0,%%rax\n  je .L%d\n", a); stmt();
        if (t.k == T_ELSE) { next(); printf("  jmp .L%d\n.L%d:\n", b, a); stmt(); printf(".L%d:\n", b); }
        else printf(".L%d:\n", a);
        return;
    }
    if (t.k == T_WHILE) {
        next(); int a = lbl++, b = lbl++; printf(".L%d:\n", a);
        expect("("); expr(); expect(")"); printf("  cmp $0,%%rax\n  je .L%d\n", b);
        stmt(); printf("  jmp .L%d\n.L%d:\n", a, b); return;
    }
    if (t.k == T_RETURN) { next(); expr(); expect(";"); printf("  jmp .Lret_%s\n", fname); return; }
    expr(); expect(";");
}
static void func(void) {
    if (t.k != T_INT) die("se esperaba 'int'");
    next(); if (t.k != T_ID) die("se esperaba el nombre de la funcion");
    strcpy(fname, t.s); next(); nloc = 0; depth = 0; expect("(");
    int np = 0;
    if (!is(")")) do {
        if (t.k != T_INT) die("tipo de parametro");
        next();
        if (t.k != T_ID) die("nombre de parametro");
        declare(t.s); np++; next();
    } while (accept(","));
    if (np > 6) die("maximo 6 parametros");
    expect(")");
    printf(".globl %s\n%s:\n  push %%rbp\n  mov %%rsp,%%rbp\n  sub $FRAME_%s,%%rsp\n", fname, fname, fname);
    for (int i = 0; i < np; i++) printf("  mov %s,%d(%%rbp)\n", regs[i], -8 * (i + 1));
    stmt();
    printf("  xor %%eax,%%eax\n.Lret_%s:\n  leave\n  ret\n.set FRAME_%s,%d\n\n", fname, fname, ((nloc * 8) + 15) & ~15);
}

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "uso: kcc fuente.kc > salida.s\n"); return 1; }
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    char *buf = calloc(n + 1, 1); if (fread(buf, 1, n, f) != (size_t)n) return 1; fclose(f);
    src = buf; next();
    printf(".text\n");
    while (t.k != T_EOF) func();
    printf(".section .note.GNU-stack,\"\",@progbits\n");
    return 0;
}
