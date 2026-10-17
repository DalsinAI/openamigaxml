/* libFuzzer target: clang -fsanitize=fuzzer,address,undefined -Iinclude tests/fuzz.c src/oxp.c
 * The first byte picks the options and the chunk size; the rest is the document.
 * MIT licence. Copyright (c) 2026 AmigaChrome Team. */
#include "oxp.h"
#include <stdint.h>
#include <string.h>

static size_t sink;
static void se(void *u, const char *l, const char *p, const char *uri, int nns, const oxp_ns *ns, int na, const oxp_attr *a)
{
    (void)u; sink += strlen(l) + (p ? strlen(p) : 0) + (uri ? strlen(uri) : 0);
    for (int i = 0; i < nns; i++) sink += strlen(ns[i].uri);
    for (int i = 0; i < na; i++) sink += strlen(a[i].local) + a[i].value_len + (a[i].uri ? strlen(a[i].uri) : 0);
}
static void ee(void *u, const char *l, const char *p, const char *uri) { (void)u; (void)p; (void)uri; sink += strlen(l); }
static void ch(void *u, const char *t, size_t n) { (void)u; if (n) sink += (unsigned char)t[n - 1]; }
static void cm(void *u, const char *t) { (void)u; sink += strlen(t); }
static void pi(void *u, const char *t, const char *d) { (void)u; sink += strlen(t) + strlen(d); }
static const char *res(void *u, const char *n) { (void)u; return n[0] == 'h' ? "&amp;h<i>x</i>" : NULL; }

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
    if (!n) return 0;
    unsigned sel = d[0];
    d++; n--;
    oxp_handler h = { 0 };
    h.start_element = se; h.end_element = ee; h.characters = ch; h.cdata = ch; h.comment = cm; h.pi = pi;
    if (sel & 8) h.resolve_entity = res;
    oxp_parser *p = oxp_new(&h, 0, 0);
    unsigned opt = 0;
    if (sel & 1) opt |= OXP_OPT_FRAGMENT;
    if (sel & 2) opt |= OXP_OPT_NO_NAMESPACES;
    if (sel & 4) opt |= OXP_OPT_UNBOUND_PREFIX;
    oxp_set_options(p, opt);
    if (sel & 16) oxp_set_encoding(p, (oxp_encoding)(1 + (sel >> 5) % 5));
    size_t chunk = (sel & 128) ? 1 + (sel % 13) : n + 1;
    oxp_code c = OXP_OK;
    for (size_t i = 0; i < n && !c; i += chunk) c = oxp_parse(p, d + i, n - i < chunk ? n - i : chunk, 0);
    if (!c) oxp_parse(p, 0, 0, 1);
    oxp_free(p);
    return 0;
}
