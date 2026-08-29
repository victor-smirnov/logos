#!/usr/bin/env bash
# run.sh <file.logos> [sites…] — compile UNARMED then ARMED(tmcbdyn).
# Prints rc for both and the ARMED per-site log as site:arrivals/flips.
cd "$(git rev-parse --show-toplevel)"
f="$1"; shift; b=$(basename "$f" .logos)
L=/tmp/tmcb-$b.un.flip; F=/tmp/tmcb-$b.ar.flip; rm -f "$L" "$F"; touch "$L" "$F"
LOGOS_TMCB_FLIP=$L ./build/bin/logosc "$f" -o /tmp/tmcb-$b.o  >/tmp/tmcb-$b.un 2>&1; un=$?
LOGOS_PROBE=tmcbdyn LOGOS_TMCB_FLIP=$F ./build/bin/logosc "$f" -o /tmp/tmcb-$b.o2 >/tmp/tmcb-$b.ar 2>&1; ar=$?
fl() { awk -F'\t' -v w="$1" '{a[$1]+=$2;b[$1]+=$3} END{for(k in a) if(w==""||index(" "w" "," "k" ")) printf "%s:%d/%d\n",k,a[k],b[k]}' "$2" | sort -t: -k1n | tr '\n' ' '; }
printf '%-12s un=%d ar=%d  %s\n' "$b" "$un" "$ar" "$(fl "$*" $F)"
if [ "$un" != "$ar" ]; then echo "   UNARMED:"; tail -2 /tmp/tmcb-$b.un|sed 's/^/     /'; echo "   ARMED:"; tail -2 /tmp/tmcb-$b.ar|sed 's/^/     /'; fi
[ "$un" != 0 ] && { grep -m2 error /tmp/tmcb-$b.un|sed 's/^/     /'; }
exit 0
