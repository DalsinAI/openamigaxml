#!/bin/sh
# OXP for AmigaOS 3.x: the 68040, soft float, with the os32-gcc16 compiler.
# MIT licence. Copyright (c) 2026 AmigaChrome Team.
#   OS32_GCC16  compiler root holding prefix/ (default ~/AmigaChrome/stoves/os32-gcc16)
#   OUT         where liboxp.a, the headers and the test programs go (default oxp/build-amiga)
# The parser uses no floating point; the soft-float link (toolchain/m68k-soft-m68040.specs)
# is for the test programs, which link libnix.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/.." && pwd)
S=${OS32_GCC16:-"$HOME/AmigaChrome/stoves/os32-gcc16"}
P=$S/prefix
OUT=${OUT:-"$HERE/build-amiga"}
CC="$P/bin/m68k-amigaos-gcc"
AR="$P/bin/m68k-amigaos-ar"
SPECS="$ROOT/toolchain/m68k-soft-m68040.specs"
CFLAGS="-m68040 -specs=$SPECS -mcrt=nix20 -O2 -std=gnu99 -fno-delete-null-pointer-checks -fomit-frame-pointer \
  -Wall -Wextra -Werror -I$HERE/include"
mkdir -p "$OUT/obj" "$OUT/lib" "$OUT/include" "$OUT/bin"
for f in oxp oxp_dom; do $CC $CFLAGS -c "$HERE/src/$f.c" -o "$OUT/obj/$f.o"; done
rm -f "$OUT/lib/liboxp.a"
$AR rcs "$OUT/lib/liboxp.a" "$OUT/obj/oxp.o" "$OUT/obj/oxp_dom.o"
cp "$HERE/include/oxp.h" "$HERE/include/oxp_dom.h" "$OUT/include/"
$CC $CFLAGS -o "$OUT/bin/oxpdump" "$HERE/tools/oxpdump.c" -L"$OUT/lib" -loxp
$CC $CFLAGS -o "$OUT/bin/domtest" "$HERE/tests/domtest.c" -L"$OUT/lib" -loxp
# no FPU code anywhere in the library
if "$P/bin/m68k-amigaos-objdump" -d "$OUT/obj/oxp.o" "$OUT/obj/oxp_dom.o" | grep -q -E '\sf(move|add|sub|mul|div|cmp)'; then
  echo "liboxp.a has FPU instructions" >&2; exit 1
fi
echo "liboxp.a: $(wc -c < "$OUT/lib/liboxp.a") bytes; code: $("$P/bin/m68k-amigaos-size" "$OUT/obj/oxp.o" "$OUT/obj/oxp_dom.o" | awk 'NR>1 {t+=$1} END {print t}') bytes"
