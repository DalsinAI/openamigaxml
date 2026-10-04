/* openamigaxml smoke test for Expat: stream-parse a document with handlers. */
#include <stdio.h>
#include <string.h>
#include <expat.h>
static int depth, elements;
static void XMLCALL start(void *u, const XML_Char *name, const XML_Char **atts)
{
    (void)u; elements++;
    printf("%*s<%s", depth * 2, "", name);
    for (; atts[0]; atts += 2) printf(" %s=\"%s\"", atts[0], atts[1]);
    printf(">\n"); depth++;
}
static void XMLCALL end(void *u, const XML_Char *name) { (void)u; (void)name; depth--; }
int main(void)
{
    const char *doc = "<?xml version=\"1.0\"?><fontconfig><dir>PROGDIR:Fonts</dir>"
        "<alias binding=\"same\"><family>sans-serif</family><prefer><family>Liberation Sans</family></prefer></alias></fontconfig>";
    XML_Parser p = XML_ParserCreate(NULL);
    printf("EXPAT %s\n", XML_ExpatVersion());
    XML_SetElementHandler(p, start, end);
    if (XML_Parse(p, doc, (int)strlen(doc), 1) == XML_STATUS_ERROR)
        printf("PARSE_FAIL %s line %lu\n", XML_ErrorString(XML_GetErrorCode(p)), (unsigned long)XML_GetCurrentLineNumber(p));
    printf("EXPAT_DONE elements=%d\n", elements);
    XML_ParserFree(p);
    return 0;
}
