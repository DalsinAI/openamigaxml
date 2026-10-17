/* OXP: the Open XML Parser, the streaming core.
 * MIT licence. Copyright (c) 2026 AmigaChrome Team.
 *
 * Written from the XML 1.0 (fifth edition) and Namespaces in XML 1.0
 * (third edition) specifications. Production names in the comments are
 * theirs. No code from any other XML parser.
 *
 * How it works: input bytes are decoded into UTF-8 with line ends
 * normalised (2.11) and every character checked against Char (2.2). The
 * parser then reads one unit at a time (a tag, a comment, a run of text...).
 * A unit that is not complete yet is read again from its start when more
 * input arrives, so no event is ever reported twice. Entity replacement
 * text is read through a stack of inputs.
 */
#include "oxp_int.h"
#include <stdlib.h>

#define R_OK   0
#define R_MORE 1
#define R_ERR  2
#define NOPOS ((size_t)-1)

/* ---------------------------------------------------------------- memory */

static void *d_alloc(void *c, size_t n) { (void)c; return malloc(n); }
static void *d_realloc(void *c, void *q, size_t n) { (void)c; return realloc(q, n); }
static void d_free(void *c, void *q) { (void)c; free(q); }

static void *xalloc(oxp_parser *p, size_t n) { return p->a.alloc(p->a.ctx, n ? n : 1); }
static void xfree(oxp_parser *p, void *q) { if (q) p->a.free(p->a.ctx, q); }
static void *xrealloc(oxp_parser *p, void *q, size_t n) { return p->a.realloc(p->a.ctx, q, n ? n : 1); }

static int fail(oxp_parser *p, oxp_code code, const char *msg);

static int grow(oxp_parser *p, void **arr, size_t *cap, size_t need, size_t elem)
{
    if (need <= *cap) return 1;
    size_t n = *cap ? *cap : 16;
    while (n < need) n *= 2;
    void *q = xrealloc(p, *arr, n * elem);
    if (!q) { fail(p, OXP_E_NOMEM, "out of memory"); return 0; }
    *arr = q; *cap = n;
    return 1;
}

static int sb_reserve(oxp_parser *p, sbuf *b, size_t more)
{
    return grow(p, (void **)&b->p, &b->cap, b->n + more + 1, 1);
}
static int sb_add(oxp_parser *p, sbuf *b, const char *s, size_t n)
{
    if (!sb_reserve(p, b, n)) return 0;
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
    return 1;
}
static int sb_addc(oxp_parser *p, sbuf *b, char c) { return sb_add(p, b, &c, 1); }
static size_t sb_put(oxp_parser *p, sbuf *b, const char *s, size_t n)   /* adds s and a NUL; returns its offset */
{
    size_t at = b->n;
    if (!sb_reserve(p, b, n + 1)) return NOPOS;
    memcpy(b->p + b->n, s, n);
    b->p[b->n + n] = 0;
    b->n += n + 1;
    return at;
}
static char *xstrndup(oxp_parser *p, const char *s, size_t n)
{
    char *q = xalloc(p, n + 1);
    if (!q) { fail(p, OXP_E_NOMEM, "out of memory"); return NULL; }
    memcpy(q, s, n);
    q[n] = 0;
    return q;
}

/* ---------------------------------------------------------------- UTF-8 */

static size_t utf8_put(char *o, unsigned long c)
{
    if (c < 0x80) { o[0] = (char)c; return 1; }
    if (c < 0x800) { o[0] = (char)(0xC0 | (c >> 6)); o[1] = (char)(0x80 | (c & 63)); return 2; }
    if (c < 0x10000) {
        o[0] = (char)(0xE0 | (c >> 12)); o[1] = (char)(0x80 | ((c >> 6) & 63)); o[2] = (char)(0x80 | (c & 63));
        return 3;
    }
    o[0] = (char)(0xF0 | (c >> 18)); o[1] = (char)(0x80 | ((c >> 12) & 63));
    o[2] = (char)(0x80 | ((c >> 6) & 63)); o[3] = (char)(0x80 | (c & 63));
    return 4;
}

/* the character at s (valid UTF-8, as the decoder wrote it); *n its length */
static unsigned long utf8_get(const char *s, size_t *n)
{
    const u8 *u = (const u8 *)s;
    if (u[0] < 0x80) { *n = 1; return u[0]; }
    if (u[0] < 0xE0) { *n = 2; return ((unsigned long)(u[0] & 31) << 6) | (u[1] & 63); }
    if (u[0] < 0xF0) { *n = 3; return ((unsigned long)(u[0] & 15) << 12) | ((unsigned long)(u[1] & 63) << 6) | (u[2] & 63); }
    *n = 4;
    return ((unsigned long)(u[0] & 7) << 18) | ((unsigned long)(u[1] & 63) << 12) | ((unsigned long)(u[2] & 63) << 6) | (u[3] & 63);
}

/* Char (2.2) */
static int is_char(unsigned long c)
{
    return c == 9 || c == 10 || c == 13 || (c >= 0x20 && c <= 0xD7FF) || (c >= 0xE000 && c <= 0xFFFD) ||
           (c >= 0x10000 && c <= 0x10FFFF);
}
/* NameStartChar and NameChar (2.3) */
static int is_name_start(unsigned long c)
{
    if (c < 0x80) return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == ':';
    return (c >= 0xC0 && c <= 0xD6) || (c >= 0xD8 && c <= 0xF6) || (c >= 0xF8 && c <= 0x2FF) ||
           (c >= 0x370 && c <= 0x37D) || (c >= 0x37F && c <= 0x1FFF) || (c >= 0x200C && c <= 0x200D) ||
           (c >= 0x2070 && c <= 0x218F) || (c >= 0x2C00 && c <= 0x2FEF) || (c >= 0x3001 && c <= 0xD7FF) ||
           (c >= 0xF900 && c <= 0xFDCF) || (c >= 0xFDF0 && c <= 0xFFFD) || (c >= 0x10000 && c <= 0xEFFFF);
}
static int is_name_char(unsigned long c)
{
    if (c < 0x80) return is_name_start(c) || c == '-' || c == '.' || (c >= '0' && c <= '9');
    return is_name_start(c) || c == 0xB7 || (c >= 0x300 && c <= 0x36F) || (c >= 0x203F && c <= 0x2040);
}
static int is_space(int c) { return c == 0x20 || c == 9 || c == 10 || c == 13; }

/* ---------------------------------------------------------------- position and errors */

/* the input width of one decoded character, for oxp_bytes_consumed */
static ulong in_width(oxp_parser *p, unsigned long c, size_t n)
{
    switch (p->enc) {
    case OXP_ENC_UTF16LE: case OXP_ENC_UTF16BE: return c >= 0x10000 ? 4 : 2;
    case OXP_ENC_LATIN1: case OXP_ENC_ASCII: return 1;
    default: return n;
    }
}

/* move the line and column count forward to absolute offset abs of the document text */
static void advance_lc(oxp_parser *p, ulong abs)
{
    while (p->lc_abs < abs && p->lc_abs < p->base + p->doc.n) {
        const char *s = p->doc.p + (p->lc_abs - p->base);
        size_t n;
        unsigned long c = utf8_get(s, &n);
        p->bytes_done += in_width(p, c, n);
        if (c == '\n') {
            p->line++; p->col = 1;
            if (p->crlf_head < p->ncrlf && p->crlf[p->crlf_head] == p->lc_abs) { p->bytes_done += in_width(p, 13, 1); p->crlf_head++; }
        } else p->col++;
        p->lc_abs += n;
    }
}

/* the document position the parser is at: the top input's, or the
 * reference that opened the outermost entity */
static ulong cur_abs(oxp_parser *p)
{
    return p->base + p->din.pos;
}

static void report(oxp_parser *p, oxp_level lv, oxp_code code, const char *msg, ulong abs)
{
    ulong save_abs = p->lc_abs, sl = p->line, sc = p->col, sb = p->bytes_done;
    size_t sh = p->crlf_head;
    advance_lc(p, abs);
    ulong line = p->line, col = p->col;
    if (lv != OXP_FATAL) { p->lc_abs = save_abs; p->line = sl; p->col = sc; p->bytes_done = sb; p->crlf_head = sh; }
    if (lv == OXP_FATAL) {
        p->err = code;
        p->err_line = line; p->err_col = col;
        size_t n = strlen(msg);
        if (n >= sizeof p->errmsg) n = sizeof p->errmsg - 1;
        memcpy(p->errmsg, msg, n); p->errmsg[n] = 0;
        p->well_formed = 0;
    }
    if (p->h.error && !p->stopped) p->h.error(p->ud, lv, code, msg, line, col);
}

static int fail(oxp_parser *p, oxp_code code, const char *msg)
{
    if (p->err) return R_ERR;
    report(p, OXP_FATAL, code, msg, cur_abs(p));
    return R_ERR;
}
static int failn(oxp_parser *p, oxp_code code, const char *msg, const char *name)
{
    char b[160];
    size_t n = strlen(msg), m = name ? strlen(name) : 0;
    if (n > 100) n = 100;
    if (m > 50) m = 50;
    memcpy(b, msg, n);
    if (name) { b[n++] = ' '; b[n++] = '\''; memcpy(b + n, name, m); n += m; b[n++] = '\''; }
    b[n] = 0;
    return fail(p, code, b);
}

/* ---------------------------------------------------------------- decoding */

static int crlf_mark(oxp_parser *p, ulong abs)
{
    if (p->crlf_head && p->crlf_head == p->ncrlf) p->crlf_head = p->ncrlf = 0;
    if (!grow(p, (void **)&p->crlf, &p->crlf_cap, p->ncrlf + 1, sizeof(ulong))) return 0;
    p->crlf[p->ncrlf++] = abs;
    return 1;
}

/* add one decoded character to the document text */
static int emit_char(oxp_parser *p, unsigned long c)
{
    if (!is_char(c)) {
        p->din.pos = p->doc.n;
        return fail(p, OXP_E_CHAR, "a character XML does not allow") == R_OK;
    }
    if (p->pending_cr) {
        p->pending_cr = 0;
        if (c == 10) {                      /* CR LF: one LF, already written */
            return crlf_mark(p, p->base + p->doc.n - 1);
        }
    }
    if (c == 13) { p->pending_cr = 1; c = 10; }
    char o[4];
    size_t n = utf8_put(o, c);
    return sb_add(p, &p->doc, o, n);
}

static int set_encoding_by_name(oxp_parser *p, const char *name);
static int ieq(const char *a, const char *b);

/* decode what the raw buffer holds, as far as the encoding allows */
static int decode(oxp_parser *p, int last)
{
    const u8 *r = (const u8 *)p->raw.p;
    size_t i = p->raw_pos, n = p->raw.n;
    if (!p->enc_known) {
        size_t have = n - i;
        if (have < 5 && !last) return 1;
        if (p->enc == OXP_ENC_AUTO) {
            if (have >= 3 && r[i] == 0xEF && r[i + 1] == 0xBB && r[i + 2] == 0xBF) { p->enc = OXP_ENC_UTF8; i += 3; p->bom_bytes = 3; }
            else if (have >= 2 && r[i] == 0xFE && r[i + 1] == 0xFF) { p->enc = OXP_ENC_UTF16BE; i += 2; p->bom_bytes = 2; }
            else if (have >= 2 && r[i] == 0xFF && r[i + 1] == 0xFE) { p->enc = OXP_ENC_UTF16LE; i += 2; p->bom_bytes = 2; }
            else if (have >= 4 && r[i] == 0 && r[i + 1] == '<' && r[i + 2] == 0 && r[i + 3] == '?') p->enc = OXP_ENC_UTF16BE;
            else if (have >= 4 && r[i] == '<' && r[i + 1] == 0 && r[i + 2] == '?' && r[i + 3] == 0) p->enc = OXP_ENC_UTF16LE;
            else {
                p->enc = OXP_ENC_UTF8;
                p->hold_decl = !p->enc_fixed && have >= 5 && !memcmp(r + i, "<?xml", 5);
            }
        } else {
            /* a fixed encoding: still drop its BOM */
            if (p->enc == OXP_ENC_UTF8 && have >= 3 && r[i] == 0xEF && r[i + 1] == 0xBB && r[i + 2] == 0xBF) { i += 3; p->bom_bytes = 3; }
            else if (p->enc == OXP_ENC_UTF16BE && have >= 2 && r[i] == 0xFE && r[i + 1] == 0xFF) { i += 2; p->bom_bytes = 2; }
            else if (p->enc == OXP_ENC_UTF16LE && have >= 2 && r[i] == 0xFF && r[i + 1] == 0xFE) { i += 2; p->bom_bytes = 2; }
        }
        p->bytes_done = p->bom_bytes;
        p->enc_known = 1;
    }
    size_t stop = n;
    if (p->hold_decl) {
        /* until the XML declaration is read, decode no further than its '>' */
        size_t k = i;
        while (k < n && r[k] != '>') k++;
        stop = k < n ? k + 1 : last ? n : i;
    }
    switch (p->enc) {
    case OXP_ENC_UTF16LE: case OXP_ENC_UTF16BE: {
        int le = p->enc == OXP_ENC_UTF16LE;
        while (i + 2 <= stop) {
            unsigned long c = le ? (r[i] | (r[i + 1] << 8)) : ((r[i] << 8) | r[i + 1]);
            if (c >= 0xD800 && c <= 0xDBFF) {
                if (i + 4 > stop) break;
                unsigned long d = le ? (r[i + 2] | (r[i + 3] << 8)) : ((r[i + 2] << 8) | r[i + 3]);
                if (d < 0xDC00 || d > 0xDFFF) { p->din.pos = p->doc.n; return fail(p, OXP_E_ENCODING, "a lone UTF-16 surrogate") == R_OK; }
                c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00);
                i += 4;
            } else if (c >= 0xDC00 && c <= 0xDFFF) {
                p->din.pos = p->doc.n; return fail(p, OXP_E_ENCODING, "a lone UTF-16 surrogate") == R_OK;
            } else i += 2;
            if (!emit_char(p, c)) return 0;
        }
        if (last && i < n) { p->din.pos = p->doc.n; return fail(p, OXP_E_ENCODING, "the input ends inside a UTF-16 character") == R_OK; }
        break;
    }
    case OXP_ENC_LATIN1: case OXP_ENC_ASCII:
        for (; i < stop; i++) {
            if (p->enc == OXP_ENC_ASCII && r[i] >= 0x80) { p->din.pos = p->doc.n; return fail(p, OXP_E_ENCODING, "a byte above 127 in US-ASCII") == R_OK; }
            if (!emit_char(p, r[i])) return 0;
        }
        break;
    default:
        while (i < stop) {
            unsigned long c = r[i];
            size_t len = c < 0x80 ? 1 : (c >= 0xC2 && c <= 0xDF) ? 2 : (c >= 0xE0 && c <= 0xEF) ? 3 : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
            if (!len) { p->din.pos = p->doc.n; return fail(p, OXP_E_ENCODING, "not UTF-8") == R_OK; }
            if (i + len > stop) {
                if (stop < n || !last) break;
                p->din.pos = p->doc.n; return fail(p, OXP_E_ENCODING, "the input ends inside a UTF-8 character") == R_OK;
            }
            if (len > 1) {
                c &= (len == 2) ? 31 : (len == 3) ? 15 : 7;
                for (size_t k = 1; k < len; k++) {
                    if ((r[i + k] & 0xC0) != 0x80) { p->din.pos = p->doc.n; return fail(p, OXP_E_ENCODING, "not UTF-8") == R_OK; }
                    c = (c << 6) | (r[i + k] & 63);
                }
                if ((len == 3 && c < 0x800) || (len == 4 && (c < 0x10000 || c > 0x10FFFF)) || (c >= 0xD800 && c <= 0xDFFF)) {
                    p->din.pos = p->doc.n; return fail(p, OXP_E_ENCODING, "not UTF-8") == R_OK;
                }
            }
            i += len;
            if (!emit_char(p, c)) return 0;
        }
        break;
    }
    p->raw_pos = i;
    if (p->raw_pos == p->raw.n) p->raw_pos = p->raw.n = 0;
    p->din.s = p->doc.p;
    p->din.len = p->doc.n;
    return 1;
}

/* the XML declaration was read (or there is none): fix the encoding and decode the rest */
static int release_decl(oxp_parser *p, const char *declared)
{
    if (!p->hold_decl) {
        /* a UTF-8 BOM: the declaration must not name another encoding (4.3.3) */
        if (declared && !p->enc_fixed && p->enc == OXP_ENC_UTF8 && p->bom_bytes == 3 && !ieq(declared, "UTF-8") && !ieq(declared, "UTF8"))
            return failn(p, OXP_E_ENCODING, "a UTF-8 byte order mark, but the declaration says", declared) == R_OK;
        /* UTF-16 found by its BOM or first bytes: a declaration must agree */
        if (declared && !p->enc_fixed && (p->enc == OXP_ENC_UTF16LE || p->enc == OXP_ENC_UTF16BE)) {
            const char *d = declared;
            if (!((d[0] | 32) == 'u' && (d[1] | 32) == 't' && (d[2] | 32) == 'f' && d[3] == '-' && d[4] == '1' && d[5] == '6'))
                return failn(p, OXP_E_ENCODING, "the declared encoding does not match the document's:", declared) == R_OK;
        }
        return 1;
    }
    p->hold_decl = 0;
    if (declared && !(p->options & OXP_OPT_IGNORE_DECL_ENCODING) && !set_encoding_by_name(p, declared)) return 0;
    return decode(p, p->last_seen);
}

static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return *a == *b;
}

static int set_encoding_by_name(oxp_parser *p, const char *name)
{
    if (ieq(name, "UTF-8") || ieq(name, "UTF8")) p->enc = OXP_ENC_UTF8;
    else if (ieq(name, "ISO-8859-1") || ieq(name, "ISO_8859-1") || ieq(name, "latin1") || ieq(name, "ISO-Latin-1") ||
             ieq(name, "l1") || ieq(name, "ISO8859-1")) p->enc = OXP_ENC_LATIN1;
    else if (ieq(name, "US-ASCII") || ieq(name, "ASCII")) p->enc = OXP_ENC_ASCII;
    else if (ieq(name, "UTF-16") || ieq(name, "UTF-16LE") || ieq(name, "UTF-16BE"))
        return failn(p, OXP_E_ENCODING, "UTF-16 declared, but the document is not in UTF-16:", name) == R_OK;
    else return failn(p, OXP_E_ENCODING, "an encoding this parser does not read:", name) == R_OK;
    return 1;
}

/* ---------------------------------------------------------------- reading helpers */

#define S(in) ((in)->s)
#define AV(in) ((in)->len - (in)->pos)

static int stopped(oxp_parser *p)
{
    if (!p->stopped) return 0;
    if (!p->err) { p->err = OXP_E_STOPPED; strcpy(p->errmsg, "stopped"); }
    return 1;
}
#define EVENT(call) do { call; if (stopped(p)) return R_ERR; } while (0)

/* k more characters (bytes) needed at in->pos */
static int need(oxp_parser *p, input *in, size_t k)
{
    if (AV(in) >= k) return R_OK;
    if (in->final) {
        if (in->ent) return fail(p, OXP_E_SYNTAX, in->dtd ? "a parameter entity ends inside a declaration" : "an entity ends inside markup");
        return fail(p, OXP_E_UNCLOSED, "the document ends inside markup");
    }
    return R_MORE;
}
#define NEED(k) do { int r_ = need(p, in, (k)); if (r_) { if (r_ == R_MORE) in->pos = start; return r_; } } while (0)
/* k characters if more may come; at the end of the input, go on with what there is */
#define WANT(k) do { if (AV(in) < (k) && !in->final) { in->pos = start; return R_MORE; } } while (0)
#define CHECK(x) do { int r_ = (x); if (r_) { if (r_ == R_MORE) in->pos = start; return r_; } } while (0)

static int at(input *in, const char *lit)
{
    size_t n = strlen(lit);
    return AV(in) >= n && !memcmp(in->s + in->pos, lit, n);
}
/* is lit at pos, as far as the input goes? (a prefix of it counts while more may come) */
static int maybe_at(input *in, const char *lit)
{
    size_t n = strlen(lit), a = AV(in);
    if (a > n) a = n;
    return !memcmp(in->s + in->pos, lit, a);
}

static size_t skip_s(input *in)
{
    size_t n = 0;
    while (in->pos < in->len && is_space((u8)in->s[in->pos])) { in->pos++; n++; }
    return n;
}

/* a Name at in->pos: *b, *e its bytes */
static int read_name(oxp_parser *p, input *in, size_t *b, size_t *e)
{
    size_t i = in->pos, n;
    if (i >= in->len) return need(p, in, 1);
    unsigned long c = utf8_get(in->s + i, &n);
    if (i + n > in->len) return need(p, in, n);
    if (!is_name_start(c)) return fail(p, OXP_E_SYNTAX, "a name was expected");
    i += n;
    for (;;) {
        if (i >= in->len) {
            if (!in->final) return R_MORE;
            break;
        }
        c = utf8_get(in->s + i, &n);
        if (i + n > in->len) { if (!in->final) return R_MORE; break; }
        if (!is_name_char(c)) break;
        i += n;
        if (i - in->pos > p->lim.max_name) return fail(p, OXP_E_LIMIT, "a name is too long");
    }
    *b = in->pos; *e = i;
    in->pos = i;
    return R_OK;
}

/* Nmtoken */
static int read_nmtoken(oxp_parser *p, input *in)
{
    size_t i = in->pos, n, k = 0;
    for (;;) {
        if (i >= in->len) { if (!in->final) return R_MORE; break; }
        unsigned long c = utf8_get(in->s + i, &n);
        if (!is_name_char(c)) break;
        i += n; k++;
    }
    if (!k) return fail(p, OXP_E_SYNTAX, "a name token was expected");
    in->pos = i;
    return R_OK;
}

/* the offset of lit at or after from, or NOPOS when the input has not got it yet */
static size_t find(oxp_parser *p, input *in, size_t unit, size_t from, const char *lit)
{
    size_t n = strlen(lit);
    if (in == &p->din && p->hint_unit == p->base + unit && p->hint_scan > p->base + from)
        from = p->hint_scan - p->base;
    for (size_t i = from; i + n <= in->len; i++)
        if (in->s[i] == lit[0] && !memcmp(in->s + i, lit, n)) return i;
    if (in == &p->din) {
        p->hint_unit = p->base + unit;
        p->hint_scan = p->base + (in->len >= n ? in->len - n + 1 : from);
        if (p->hint_scan < p->base + from) p->hint_scan = p->base + from;
    }
    return NOPOS;
}

/* ---------------------------------------------------------------- entities and attribute lists */

static entity *find_ent(entity *list, const char *name, size_t n)
{
    for (entity *e = list; e; e = e->next)
        if (!strncmp(e->name, name, n) && !e->name[n]) return e;
    return NULL;
}
static attlist *find_attlist(oxp_parser *p, const char *name, size_t n)
{
    for (attlist *a = p->atts; a; a = a->next)
        if (!strncmp(a->element, name, n) && !a->element[n]) return a;
    return NULL;
}
static const char *predefined(const char *name, size_t n)
{
    if (n == 2 && !memcmp(name, "lt", 2)) return "<";
    if (n == 2 && !memcmp(name, "gt", 2)) return ">";
    if (n == 3 && !memcmp(name, "amp", 3)) return "&";
    if (n == 4 && !memcmp(name, "apos", 4)) return "'";
    if (n == 4 && !memcmp(name, "quot", 4)) return "\"";
    return NULL;
}

/* is an undeclared general entity a well-formedness error here? (4.1, WFC: Entity Declared) */
static int wfc_declared(oxp_parser *p)
{
    return !p->seen_doctype || (!p->doc_has_external && !p->doc_has_pe_ref) || p->standalone == 1;
}

/* the bytes entities may still produce */
static int budget(oxp_parser *p, size_t n)
{
    ulong allow = p->lim.max_expansion_ratio * (p->in_total + 1);
    if (allow < p->lim.max_expansion_floor) allow = p->lim.max_expansion_floor;
    p->expanded += n;
    if (p->expanded > allow) return fail(p, OXP_E_ENTITY_LIMIT, "entities expand to too much text");
    return R_OK;
}

/* &#...; at s (n bytes, up to and including ';'): the character, or 0 */
static unsigned long charref(const char *s, size_t n)
{
    unsigned long c = 0;
    size_t i = 2;
    if (n < 4) return 0;
    if (s[2] == 'x') {
        i = 3;
        if (n < 5) return 0;
        for (; i < n - 1; i++) {
            int d = s[i];
            if (d >= '0' && d <= '9') d -= '0';
            else if (d >= 'a' && d <= 'f') d -= 'a' - 10;
            else if (d >= 'A' && d <= 'F') d -= 'A' - 10;
            else return 0;
            c = c * 16 + (unsigned long)d;
            if (c > 0x10FFFF) return 0;
        }
    } else {
        for (; i < n - 1; i++) {
            if (s[i] < '0' || s[i] > '9') return 0;
            c = c * 10 + (unsigned long)(s[i] - '0');
            if (c > 0x10FFFF) return 0;
        }
    }
    return is_char(c) ? c : 0;
}

/* the end of a reference starting at s[i] ('&'): the index after ';', or 0 when it is not well-formed */
static size_t ref_end(const char *s, size_t n, size_t i)
{
    size_t k = i + 1;
    if (k < n && s[k] == '#') {
        k++;
        if (k < n && s[k] == 'x') k++;
        while (k < n && ((s[k] >= '0' && s[k] <= '9') || (s[k] >= 'a' && s[k] <= 'f') || (s[k] >= 'A' && s[k] <= 'F'))) k++;
        return k < n && s[k] == ';' ? k + 1 : 0;
    }
    size_t m;
    if (k >= n || !is_name_start(utf8_get(s + k, &m))) return 0;
    k += m;
    while (k < n && s[k] != ';') {
        if (!is_name_char(utf8_get(s + k, &m))) return 0;
        k += m;
    }
    return k < n ? k + 1 : 0;
}

/* Attribute-value normalisation (3.3.3) of n bytes at s, appended to out.
 * depth: entities inside one another. */
static int attval(oxp_parser *p, sbuf *out, const char *s, size_t n, int depth)
{
    for (size_t i = 0; i < n;) {
        u8 c = (u8)s[i];
        if (c == '<') return fail(p, OXP_E_SYNTAX, "'<' in an attribute value");
        if (c == '&') {
            size_t e = ref_end(s, n, i);
            if (!e) return fail(p, OXP_E_SYNTAX, "a malformed reference in an attribute value");
            if (s[i + 1] == '#') {
                unsigned long ch = charref(s + i, e - i);
                if (!ch) return fail(p, OXP_E_CHAR, "a character reference to a character XML does not allow");
                char o[4];
                if (!sb_add(p, out, o, utf8_put(o, ch))) return R_ERR;
            } else {
                const char *nm = s + i + 1;
                size_t nl = e - i - 2;
                const char *pre = predefined(nm, nl);
                if (pre) { if (!sb_add(p, out, pre, 1)) return R_ERR; }
                else {
                    entity *en = find_ent(p->gents, nm, nl);
                    const char *text = NULL;
                    char nb[64];
                    if (!en) {
                        size_t m = nl < sizeof nb - 1 ? nl : sizeof nb - 1;
                        memcpy(nb, nm, m); nb[m] = 0;
                        if (p->h.resolve_entity) text = p->h.resolve_entity(p->ud, nb);
                        if (!text) {
                            if (wfc_declared(p)) return failn(p, OXP_E_ENTITY_UNDECLARED, "an undeclared entity:", nb);
                            if (p->h.skipped_entity) EVENT(p->h.skipped_entity(p->ud, nb));
                            i = e;
                            continue;
                        }
                        if ((ulong)depth >= p->lim.max_entity_depth) return fail(p, OXP_E_ENTITY_LIMIT, "entities nest too deep");
                        if (budget(p, strlen(text))) return R_ERR;
                        if (attval(p, out, text, strlen(text), depth + 1)) return R_ERR;
                        i = e;
                        continue;
                    }
                    if (en->type != OXP_ENTITY_INTERNAL_GENERAL)
                        return failn(p, en->type == OXP_ENTITY_EXTERNAL_GENERAL_UNPARSED ? OXP_E_ENTITY_UNPARSED : OXP_E_ENTITY_EXTERNAL,
                                     "an attribute value refers to an external entity:", en->name);
                    if (en->open) return failn(p, OXP_E_ENTITY_RECURSION, "an entity refers to itself:", en->name);
                    if ((ulong)depth >= p->lim.max_entity_depth) return fail(p, OXP_E_ENTITY_LIMIT, "entities nest too deep");
                    if (budget(p, en->value_len)) return R_ERR;
                    en->open = 1;
                    int r = attval(p, out, en->value, en->value_len, depth + 1);
                    en->open = 0;
                    if (r) return r;
                }
            }
            i = e;
            continue;
        }
        if (c == 9 || c == 10 || c == 13) c = ' ';
        if (!sb_addc(p, out, (char)c)) return R_ERR;
        i++;
    }
    if (out->n > p->lim.max_text) return fail(p, OXP_E_LIMIT, "an attribute value is too long");
    return R_OK;
}

/* a tokenized type: drop leading and trailing spaces, and fold runs of them */
static size_t collapse(char *s, size_t n)
{
    size_t o = 0;
    int sp = 1;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == ' ') { if (!sp) { s[o++] = ' '; sp = 1; } }
        else { s[o++] = s[i]; sp = 0; }
    }
    if (o && s[o - 1] == ' ') o--;
    s[o] = 0;
    return o;
}

/* ---------------------------------------------------------------- namespaces */

static const char *ns_lookup(oxp_parser *p, const char *prefix, size_t n)
{
    if (prefix && n == 3 && !memcmp(prefix, "xml", 3)) return OXP_XML_NS;
    for (size_t i = p->nbinds; i-- > 0;) {
        binding *b = &p->binds[i];
        if (!prefix) { if (b->prefix == NOPOS) return p->nsbuf.p + b->uri; }
        else if (b->prefix != NOPOS && !strncmp(p->nsbuf.p + b->prefix, prefix, n) && !p->nsbuf.p[b->prefix + n])
            return p->nsbuf.p + b->uri;
    }
    return NULL;
}

static int bind(oxp_parser *p, const char *prefix, size_t pn, const char *uri, size_t un)
{
    if (!grow(p, (void **)&p->binds, &p->cap_binds, p->nbinds + 1, sizeof(binding))) return R_ERR;
    binding *b = &p->binds[p->nbinds];
    b->prefix = prefix ? sb_put(p, &p->nsbuf, prefix, pn) : NOPOS;
    if (prefix && b->prefix == NOPOS) return R_ERR;
    b->uri = sb_put(p, &p->nsbuf, uri, un);
    if (b->uri == NOPOS) return R_ERR;
    p->nbinds++;
    return R_OK;
}

/* an undeclared prefix: fatal, or with OXP_OPT_UNBOUND_PREFIX an error the parse goes on from */
static int unbound(oxp_parser *p, const char *prefix, size_t n)
{
    char nb[64];
    size_t m = n < sizeof nb - 1 ? n : sizeof nb - 1;
    memcpy(nb, prefix, m); nb[m] = 0;
    if (!(p->options & OXP_OPT_UNBOUND_PREFIX)) return failn(p, OXP_E_NS_PREFIX, "an undeclared namespace prefix:", nb);
    char b[100];
    strcpy(b, "an undeclared namespace prefix: '"); strcat(b, nb); strcat(b, "'");
    p->well_formed = 0;
    report(p, OXP_ERROR, OXP_E_NS_PREFIX, b, cur_abs(p));
    return R_OK;
}

/* split a qualified name: the colon's index, NOPOS when there is none; R_ERR when it is not a QName */
static int qsplit(oxp_parser *p, const char *q, size_t n, size_t *colon)
{
    *colon = NOPOS;
    for (size_t i = 0; i < n; i++)
        if (q[i] == ':') {
            if (*colon != NOPOS || i == 0 || i == n - 1) return fail(p, OXP_E_NS_PREFIX, "a name with a misplaced colon");
            *colon = i;
        }
    if (*colon != NOPOS) {
        size_t m;
        if (!is_name_start(utf8_get(q + *colon + 1, &m))) return fail(p, OXP_E_NS_PREFIX, "a local name must start with a letter or '_'");
    }
    return R_OK;
}

/* ---------------------------------------------------------------- elements */

static int push_el(oxp_parser *p, const char *q, size_t qn, size_t colon, const char *uri, size_t nbind)
{
    if (p->nels >= p->lim.max_depth) return fail(p, OXP_E_LIMIT, "elements nest too deep");
    if (!grow(p, (void **)&p->els, &p->cap_els, p->nels + 1, sizeof(openel))) return R_ERR;
    openel *e = &p->els[p->nels];
    e->qname = sb_put(p, &p->stack, q, qn);
    if (e->qname == NOPOS) return R_ERR;
    if (colon != NOPOS) {
        e->prefix = sb_put(p, &p->stack, q, colon);
        e->local = sb_put(p, &p->stack, q + colon + 1, qn - colon - 1);
        if (e->prefix == NOPOS || e->local == NOPOS) return R_ERR;
    } else { e->prefix = NOPOS; e->local = e->qname; }
    e->uri = uri ? sb_put(p, &p->stack, uri, strlen(uri)) : NOPOS;
    if (uri && e->uri == NOPOS) return R_ERR;
    e->nbind = nbind;
    p->nels++;
    return R_OK;
}

static int end_el(oxp_parser *p)
{
    openel *e = &p->els[p->nels - 1];
    const char *st = p->stack.p;
    if (p->h.end_element)
        EVENT(p->h.end_element(p->ud, st + e->local, e->prefix == NOPOS ? NULL : st + e->prefix, e->uri == NOPOS ? NULL : st + e->uri));
    if (e->nbind) {
        size_t first = p->nbinds - e->nbind;
        binding *b = &p->binds[first];
        p->nsbuf.n = b->prefix != NOPOS ? b->prefix : b->uri;
        p->nbinds = first;
    }
    p->stack.n = e->qname;
    p->nels--;
    return R_OK;
}

/* a start tag or an empty-element tag, at '<' */
static int start_tag(oxp_parser *p, input *in)
{
    size_t start = in->pos, qb, qe;
    in->pos++;
    CHECK(read_name(p, in, &qb, &qe));
    p->scratch.n = 0;
    p->nra = 0;
    int empty = 0;
    for (;;) {
        size_t sp = skip_s(in);
        NEED(1);
        char c = S(in)[in->pos];
        if (c == '>') { in->pos++; break; }
        if (c == '/') { NEED(2); if (S(in)[in->pos + 1] != '>') return fail(p, OXP_E_SYNTAX, "'/' must be followed by '>'"); in->pos += 2; empty = 1; break; }
        if (!sp) return fail(p, OXP_E_SYNTAX, "a space is needed before an attribute");
        size_t ab, ae;
        CHECK(read_name(p, in, &ab, &ae));
        skip_s(in);
        NEED(1);
        if (S(in)[in->pos] != '=') return fail(p, OXP_E_SYNTAX, "an attribute needs '=' and a value");
        in->pos++;
        skip_s(in);
        NEED(1);
        char qch = S(in)[in->pos];
        if (qch != '"' && qch != '\'') return fail(p, OXP_E_SYNTAX, "an attribute value must be quoted");
        size_t vb = in->pos + 1, ve = vb;
        while (ve < in->len && S(in)[ve] != qch) ve++;
        if (ve >= in->len) { if (ve - vb > p->lim.max_text) return fail(p, OXP_E_LIMIT, "an attribute value is too long"); NEED(ve - in->pos + 1); }
        if (p->nra >= p->lim.max_attrs) return fail(p, OXP_E_LIMIT, "too many attributes");
        if (!grow(p, (void **)&p->ra, &p->cap_ra, p->nra + 1, sizeof(rawattr))) return R_ERR;
        rawattr *r = &p->ra[p->nra];
        memset(r, 0, sizeof *r);
        r->name = sb_put(p, &p->scratch, S(in) + ab, ae - ab);
        r->name_len = ae - ab;
        if (r->name == NOPOS) return R_ERR;
        r->raw_b = vb; r->raw_e = ve;
        in->pos = ve + 1;
        p->nra++;
    }
    /* the tag is complete: normalise the values (entity events may happen now) */
    for (size_t i = 0; i < p->nra; i++) {
        rawattr *r = &p->ra[i];
        r->value = p->scratch.n;
        if (attval(p, &p->scratch, S(in) + r->raw_b, r->raw_e - r->raw_b, 0)) return R_ERR;
        r->value_len = p->scratch.n - r->value;
        if (!sb_addc(p, &p->scratch, 0)) return R_ERR;
    }

    /* the tag is complete: from here on nothing asks for more input */
    const char *q = S(in) + qb;
    size_t qn = qe - qb;
    attlist *al = p->atts ? find_attlist(p, q, qn) : NULL;
    size_t nspec = p->nra;
    /* Unique Att Spec, on the names as written */
    if (nspec > 1) {
        if (nspec <= 64) {
            for (size_t i = 0; i < nspec; i++)
                for (size_t j = i + 1; j < nspec; j++)
                    if (p->ra[i].name_len == p->ra[j].name_len && !strcmp(p->scratch.p + p->ra[i].name, p->scratch.p + p->ra[j].name))
                        return failn(p, OXP_E_DUP_ATTR, "an attribute given twice:", p->scratch.p + p->ra[i].name);
        } else {
            size_t hn = 1;
            while (hn < nspec * 2) hn <<= 1;
            size_t *h = xalloc(p, hn * sizeof(size_t));
            if (!h) return fail(p, OXP_E_NOMEM, "out of memory");
            for (size_t i = 0; i < hn; i++) h[i] = NOPOS;
            for (size_t i = 0; i < nspec; i++) {
                const char *nm = p->scratch.p + p->ra[i].name;
                unsigned long hv = 5381;
                for (const char *t = nm; *t; t++) hv = hv * 33 + (u8)*t;
                size_t k = hv & (hn - 1);
                while (h[k] != NOPOS) {
                    if (!strcmp(p->scratch.p + p->ra[h[k]].name, nm)) { xfree(p, h); return failn(p, OXP_E_DUP_ATTR, "an attribute given twice:", nm); }
                    k = (k + 1) & (hn - 1);
                }
                h[k] = i;
            }
            xfree(p, h);
        }
    }
    /* types and defaults from the internal subset */
    if (al) {
        for (attdef *d = al->defs; d; d = d->next) {
            size_t i;
            for (i = 0; i < nspec; i++) if (!strcmp(p->scratch.p + p->ra[i].name, d->name)) break;
            if (i < nspec) {
                if (d->type == AT_TOKENIZED) p->ra[i].value_len = collapse(p->scratch.p + p->ra[i].value, p->ra[i].value_len);
                continue;
            }
            if (d->dflt != AD_DEFAULT && d->dflt != AD_FIXED) continue;
            if (!grow(p, (void **)&p->ra, &p->cap_ra, p->nra + 1, sizeof(rawattr))) return R_ERR;
            rawattr *r = &p->ra[p->nra];
            memset(r, 0, sizeof *r);
            r->name = sb_put(p, &p->scratch, d->name, strlen(d->name));
            r->name_len = strlen(d->name);
            r->value = sb_put(p, &p->scratch, d->value, d->value_len);
            if (r->name == NOPOS || r->value == NOPOS) return R_ERR;
            r->value_len = d->value_len;
            r->defaulted = 1;
            p->nra++;
        }
    }

    size_t nbind0 = p->nbinds, colon = NOPOS;
    const char *uri = NULL;
    int nsmode = !(p->options & OXP_OPT_NO_NAMESPACES);
    if (nsmode) {
        if (qsplit(p, q, qn, &colon)) return R_ERR;
        /* the namespace declarations */
        for (size_t i = 0; i < p->nra; i++) {
            rawattr *r = &p->ra[i];
            const char *nm = p->scratch.p + r->name;
            const char *v = p->scratch.p + r->value;
            if (r->name_len == 5 && !memcmp(nm, "xmlns", 5)) {
                if (!strcmp(v, OXP_XML_NS) || !strcmp(v, OXP_XMLNS_NS)) return fail(p, OXP_E_NS_PREFIX, "the default namespace cannot be a reserved one");
                r->is_ns = 1;
                if (bind(p, NULL, 0, v, r->value_len)) return R_ERR;
            } else if (r->name_len > 6 && !memcmp(nm, "xmlns:", 6)) {
                const char *px = nm + 6;
                size_t pn = r->name_len - 6, cc;
                if (qsplit(p, nm, r->name_len, &cc)) return R_ERR;
                r->is_ns = 1;
                if (!strcmp(px, "xmlns")) return fail(p, OXP_E_NS_PREFIX, "the prefix xmlns cannot be declared");
                if (!strcmp(px, "xml")) {
                    if (strcmp(v, OXP_XML_NS)) return fail(p, OXP_E_NS_PREFIX, "the prefix xml can only be bound to its own namespace");
                    continue;   /* allowed, and bound already */
                }
                if (!strcmp(v, OXP_XML_NS) || !strcmp(v, OXP_XMLNS_NS)) return fail(p, OXP_E_NS_PREFIX, "a reserved namespace bound to another prefix");
                if (!r->value_len) return failn(p, OXP_E_NS_PREFIX, "a prefix cannot be undeclared in XML 1.0:", px);
                if (bind(p, px, pn, v, r->value_len)) return R_ERR;
            }
        }
        if (colon != NOPOS) {
            if (colon == 5 && !memcmp(q, "xmlns", 5)) return fail(p, OXP_E_NS_PREFIX, "an element cannot have the prefix xmlns");
            uri = ns_lookup(p, q, colon);
            if (!uri && unbound(p, q, colon)) return R_ERR;
        } else {
            uri = ns_lookup(p, NULL, 0);
            if (uri && !*uri) uri = NULL;
        }
        /* the attributes' names */
        for (size_t i = 0; i < p->nra; i++) {
            rawattr *r = &p->ra[i];
            if (r->is_ns) continue;
            const char *nm = p->scratch.p + r->name;
            size_t cc;
            if (qsplit(p, nm, r->name_len, &cc)) return R_ERR;
            if (cc == NOPOS) { r->prefix = NOPOS; r->local = r->name; r->uri = NULL; continue; }
            r->prefix = r->name; r->local = r->name + cc + 1;
            r->uri = ns_lookup(p, nm, cc);
            if (!r->uri && unbound(p, nm, cc)) return R_ERR;
        }
        /* NSC: Attributes Unique, on the expanded names */
        for (size_t i = 0; i < p->nra; i++) {
            rawattr *a = &p->ra[i];
            if (a->is_ns || !a->uri) continue;
            size_t al_ = strlen(p->scratch.p + a->local);
            for (size_t j = i + 1; j < p->nra; j++) {
                rawattr *b = &p->ra[j];
                if (b->is_ns || !b->uri || (b->uri != a->uri && strcmp(b->uri, a->uri))) continue;
                if (!strcmp(p->scratch.p + b->local, p->scratch.p + a->local))
                    return failn(p, OXP_E_DUP_ATTR, "an attribute given twice (after namespace expansion):", p->scratch.p + a->local);
            }
            (void)al_;
        }
    }

    size_t nb = p->nbinds - nbind0;
    if (p->nels == 0 && in == &p->din) {
        if (p->state == ST_EPILOG) return fail(p, OXP_E_SYNTAX, "a second root element");
        if (!(p->options & OXP_OPT_FRAGMENT)) { p->seen_root = 1; p->state = ST_CONTENT; }
    }
    if (push_el(p, q, qn, colon, uri, nb)) return R_ERR;

    if (p->h.start_element) {
        /* the arrays, now that no buffer will move */
        size_t na = 0, nn = 0;
        if (!grow(p, (void **)&p->oa, &p->cap_oa, p->nra + 1, sizeof(oxp_attr))) return R_ERR;
        if (!grow(p, (void **)&p->ons, &p->cap_ons, nb + 1, sizeof(oxp_ns))) return R_ERR;
        for (size_t i = 0; i < nb; i++) {
            binding *b = &p->binds[nbind0 + i];
            p->ons[nn].prefix = b->prefix == NOPOS ? NULL : p->nsbuf.p + b->prefix;
            p->ons[nn].uri = p->nsbuf.p + b->uri;
            nn++;
        }
        for (size_t i = 0; i < p->nra; i++) {
            rawattr *r = &p->ra[i];
            if (r->is_ns && nsmode) continue;
            oxp_attr *o = &p->oa[na++];
            if (nsmode && r->prefix != NOPOS) {
                /* the prefix as its own string: write a NUL over the colon in the copy */
                p->scratch.p[r->local - 1] = 0;
                o->prefix = p->scratch.p + r->prefix;
                o->local = p->scratch.p + r->local;
            } else { o->prefix = NULL; o->local = p->scratch.p + r->name; }
            o->uri = nsmode ? r->uri : NULL;
            o->value = p->scratch.p + r->value;
            o->value_len = r->value_len;
            o->defaulted = r->defaulted;
        }
        const char *st = p->stack.p;
        openel *e = &p->els[p->nels - 1];
        EVENT(p->h.start_element(p->ud, st + e->local, e->prefix == NOPOS ? NULL : st + e->prefix,
                                 e->uri == NOPOS ? NULL : st + e->uri, (int)nn, p->ons, (int)na, p->oa));
    }
    if (empty) {
        if (end_el(p)) return R_ERR;
        if (p->nels == 0 && in == &p->din && !(p->options & OXP_OPT_FRAGMENT)) p->state = ST_EPILOG;
    }
    return R_OK;
}

/* an end tag, at "</" */
static int end_tag(oxp_parser *p, input *in)
{
    size_t start = in->pos, qb, qe;
    in->pos += 2;
    CHECK(read_name(p, in, &qb, &qe));
    skip_s(in);
    NEED(1);
    if (S(in)[in->pos] != '>') return fail(p, OXP_E_SYNTAX, "an end tag must end with '>'");
    in->pos++;
    if (!p->nels) return fail(p, OXP_E_TAG_MISMATCH, "an end tag with no start tag");
    if (in->ent && p->nels <= in->depth_at_start) return fail(p, OXP_E_SYNTAX, "an entity closes an element it did not open");
    openel *e = &p->els[p->nels - 1];
    const char *q = p->stack.p + e->qname;
    if (strlen(q) != qe - qb || memcmp(q, S(in) + qb, qe - qb)) return failn(p, OXP_E_TAG_MISMATCH, "the end tag does not match the start tag", q);
    if (end_el(p)) return R_ERR;
    if (p->nels == 0 && in == &p->din && !(p->options & OXP_OPT_FRAGMENT)) p->state = ST_EPILOG;
    return R_OK;
}

/* ---------------------------------------------------------------- content */

static int content_unit(oxp_parser *p, input *in);

static int emit_text(oxp_parser *p, const char *s, size_t n)
{
    if (n && p->h.characters) EVENT(p->h.characters(p->ud, s, n));
    return R_OK;
}

/* a general entity reference in content: expand it */
static int content_entity(oxp_parser *p, input *in, const char *nm, size_t nl)
{
    const char *pre = predefined(nm, nl);
    if (pre) return emit_text(p, pre, 1);
    entity *en = find_ent(p->gents, nm, nl);
    entity tmp;
    if (!en) {
        char nb[64];
        size_t m = nl < sizeof nb - 1 ? nl : sizeof nb - 1;
        memcpy(nb, nm, m); nb[m] = 0;
        const char *text = p->h.resolve_entity ? p->h.resolve_entity(p->ud, nb) : NULL;
        if (!text) {
            if (wfc_declared(p)) return failn(p, OXP_E_ENTITY_UNDECLARED, "an undeclared entity:", nb);
            if (p->h.skipped_entity) EVENT(p->h.skipped_entity(p->ud, nb));
            return R_OK;
        }
        memset(&tmp, 0, sizeof tmp);
        tmp.name = nb; tmp.value = (char *)text; tmp.value_len = strlen(text);
        tmp.type = OXP_ENTITY_INTERNAL_GENERAL;
        en = &tmp;
        /* a resolved text may itself refer to entities: it nests like any other */
    }
    if (en->type == OXP_ENTITY_EXTERNAL_GENERAL_PARSED) {
        if (p->h.skipped_entity) EVENT(p->h.skipped_entity(p->ud, en->name));
        return R_OK;
    }
    if (en->type != OXP_ENTITY_INTERNAL_GENERAL) return failn(p, OXP_E_ENTITY_UNPARSED, "a reference to an unparsed entity:", en->name);
    if (en->open) return failn(p, OXP_E_ENTITY_RECURSION, "an entity refers to itself:", en->name);
    if ((ulong)p->nin >= p->lim.max_entity_depth || p->nin >= (int)(sizeof p->stackin / sizeof p->stackin[0]))
        return fail(p, OXP_E_ENTITY_LIMIT, "entities nest too deep");
    if (budget(p, en->value_len)) return R_ERR;
    input *e = &p->stackin[p->nin++];
    e->s = en->value; e->len = en->value_len; e->pos = 0; e->final = 1; e->ent = en; e->dtd = 0;
    e->depth_at_start = p->nels;
    en->open = 1;
    int r = R_OK;
    while (e->pos < e->len && !r) r = content_unit(p, e);
    en->open = 0;
    p->nin--;
    if (r) return r == R_MORE ? fail(p, OXP_E_SYNTAX, "an entity ends inside markup") : r;
    if (p->nels != e->depth_at_start) return failn(p, OXP_E_SYNTAX, "an entity leaves an element open:", en->name);
    (void)in;
    return R_OK;
}

/* a reference in content, at '&' */
static int reference(oxp_parser *p, input *in)
{
    size_t start = in->pos, i = in->pos + 1;
    if (in->pos + 1 >= in->len) NEED(2);
    if (S(in)[i] == '#') {
        size_t k = i + 1;
        if (k < in->len && S(in)[k] == 'x') k++;
        while (k < in->len && ((S(in)[k] >= '0' && S(in)[k] <= '9') || (S(in)[k] >= 'a' && S(in)[k] <= 'f') || (S(in)[k] >= 'A' && S(in)[k] <= 'F'))) {
            k++;
            if (k - start > p->lim.max_name) return fail(p, OXP_E_LIMIT, "a character reference is too long");
        }
        if (k >= in->len) NEED(k - start + 1);
        if (S(in)[k] != ';') return fail(p, OXP_E_SYNTAX, "a malformed character reference");
        unsigned long c = charref(S(in) + start, k + 1 - start);
        if (!c) return fail(p, OXP_E_CHAR, "a character reference to a character XML does not allow");
        in->pos = k + 1;
        char o[4];
        return emit_text(p, o, utf8_put(o, c));
    }
    size_t nb, ne;
    in->pos = i;
    CHECK(read_name(p, in, &nb, &ne));
    NEED(1);
    if (S(in)[in->pos] != ';') return fail(p, OXP_E_SYNTAX, "an entity reference must end with ';'");
    in->pos++;
    return content_entity(p, in, S(in) + nb, ne - nb);
}

/* a run of character data */
static int text(oxp_parser *p, input *in)
{
    size_t i = in->pos, n = in->len;
    const char *s = S(in);
    while (i < n && s[i] != '<' && s[i] != '&') {
        if (s[i] == ']') {
            if (i + 2 >= n && !in->final) break;           /* might be "]]>" */
            if (i + 2 < n && s[i + 1] == ']' && s[i + 2] == '>') {
                in->pos = i;
                return fail(p, OXP_E_SYNTAX, "']]>' in character data");
            }
        }
        i++;
    }
    if (i == in->pos) return R_MORE;
    size_t b = in->pos;
    in->pos = i;
    if (p->nels == 0 && !(p->options & OXP_OPT_FRAGMENT)) {
        for (size_t k = b; k < i; k++)
            if (!is_space((u8)s[k])) { in->pos = k; return fail(p, OXP_E_SYNTAX, "text outside the root element"); }
        return R_OK;
    }
    return emit_text(p, s + b, i - b);
}

/* <!-- ... --> */
static int comment(oxp_parser *p, input *in, int report_it)
{
    size_t start = in->pos;
    size_t e = find(p, in, start, start + 4, "--");
    if (e == NOPOS) {
        if (in->len - start > p->lim.max_text) return fail(p, OXP_E_LIMIT, "a comment is too long");
        NEED(in->len - start + 3);
        return R_MORE;
    }
    if (e + 2 >= in->len) NEED(e + 3 - start);
    if (S(in)[e + 2] != '>') { in->pos = e; return fail(p, OXP_E_SYNTAX, "'--' inside a comment"); }
    in->pos = e + 3;
    if (report_it && p->h.comment) {
        p->tmp.n = 0;
        if (!sb_add(p, &p->tmp, S(in) + start + 4, e - start - 4)) return R_ERR;
        EVENT(p->h.comment(p->ud, p->tmp.p ? p->tmp.p : ""));
    }
    return R_OK;
}

/* <? target data ?> */
static int pi(oxp_parser *p, input *in, int report_it)
{
    size_t start = in->pos, nb, ne;
    in->pos += 2;
    CHECK(read_name(p, in, &nb, &ne));
    if (ne - nb == 3 && (S(in)[nb] | 32) == 'x' && (S(in)[nb + 1] | 32) == 'm' && (S(in)[nb + 2] | 32) == 'l')
        return fail(p, OXP_E_SYNTAX, "the target 'xml' is reserved (an XML declaration must come first)");
    if (!(p->options & OXP_OPT_NO_NAMESPACES) && memchr(S(in) + nb, ':', ne - nb))
        return fail(p, OXP_E_NS_PREFIX, "a processing instruction's target cannot hold a colon");
    size_t db = in->pos;
    size_t sp = skip_s(in);
    if (in->pos >= in->len) NEED(in->pos - start + 2);
    if (!sp && !at(in, "?>")) { NEED(2); return fail(p, OXP_E_SYNTAX, "a space is needed after a processing instruction's target"); }
    db = in->pos;
    size_t e = find(p, in, start, db, "?>");
    if (e == NOPOS) {
        if (in->len - start > p->lim.max_text) return fail(p, OXP_E_LIMIT, "a processing instruction is too long");
        NEED(in->len - start + 2);
        return R_MORE;
    }
    in->pos = e + 2;
    if (report_it && p->h.pi) {
        p->tmp.n = 0; p->tmp2.n = 0;
        if (!sb_add(p, &p->tmp, S(in) + nb, ne - nb) || !sb_add(p, &p->tmp2, S(in) + db, e - db)) return R_ERR;
        EVENT(p->h.pi(p->ud, p->tmp.p, p->tmp2.p ? p->tmp2.p : ""));
    }
    return R_OK;
}

/* <![CDATA[ ... ]]> */
static int cdata(oxp_parser *p, input *in)
{
    size_t start = in->pos;
    size_t e = find(p, in, start, start + 9, "]]>");
    if (e == NOPOS) {
        if (in->len - start > p->lim.max_text) return fail(p, OXP_E_LIMIT, "a CDATA section is too long");
        NEED(in->len - start + 3);
        return R_MORE;
    }
    in->pos = e + 3;
    if (p->h.cdata) EVENT(p->h.cdata(p->ud, S(in) + start + 9, e - start - 9));
    else if (p->h.characters && e > start + 9) EVENT(p->h.characters(p->ud, S(in) + start + 9, e - start - 9));
    return R_OK;
}

/* one unit of content: a tag, a reference, a comment, a PI, a CDATA section or text */
static int content_unit(oxp_parser *p, input *in)
{
    size_t start = in->pos;
    NEED(1);
    char c = S(in)[in->pos];
    if (c == '&') {
        if (p->nels == 0 && !(p->options & OXP_OPT_FRAGMENT)) return fail(p, OXP_E_SYNTAX, "a reference outside the root element");
        return reference(p, in);
    }
    if (c != '<') return text(p, in);
    NEED(2);
    c = S(in)[in->pos + 1];
    if (c == '/') return end_tag(p, in);
    if (c == '?') return pi(p, in, 1);
    if (c == '!') {
        if (maybe_at(in, "<!--")) { NEED(4); return comment(p, in, 1); }
        if (maybe_at(in, "<![CDATA[")) {
            NEED(9);
            if (p->nels == 0 && !(p->options & OXP_OPT_FRAGMENT)) return fail(p, OXP_E_SYNTAX, "a CDATA section outside the root element");
            return cdata(p, in);
        }
        return fail(p, OXP_E_SYNTAX, "'<!' that is not a comment or a CDATA section");
    }
    return start_tag(p, in);
}

/* ---------------------------------------------------------------- the prolog */

/* a quoted literal at in->pos: *b, *e its contents */
static int literal(oxp_parser *p, input *in, size_t start, size_t *b, size_t *e)
{
    NEED(1);
    char q = S(in)[in->pos];
    if (q != '"' && q != '\'') return fail(p, OXP_E_SYNTAX, "a quoted string was expected");
    size_t i = in->pos + 1;
    while (i < in->len && S(in)[i] != q) i++;
    if (i >= in->len) {
        if (in->final && !in->ent) return fail(p, OXP_E_SYNTAX, "a quoted string is not closed");
        if (i - in->pos > p->lim.max_text) return fail(p, OXP_E_LIMIT, "a quoted string is too long");
        NEED(i - in->pos + 1);
    }
    *b = in->pos + 1; *e = i;
    in->pos = i + 1;
    return R_OK;
}

/* the XML declaration (2.8), at the start of the document */
static int xml_decl(oxp_parser *p, input *in)
{
    size_t start = in->pos;
    WANT(6);
    if (!at(in, "<?xml") || AV(in) < 6 || !is_space((u8)S(in)[in->pos + 5])) {
        if (!release_decl(p, NULL)) return R_ERR;
        p->state = ST_PROLOG;
        if (p->h.start_document) EVENT(p->h.start_document(p->ud, "1.0", NULL, -1));
        return R_OK;
    }
    /* the declaration is short: wait for all of it */
    size_t e = find(p, in, start, start + 5, "?>");
    if (e == NOPOS) {
        /* while the declaration is held, the text stops at its first '>': no "?>" there means a bad declaration */
        if ((p->hold_decl && in->len > start && S(in)[in->len - 1] == '>') || in->len - start > 1000)
            return fail(p, OXP_E_SYNTAX, "the XML declaration must end with '?>'");
        NEED(in->len - start + 2);
        return R_MORE;
    }
    /* read it within its own bounds: a bad quote must not run past "?>" */
    input bounded = *in;
    bounded.len = e + 2;
    bounded.final = 1;
    in = &bounded;
    in->pos += 5;
    char ver[16] = "", encn[64] = "", sa[8] = "";
    int seen = 0;
    for (;;) {
        size_t sp = skip_s(in);
        if (at(in, "?>")) { in->pos += 2; break; }
        if (!sp) return fail(p, OXP_E_SYNTAX, "a space is needed in the XML declaration");
        size_t nb, ne, vb, ve;
        CHECK(read_name(p, in, &nb, &ne));
        skip_s(in);
        if (in->pos >= in->len || S(in)[in->pos] != '=') return fail(p, OXP_E_SYNTAX, "'=' expected in the XML declaration");
        in->pos++;
        skip_s(in);
        CHECK(literal(p, in, start, &vb, &ve));
        const char *nm = S(in) + nb, *v = S(in) + vb;
        size_t nl = ne - nb, vl = ve - vb;
        if (nl == 7 && !memcmp(nm, "version", 7) && seen == 0) {
            if (vl < 3 || vl >= sizeof ver || v[0] != '1' || v[1] != '.') return fail(p, OXP_E_SYNTAX, "a version number must be 1.x");
            for (size_t k = 2; k < vl; k++) if (v[k] < '0' || v[k] > '9') return fail(p, OXP_E_SYNTAX, "a version number must be 1.x");
            memcpy(ver, v, vl); ver[vl] = 0;
            seen = 1;
        } else if (nl == 8 && !memcmp(nm, "encoding", 8) && seen == 1) {
            if (!vl || vl >= sizeof encn) return fail(p, OXP_E_SYNTAX, "a bad encoding name");
            int ok = (v[0] | 32) >= 'a' && (v[0] | 32) <= 'z';
            for (size_t k = 1; k < vl && ok; k++) {
                char c = v[k];
                ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
            }
            if (!ok) return fail(p, OXP_E_SYNTAX, "a bad encoding name");
            memcpy(encn, v, vl); encn[vl] = 0;
            seen = 2;
        } else if (nl == 10 && !memcmp(nm, "standalone", 10) && seen >= 1 && seen < 3) {
            if (vl == 3 && !memcmp(v, "yes", 3)) p->standalone = 1;
            else if (vl == 2 && !memcmp(v, "no", 2)) p->standalone = 0;
            else return fail(p, OXP_E_SYNTAX, "standalone must be 'yes' or 'no'");
            memcpy(sa, v, vl); sa[vl] = 0;
            seen = 3;
        } else return fail(p, OXP_E_SYNTAX, "a bad XML declaration (version first, then encoding, then standalone)");
    }
    if (!seen) return fail(p, OXP_E_SYNTAX, "the XML declaration has no version");
    if (in->pos != in->len) return fail(p, OXP_E_SYNTAX, "the XML declaration must end with '?>'");
    p->din.pos = in->pos;
    p->seen_decl = 1;
    p->state = ST_PROLOG;
    if (!release_decl(p, encn[0] ? encn : NULL)) return R_ERR;
    if (p->h.start_document) EVENT(p->h.start_document(p->ud, ver, encn[0] ? encn : NULL, p->standalone));
    return R_OK;
}

/* PubidChar (2.3) */
static int pubid_ok(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        u8 c = (u8)s[i];
        if (!(c == 0x20 || c == 0xD || c == 0xA || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              strchr("-'()+,./:=?;!*#@$_%", c))) return 0;
    }
    return 1;
}

/* ExternalID, or with notation set, PublicID alone. *pb.. are the literals' contents (NOPOS when absent).
 * Returns R_OK with *got 0 when no external ID is here. */
static int external_id(oxp_parser *p, input *in, size_t start, int notation, size_t *pb, size_t *pe, size_t *sb, size_t *se, int *got)
{
    *pb = *pe = *sb = *se = NOPOS;
    *got = 0;
    WANT(6);
    if (at(in, "SYSTEM")) {
        in->pos += 6;
        if (!skip_s(in)) { NEED(1); return fail(p, OXP_E_SYNTAX, "a space is needed after SYSTEM"); }
        CHECK(literal(p, in, start, sb, se));
        *got = 1;
        return R_OK;
    }
    if (at(in, "PUBLIC")) {
        in->pos += 6;
        if (!skip_s(in)) { NEED(1); return fail(p, OXP_E_SYNTAX, "a space is needed after PUBLIC"); }
        CHECK(literal(p, in, start, pb, pe));
        if (!pubid_ok(S(in) + *pb, *pe - *pb)) return fail(p, OXP_E_SYNTAX, "a character a public identifier cannot hold");
        size_t sp = skip_s(in);
        NEED(1);
        char c = S(in)[in->pos];
        if (c == '"' || c == '\'') {
            if (!sp) return fail(p, OXP_E_SYNTAX, "a space is needed before the system literal");
            CHECK(literal(p, in, start, sb, se));
        } else if (!notation) return fail(p, OXP_E_SYNTAX, "PUBLIC needs a system literal too");
        *got = 1;
        return R_OK;
    }
    return R_OK;
}

static char *dupslice(oxp_parser *p, input *in, size_t b, size_t e)
{
    return b == NOPOS ? NULL : xstrndup(p, S(in) + b, e - b);
}

/* <!DOCTYPE ... [ or > */
static int doctype(oxp_parser *p, input *in)
{
    size_t start = in->pos, nb, ne, pb, pe, sb, se;
    int got;
    in->pos += 9;
    if (!skip_s(in)) { NEED(1); return fail(p, OXP_E_SYNTAX, "a space is needed after DOCTYPE"); }
    CHECK(read_name(p, in, &nb, &ne));
    size_t sp = skip_s(in);
    NEED(1);
    char c = S(in)[in->pos];
    if (c != '[' && c != '>') {
        if (!sp) return fail(p, OXP_E_SYNTAX, "a space is needed before the external ID");
        CHECK(external_id(p, in, start, 0, &pb, &pe, &sb, &se, &got));
        if (!got) return fail(p, OXP_E_SYNTAX, "SYSTEM or PUBLIC was expected");
        skip_s(in);
        NEED(1);
        c = S(in)[in->pos];
    } else pb = pe = sb = se = NOPOS;
    if (c != '[' && c != '>') return fail(p, OXP_E_SYNTAX, "'[' or '>' was expected in the DOCTYPE");
    in->pos++;
    p->seen_doctype = 1;
    p->doc_has_external = sb != NOPOS;
    p->state = c == '[' ? ST_DTD : ST_PROLOG;
    if (p->h.doctype) {
        p->tmp.n = 0;
        size_t o1 = sb_put(p, &p->tmp, S(in) + nb, ne - nb);
        size_t o2 = pb == NOPOS ? NOPOS : sb_put(p, &p->tmp, S(in) + pb, pe - pb);
        size_t o3 = sb == NOPOS ? NOPOS : sb_put(p, &p->tmp, S(in) + sb, se - sb);
        if (o1 == NOPOS) return R_ERR;
        EVENT(p->h.doctype(p->ud, p->tmp.p + o1, o2 == NOPOS ? NULL : p->tmp.p + o2, o3 == NOPOS ? NULL : p->tmp.p + o3, c == '['));
    }
    if (c == '>' && p->h.end_doctype) EVENT(p->h.end_doctype(p->ud));
    return R_OK;
}

/* ---------------------------------------------------------------- the internal subset */

/* processing the declarations is allowed (5.1: not after an unread parameter entity, unless standalone) */
static int decls_on(oxp_parser *p) { return !p->pe_unread || p->standalone == 1; }

/* the content spec of an element type (3.2): cp, choice and seq, and Mixed */
static int cspec(oxp_parser *p, input *in, size_t start, int depth)
{
    if (depth > 64) return fail(p, OXP_E_LIMIT, "a content model nests too deep");
    /* at '(' */
    in->pos++;
    skip_s(in);
    NEED(1);
    if (depth == 0 && maybe_at(in, "#PCDATA")) {
        NEED(7);
        in->pos += 7;
        int names = 0;
        for (;;) {
            skip_s(in);
            NEED(1);
            char c = S(in)[in->pos];
            if (c == ')') {
                in->pos++;
                if (names) { NEED(1); if (S(in)[in->pos] != '*') return fail(p, OXP_E_SYNTAX, "mixed content with names must end ')*'"); in->pos++; }
                else if (in->pos < in->len && S(in)[in->pos] == '*') in->pos++;
                return R_OK;
            }
            if (c != '|') return fail(p, OXP_E_SYNTAX, "'|' or ')' expected in mixed content");
            in->pos++;
            skip_s(in);
            size_t b, e;
            CHECK(read_name(p, in, &b, &e));
            names++;
        }
    }
    char sep = 0;
    for (;;) {
        NEED(1);
        if (S(in)[in->pos] == '(') CHECK(cspec(p, in, start, depth + 1));
        else { size_t b, e; CHECK(read_name(p, in, &b, &e)); }
        NEED(1);
        char m = S(in)[in->pos];
        if (m == '?' || m == '*' || m == '+') in->pos++;
        skip_s(in);
        NEED(1);
        char c = S(in)[in->pos];
        if (c == ')') {
            in->pos++;
            if (in->pos < in->len) { m = S(in)[in->pos]; if (m == '?' || m == '*' || m == '+') in->pos++; }
            else NEED(1);
            return R_OK;
        }
        if (c != '|' && c != ',') return fail(p, OXP_E_SYNTAX, "'|', ',' or ')' expected in a content model");
        if (sep && c != sep) return fail(p, OXP_E_SYNTAX, "'|' and ',' mixed in one group");
        sep = c;
        in->pos++;
        skip_s(in);
    }
}

static int decl_element(oxp_parser *p, input *in)
{
    size_t start = in->pos, b, e;
    in->pos += 9;
    if (!skip_s(in)) { NEED(1); return fail(p, OXP_E_SYNTAX, "a space is needed after ELEMENT"); }
    CHECK(read_name(p, in, &b, &e));
    if (!skip_s(in)) { NEED(1); return fail(p, OXP_E_SYNTAX, "a space is needed after the element name"); }
    WANT(5);
    if (at(in, "EMPTY")) in->pos += 5;
    else if (at(in, "ANY")) in->pos += 3;
    else if (S(in)[in->pos] == '(') CHECK(cspec(p, in, start, 0));
    else return fail(p, OXP_E_SYNTAX, "EMPTY, ANY or a content model was expected");
    skip_s(in);
    NEED(1);
    if (S(in)[in->pos] != '>') return fail(p, OXP_E_SYNTAX, "'>' expected at the end of an element declaration");
    in->pos++;
    return R_OK;
}

static int decl_attlist(oxp_parser *p, input *in)
{
    size_t start = in->pos, eb, ee;
    in->pos += 9;
    if (!skip_s(in)) { NEED(1); return fail(p, OXP_E_SYNTAX, "a space is needed after ATTLIST"); }
    CHECK(read_name(p, in, &eb, &ee));
    /* first read the whole declaration; then record it */
    struct { size_t nb, ne; int type, dflt; size_t vb, ve; } defs[64];
    int nd = 0;
    attdef *extra = NULL;
    (void)extra;
    for (;;) {
        size_t sp = skip_s(in);
        NEED(1);
        if (S(in)[in->pos] == '>') { in->pos++; break; }
        if (!sp) return fail(p, OXP_E_SYNTAX, "a space is needed before an attribute definition");
        size_t nb, ne;
        CHECK(read_name(p, in, &nb, &ne));
        if (!skip_s(in)) { NEED(1); return fail(p, OXP_E_SYNTAX, "a space is needed after the attribute name"); }
        WANT(8);
        int type = AT_TOKENIZED;
        static const char *const kw[] = { "CDATA", "IDREFS", "IDREF", "ID", "ENTITIES", "ENTITY", "NMTOKENS", "NMTOKEN", NULL };
        int k;
        for (k = 0; kw[k]; k++) if (at(in, kw[k])) break;
        if (kw[k]) {
            in->pos += strlen(kw[k]);
            if (k == 0) type = AT_CDATA;
        } else if (at(in, "NOTATION") || S(in)[in->pos] == '(') {
            int notation = at(in, "NOTATION");
            if (notation) {
                in->pos += 8;
                if (!skip_s(in)) { NEED(1); return fail(p, OXP_E_SYNTAX, "a space is needed after NOTATION"); }
                NEED(1);
                if (S(in)[in->pos] != '(') return fail(p, OXP_E_SYNTAX, "'(' expected after NOTATION");
            }
            in->pos++;
            for (;;) {
                skip_s(in);
                if (notation) { size_t b, e; CHECK(read_name(p, in, &b, &e)); }
                else CHECK(read_nmtoken(p, in));
                skip_s(in);
                NEED(1);
                char c = S(in)[in->pos++];
                if (c == ')') break;
                if (c != '|') return fail(p, OXP_E_SYNTAX, "'|' or ')' expected in an enumeration");
            }
        } else return fail(p, OXP_E_SYNTAX, "an attribute type was expected");
        if (!skip_s(in)) { NEED(1); return fail(p, OXP_E_SYNTAX, "a space is needed after the attribute type"); }
        WANT(9);
        int dflt = AD_DEFAULT;
        size_t vb = NOPOS, ve = NOPOS;
        if (at(in, "#REQUIRED")) { in->pos += 9; dflt = AD_REQUIRED; }
        else if (at(in, "#IMPLIED")) { in->pos += 8; dflt = AD_IMPLIED; }
        else {
            if (at(in, "#FIXED")) {
                in->pos += 6; dflt = AD_FIXED;
                if (!skip_s(in)) { NEED(1); return fail(p, OXP_E_SYNTAX, "a space is needed after #FIXED"); }
            }
            CHECK(literal(p, in, start, &vb, &ve));
        }
        if (nd < 64) {
            defs[nd].nb = nb; defs[nd].ne = ne; defs[nd].type = type; defs[nd].dflt = dflt; defs[nd].vb = vb; defs[nd].ve = ve;
            nd++;
        }
    }
    /* complete: check the defaults and record them */
    attlist *al = find_attlist(p, S(in) + eb, ee - eb);
    for (int i = 0; i < nd; i++) {
        p->tmp.n = 0;
        if (defs[i].vb != NOPOS) {
            if (attval(p, &p->tmp, S(in) + defs[i].vb, defs[i].ve - defs[i].vb, 0)) return R_ERR;
            if (!sb_addc(p, &p->tmp, 0)) return R_ERR;
            p->tmp.n--;
        }
        if (!decls_on(p)) continue;
        if (!al) {
            al = xalloc(p, sizeof *al);
            if (!al) return fail(p, OXP_E_NOMEM, "out of memory");
            al->element = xstrndup(p, S(in) + eb, ee - eb);
            al->defs = NULL;
            if (!al->element) { xfree(p, al); return R_ERR; }
            al->next = p->atts; p->atts = al;
        }
        size_t nl = defs[i].ne - defs[i].nb;
        attdef *d, **tail = &al->defs;
        for (d = al->defs; d; d = d->next) {
            if (!strncmp(d->name, S(in) + defs[i].nb, nl) && !d->name[nl]) break;
            tail = &d->next;
        }
        if (d) continue;                    /* the first declaration binds */
        d = xalloc(p, sizeof *d);
        if (!d) return fail(p, OXP_E_NOMEM, "out of memory");
        memset(d, 0, sizeof *d);
        d->name = xstrndup(p, S(in) + defs[i].nb, nl);
        d->type = defs[i].type;
        d->dflt = defs[i].dflt;
        if (defs[i].vb != NOPOS) {
            size_t vl = p->tmp.n;
            if (d->type == AT_TOKENIZED) vl = collapse(p->tmp.p, vl);
            d->value = xstrndup(p, p->tmp.p ? p->tmp.p : "", vl);
            d->value_len = vl;
        }
        *tail = d;
        if (!d->name || (defs[i].vb != NOPOS && !d->value)) return R_ERR;
    }
    return R_OK;
}

/* an EntityValue (2.3) into p->tmp: character references expanded, general
 * entity references kept (4.5) */
static int entity_value(oxp_parser *p, input *in, size_t start, int pe)
{
    size_t b, e;
    CHECK(literal(p, in, start, &b, &e));
    const char *s = S(in);
    p->tmp.n = 0;
    for (size_t i = b; i < e;) {
        char c = s[i];
        if (c == '%') return fail(p, OXP_E_SYNTAX, "a parameter-entity reference inside a declaration in the internal subset");
        if (c == '&') {
            size_t r = ref_end(s, e, i);
            if (!r) return fail(p, OXP_E_SYNTAX, "a malformed reference in an entity value");
            if (s[i + 1] == '#') {
                unsigned long ch = charref(s + i, r - i);
                if (!ch) return fail(p, OXP_E_CHAR, "a character reference to a character XML does not allow");
                char o[4];
                if (!sb_add(p, &p->tmp, o, utf8_put(o, ch))) return R_ERR;
            } else if (!sb_add(p, &p->tmp, s + i, r - i)) return R_ERR;
            i = r;
            continue;
        }
        if (!sb_addc(p, &p->tmp, c)) return R_ERR;
        i++;
    }
    (void)pe;
    if (p->tmp.n > p->lim.max_text) return fail(p, OXP_E_LIMIT, "an entity value is too long");
    return R_OK;
}

static int decl_entity(oxp_parser *p, input *in)
{
    size_t start = in->pos, nb, ne, pb, pe, sb, se;
    int got, is_pe = 0;
    in->pos += 8;
    if (!skip_s(in)) { NEED(1); return fail(p, OXP_E_SYNTAX, "a space is needed after ENTITY"); }
    NEED(1);
    if (S(in)[in->pos] == '%') {
        in->pos++;
        if (!skip_s(in)) { NEED(1); return fail(p, OXP_E_SYNTAX, "a space is needed after '%'"); }
        is_pe = 1;
    }
    CHECK(read_name(p, in, &nb, &ne));
    if (!(p->options & OXP_OPT_NO_NAMESPACES) && memchr(S(in) + nb, ':', ne - nb))
        return fail(p, OXP_E_NS_PREFIX, "an entity name cannot hold a colon");
    if (!skip_s(in)) { NEED(1); return fail(p, OXP_E_SYNTAX, "a space is needed after the entity name"); }
    NEED(1);
    int internal = S(in)[in->pos] == '"' || S(in)[in->pos] == '\'';
    size_t nd_b = NOPOS, nd_e = NOPOS;
    if (internal) {
        CHECK(entity_value(p, in, start, is_pe));
    } else {
        CHECK(external_id(p, in, start, 0, &pb, &pe, &sb, &se, &got));
        if (!got) return fail(p, OXP_E_SYNTAX, "a value or SYSTEM or PUBLIC was expected");
        size_t sp = skip_s(in);
        WANT(5);
        if (at(in, "NDATA")) {
            if (is_pe) return fail(p, OXP_E_SYNTAX, "a parameter entity cannot be unparsed (NDATA)");
            if (!sp) return fail(p, OXP_E_SYNTAX, "a space is needed before NDATA");
            in->pos += 5;
            if (!skip_s(in)) { NEED(1); return fail(p, OXP_E_SYNTAX, "a space is needed after NDATA"); }
            CHECK(read_name(p, in, &nd_b, &nd_e));
        }
    }
    skip_s(in);
    NEED(1);
    if (S(in)[in->pos] != '>') return fail(p, OXP_E_SYNTAX, "'>' expected at the end of an entity declaration");
    in->pos++;
    if (!decls_on(p)) return R_OK;
    entity **list = is_pe ? &p->pents : &p->gents;
    if (find_ent(*list, S(in) + nb, ne - nb)) return R_OK;          /* the first declaration binds */
    if (!is_pe && predefined(S(in) + nb, ne - nb)) return R_OK;   /* kept as the built-in ones */
    entity *en = xalloc(p, sizeof *en);
    if (!en) return fail(p, OXP_E_NOMEM, "out of memory");
    memset(en, 0, sizeof *en);
    en->name = dupslice(p, in, nb, ne);
    if (internal) {
        en->value = xstrndup(p, p->tmp.p ? p->tmp.p : "", p->tmp.n);
        en->value_len = p->tmp.n;
        en->type = is_pe ? OXP_ENTITY_INTERNAL_PARAMETER : OXP_ENTITY_INTERNAL_GENERAL;
    } else {
        en->public_id = dupslice(p, in, pb, pe);
        en->system_id = dupslice(p, in, sb, se);
        en->notation = dupslice(p, in, nd_b, nd_e);
        en->type = is_pe ? OXP_ENTITY_EXTERNAL_PARAMETER :
                   nd_b != NOPOS ? OXP_ENTITY_EXTERNAL_GENERAL_UNPARSED : OXP_ENTITY_EXTERNAL_GENERAL_PARSED;
    }
    en->next = *list; *list = en;
    if (!en->name || (internal && !en->value)) return R_ERR;
    if (p->h.entity_decl) EVENT(p->h.entity_decl(p->ud, en->name, en->type, en->public_id, en->system_id, en->value, en->notation));
    return R_OK;
}

static int decl_notation(oxp_parser *p, input *in)
{
    size_t start = in->pos, b, e, pb, pe, sb, se;
    int got;
    in->pos += 10;
    if (!skip_s(in)) { NEED(1); return fail(p, OXP_E_SYNTAX, "a space is needed after NOTATION"); }
    CHECK(read_name(p, in, &b, &e));
    if (!(p->options & OXP_OPT_NO_NAMESPACES) && memchr(S(in) + b, ':', e - b))
        return fail(p, OXP_E_NS_PREFIX, "a notation name cannot hold a colon");
    if (!skip_s(in)) { NEED(1); return fail(p, OXP_E_SYNTAX, "a space is needed after the notation name"); }
    CHECK(external_id(p, in, start, 1, &pb, &pe, &sb, &se, &got));
    if (!got) return fail(p, OXP_E_SYNTAX, "SYSTEM or PUBLIC was expected");
    skip_s(in);
    NEED(1);
    if (S(in)[in->pos] != '>') return fail(p, OXP_E_SYNTAX, "'>' expected at the end of a notation declaration");
    in->pos++;
    return R_OK;
}

static int dtd_unit(oxp_parser *p, input *in);

/* %name; between declarations */
static int pe_reference(oxp_parser *p, input *in)
{
    size_t start = in->pos, b, e;
    in->pos++;
    CHECK(read_name(p, in, &b, &e));
    NEED(1);
    if (S(in)[in->pos] != ';') return fail(p, OXP_E_SYNTAX, "a parameter-entity reference must end with ';'");
    in->pos++;
    p->doc_has_pe_ref = 1;
    entity *en = find_ent(p->pents, S(in) + b, e - b);
    if (!en || en->type == OXP_ENTITY_EXTERNAL_PARAMETER) {
        if (!en && p->standalone == 1) return failn(p, OXP_E_ENTITY_UNDECLARED, "an undeclared parameter entity", NULL);
        if (p->standalone != 1) p->pe_unread = 1;
        if (p->h.skipped_entity) {
            p->tmp.n = 0;
            if (!sb_addc(p, &p->tmp, '%') || !sb_add(p, &p->tmp, S(in) + b, e - b)) return R_ERR;
            EVENT(p->h.skipped_entity(p->ud, p->tmp.p));
        }
        return R_OK;
    }
    if (en->open) return failn(p, OXP_E_ENTITY_RECURSION, "a parameter entity refers to itself:", en->name);
    if ((ulong)p->nin >= p->lim.max_entity_depth || p->nin >= (int)(sizeof p->stackin / sizeof p->stackin[0]))
        return fail(p, OXP_E_ENTITY_LIMIT, "entities nest too deep");
    if (budget(p, en->value_len)) return R_ERR;
    input *x = &p->stackin[p->nin++];
    x->s = en->value; x->len = en->value_len; x->pos = 0; x->final = 1; x->ent = en; x->dtd = 1; x->depth_at_start = 0;
    en->open = 1;
    int r = R_OK;
    while (!r) {
        skip_s(x);
        if (x->pos >= x->len) break;
        r = dtd_unit(p, x);
    }
    en->open = 0;
    p->nin--;
    return r == R_MORE ? fail(p, OXP_E_SYNTAX, "a parameter entity ends inside a declaration") : r;
}

/* one unit of the internal subset */
static int dtd_unit(oxp_parser *p, input *in)
{
    size_t start = in->pos;
    skip_s(in);
    if (in->pos > start) return R_OK;
    NEED(1);
    char c = S(in)[in->pos];
    if (c == ']' && !in->ent) {
        in->pos++;
        skip_s(in);
        NEED(1);
        if (S(in)[in->pos] != '>') return fail(p, OXP_E_SYNTAX, "'>' expected after the internal subset");
        in->pos++;
        p->state = ST_PROLOG;
        if (p->h.end_doctype) EVENT(p->h.end_doctype(p->ud));
        return R_OK;
    }
    if (c == '%') return pe_reference(p, in);
    if (c != '<') return fail(p, OXP_E_SYNTAX, "a declaration was expected in the internal subset");
    NEED(2);
    if (S(in)[in->pos + 1] == '?') return pi(p, in, 0);
    NEED(4);
    if (at(in, "<!--")) return comment(p, in, 0);
    WANT(10);
    if (at(in, "<!ELEMENT") && AV(in) > 9 && is_space((u8)S(in)[in->pos + 9])) return decl_element(p, in);
    if (at(in, "<!ATTLIST") && AV(in) > 9 && is_space((u8)S(in)[in->pos + 9])) return decl_attlist(p, in);
    if (at(in, "<!ENTITY") && AV(in) > 8 && is_space((u8)S(in)[in->pos + 8])) return decl_entity(p, in);
    if (at(in, "<!NOTATION") && (in->pos + 10 >= in->len || is_space((u8)S(in)[in->pos + 10]))) { NEED(11); return decl_notation(p, in); }
    return fail(p, OXP_E_SYNTAX, "an unknown declaration in the internal subset");
}

/* ---------------------------------------------------------------- the driver */

/* the prolog and the epilog: space, comments, PIs, the DOCTYPE (prolog only), the root */
static int misc_unit(oxp_parser *p, input *in)
{
    size_t start = in->pos;
    if (skip_s(in)) return R_OK;
    NEED(1);
    if (S(in)[in->pos] != '<') return fail(p, OXP_E_SYNTAX, p->state == ST_PROLOG ? "text before the root element" : "text after the root element");
    NEED(2);
    char c = S(in)[in->pos + 1];
    if (c == '?') return pi(p, in, 1);
    if (c == '!') {
        if (maybe_at(in, "<!--")) { NEED(4); return comment(p, in, 1); }
        if (maybe_at(in, "<!DOCTYPE")) {
            NEED(10);
            if (p->state != ST_PROLOG || p->seen_doctype) return fail(p, OXP_E_SYNTAX, "a DOCTYPE in the wrong place");
            return doctype(p, in);
        }
        return fail(p, OXP_E_SYNTAX, "'<!' that is not a comment or a DOCTYPE");
    }
    if (c == '/') return fail(p, OXP_E_SYNTAX, "an end tag outside the root element");
    if (p->state == ST_EPILOG) return fail(p, OXP_E_SYNTAX, "a second root element");
    return start_tag(p, in);
}

static int run(oxp_parser *p)
{
    input *in = &p->din;
    in->s = p->doc.p; in->len = p->doc.n;
    for (;;) {
        if (p->err) return R_ERR;
        if (stopped(p)) return R_ERR;
        int r;
        if (in->pos >= in->len && !in->final) return R_MORE;
        if (in->pos >= in->len) {
            /* the end of the document */
            switch (p->state) {
            case ST_START: case ST_PROLOG:
                if (p->options & OXP_OPT_FRAGMENT) break;
                return fail(p, OXP_E_UNCLOSED, "no root element");
            case ST_DTD: return fail(p, OXP_E_UNCLOSED, "the document ends inside the DOCTYPE");
            case ST_CONTENT:
                if (p->nels) return failn(p, OXP_E_UNCLOSED, "the document ends inside an element:", p->stack.p + p->els[p->nels - 1].qname);
                break;
            default: break;
            }
            if (p->state != ST_DONE) {
                p->state = ST_DONE;
                if (p->h.end_document) EVENT(p->h.end_document(p->ud));
            }
            return R_OK;
        }
        switch (p->state) {
        case ST_START: r = xml_decl(p, in); break;
        case ST_PROLOG: case ST_EPILOG: r = misc_unit(p, in); break;
        case ST_DTD: r = dtd_unit(p, in); break;
        case ST_CONTENT: r = content_unit(p, in); break;
        default: r = fail(p, OXP_E_SYNTAX, "content after the end of the document"); break;
        }
        if (r == R_MORE) return R_MORE;
        if (r) return R_ERR;
        advance_lc(p, p->base + in->pos);
    }
}

/* ---------------------------------------------------------------- the interface */

static const oxp_limits default_limits = { 10000, 50000, 10ul * 1024 * 1024, 16, 1024ul * 1024, 10, 10000 };

oxp_parser *oxp_new(const oxp_handler *h, void *ud, const oxp_alloc *alloc)
{
    oxp_alloc a = { d_alloc, d_realloc, d_free, NULL };
    if (alloc) a = *alloc;
    oxp_parser *p = a.alloc(a.ctx, sizeof *p);
    if (!p) return NULL;
    memset(p, 0, sizeof *p);
    p->a = a;
    if (h) p->h = *h;
    p->ud = ud;
    p->lim = default_limits;
    p->line = p->col = 1;
    p->standalone = -1;
    p->well_formed = 1;
    p->state = ST_START;
    return p;
}

static void free_ents(oxp_parser *p, entity *e)
{
    while (e) {
        entity *n = e->next;
        xfree(p, e->name); xfree(p, e->value); xfree(p, e->public_id); xfree(p, e->system_id); xfree(p, e->notation);
        xfree(p, e);
        e = n;
    }
}

void oxp_free(oxp_parser *p)
{
    if (!p) return;
    free_ents(p, p->gents);
    free_ents(p, p->pents);
    for (attlist *a = p->atts; a;) {
        attlist *n = a->next;
        for (attdef *d = a->defs; d;) { attdef *m = d->next; xfree(p, d->name); xfree(p, d->value); xfree(p, d); d = m; }
        xfree(p, a->element); xfree(p, a);
        a = n;
    }
    xfree(p, p->raw.p); xfree(p, p->doc.p); xfree(p, p->crlf); xfree(p, p->els); xfree(p, p->stack.p);
    xfree(p, p->binds); xfree(p, p->nsbuf.p); xfree(p, p->scratch.p); xfree(p, p->ra); xfree(p, p->oa);
    xfree(p, p->ons); xfree(p, p->tmp.p); xfree(p, p->tmp2.p);
    p->a.free(p->a.ctx, p);
}

void oxp_set_options(oxp_parser *p, unsigned options)
{
    p->options = options;
    if (options & OXP_OPT_FRAGMENT) { if (p->state == ST_START) p->state = ST_CONTENT; }
}

void oxp_set_limits(oxp_parser *p, const oxp_limits *l)
{
#define L(f) if (l->f) p->lim.f = l->f
    L(max_depth); L(max_name); L(max_text); L(max_entity_depth); L(max_expansion_floor); L(max_expansion_ratio); L(max_attrs);
#undef L
}

int oxp_set_encoding(oxp_parser *p, oxp_encoding enc)
{
    if (p->in_total) return 0;
    p->enc = enc;
    p->enc_fixed = enc != OXP_ENC_AUTO;
    return 1;
}

int oxp_bind_ns(oxp_parser *p, const char *prefix, const char *uri)
{
    if (p->nels || p->in_total) return 0;
    return bind(p, prefix, prefix ? strlen(prefix) : 0, uri ? uri : "", uri ? strlen(uri) : 0) == R_OK;
}

oxp_code oxp_parse(oxp_parser *p, const void *data, size_t len, int last)
{
    if (p->err) return p->err;
    if (p->last_seen) return p->err ? p->err : OXP_OK;
    p->in_total += len;
    p->last_seen = last;
    if (len && !sb_add(p, &p->raw, (const char *)data, len)) return p->err;
    if (!decode(p, last)) return p->err;
    if (last && p->pending_cr) p->pending_cr = 0;
    p->din.final = last && p->raw.n == p->raw_pos && !p->hold_decl;
    if (p->options & OXP_OPT_FRAGMENT && p->hold_decl) {        /* no declaration in a fragment */
        if (!release_decl(p, NULL)) return p->err;
        p->din.final = last && p->raw.n == p->raw_pos;
    }
    for (;;) {
        p->din.s = p->doc.p; p->din.len = p->doc.n;
        int was_held = p->hold_decl;
        run(p);
        if (p->err) return p->err;
        /* the declaration was read (or found missing) while the rest waited: go on with it */
        if (was_held && !p->hold_decl) { p->din.final = last && p->raw.n == p->raw_pos; continue; }
        break;
    }
    /* drop what was read */
    if (p->din.pos > 4096 && p->din.pos * 2 > p->doc.n) {
        advance_lc(p, p->base + p->din.pos);
        memmove(p->doc.p, p->doc.p + p->din.pos, p->doc.n - p->din.pos);
        p->doc.n -= p->din.pos;
        p->base += p->din.pos;
        p->din.pos = 0;
        p->din.s = p->doc.p; p->din.len = p->doc.n;
    }
    return OXP_OK;
}

void oxp_stop(oxp_parser *p) { p->stopped = 1; }
unsigned long oxp_line(const oxp_parser *p) { return p->line; }
unsigned long oxp_column(const oxp_parser *p) { return p->col; }
unsigned long oxp_bytes_consumed(const oxp_parser *p) { return p->bytes_done; }
int oxp_well_formed(const oxp_parser *p) { return p->well_formed && !p->err; }

oxp_code oxp_error(const oxp_parser *p, const char **message, unsigned long *line, unsigned long *column)
{
    if (message) *message = p->err ? p->errmsg : NULL;
    if (line) *line = p->err_line;
    if (column) *column = p->err_col;
    return p->err;
}

oxp_code oxp_parse_memory(const oxp_handler *h, void *ud, const void *data, size_t len, unsigned options)
{
    oxp_parser *p = oxp_new(h, ud, NULL);
    if (!p) return OXP_E_NOMEM;
    oxp_set_options(p, options);
    oxp_code c = oxp_parse(p, data, len, 1);
    oxp_free(p);
    return c;
}

const char *oxp_code_name(oxp_code c)
{
    static const char *const n[] = { "ok", "out of memory", "syntax", "character", "encoding", "tag mismatch", "unclosed",
                                     "duplicate attribute", "namespace prefix", "undeclared entity", "entity recursion",
                                     "entity limit", "unparsed entity", "external entity", "stopped", "limit" };
    return (unsigned)c < sizeof n / sizeof n[0] ? n[c] : "?";
}
