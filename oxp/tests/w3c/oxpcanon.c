/* oxpcanon: a document in James Clark's canonical form, the form the W3C XML
 * test suite's OUTPUT files use: no DOCTYPE or comments; attributes (namespace
 * declarations among them) sorted by name; & < > " TAB LF CR as references.
 *   oxpcanon [-n] FILE      (-n: no namespace processing)
 * Exit status: 0 well-formed, 1 not.  MIT licence. Copyright (c) 2026 AmigaChrome Team. */
#include "oxp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void esc(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        switch (s[i]) {
        case '&': fputs("&amp;", stdout); break;
        case '<': fputs("&lt;", stdout); break;
        case '>': fputs("&gt;", stdout); break;
        case '"': fputs("&quot;", stdout); break;
        case '\t': fputs("&#9;", stdout); break;
        case '\n': fputs("&#10;", stdout); break;
        case '\r': fputs("&#13;", stdout); break;
        default: putchar(s[i]);
        }
    }
}
typedef struct { char *name; const char *value; size_t len; } ca;
static int cmp(const void *a, const void *b)
{
    /* by UTF-8 bytes, which is code point order */
    return strcmp(((const ca *)a)->name, ((const ca *)b)->name);
}
static char *qn(const char *prefix, const char *local)
{
    size_t n = (prefix ? strlen(prefix) + 1 : 0) + strlen(local) + 1;
    char *q = malloc(n);
    if (prefix) { strcpy(q, prefix); strcat(q, ":"); strcat(q, local); } else strcpy(q, local);
    return q;
}
static void se(void *u, const char *l, const char *p, const char *uri, int nns, const oxp_ns *ns, int na, const oxp_attr *a)
{
    (void)u; (void)uri;
    char *e = qn(p, l);
    ca *v = malloc(sizeof(ca) * (size_t)(nns + na + 1));
    int n = 0;
    for (int i = 0; i < nns; i++) { v[n].name = qn(ns[i].prefix ? "xmlns" : NULL, ns[i].prefix ? ns[i].prefix : "xmlns"); v[n].value = ns[i].uri; v[n].len = strlen(ns[i].uri); n++; }
    for (int i = 0; i < na; i++) { v[n].name = qn(a[i].prefix, a[i].local); v[n].value = a[i].value; v[n].len = a[i].value_len; n++; }
    qsort(v, (size_t)n, sizeof(ca), cmp);
    printf("<%s", e);
    for (int i = 0; i < n; i++) { printf(" %s=\"", v[i].name); esc(v[i].value, v[i].len); putchar('"'); free(v[i].name); }
    putchar('>');
    free(v); free(e);
}
static void ee(void *u, const char *l, const char *p, const char *uri) { (void)u; (void)uri; char *e = qn(p, l); printf("</%s>", e); free(e); }
static void ch(void *u, const char *t, size_t n) { (void)u; esc(t, n); }
static void pi(void *u, const char *t, const char *d) { (void)u; printf("<?%s %s?>", t, d); }

int main(int argc, char **argv)
{
    unsigned opt = 0;
    int i = 1;
    if (i < argc && !strcmp(argv[i], "-n")) { opt |= OXP_OPT_NO_NAMESPACES; i++; }
    if (i >= argc) return 2;
    FILE *f = fopen(argv[i], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    char *b = malloc((size_t)n + 1);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) return 2;
    fclose(f);
    oxp_handler h = { 0 };
    h.start_element = se; h.end_element = ee; h.characters = ch; h.cdata = ch; h.pi = pi;
    oxp_parser *p = oxp_new(&h, NULL, NULL);
    oxp_set_options(p, opt);
    oxp_code c = oxp_parse(p, b, (size_t)n, 1);
    if (c) { const char *m; unsigned long l, col; oxp_error(p, &m, &l, &col); fprintf(stderr, "%s %lu:%lu %s\n", oxp_code_name(c), l, col, m); }
    oxp_free(p);
    free(b);
    return c ? 1 : 0;
}
