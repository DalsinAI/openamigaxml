/* OXP internals. MIT licence. Copyright (c) 2026 AmigaChrome Team. */
#ifndef OXP_INT_H
#define OXP_INT_H

#include "oxp.h"
#include <string.h>

typedef unsigned char u8;
typedef unsigned long ulong;

/* a growable byte buffer */
typedef struct sbuf { char *p; size_t n, cap; } sbuf;

/* a source of decoded text: the document (it grows) or an entity's replacement text */
typedef struct input {
    const char *s;
    size_t len, pos;
    int final;                  /* no more text will come */
    struct entity *ent;         /* the entity being expanded, or NULL for the document */
    int dtd;                    /* an entity read as part of the DTD (a parameter entity) */
    ulong depth_at_start;       /* element depth when the entity began */
} input;

typedef struct entity {
    char *name;
    char *value;                /* replacement text (internal), UTF-8 */
    size_t value_len;
    char *public_id, *system_id, *notation;
    oxp_entity_type type;
    int open;                   /* being expanded now (recursion check) */
    int external_decl;          /* reserved: declared in an external subset (never) */
    struct entity *next;
} entity;

enum { AT_CDATA = 0, AT_TOKENIZED = 1 };
enum { AD_IMPLIED = 0, AD_REQUIRED, AD_FIXED, AD_DEFAULT };

typedef struct attdef {
    char *name;                 /* qualified name */
    int type;                   /* AT_CDATA or AT_TOKENIZED */
    int dflt;                   /* AD_* */
    char *value;                /* normalised default, or NULL */
    size_t value_len;
    struct attdef *next;
} attdef;

typedef struct attlist {
    char *element;              /* qualified name */
    attdef *defs;
    struct attlist *next;
} attlist;

/* one raw attribute while a start tag is read: offsets into p->scratch */
typedef struct rawattr {
    size_t name, name_len;      /* qualified name */
    size_t value, value_len;    /* normalised value */
    size_t raw_b, raw_e;        /* the value as written, in the input */
    int defaulted;
    int is_ns;                  /* a namespace declaration */
    size_t prefix, local;       /* offsets of the split name (prefix may be (size_t)-1) */
    const char *uri;
} rawattr;

/* an open element: offsets into p->stack */
typedef struct openel {
    size_t qname;               /* NUL-terminated qualified name */
    size_t prefix, local, uri;  /* (size_t)-1 when absent */
    size_t nbind;               /* namespace bindings it added */
} openel;

typedef struct binding { size_t prefix, uri; } binding;   /* offsets into p->nsbuf; prefix (size_t)-1: default */

enum { ST_START, ST_PROLOG, ST_DTD, ST_CONTENT, ST_EPILOG, ST_DONE };

struct oxp_parser {
    oxp_handler h;
    void *ud;
    oxp_alloc a;
    unsigned options;
    oxp_limits lim;

    /* input and decoding */
    sbuf raw;                   /* bytes not yet decoded */
    size_t raw_pos;
    oxp_encoding enc;           /* the encoding in use */
    int enc_fixed;              /* set by oxp_set_encoding */
    int enc_known;              /* detection is over */
    int hold_decl;              /* 8-bit: decode only up to the first '>' until the declaration is read */
    int pending_cr;             /* a CR was the last character decoded */
    ulong in_total;             /* bytes given to oxp_parse */
    ulong bom_bytes;
    int last_seen;

    sbuf doc;                   /* decoded document text (UTF-8, line ends normalised) */
    ulong base;                 /* absolute offset of doc.p[0] */
    input din;                  /* the document input */
    input stackin[24];          /* entity inputs */
    int nin;

    ulong *crlf; size_t ncrlf, crlf_head, crlf_cap;  /* absolute offsets of LFs that were CR LF */

    /* position */
    ulong lc_abs, line, col, bytes_done;

    /* state */
    int state;
    int seen_root;
    int seen_doctype;
    int seen_decl;
    int standalone;
    int doc_has_external;       /* the DOCTYPE has an external ID */
    int doc_has_pe_ref;         /* a PE reference was seen in the DTD */
    int pe_unread;              /* a PE reference whose text was not read: stop processing declarations */
    int stopped;
    oxp_code err;
    char errmsg[160];
    ulong err_line, err_col;
    int well_formed;

    /* DTD */
    entity *gents, *pents;
    attlist *atts;

    /* elements and namespaces */
    openel *els; size_t nels, cap_els;
    sbuf stack;                 /* names of the open elements */
    binding *binds; size_t nbinds, cap_binds;
    sbuf nsbuf;

    /* scratch for one unit */
    sbuf scratch;
    rawattr *ra; size_t nra, cap_ra;
    oxp_attr *oa; size_t cap_oa;
    oxp_ns *ons; size_t cap_ons;
    sbuf tmp, tmp2;

    ulong hint_unit, hint_scan; /* a search that ran out of input: where its unit began and how far it got (absolute) */
    ulong expanded;             /* bytes produced by entity expansion */
    int edepth;
};

#endif
