/* OXP's small tree. MIT licence. Copyright (c) 2026 AmigaChrome Team. */
#include "oxp_dom.h"
#include <stdlib.h>
#include <string.h>

typedef struct block { struct block *next; size_t used, size; char data[]; } block;

struct oxp_doc {
    block *blocks;
    oxp_node docnode;
    const char **uris; size_t nuris, capuris;   /* each namespace name stored once */
};

struct oxp_dom_builder {
    oxp_doc *d;
    oxp_node *cur;
    const oxp_parser *p;
    char *pend; size_t npend, cappend; int pend_cdata;
    int nomem;
};

static void *arena(oxp_doc *d, size_t n)
{
    n = (n + 7) & ~(size_t)7;
    block *b = d->blocks;
    if (!b || b->size - b->used < n) {
        size_t sz = n > 16384 ? n : 16384;
        b = malloc(sizeof(block) + sz);
        if (!b) return NULL;
        b->size = sz; b->used = 0; b->next = d->blocks; d->blocks = b;
    }
    void *r = b->data + b->used;
    b->used += n;
    return r;
}
static const char *str(oxp_doc *d, const char *s, size_t n)
{
    if (!s) return NULL;
    char *q = arena(d, n + 1);
    if (!q) return NULL;
    memcpy(q, s, n); q[n] = 0;
    return q;
}
static const char *uri(oxp_doc *d, const char *u)
{
    if (!u) return NULL;
    for (size_t i = d->nuris; i-- > 0;) if (!strcmp(d->uris[i], u)) return d->uris[i];
    const char *s = str(d, u, strlen(u));
    if (!s) return NULL;
    if (d->nuris == d->capuris) {
        size_t c = d->capuris ? d->capuris * 2 : 8;
        const char **q = realloc(d->uris, c * sizeof *q);
        if (!q) return NULL;
        d->uris = q; d->capuris = c;
    }
    d->uris[d->nuris++] = s;
    return s;
}

static oxp_node *node(oxp_dom_builder *b, oxp_node_type t)
{
    oxp_node *n = arena(b->d, sizeof *n);
    if (!n) { b->nomem = 1; return NULL; }
    memset(n, 0, sizeof *n);
    n->type = t;
    n->parent = b->cur;
    n->line = b->p ? oxp_line(b->p) : 0;
    if (b->cur->last) { b->cur->last->next = n; n->prev = b->cur->last; } else b->cur->first = n;
    b->cur->last = n;
    return n;
}

static void flush(oxp_dom_builder *b)
{
    if (!b->npend) return;
    oxp_node *n = node(b, b->pend_cdata ? OXP_NODE_CDATA : OXP_NODE_TEXT);
    if (n) { n->text = str(b->d, b->pend, b->npend); n->len = b->npend; if (!n->text) b->nomem = 1; }
    b->npend = 0;
}
static void addtext(oxp_dom_builder *b, const char *t, size_t n, int cdata)
{
    if (b->npend && b->pend_cdata != cdata) flush(b);
    if (cdata && !n) { b->pend_cdata = 1; oxp_node *x = node(b, OXP_NODE_CDATA); if (x) { x->text = ""; } return; }
    if (b->npend + n + 1 > b->cappend) {
        size_t c = (b->npend + n + 1) * 2;
        char *q = realloc(b->pend, c);
        if (!q) { b->nomem = 1; return; }
        b->pend = q; b->cappend = c;
    }
    memcpy(b->pend + b->npend, t, n);
    b->npend += n;
    b->pend_cdata = cdata;
    if (cdata) flush(b);          /* each CDATA section is a node of its own */
}

static void h_start(void *ud, const char *l, const char *pfx, const char *u, int nns, const oxp_ns *ns, int na, const oxp_attr *a)
{
    oxp_dom_builder *b = ud;
    flush(b);
    oxp_node *n = node(b, OXP_NODE_ELEMENT);
    if (!n) return;
    oxp_doc *d = b->d;
    n->name = str(d, l, strlen(l));
    n->prefix = pfx ? str(d, pfx, strlen(pfx)) : NULL;
    n->uri = uri(d, u);
    oxp_dns **nt = &n->ns;
    for (int i = 0; i < nns; i++) {
        oxp_dns *x = arena(d, sizeof *x);
        if (!x) { b->nomem = 1; return; }
        x->prefix = ns[i].prefix ? str(d, ns[i].prefix, strlen(ns[i].prefix)) : NULL;
        x->uri = uri(d, ns[i].uri);
        x->next = NULL; *nt = x; nt = &x->next;
    }
    oxp_dattr **at = &n->attrs;
    for (int i = 0; i < na; i++) {
        oxp_dattr *x = arena(d, sizeof *x);
        if (!x) { b->nomem = 1; return; }
        x->local = str(d, a[i].local, strlen(a[i].local));
        x->prefix = a[i].prefix ? str(d, a[i].prefix, strlen(a[i].prefix)) : NULL;
        x->uri = uri(d, a[i].uri);
        x->value = str(d, a[i].value, a[i].value_len);
        x->value_len = a[i].value_len;
        x->next = NULL; *at = x; at = &x->next;
    }
    b->cur = n;
}
static void h_end(void *ud, const char *l, const char *p, const char *u)
{
    oxp_dom_builder *b = ud;
    (void)l; (void)p; (void)u;
    flush(b);
    if (b->cur->parent) b->cur = b->cur->parent;
}
static void h_chars(void *ud, const char *t, size_t n) { addtext(ud, t, n, 0); }
static void h_cdata(void *ud, const char *t, size_t n) { addtext(ud, t, n, 1); }
static void h_comment(void *ud, const char *t)
{
    oxp_dom_builder *b = ud;
    flush(b);
    oxp_node *n = node(b, OXP_NODE_COMMENT);
    if (n) { n->len = strlen(t); n->text = str(b->d, t, n->len); }
}
static void h_pi(void *ud, const char *target, const char *data)
{
    oxp_dom_builder *b = ud;
    flush(b);
    oxp_node *n = node(b, OXP_NODE_PI);
    if (n) { n->name = str(b->d, target, strlen(target)); n->len = strlen(data); n->text = str(b->d, data, n->len); }
}

static const oxp_handler dom_handler = {
    NULL, NULL, NULL, NULL, NULL, h_start, h_end, h_chars, h_cdata, h_comment, h_pi, NULL, NULL, NULL
};
const oxp_handler *oxp_dom_handler(void) { return &dom_handler; }

oxp_dom_builder *oxp_dom_builder_new(void)
{
    oxp_dom_builder *b = calloc(1, sizeof *b);
    if (!b) return NULL;
    b->d = calloc(1, sizeof *b->d);
    if (!b->d) { free(b); return NULL; }
    b->d->docnode.type = OXP_NODE_DOCUMENT;
    b->cur = &b->d->docnode;
    return b;
}
void oxp_dom_builder_set_parser(oxp_dom_builder *b, const oxp_parser *p) { b->p = p; }

oxp_doc *oxp_dom_builder_finish(oxp_dom_builder *b)
{
    flush(b);
    oxp_doc *d = b->d;
    int bad = b->nomem;
    free(b->pend);
    free(b);
    if (bad) { oxp_dom_free(d); return NULL; }
    return d;
}

oxp_doc *oxp_dom_parse(const void *data, size_t len, unsigned options, oxp_code *err, char *msg, size_t msgsize,
                       unsigned long *line, unsigned long *column)
{
    oxp_dom_builder *b = oxp_dom_builder_new();
    if (!b) { if (err) *err = OXP_E_NOMEM; return NULL; }
    oxp_parser *p = oxp_new(&dom_handler, b, NULL);
    if (!p) { oxp_dom_free(oxp_dom_builder_finish(b)); if (err) *err = OXP_E_NOMEM; return NULL; }
    b->p = p;
    oxp_set_options(p, options);
    oxp_code c = oxp_parse(p, data, len, 1);
    const char *m;
    unsigned long l, col;
    oxp_error(p, &m, &l, &col);
    if (err) *err = c;
    if (msg && msgsize) { msg[0] = 0; if (m) { size_t n = strlen(m); if (n >= msgsize) n = msgsize - 1; memcpy(msg, m, n); msg[n] = 0; } }
    if (line) *line = l;
    if (column) *column = col;
    oxp_free(p);
    oxp_doc *d = oxp_dom_builder_finish(b);
    if (c) { oxp_dom_free(d); return NULL; }
    if (!d && err) *err = OXP_E_NOMEM;
    return d;
}

void oxp_dom_free(oxp_doc *d)
{
    if (!d) return;
    for (block *b = d->blocks; b;) { block *n = b->next; free(b); b = n; }
    free(d->uris);
    free(d);
}

oxp_node *oxp_dom_document(oxp_doc *d) { return &d->docnode; }
oxp_node *oxp_dom_root(oxp_doc *d)
{
    for (oxp_node *n = d->docnode.first; n; n = n->next) if (n->type == OXP_NODE_ELEMENT) return n;
    return NULL;
}

static int same(const char *a, const char *b) { return a == b || (a && b && !strcmp(a, b)); }
static int match(const oxp_node *n, const char *u, const char *l)
{
    if (n->type != OXP_NODE_ELEMENT) return 0;
    if (l && strcmp(n->name, l)) return 0;
    if (u && u[0] == '*' && !u[1]) return 1;
    return same(n->uri, u);
}

const char *oxp_dom_attr(const oxp_node *el, const char *u, const char *l)
{
    for (const oxp_dattr *a = el->attrs; a; a = a->next)
        if (!strcmp(a->local, l) && same(a->uri, u)) return a->value;
    return NULL;
}
oxp_node *oxp_dom_child(const oxp_node *n, const char *u, const char *l)
{
    for (oxp_node *c = n->first; c; c = c->next) if (match(c, u, l)) return c;
    return NULL;
}
oxp_node *oxp_dom_next(const oxp_node *n, const char *u, const char *l)
{
    for (oxp_node *c = n->next; c; c = c->next) if (match(c, u, l)) return c;
    return NULL;
}

static size_t text_into(const oxp_node *n, char *buf, size_t size, size_t at)
{
    if (n->type == OXP_NODE_TEXT || n->type == OXP_NODE_CDATA) {
        if (at < size) { size_t k = size - at - 1 < n->len ? size - at - 1 : n->len; memcpy(buf + at, n->text, k); }
        return at + n->len;
    }
    if (n->type == OXP_NODE_ELEMENT || n->type == OXP_NODE_DOCUMENT)
        for (const oxp_node *c = n->first; c; c = c->next) at = text_into(c, buf, size, at);
    return at;
}
size_t oxp_dom_text(const oxp_node *n, char *buf, size_t size)
{
    size_t total = text_into(n, buf, size, 0);
    if (size) buf[total < size ? total : size - 1] = 0;
    return total;
}

typedef int (*wfn)(void *, const char *, size_t);
static int w(wfn f, void *c, const char *s) { return f(c, s, strlen(s)); }
static int wesc(wfn f, void *c, const char *s, size_t n, int attr)
{
    size_t b = 0;
    for (size_t i = 0; i < n; i++) {
        const char *r = NULL;
        switch (s[i]) {
        case '<': r = "&lt;"; break;
        case '>': r = "&gt;"; break;
        case '&': r = "&amp;"; break;
        case '"': if (attr) r = "&quot;"; break;
        case '\t': if (attr) r = "&#9;"; break;
        case '\n': if (attr) r = "&#10;"; break;
        case '\r': r = "&#13;"; break;
        }
        if (r) { if ((i > b && f(c, s + b, i - b)) || w(f, c, r)) return -1; b = i + 1; }
    }
    return n > b ? f(c, s + b, n - b) : 0;
}
static int qname(wfn f, void *c, const char *pfx, const char *l)
{
    if (pfx && (w(f, c, pfx) || w(f, c, ":"))) return -1;
    return w(f, c, l);
}

int oxp_dom_write(const oxp_node *n, wfn f, void *c)
{
    switch (n->type) {
    case OXP_NODE_DOCUMENT:
        for (const oxp_node *k = n->first; k; k = k->next) if (oxp_dom_write(k, f, c)) return -1;
        return 0;
    case OXP_NODE_TEXT: return wesc(f, c, n->text, n->len, 0);
    case OXP_NODE_CDATA: return (w(f, c, "<![CDATA[") || f(c, n->text, n->len) || w(f, c, "]]>")) ? -1 : 0;
    case OXP_NODE_COMMENT: return (w(f, c, "<!--") || f(c, n->text, n->len) || w(f, c, "-->")) ? -1 : 0;
    case OXP_NODE_PI:
        if (w(f, c, "<?") || w(f, c, n->name)) return -1;
        if (n->len && (w(f, c, " ") || f(c, n->text, n->len))) return -1;
        return w(f, c, "?>");
    case OXP_NODE_ELEMENT:
        if (w(f, c, "<") || qname(f, c, n->prefix, n->name)) return -1;
        for (const oxp_dns *x = n->ns; x; x = x->next) {
            if (w(f, c, x->prefix ? " xmlns:" : " xmlns") || (x->prefix && w(f, c, x->prefix)) || w(f, c, "=\"") ||
                wesc(f, c, x->uri, strlen(x->uri), 1) || w(f, c, "\"")) return -1;
        }
        for (const oxp_dattr *a = n->attrs; a; a = a->next)
            if (w(f, c, " ") || qname(f, c, a->prefix, a->local) || w(f, c, "=\"") || wesc(f, c, a->value, a->value_len, 1) || w(f, c, "\""))
                return -1;
        if (!n->first) return w(f, c, "/>");
        if (w(f, c, ">")) return -1;
        for (const oxp_node *k = n->first; k; k = k->next) if (oxp_dom_write(k, f, c)) return -1;
        return (w(f, c, "</") || qname(f, c, n->prefix, n->name) || w(f, c, ">")) ? -1 : 0;
    }
    return 0;
}
