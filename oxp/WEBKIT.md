# OXP under WebKit: what a backend needs

WebKit's XML parser (`Source/WebCore/xml/parser/XMLDocumentParserLibxml2.cpp`,
1745 lines, in the WebKit tree OpenBrowser builds) is written against libxml2.
This page lists every libxml2 callback and call it uses, and what OXP gives
for each. A new `XMLDocumentParserOXP.cpp` would sit on this API, and so
could WebKit's other XML user, `XMLDocumentParser`'s fragment parsing.

## The callbacks (`xmlSAXHandler`)

| WebKit sets | It does | OXP |
| --- | --- | --- |
| `startElementNs(localname, prefix, URI, nb_namespaces, namespaces, nb_attributes, nb_defaulted, attributes)` | creates the element; namespaces as (prefix, URI) pairs, attributes as (localname, prefix, URI, value, end) | `start_element(local, prefix, uri, nns, ns, nattrs, attrs)`. `ns[i]` is `{prefix, uri}`; `attrs[i]` is `{local, prefix, uri, value, value_len, defaulted}` (the value's length in place of its end) |
| `endElementNs` | closes it | `end_element(local, prefix, uri)` |
| `characters`, `cdataBlock` | text, CDATA nodes | `characters(text, len)`, `cdata(text, len)` |
| `ignorableWhitespace` | nothing (a crash workaround) | not needed: never called |
| `processingInstruction`, `comment` | PI and comment nodes | `pi(target, data)`, `comment(text)`; not called inside the internal subset |
| `startDocument` (reads `ctxt->version`, `encoding`, `standalone`) | the document's XML version and encoding; `standalone` -1 when there is no declaration | `start_document(version, encoding, standalone)`: `version` is NULL when there is no XML declaration |
| `endDocument` | ends the parse | `end_document()` |
| `internalSubset(name, externalID, systemID)` | the doctype node | `doctype(name, public_id, system_id, has_internal_subset)` |
| `externalSubset(name, externalID, systemID)` | checks the public ID against the XHTML ones (named entities allowed) | the same `doctype` event |
| `entityDecl` | counts each entity's references (its billion-laughs guard, `maximumXMLParserEntityExpansionCount`, 512) | `entity_decl(...)`. The guard is OXP's: `oxp_limits.max_entity_refs`, the references expanded inside one reference's text, set from that setting |
| `getEntity` | an XHTML document's HTML named entities | `resolve_entity(name)`: asked for an entity no declaration gives; it returns the replacement text |
| `warning`, `error`, `fatalError` (printf-style) | error messages with a position | `error(level, code, message, line, column)` |

## The calls

| libxml2 | OXP |
| --- | --- |
| `xmlCreatePushParserCtxt`, `ctxt->_private` | `oxp_new(handler, user_data, NULL)` |
| `xmlParseChunk(ctxt, data, len, terminate)` | `oxp_parse(p, data, len, last)` |
| `xmlSwitchEncoding(UTF16LE/BE)` before every chunk (WebKit feeds UTF-16 and ignores the declaration) | `oxp_set_encoding(p, OXP_ENC_UTF16LE)` (or `BE`) once, with `OXP_OPT_IGNORE_DECL_ENCODING` |
| `xmlCtxtUseOptions(XML_PARSE_NOENT)` | always so: internal entities are expanded |
| `XML_PARSE_HUGE` | `oxp_set_limits` with larger limits |
| `XML_PARSE_NODICT`, `xmlDictLookup` | nothing: OXP has no dictionary |
| `xmlCreateMemoryParserCtxt` + `xmlParseContent` (fragments, `innerHTML` on XHTML) | `OXP_OPT_FRAGMENT`: content only, several top-level nodes. `oxp_bind_ns` gives the context element's prefixes. `OXP_OPT_UNBOUND_PREFIX` keeps libxml2's null URI for a prefix nobody bound, which WebKit fills in |
| `ctxt->input->line`, `ctxt->input->col` (`textPosition`) | `oxp_line`, `oxp_column` |
| `xmlStopParser` | `oxp_stop` |
| `xmlByteConsumed` (only for libxml2 before 2.14) | `oxp_bytes_consumed` |
| `ctxt->wellFormed`, `xmlCtxtGetLastError` | `oxp_well_formed`, `oxp_error` |
| `xmlRegisterInputCallbacks`, `xmlGetExternalEntityLoader`, the catalog check | nothing: OXP never loads anything |
| `xmlReadMemory` (for XSLT) | none: XSLT is off |
| `parseAttributes` (the `xml-stylesheet` pseudo-attributes, its own `startElementNs`) | parse `<a DATA/>` with `oxp_parse_memory` and take `start_element`'s attributes |
| `xmlInitParser`, `xmlMalloc`, `xmlFree`, `xmlStrdup` | nothing. Strings are valid during the callback; WebKit copies them into `AtomString` already |

WebKit pauses for scripts by queueing callbacks itself (`PendingCallbacks`),
so the parser never has to pause. OXP can simply be fed later.

## Where OXP and libxml2 differ

- An undeclared entity in a document with an external DTD (`&nbsp;` in
  XHTML): OXP asks `resolve_entity`, then reports `skipped_entity` and goes
  on. libxml2 calls `getEntity` (WebKit answers there), then reports an
  error and goes on.
- Namespace errors other than an unbound prefix are fatal in OXP.
  libxml2 reports some as errors and goes on.
- Comments and PIs in the internal subset: OXP reports none.
- Declarations after a parameter-entity reference that was not read are not
  processed (XML 1.0, section 5.1). libxml2 processes them.
