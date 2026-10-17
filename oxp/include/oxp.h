/* OXP: the Open XML Parser.
 * MIT licence. Copyright (c) 2026 AmigaChrome Team.
 *
 * A non-validating XML 1.0 (fifth edition) parser with Namespaces in XML 1.0,
 * written from those two specifications. A streaming (SAX-style) core, fed
 * in chunks of any size, and an optional small tree (oxp_dom.h) on top.
 *
 * Covered: well-formedness, namespaces, the predefined entities, character
 * references, internal general and parameter entities declared in the
 * internal subset (with expansion limits), attribute defaults and types
 * from the internal subset, CDATA sections, comments, processing
 * instructions, UTF-8, UTF-16 (both byte orders, with or without a BOM),
 * ISO-8859-1 and US-ASCII input, and the line and column of each error.
 *
 * Off by design: loading external entities or an external DTD subset,
 * validation, and XSLT. A reference to an external entity is reported with
 * skipped_entity and never fetched.
 *
 * Every string the parser passes to a callback is UTF-8 and valid only during
 * the call. Names and values are NUL-terminated; the text of characters and
 * cdata is not (use its length). Lengths are in bytes.
 */
#ifndef OXP_H
#define OXP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OXP_VERSION_MAJOR 0
#define OXP_VERSION_MINOR 1

typedef struct oxp_parser oxp_parser;

/* The namespace name of the xml prefix (Namespaces in XML 1.0, section 3). */
#define OXP_XML_NS "http://www.w3.org/XML/1998/namespace"
#define OXP_XMLNS_NS "http://www.w3.org/2000/xmlns/"

/* ---- events ---- */

typedef struct oxp_attr {
    const char *local;     /* local part */
    const char *prefix;    /* NULL when none */
    const char *uri;       /* namespace name; NULL when none */
    const char *value;     /* normalised value, NUL-terminated */
    size_t value_len;      /* bytes (the value may hold a NUL from &#0;? never: &#0; is an error) */
    int defaulted;         /* 1: it came from an ATTLIST default, not the tag */
} oxp_attr;

typedef struct oxp_ns {
    const char *prefix;    /* NULL for the default namespace (xmlns="...") */
    const char *uri;       /* "" when xmlns="" undeclares the default */
} oxp_ns;

typedef enum {
    OXP_ENTITY_INTERNAL_GENERAL = 1,
    OXP_ENTITY_EXTERNAL_GENERAL_PARSED = 2,
    OXP_ENTITY_EXTERNAL_GENERAL_UNPARSED = 3,
    OXP_ENTITY_INTERNAL_PARAMETER = 4,
    OXP_ENTITY_EXTERNAL_PARAMETER = 5
} oxp_entity_type;

typedef enum { OXP_WARNING = 0, OXP_ERROR = 1, OXP_FATAL = 2 } oxp_level;

/* Error codes: well-formedness (fatal) unless marked. */
typedef enum {
    OXP_OK = 0,
    OXP_E_NOMEM,               /* out of memory */
    OXP_E_SYNTAX,              /* not well-formed: see the message */
    OXP_E_CHAR,                /* a character XML 1.0 does not allow */
    OXP_E_ENCODING,            /* bad byte sequence, or an unsupported encoding */
    OXP_E_TAG_MISMATCH,        /* end tag does not match the start tag */
    OXP_E_UNCLOSED,            /* the document ended inside an element or markup */
    OXP_E_DUP_ATTR,            /* an attribute given twice (also after namespace expansion) */
    OXP_E_NS_PREFIX,           /* an undeclared prefix, or a reserved one misused */
    OXP_E_ENTITY_UNDECLARED,   /* a reference to an entity that was never declared */
    OXP_E_ENTITY_RECURSION,    /* an entity that refers to itself */
    OXP_E_ENTITY_LIMIT,        /* an expansion limit was reached (oxp_limits) */
    OXP_E_ENTITY_UNPARSED,     /* a reference to an unparsed (NDATA) entity */
    OXP_E_ENTITY_EXTERNAL,     /* an external entity where only an internal one may be (in an attribute) */
    OXP_E_STOPPED,             /* oxp_stop was called */
    OXP_E_LIMIT                /* a size or depth limit (oxp_limits) */
} oxp_code;

typedef struct oxp_handler {
    /* version is "1.0" (or the declaration's), encoding is the declaration's
     * encoding name or NULL, standalone is 1 (yes), 0 (no) or -1 (not said). */
    void (*start_document)(void *ud, const char *version, const char *encoding, int standalone);
    void (*end_document)(void *ud);
    /* <!DOCTYPE name PUBLIC "pub" "sys" [ ... ]>: at the start of the declaration. */
    void (*doctype)(void *ud, const char *name, const char *public_id, const char *system_id, int has_internal_subset);
    void (*end_doctype)(void *ud);
    void (*entity_decl)(void *ud, const char *name, oxp_entity_type type, const char *public_id,
                        const char *system_id, const char *value, const char *notation);
    void (*start_element)(void *ud, const char *local, const char *prefix, const char *uri,
                          int nns, const oxp_ns *ns, int nattrs, const oxp_attr *attrs);
    void (*end_element)(void *ud, const char *local, const char *prefix, const char *uri);
    void (*characters)(void *ud, const char *text, size_t len);
    void (*cdata)(void *ud, const char *text, size_t len);
    void (*comment)(void *ud, const char *text);
    void (*pi)(void *ud, const char *target, const char *data);
    /* A reference to an entity whose text the parser does not have: an
     * external parsed entity (never loaded), or one only an external
     * subset could have declared. */
    void (*skipped_entity)(void *ud, const char *name);
    /* Asked before an undeclared general entity is an error: return its
     * replacement text (UTF-8, parsed as content) or NULL. For an XHTML
     * document a browser answers with the HTML named character references. */
    const char *(*resolve_entity)(void *ud, const char *name);
    void (*error)(void *ud, oxp_level level, oxp_code code, const char *message,
                  unsigned long line, unsigned long column);
} oxp_handler;

/* ---- limits (defaults in brackets; 0 keeps the default) ---- */
typedef struct oxp_limits {
    unsigned long max_depth;            /* element nesting [10000] */
    unsigned long max_name;             /* bytes in one name [50000] */
    unsigned long max_text;             /* bytes in one attribute value, comment or PI [10 MB] */
    unsigned long max_entity_depth;     /* entities expanded inside one another [16] */
    unsigned long max_expansion_floor;  /* bytes entities may produce before the ratio applies [1 MB] */
    unsigned long max_expansion_ratio;  /* entity output may be this many times the input [10] */
    unsigned long max_attrs;            /* attributes on one element [10000] */
} oxp_limits;

/* ---- options ---- */
enum {
    OXP_OPT_NO_NAMESPACES = 1u << 0,   /* plain XML 1.0 names: no prefixes resolved */
    OXP_OPT_FRAGMENT      = 1u << 1,   /* content only: no prolog, several top-level elements and text */
    OXP_OPT_KEEP_REFS     = 1u << 2,   /* reserved */
    OXP_OPT_IGNORE_DECL_ENCODING = 1u << 3, /* keep the encoding given to oxp_set_encoding */
    OXP_OPT_UNBOUND_PREFIX = 1u << 4  /* an undeclared prefix is a non-fatal error (OXP_ERROR) and its
                                         name has no namespace (uri NULL), as libxml2 does; a browser's
                                         fragment parser then fills it in */
};

typedef enum {
    OXP_ENC_AUTO = 0,     /* the BOM, the first bytes, then the declaration */
    OXP_ENC_UTF8,
    OXP_ENC_UTF16LE,
    OXP_ENC_UTF16BE,
    OXP_ENC_LATIN1,       /* ISO-8859-1 */
    OXP_ENC_ASCII
} oxp_encoding;

typedef struct oxp_alloc {
    void *(*alloc)(void *ctx, size_t size);
    void *(*realloc)(void *ctx, void *p, size_t size);
    void (*free)(void *ctx, void *p);
    void *ctx;
} oxp_alloc;

/* A parser. alloc may be NULL (malloc, realloc and free). */
oxp_parser *oxp_new(const oxp_handler *h, void *ud, const oxp_alloc *alloc);
void oxp_free(oxp_parser *p);

void oxp_set_options(oxp_parser *p, unsigned options);
void oxp_set_limits(oxp_parser *p, const oxp_limits *limits);
/* Before the first chunk. OXP_ENC_AUTO is the default. */
int oxp_set_encoding(oxp_parser *p, oxp_encoding enc);
/* Fragment mode: a namespace in scope before the content (prefix NULL: the default). */
int oxp_bind_ns(oxp_parser *p, const char *prefix, const char *uri);

/* Feed bytes; last = 1 on the final chunk (len may be 0). Returns OXP_OK,
 * or the code of the fatal error that stopped the parse. After a fatal
 * error every later call returns the same code. */
oxp_code oxp_parse(oxp_parser *p, const void *data, size_t len, int last);
/* Stop from a callback: no more events; oxp_parse returns OXP_E_STOPPED. */
void oxp_stop(oxp_parser *p);

/* Where the parser is: the 1-based line and column (in characters) after
 * the last markup it reported, and the bytes of input consumed. */
unsigned long oxp_line(const oxp_parser *p);
unsigned long oxp_column(const oxp_parser *p);
unsigned long oxp_bytes_consumed(const oxp_parser *p);
/* The last fatal error: its code, message, line and column. */
oxp_code oxp_error(const oxp_parser *p, const char **message, unsigned long *line, unsigned long *column);
/* 1 when nothing was wrong so far. */
int oxp_well_formed(const oxp_parser *p);

/* A whole document from memory (len bytes). */
oxp_code oxp_parse_memory(const oxp_handler *h, void *ud, const void *data, size_t len, unsigned options);

const char *oxp_code_name(oxp_code code);

#ifdef __cplusplus
}
#endif
#endif
