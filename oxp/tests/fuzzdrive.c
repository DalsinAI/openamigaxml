/* A small mutation fuzzer for hosts without libFuzzer: it runs tests/fuzz.c's
 * LLVMFuzzerTestOneInput on mutated seeds for a number of seconds. Build it
 * with the sanitizers:
 *   cc -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -Iinclude \
 *      tests/fuzzdrive.c tests/fuzz.c src/oxp.c -o fuzzdrive
 *   ./fuzzdrive SECONDS SEEDDIR...   (the input being run is kept in fuzz-cur.bin)
 * MIT licence. Copyright (c) 2026 AmigaChrome Team. */
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n);
static uint8_t **seed; static size_t *seedn, nseed;
static unsigned long long rs = 88172645463325252ull;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)rs; }
static const char *const dict[] = { "<", ">", "&", ";", "</", "/>", "<!--", "-->", "<![CDATA[", "]]>", "<?", "?>", "<!DOCTYPE a [",
    "]>", "<!ENTITY e \"&e;\">", "<!ENTITY % p \"<!ENTITY q 'x'>\">", "%p;", "&e;", "&q;", "&#x10FFFF;", "&#0;", "xmlns:a=\"u\"",
    " a:b=\"1\"", "xmlns=\"\"", "<!ATTLIST a b CDATA \"&e;\">", "<!ELEMENT a (b|c)*>", "\xef\xbb\xbf", "\xfe\xff", "\r\n", "\xc3",
    "\xed\xa0\x80", "&h;", "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>", "SYSTEM \"x\"", "NDATA n" };

static void add(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return;
    seed = realloc(seed, (nseed + 1) * sizeof *seed); seedn = realloc(seedn, (nseed + 1) * sizeof *seedn);
    uint8_t *b = malloc(1 << 16);
    size_t n = fread(b, 1, 1 << 16, f);
    fclose(f);
    seed[nseed] = b; seedn[nseed++] = n;
}

int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 60;
    for (int i = 2; i < argc; i++) {
        DIR *d = opendir(argv[i]);
        struct dirent *e;
        if (!d) continue;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            char p[4096]; snprintf(p, sizeof p, "%s/%s", argv[i], e->d_name); add(p);
        }
        closedir(d);
    }
    if (!nseed) { fprintf(stderr, "no seeds\n"); return 2; }
    static uint8_t buf[1 << 17];
    time_t end = time(0) + secs;
    unsigned long runs = 0;
    while (time(0) < end) {
        for (int k = 0; k < 200; k++) {
            size_t s = rnd() % nseed, n = seedn[s];
            if (n > sizeof buf / 2) n = sizeof buf / 2;
            memcpy(buf, seed[s], n);
            int muts = 1 + rnd() % 8;
            for (int m = 0; m < muts; m++) {
                unsigned op = rnd() % 6;
                size_t at = n ? rnd() % n : 0;
                if (op == 0 && n) buf[at] ^= (uint8_t)(1u << (rnd() % 8));
                else if (op == 1 && n) buf[at] = (uint8_t)rnd();
                else if (op == 2 && n > 1) { size_t l = 1 + rnd() % (n - at); memmove(buf + at, buf + at + l, n - at - l); n -= l; }
                else if (op == 3) {
                    const char *t = dict[rnd() % (sizeof dict / sizeof dict[0])];
                    size_t l = strlen(t);
                    if (n + l < sizeof buf) { memmove(buf + at + l, buf + at, n - at); memcpy(buf + at, t, l); n += l; }
                } else if (op == 4 && n) {   /* repeat a piece */
                    size_t l = 1 + rnd() % (n - at), times = 1 + rnd() % 4;
                    for (size_t t = 0; t < times && n + l < sizeof buf; t++) { memmove(buf + at + l, buf + at, n - at); n += l; }
                } else if (op == 5) {        /* splice another seed */
                    size_t o = rnd() % nseed, l = seedn[o] / 2;
                    if (n + l < sizeof buf) { memmove(buf + at + l, buf + at, n - at); memcpy(buf + at, seed[o] + seedn[o] / 4, l); n += l; }
                }
            }
            if (n && (rnd() & 1)) buf[0] = (uint8_t)rnd();
            FILE *f = fopen("fuzz-cur.bin", "wb"); fwrite(buf, 1, n, f); fclose(f);
            LLVMFuzzerTestOneInput(buf, n);
            runs++;
        }
    }
    printf("fuzz: %lu runs in %d s, no crash\n", runs, secs);
    return 0;
}
