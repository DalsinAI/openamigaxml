#!/bin/sh
# OXP on the host: liboxp.a, oxpdump and domtest in oxp/build, then the cases.
# With REF=1 it also builds tests/ref/libxml2dump (needs libxml2's headers),
# the reference side of the event comparison (tests/ref/compare.sh).
# MIT licence. Copyright (c) 2026 AmigaChrome Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CC=${CC:-cc}
OUT="$HERE/build"
CFLAGS=${CFLAGS:-"-std=c99 -O2 -g -Wall -Wextra -Werror"}
mkdir -p "$OUT"
# shellcheck disable=SC2086
$CC $CFLAGS -I"$HERE/include" -c "$HERE/src/oxp.c" -o "$OUT/oxp.o"
# shellcheck disable=SC2086
$CC $CFLAGS -I"$HERE/include" -c "$HERE/src/oxp_dom.c" -o "$OUT/oxp_dom.o"
ar rcs "$OUT/liboxp.a" "$OUT/oxp.o" "$OUT/oxp_dom.o"
# shellcheck disable=SC2086
$CC $CFLAGS -I"$HERE/include" -o "$OUT/oxpdump" "$HERE/tools/oxpdump.c" "$OUT/liboxp.a"
# shellcheck disable=SC2086
$CC $CFLAGS -I"$HERE/include" -o "$OUT/domtest" "$HERE/tests/domtest.c" "$OUT/liboxp.a"
if [ "${REF:-0}" = 1 ]; then
  $CC -O2 -I/usr/include/libxml2 -o "$OUT/libxml2dump" "$HERE/tests/ref/libxml2dump.c" -lxml2
fi
# shellcheck disable=SC2086
$CC $CFLAGS -I"$HERE/include" -o "$OUT/apitest" "$HERE/tests/apitest.c" "$OUT/liboxp.a"
"$OUT/apitest"
sh "$HERE/tests/run-cases.sh" "$OUT/oxpdump"
