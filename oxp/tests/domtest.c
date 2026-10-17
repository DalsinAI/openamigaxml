/* DOM test: read a file, check a few lookups, write it back, and check the
 * written text reads to the same events. MIT licence. Copyright (c) 2026 AmigaChrome Team.
 *   domtest FILE OUT   (OUT: the serialised document) */
#include "oxp_dom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int wr(void *c, const char *s, size_t n) { return fwrite(s, 1, n, c) == n ? 0 : -1; }
int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    char *b = malloc(n);
    if (fread(b, 1, n, f) != (size_t)n) return 2;
    fclose(f);
    oxp_code c; char msg[160]; unsigned long l, col;
    oxp_doc *d = oxp_dom_parse(b, n, 0, &c, msg, sizeof msg, &l, &col);
    if (!d) { printf("error %s at %lu:%lu: %s\n", oxp_code_name(c), l, col, msg); return 1; }
    oxp_node *r = oxp_dom_root(d);
    printf("root {%s}%s line %lu\n", r->uri ? r->uri : "", r->name, r->line);
    size_t tl = oxp_dom_text(r, NULL, 0);
    printf("text %lu bytes\n", (unsigned long)tl);
    FILE *o = fopen(argv[2], "wb");
    oxp_dom_write(oxp_dom_document(d), wr, o);
    fclose(o);
    oxp_dom_free(d);
    free(b);
    return 0;
}
