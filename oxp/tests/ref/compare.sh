#!/bin/sh
# Compare OXP's events with libxml2's over a list of files (one path a line).
#   sh tests/ref/compare.sh LIST     (needs build/oxpdump and build/libxml2dump: REF=1 sh build-host.sh)
# SAME: the same events. DIFF: both read it, the events differ (kept in ref-diffs/).
# BOTH-ERR / ERR-OXP / ERR-LIBXML2: who refused the file. Unreadable files are left out.
# MIT licence. Copyright (c) 2026 AmigaChrome Team.
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
B="$HERE/../../build"
mkdir -p ref-diffs
: > ref-results.txt
while read -r f; do
  [ -r "$f" ] && [ -f "$f" ] || continue
  "$B/libxml2dump" "$f" > ref-r.out 2>/dev/null; rr=$?
  "$B/oxpdump" "$f" > ref-o.out 2>/dev/null; ro=$?
  if [ $rr -ne 0 ] || [ $ro -ne 0 ]; then
    if [ $rr -ne 0 ] && [ $ro -ne 0 ]; then echo "BOTH-ERR $f"; elif [ $ro -ne 0 ]; then echo "ERR-OXP $f"; else echo "ERR-LIBXML2 $f"; fi
  elif cmp -s ref-r.out ref-o.out; then echo "SAME $f"
  else n=$(ls ref-diffs | wc -l); { echo "# $f"; diff ref-r.out ref-o.out | head -40; } > "ref-diffs/$n.diff"; echo "DIFF $f"; fi
done < "${1:?a list of files}" >> ref-results.txt
cut -d' ' -f1 ref-results.txt | sort | uniq -c
