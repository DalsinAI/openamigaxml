# Contributors

## Creator and maintainer

- **SacredTrees** ([@SacredTrees](https://github.com/SacredTrees)): created and maintains this AmigaOS port of libxml2 and Expat (openamigaxml).

## The AmigaChrome team

We are the AI agents who build AmigaChrome alongside SacredTrees:

- **Agnus**, our coordinator, who keeps every thread moving.
- **Thufir**, **Kynes** and **Galen**, the earlier agents who started the work on SacredTrees's PC.
- **The Claude Code threads**, each one taking a piece of the work from design to release.

## Copyright holder

Our Amiga work here (the build script, the Expat configuration header
`config/expat/expat_config.h` and the tests) is Copyright (c) 2026 Dalsin
Limited, released under the MIT licence (`LICENSE`). libxml2 and Expat are
not ours: they stay copyright their authors under their own licences.

## Third-party work in this repository

Only the upstream licence notices are committed here; the libraries' source
is not.

| Component | Where | Authors | Licence |
| --- | --- | --- | --- |
| libxml2 licence notice | `upstream/libxml2/Copyright` | Daniel Veillard and the libxml2 contributors | MIT |
| Expat licence and author list | `upstream/expat/COPYING`, `upstream/expat/AUTHORS` | Thai Open Source Software Center Ltd, Clark Cooper and the Expat maintainers (Clark Cooper, Fred L. Drake, Jr., Greg Stein, James Clark, Karl Waclawek, Rhodri James, Sebastian Pipping, Steven Solie) | MIT |

## Fetched at build time, not committed

`build.sh` unpacks these tarballs, listed with their SHA-256 sums in `SOURCES`:

- **libxml2 2.15.4** (`libxml2-2.15.4.tar.xz`): Daniel Veillard and the libxml2 contributors, MIT.
- **Expat 2.8.2** (`expat-2.8.2.tar.bz2`): Clark Cooper, James Clark and the Expat maintainers, MIT.

## Used at build time, not included

- **bebbo's amiga-gcc** (GCC 16.2 with libnix and libpthread), the os32-gcc16 compiler, under its own licences.

Amiga, AmigaOS and other product names are trademarks of their respective
owners.
