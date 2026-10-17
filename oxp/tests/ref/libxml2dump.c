/* libxml2dump: the reference side of tests/ref. It prints libxml2's SAX2
 * events in oxpdump's format, so the two outputs can be compared. A host
 * test tool only: it uses libxml2 through its public interface and is never
 * part of OXP.  MIT licence. Copyright (c) 2026 AmigaChrome Team.
 *   cc -I/usr/include/libxml2 libxml2dump.c -lxml2 */
#include <libxml/parser.h>
#include <libxml/parserInternals.h>
#include <libxml/SAX2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *pend; static size_t npend, cpend;
static void esc(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\n') fputs("\\n", stdout);
        else if (c == '\t') fputs("\\t", stdout);
        else if (c == '\\') fputs("\\\\", stdout);
        else if (c < 0x20) printf("\\x%02x", c);
        else putchar(c);
    }
}
static void flush(void) { if (npend) { fputs("T ", stdout); esc(pend, npend); putchar('\n'); npend = 0; } }
static void name(const xmlChar *uri, const xmlChar *l) { if (uri) printf("{%s}", uri); fputs((const char *)l, stdout); }
static int insub(void *ctx) { return ((xmlParserCtxtPtr)ctx)->inSubset != 0; }

static void se(void *ctx, const xmlChar *l, const xmlChar *pfx, const xmlChar *uri, int nns, const xmlChar **ns,
               int na, int nd, const xmlChar **a)
{
    (void)ctx; (void)pfx; (void)nd; flush();
    fputs("S ", stdout); name(uri, l); putchar('\n');
    for (int i = 0; i < nns; i++) printf("N %s=%s\n", ns[2 * i] ? (const char *)ns[2 * i] : "", ns[2 * i + 1] ? (const char *)ns[2 * i + 1] : "");
    for (int i = 0; i < na; i++) {
        const xmlChar **t = a + 5 * i;
        fputs("A ", stdout); name(t[2], t[0]); putchar('=');
        /* libxml2 leaves "&#38;" in a value as it is when entities are not substituted; with NOENT it is the text */
        esc((const char *)t[3], (size_t)(t[4] - t[3])); putchar('\n');
    }
}
static void ee(void *ctx, const xmlChar *l, const xmlChar *pfx, const xmlChar *uri) { (void)ctx; (void)pfx; flush(); fputs("E ", stdout); name(uri, l); putchar('\n'); }
static void ch(void *ctx, const xmlChar *t, int n)
{
    (void)ctx;
    if (npend + n + 1 > cpend) { cpend = (npend + n + 1) * 2; pend = realloc(pend, cpend); }
    memcpy(pend + npend, t, n); npend += n;
}
static void cm(void *ctx, const xmlChar *t) { if (insub(ctx)) return; flush(); fputs("C ", stdout); esc((const char *)t, strlen((const char *)t)); putchar('\n'); }
static void pi(void *ctx, const xmlChar *t, const xmlChar *d)
{
    if (insub(ctx)) return;
    flush(); printf("P %s ", t); if (d) esc((const char *)d, strlen((const char *)d)); putchar('\n');
}
static void isub(void *ctx, const xmlChar *n, const xmlChar *pub, const xmlChar *sys)
{
    flush(); printf("DOCTYPE %s %s %s\n", n, pub ? (const char *)pub : "-", sys ? (const char *)sys : "-");
    xmlSAX2InternalSubset(ctx, n, pub, sys);
}
static void ed(void *ctx) { flush(); puts("END"); xmlSAX2EndDocument(ctx); }
static void err(void *ctx, const xmlError *e) { (void)ctx; if (e->level >= XML_ERR_ERROR) { flush(); printf("ERROR %d %d:%d\n", e->level, e->line, e->int2); } }

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    xmlSAXHandler sax;
    memset(&sax, 0, sizeof sax);
    xmlSAXVersion(&sax, 2);
    sax.startElementNs = se; sax.endElementNs = ee; sax.characters = ch; sax.cdataBlock = ch;
    sax.ignorableWhitespace = ch; sax.comment = cm; sax.processingInstruction = pi; sax.internalSubset = isub;
    sax.endDocument = ed; sax.serror = err;
    xmlParserCtxtPtr c = xmlNewSAXParserCtxt(&sax, NULL);
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    char *b = malloc(n + 1);
    if (fread(b, 1, n, f) != (size_t)n) return 2;
    fclose(f);
    xmlDocPtr d = xmlCtxtReadMemory(c, b, (int)n, NULL, NULL, XML_PARSE_NOENT | XML_PARSE_NONET);
    flush();
    int wf = c->wellFormed;
    if (d) xmlFreeDoc(d);
    xmlFreeParserCtxt(c);
    free(b);
    return wf ? 0 : 1;
}
