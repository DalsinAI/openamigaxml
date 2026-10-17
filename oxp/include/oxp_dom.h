/* OXP's small tree: a document read into nodes, for programs that want the
 * whole of a document at once (OpenWrite's ODT and DOCX parts, OpenMail's
 * feeds and settings). Built on the streaming parser (oxp.h).
 * MIT licence. Copyright (c) 2026 AmigaChrome Team.
 *
 * Every string in a tree is UTF-8 and NUL-terminated, and lives as long as
 * the tree. Adjacent text is one node; CDATA sections are their own nodes.
 */
#ifndef OXP_DOM_H
#define OXP_DOM_H

#include "oxp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { OXP_NODE_DOCUMENT, OXP_NODE_ELEMENT, OXP_NODE_TEXT, OXP_NODE_CDATA, OXP_NODE_COMMENT, OXP_NODE_PI } oxp_node_type;

typedef struct oxp_dattr {
    const char *local, *prefix, *uri;   /* prefix and uri NULL when none */
    const char *value;
    size_t value_len;
    struct oxp_dattr *next;
} oxp_dattr;

typedef struct oxp_dns {                /* a namespace declaration on an element */
    const char *prefix;                 /* NULL: the default namespace */
    const char *uri;
    struct oxp_dns *next;
} oxp_dns;

typedef struct oxp_node {
    oxp_node_type type;
    struct oxp_node *parent, *first, *last, *prev, *next;
    const char *name;                   /* an element's local name; a PI's target */
    const char *prefix, *uri;           /* an element's prefix and namespace (NULL when none) */
    const char *text;                   /* text, CDATA, comment, PI data */
    size_t len;                         /* bytes in text */
    oxp_dattr *attrs;
    oxp_dns *ns;
    unsigned long line;                 /* where it started */
} oxp_node;

typedef struct oxp_doc oxp_doc;

/* Read a whole document. On an error it returns NULL; err, message (copied
 * into msg) and the line and column say why. Any of them may be NULL. */
oxp_doc *oxp_dom_parse(const void *data, size_t len, unsigned options, oxp_code *err,
                       char *msg, size_t msgsize, unsigned long *line, unsigned long *column);
void oxp_dom_free(oxp_doc *d);

/* the document node, and the document (root) element */
oxp_node *oxp_dom_document(oxp_doc *d);
oxp_node *oxp_dom_root(oxp_doc *d);

/* An attribute's value: uri NULL for an attribute with no namespace. NULL when absent. */
const char *oxp_dom_attr(const oxp_node *el, const char *uri, const char *local);
/* The first child element, or the next sibling element, with this name
 * (local NULL: any name; uri NULL: no namespace; uri "*": any namespace). */
oxp_node *oxp_dom_child(const oxp_node *n, const char *uri, const char *local);
oxp_node *oxp_dom_next(const oxp_node *n, const char *uri, const char *local);
/* The text of a node and all it holds (as DOM's textContent), into buf
 * (size bytes, NUL-terminated). Returns the full length. */
size_t oxp_dom_text(const oxp_node *n, char *buf, size_t size);

/* Write a node (or the document) as XML through write(), which returns 0 on
 * success. Text and attribute values are escaped; namespace declarations are
 * written where the document had them. */
int oxp_dom_write(const oxp_node *n, int (*write)(void *ctx, const char *s, size_t n), void *ctx);

/* To build a tree from a parser you feed yourself: */
typedef struct oxp_dom_builder oxp_dom_builder;
oxp_dom_builder *oxp_dom_builder_new(void);
const oxp_handler *oxp_dom_handler(void);          /* use with oxp_new(..., builder, ...) */
oxp_doc *oxp_dom_builder_finish(oxp_dom_builder *b); /* the tree (also after an error: what was read) */
/* optional: the parser, so each node gets its line */
void oxp_dom_builder_set_parser(oxp_dom_builder *b, const oxp_parser *p);

#ifdef __cplusplus
}
#endif
#endif
