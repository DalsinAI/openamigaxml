/* API checks that the event dumps don't show: the XML declaration signal and
 * the nested-reference limit. MIT licence. Copyright (c) 2026 AmigaChrome Team. */
#include "oxp.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails;
#define CHECK(c, what) do { if (!(c)) { printf("FAIL %s\n", what); fails++; } else printf("ok   %s\n", what); } while (0)

static const char *seen_version; static int seen_sd, seen_standalone;
static char vbuf[16];
static void sd(void *u, const char *v, const char *e, int s)
{
    (void)u; (void)e; seen_sd = 1; seen_standalone = s;
    seen_version = v ? (strcpy(vbuf, v), vbuf) : NULL;
}

static oxp_code run(const char *doc, unsigned long max_refs)
{
    oxp_handler h = { 0 };
    h.start_document = sd;
    oxp_parser *p = oxp_new(&h, NULL, NULL);
    oxp_limits l = { 0 };
    l.max_entity_refs = max_refs;
    oxp_set_limits(p, &l);
    oxp_code c = oxp_parse(p, doc, strlen(doc), 1);
    oxp_free(p);
    return c;
}

int main(void)
{
    seen_sd = 0; run("<a/>", 0);
    CHECK(seen_sd && seen_version == NULL && seen_standalone == -1, "no XML declaration: version NULL");
    run("<?xml version=\"1.0\"?><a/>", 0);
    CHECK(seen_version && !strcmp(seen_version, "1.0") && seen_standalone == -1, "a declaration: its version, standalone not said");
    run("<?xml version=\"1.0\" standalone=\"no\"?><a/>", 0);
    CHECK(seen_standalone == 0, "standalone='no'");

    /* &l3; holds 10 + 100 + 1000 = 1,110 nested references */
    char doc[2048] = "<!DOCTYPE a [<!ENTITY l0 'x'>";
    for (int i = 1; i <= 4; i++) {
        char d[200];
        snprintf(d, sizeof d, "<!ENTITY l%d '", i);
        strcat(doc, d);
        for (int k = 0; k < 10; k++) { snprintf(d, sizeof d, "&l%d;", i - 1); strcat(doc, d); }
        strcat(doc, "'>");
    }
    strcat(doc, "]><a>&l3;</a>");
    CHECK(run(doc, 512) == OXP_E_ENTITY_LIMIT, "1,110 nested references against a limit of 512: refused");
    CHECK(run(doc, 2000) == OXP_OK, "the same against 2,000: read");
    CHECK(run(doc, 0) == OXP_OK, "the same with no limit set: read");

    /* many uses at the top level do not add up */
    static char many[40000];
    strcpy(many, "<!DOCTYPE a [<!ENTITY e 'y'>]><a>");
    for (int i = 0; i < 2000; i++) strcat(many, "&e;");
    strcat(many, "</a>");
    CHECK(run(many, 512) == OXP_OK, "2,000 top-level references, limit 512: read");
    /* in an attribute value too */
    CHECK(run("<!DOCTYPE a [<!ENTITY b 'z'><!ENTITY c '&b;&b;&b;'>]><a v='&c;&c;'/>", 2) == OXP_E_ENTITY_LIMIT,
          "3 nested references in an attribute, limit 2: refused");
    printf("%s\n", fails ? "apitest: FAILED" : "apitest: all ok");
    return fails ? 1 : 0;
}
