/* openamigaxml smoke test: parse a document, walk it, serialise it. */
#include <stdio.h>
#include <string.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
static void walk(xmlNode *n, int depth)
{
    for (; n; n = n->next) {
        if (n->type == XML_ELEMENT_NODE) {
            xmlChar *id = xmlGetProp(n, (const xmlChar *)"id");
            printf("%*s<%s>%s%s\n", depth * 2, "", n->name, id ? " id=" : "", id ? (char *)id : "");
            xmlFree(id);
        } else if (n->type == XML_TEXT_NODE && !xmlIsBlankNode(n))
            printf("%*s\"%s\"\n", depth * 2, "", n->content);
        walk(n->children, depth + 1);
    }
}
int main(void)
{
    const char *doc = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" id=\"logo\">"
        "<title>Caf\xc3\xa9 &amp; Amiga</title><rect id=\"r1\" width=\"10\"/><g><circle id=\"c\"/></g></svg>";
    xmlDoc *d; xmlChar *out; int len;
    LIBXML_TEST_VERSION
    printf("LIBXML %s\n", xmlParserVersion);
    d = xmlReadMemory(doc, (int)strlen(doc), "test.svg", NULL, 0);
    if (!d) { printf("PARSE_FAIL\n"); return 20; }
    walk(xmlDocGetRootElement(d), 0);
    xmlDocDumpMemory(d, &out, &len);
    printf("SERIALISED %d bytes\n", len);
    xmlFree(out); xmlFreeDoc(d); xmlCleanupParser();
    printf("XML_DONE\n");
    return 0;
}
