# OXP: the Open XML Parser

The Team's own XML 1.0 parser, written from the XML 1.0 (fifth edition) and
Namespaces in XML 1.0 specifications only. No code from libxml2, Expat or any
other parser went into it. It is meant to replace libxml2 under OpenBrowser's
WebKit (SVG, XHTML, `DOMParser`, `responseXML`). It is also meant for the
Open family's other XML readers, such as OpenWrite (ODT and DOCX are XML)
and OpenMail.

MIT licensed and free. Copyright (c) 2026 AmigaChrome Team.

**Status: 0.1, new.** It passes every in-scope test of the W3C XML
Conformance Test Suite and every case in `tests/cases`. On 4,780 real files
it gives the same events as libxml2 for 4,771 (below). It has not yet replaced libxml2 in WebKit; that is a WebKit
backend of its own (`WEBKIT.md`).

## What it does

- **A streaming core** (`include/oxp.h`): feed it bytes in chunks of any size
  and it calls you back for each event, as SAX does. A unit that is not
  complete yet is read again when more input comes, so no event is ever
  reported twice.
- **A small tree** (`include/oxp_dom.h`), built from those events. It has
  lookups by namespace and name, `textContent`, and a writer that escapes
  text and values.
- **Namespaces** (Namespaces in XML 1.0, third edition), with every
  namespace constraint checked. One option, `OXP_OPT_UNBOUND_PREFIX`, makes
  an undeclared prefix a non-fatal error with no namespace, as libxml2 does.
  A browser's fragment parser relies on that.
- **Entities:**
  - the five predefined ones, and character references;
  - internal general and parameter entities from the internal subset;
  - attribute defaults and types from `ATTLIST`.
  - Expansion is limited (`oxp_limits`): entities nested 16 deep at most, and
    entity output no more than 10 times the input once past 1 MB. A
    billion-laughs or quadratic-blowup document stops with
    `OXP_E_ENTITY_LIMIT`.
- **The rest of XML 1.0:** CDATA sections, comments, processing instructions,
  and every well-formedness constraint. Each error has its line and column,
  in characters.
- **Encodings:** UTF-8, UTF-16 (either byte order, with or without a BOM),
  ISO-8859-1 and US-ASCII. A caller can fix the encoding and ignore the
  declaration's, as WebKit does when it feeds UTF-16.
- **Off by design:** loading anything (external entities, an external DTD
  subset, catalogs), validation, and XSLT.
  - A reference to an external entity is reported with `skipped_entity` and
    never fetched.
  - After a parameter-entity reference that was not read, later declarations
    are not processed, as section 5.1 says.

## Building

On the host (any C99 compiler):

```sh
sh oxp/build-host.sh          # oxp/build: liboxp.a, oxpdump, domtest; then the cases
REF=1 sh oxp/build-host.sh    # also tests/ref/libxml2dump (needs libxml2's headers)
```

For AmigaOS 3.x, the 68040, soft float, with the os32-gcc16 compiler:

```sh
sh oxp/build-amiga.sh         # oxp/build-amiga: lib/liboxp.a, include/, bin/oxpdump, bin/domtest
```

It builds with `-m68040 -specs=toolchain/m68k-soft-m68040.specs`. The parser
uses no floating point; the build checks that the library holds no FPU
instruction. It is 47 KB of code at `-O2`.

## Using it

```c
#include "oxp.h"

static void start(void *ud, const char *local, const char *prefix, const char *uri,
                  int nns, const oxp_ns *ns, int nattrs, const oxp_attr *attrs)
{ /* ... */ }

oxp_handler h = { 0 };
h.start_element = start;
oxp_parser *p = oxp_new(&h, my_data, NULL);
while (more) oxp_parse(p, buf, len, 0);
oxp_code c = oxp_parse(p, NULL, 0, 1);      /* the last chunk */
if (c) { const char *m; unsigned long l, col; oxp_error(p, &m, &l, &col); }
oxp_free(p);
```

Or the whole document as a tree:

```c
#include "oxp_dom.h"

oxp_doc *d = oxp_dom_parse(data, len, 0, &code, msg, sizeof msg, &line, &col);
oxp_node *body = oxp_dom_child(oxp_dom_root(d), "urn:oasis:names:tc:opendocument:xmlns:office:1.0", "body");
oxp_dom_free(d);
```

## Tests

- **`tests/cases`:** 70 cases of the Team's own, each with the events it must
  give (`.out`).
  - Covered: well-formedness errors with their line and column; namespaces;
    entity limits, billion laughs, recursion, external and unparsed
    entities, parameter entities; attribute defaults and normalisation;
    UTF-16 in both byte orders with and without a BOM; ISO-8859-1; bad
    UTF-8; CR LF.
  - `tests/run-cases.sh` runs each one whole and fed 1, 3 and 7 bytes at a
    time.
- **`tests/ref`:** the same events from libxml2, for comparing on real files.
  - The test was 4,780 readable files on a Linux system: 3,000 SVG icons, 42
    XHTML pages, and 1,738 other XML documents (D-Bus interfaces, fontconfig,
    AppStream, Mallard help pages, ISO code lists).
  - 4,771 gave the same events, and libxml2 and OXP both refuse 6 that are
    not well-formed.
  - In 2, OXP reports `skipped_entity` for `&nbsp;` in a document with an
    external DTD; libxml2 reports an error and goes on. The specification
    makes it a validity matter, not well-formedness.
  - 1 has an undeclared namespace prefix: OXP stops, libxml2 goes on.
    `OXP_OPT_UNBOUND_PREFIX` gives the same events as libxml2.
- **`tests/fuzz.c`:** a libFuzzer target. `tests/fuzzdrive.c` runs it on
  mutated seeds where libFuzzer is not available. On the host, with
  AddressSanitizer and UBSan, 16 minutes of it (570,000 inputs, from the
  cases and 200 of the real files) found no fault.
- **The tree:** every one of the 4,771 files read, written back with
  `oxp_dom_write` and read again, gives the same events (the DOCTYPE aside,
  which the writer leaves out). `tests/domtest.c` does it for one file.
- **The W3C XML Conformance Test Suite** (release 20130923, test data
  only, never in this repository: `tests/w3c/README.md`). Of the XML 1.0
  fifth-edition and Namespaces 1.0 tests in scope, all 1,915 pass, and 262
  of them also match the suite's canonical output. 59 not-well-formed tests
  have their error in an external entity, which OXP never reads.
  `tests/w3c/run-w3c.py` gives the counts by section.

Run on AmigaOS 3.2.3 (an AC090 lab, 68040), the 68040 build gives the
same output as the host for all 70 cases, and for 139 of the real SVG and
XHTML files. For 0.1's 64 cases it also matched when fed 7 bytes at a time.

## Differences from libxml2 a caller should know

- Comments and processing instructions inside the internal subset are not
  reported (a DOM has no place for them).
- No `ignorable_whitespace`: a non-validating parser cannot tell.
- Namespace errors are fatal, except an unbound prefix under
  `OXP_OPT_UNBOUND_PREFIX`.
- Only the encodings above. Another declared encoding is an error, unless
  the caller fixes the encoding.
