#!/bin/sh
# Runs every case in cases/ through oxpdump and compares with its .out file,
# then again fed 1, 3 and 7 bytes at a time: a well-formed case must give the
# same events, an ill-formed one the same error (events before an error may
# come earlier when the input arrives in pieces).
#   sh tests/run-cases.sh [OXPDUMP]       (default: build/oxpdump)
# MIT licence. Copyright (c) 2026 AmigaChrome Team.
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DUMP=${1:-"$HERE/../build/oxpdump"}
pass=0; failn=0
for f in "$HERE"/cases/*.xml; do
  n=$(basename "$f" .xml)
  exp="$HERE/cases/$n.out"
  got=$("$DUMP" "$f" 2>&1)
  if [ "$got" != "$(cat "$exp")" ]; then echo "FAIL $n"; failn=$((failn+1)); continue; fi
  ok=1
  for c in 1 3 7; do
    gc=$("$DUMP" -c $c "$f" 2>&1)
    if grep -q '^ERROR 2' "$exp"; then
      [ "$(echo "$gc" | grep '^ERROR 2')" = "$(grep '^ERROR 2' "$exp")" ] || ok=0
    else
      [ "$gc" = "$got" ] || ok=0
    fi
  done
  if [ $ok = 1 ]; then pass=$((pass+1)); else echo "FAIL $n (fed in pieces)"; failn=$((failn+1)); fi
done
echo "cases: $pass passed, $failn failed"
[ $failn = 0 ]
