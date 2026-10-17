/* oxpdump: print a document's events, one a line (the format tests/ compare).
 * MIT licence. Copyright (c) 2026 AmigaChrome Team.
 *   oxpdump [-c N] [-f] [-n] [-e utf16le|utf16be|latin1|utf8] FILE
 *   -c N  feed N bytes at a time (default: all at once)   -f fragment   -n no namespaces */
#include "oxp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *o;
static void esc(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\n') fputs("\\n", o);
        else if (c == '\t') fputs("\\t", o);
        else if (c == '\\') fputs("\\\\", o);
        else if (c < 0x20) fprintf(o, "\\x%02x", c);
        else fputc(c, o);
    }
}
static char *pend; static size_t npend, cpend;
static void flush(void) { if (npend) { fputs("T ", o); esc(pend, npend); fputc('\n', o); npend = 0; } }
static void name(const char *uri, const char *local) { if (uri) fprintf(o, "{%s}", uri); fputs(local, o); }

static void sd(void *u, const char *v, const char *e, int s) { (void)u; (void)v; (void)e; (void)s; }
static void ed(void *u) { (void)u; flush(); fputs("END\n", o); }
static void dt(void *u, const char *n, const char *pub, const char *sys, int i) { (void)u; (void)i; flush(); fprintf(o, "DOCTYPE %s %s %s\n", n, pub ? pub : "-", sys ? sys : "-"); }
static void se(void *u, const char *l, const char *pfx, const char *uri, int nns, const oxp_ns *ns, int na, const oxp_attr *a)
{
    (void)u; (void)pfx; flush();
    fputs("S ", o); name(uri, l); fputc('\n', o);
    for (int i = 0; i < nns; i++) fprintf(o, "N %s=%s\n", ns[i].prefix ? ns[i].prefix : "", ns[i].uri);
    for (int i = 0; i < na; i++) { fputs("A ", o); name(a[i].uri, a[i].local); fputc('=', o); esc(a[i].value, a[i].value_len); fputc('\n', o); }
}
static void ee(void *u, const char *l, const char *pfx, const char *uri) { (void)u; (void)pfx; flush(); fputs("E ", o); name(uri, l); fputc('\n', o); }
static void ch(void *u, const char *t, size_t n)
{
    (void)u;
    if (npend + n + 1 > cpend) { cpend = (npend + n + 1) * 2; pend = realloc(pend, cpend); }
    memcpy(pend + npend, t, n); npend += n;
}
static void cm(void *u, const char *t) { (void)u; flush(); fputs("C ", o); esc(t, strlen(t)); fputc('\n', o); }
static void pi(void *u, const char *t, const char *d) { (void)u; flush(); fprintf(o, "P %s ", t); esc(d, strlen(d)); fputc('\n', o); }
static void sk(void *u, const char *n) { (void)u; flush(); fprintf(o, "SKIP %s\n", n); }
static void er(void *u, oxp_level lv, oxp_code c, const char *m, unsigned long l, unsigned long col)
{
    (void)u; flush();
    fprintf(o, "ERROR %d %s %lu:%lu %s\n", (int)lv, oxp_code_name(c), l, col, m);
}

int main(int argc, char **argv)
{
    size_t chunk = 0; unsigned opt = 0; oxp_encoding enc = OXP_ENC_AUTO;
    int i = 1;
    o = stdout;
    for (; i < argc - 1; i++) {
        if (!strcmp(argv[i], "-c")) chunk = strtoul(argv[++i], 0, 0);
        else if (!strcmp(argv[i], "-f")) opt |= OXP_OPT_FRAGMENT;
        else if (!strcmp(argv[i], "-n")) opt |= OXP_OPT_NO_NAMESPACES;
        else if (!strcmp(argv[i], "-u")) opt |= OXP_OPT_UNBOUND_PREFIX;
        else if (!strcmp(argv[i], "-e")) {
            const char *e = argv[++i];
            enc = !strcmp(e, "utf16le") ? OXP_ENC_UTF16LE : !strcmp(e, "utf16be") ? OXP_ENC_UTF16BE : !strcmp(e, "latin1") ? OXP_ENC_LATIN1 : OXP_ENC_UTF8;
            opt |= OXP_OPT_IGNORE_DECL_ENCODING;
        }
    }
    if (i >= argc) { fprintf(stderr, "usage: oxpdump [-c N] [-f] [-n] [-e enc] FILE\n"); return 2; }
    FILE *f = fopen(argv[i], "rb");
    if (!f) { perror(argv[i]); return 2; }
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    char *b = malloc(n + 1);
    if (fread(b, 1, n, f) != (size_t)n) return 2;
    fclose(f);
    oxp_handler h = { sd, ed, dt, NULL, NULL, se, ee, ch, ch, cm, pi, sk, NULL, er };
    oxp_parser *p = oxp_new(&h, NULL, NULL);
    oxp_set_options(p, opt);
    if (enc) oxp_set_encoding(p, enc);
    oxp_code c = OXP_OK;
    if (!chunk) c = oxp_parse(p, b, n, 1);
    else {
        for (long k = 0; k < n && !c; k += chunk) c = oxp_parse(p, b + k, (size_t)(n - k < (long)chunk ? n - k : (long)chunk), 0);
        if (!c) c = oxp_parse(p, NULL, 0, 1);
    }
    flush();
    oxp_free(p);
    free(b); free(pend);
    return c ? 1 : 0;
}
