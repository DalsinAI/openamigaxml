# openamigaxml

libxml2 and Expat for AmigaOS 3.x on 68k, built as static link libraries for
GCC programs. Part of the [OpenAmiga](https://github.com/DalsinAI/openamiga)
ports, made for [OpenBrowser](https://github.com/DalsinAI/openamigabrowser),
the WebKit browser for AmigaOS 3.2.

**Status:** Working: both build, and their smoke tests pass on the bench.

This repository holds the Amiga build, not libxml2, Expat itself: a build script,
configuration headers, a smoke test and the upstream licences.

## Upstream

| Library | Version | Licence | Home |
| --- | --- | --- | --- |
| libxml2 | 2.15.4 | MIT (upstream/libxml2/Copyright) | https://gitlab.gnome.org/GNOME/libxml2 |
| Expat | 2.8.2 | MIT (upstream/expat/COPYING) | https://libexpat.github.io/ |

The exact files and their SHA-256 sums are in [SOURCES](SOURCES). All credit
for the library goes to its authors; see `upstream/` for their notices.

## What the Amiga port changes

- libxml2: no source changes; its CMake build asks for position-independent code, which the script turns off (AmigaOS hunk files have none). No iconv: libxml2's own UTF-8, UTF-16 and ISO-8859 converters are used.
- Expat: a hand-written `config/expat/expat_config.h` for big-endian 68k; `XML_POOR_ENTROPY` because AmigaOS has no `getrandom()`.

## Building

You need the os32-gcc16 compiler (bebbo's amiga-gcc on GCC 16.2 with libnix
and libpthread; see DalsinAI/openamigabrowser `stove/`) and the upstream
tarballs from [SOURCES](SOURCES) in `tarballs/`. Then:

```
./build.sh
```

The libraries and headers land in `out/` (set `PREFIX` to change that). The
script prints which other settings it needs, if any. Target: 68020 or better
with an FPU (`-m68020 -m68881`), libnix (`-mcrt=nix20`).

Link with: `-lxml2 -lexpat -lpthread -lm`

## Tested

`tests/xmltest.c`, run on AmigaOS 3.2.3 on AmigaChrome's AC090 emulation (68040 with FPU, 256 MB), Instance-24, 4 October 2026, as `xmltest`:

```
LIBXML 21504
<svg> id=logo
  <title>
    "Café & Amiga"
  <rect> id=r1
  <g>
    <circle> id=c
SERIALISED 177 bytes
XML_DONE
```

`tests/expattest.c`, as `expattest`:

```
EXPAT expat_2.8.2
<fontconfig>
  <dir>
  <alias binding="same">
    <family>
    <prefer>
      <family>
EXPAT_DONE elements=6
```

It has not yet been run on real Amiga hardware.

## Known issues

- None known.

## Licence

Dalsin Limited's Amiga changes (the build script, patches, configuration
headers and tests) are MIT, Copyright (c) 2026 Dalsin Limited: see
[LICENSE](LICENSE). libxml2, Expat keep their own licences, in
[upstream/](upstream/); a patch to their source stays under that licence.

## Contributors

This port is maintained by [SacredTrees](https://github.com/SacredTrees) with the AmigaChrome agent team, copyright Dalsin Limited. Everyone whose work it includes is credited in [`CONTRIBUTORS.md`](CONTRIBUTORS.md).
