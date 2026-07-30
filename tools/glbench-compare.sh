#!/bin/bash
# SimpleFPEWrapper - tools/glbench-compare.sh
# Copyright (c) 2026 MobileGL-Dev
# Licensed under the GNU Lesser General Public License v3.0:
#   https://www.gnu.org/licenses/gpl-3.0.txt
#   https://www.gnu.org/licenses/lgpl-3.0.txt
# SPDX-License-Identifier: LGPL-3.0-only
# End of Source File Header
#
# Runs tools/glbench against two GL libraries and prints the ratio table.
#
#   tools/glbench-compare.sh <baseline.so> <candidate.so> [rounds]
#
# Neither library is privileged: both are dlopened by the same binary, on the
# same GPU, alternating round by round so thermal or frequency drift hits both
# equally. The reported value per phase is the MEDIAN across rounds, and the
# min-max range is printed so a difference smaller than the spread can be seen
# for what it is.
#
# The ratio column is always "how many times the cost of the baseline", with
# the unit's direction accounted for, so >1.00 means the candidate is slower
# whichever way the phase's metric points.
#
# Example, comparing this wrapper against gl4es:
#   tools/glbench-compare.sh \
#       /path/to/gl4es/lib/libGL.so.1 \
#       /path/to/build/libSimpleFPEWrapper.so 6
set -u

BASE="${1:-}"
CAND="${2:-}"
ROUNDS="${3:-5}"
if [ -z "$BASE" ] || [ -z "$CAND" ]; then
    sed -n '10,27p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi
for f in "$BASE" "$CAND"; do
    [ -e "$f" ] || { echo "no such library: $f" >&2; exit 2; }
done

BENCH="${GLBENCH:-}"
if [ -z "$BENCH" ]; then
    for c in "$(dirname "$0")/../build/glbench" "$(dirname "$0")/../glbench" ./glbench /tmp/glbench; do
        [ -x "$c" ] && BENCH="$c" && break
    done
fi
if [ -z "$BENCH" ] || [ ! -x "$BENCH" ]; then
    echo "glbench binary not found; build it or set GLBENCH=<path>" >&2
    echo "  gcc -O2 -o glbench tools/glbench.c -ldl -lEGL" >&2
    exit 2
fi

: "${EGL_PLATFORM:=surfaceless}"
export EGL_PLATFORM
export CMPBENCH_TSV=1

b_out=$(mktemp) ; c_out=$(mktemp)
trap 'rm -f "$b_out" "$c_out"' EXIT

echo "# glbench comparison, $ROUNDS alternating rounds" >&2
echo "#   baseline : $BASE" >&2
echo "#   candidate: $CAND" >&2
for r in $(seq 1 "$ROUNDS"); do
    CMPBENCH_LIB="$BASE" "$BENCH" 2>/dev/null >> "$b_out"
    CMPBENCH_LIB="$CAND" "$BENCH" 2>/dev/null >> "$c_out"
    echo "#   round $r done" >&2
done

BASE="$BASE" CAND="$CAND" ROUNDS="$ROUNDS" python3 - "$b_out" "$c_out" <<'PY'
import sys, os, statistics as st
from collections import defaultdict, OrderedDict

# Phases whose metric is a rate (higher is better). Everything else is a cost
# per unit of work, where lower is better.
HIGHER_BETTER = {"MB/s"}

def load(path):
    vals, units = defaultdict(list), {}
    for line in open(path):
        parts = line.rstrip("\n").split("\t")
        if len(parts) != 3:
            continue
        name, value, unit = parts
        try:
            vals[name].append(float(value))
        except ValueError:
            continue
        units[name] = unit
    return vals, units

bv, bu = load(sys.argv[1])
cv, cu = load(sys.argv[2])

order = list(OrderedDict.fromkeys(list(bv.keys()) + list(cv.keys())))
if not order:
    print("no results parsed - did both libraries run?", file=sys.stderr)
    sys.exit(1)

print()
print(f"baseline : {os.environ['BASE']}")
print(f"candidate: {os.environ['CAND']}")
print(f"rounds   : {os.environ['ROUNDS']} (median; range in brackets)")
print()
hdr = f"{'phase':<14}{'unit':<9}{'baseline':>20}{'candidate':>20}{'ratio':>8}"
print(hdr); print("-" * len(hdr))

slower, faster = [], []
for name in order:
    b, c = bv.get(name, []), cv.get(name, [])
    if not b or not c:
        print(f"{name:<14}{(bu.get(name) or cu.get(name) or ''):<9}"
              f"{'(missing on one side)':>41}")
        continue
    unit = bu.get(name, cu.get(name, ""))
    bm, cm = st.median(b), st.median(c)
    if bm == 0.0 or cm == 0.0:
        print(f"{name:<14}{unit:<9}{bm:>20.2f}{cm:>20.2f}{'   n/a':>8}  below timer resolution")
        continue
    # ratio > 1 always means "candidate costs more"
    ratio = (bm / cm) if unit in HIGHER_BETTER else (cm / bm)
    bs = f"{bm:.2f} [{min(b):.2f}-{max(b):.2f}]"
    cs = f"{cm:.2f} [{min(c):.2f}-{max(c):.2f}]"
    # Only call it either way when the medians differ by more than the noisier
    # side's own spread.
    bspread = (max(b) - min(b)) / bm
    cspread = (max(c) - min(c)) / cm
    noise = max(bspread, cspread)
    tag = ""
    if abs(ratio - 1.0) <= noise:
        tag = "  ~parity (within noise)"
    elif ratio > 1.0:
        tag = "  SLOWER"; slower.append((name, ratio))
    else:
        tag = "  faster"; faster.append((name, ratio))
    print(f"{name:<14}{unit:<9}{bs:>20}{cs:>20}{ratio:>8.2f}{tag}")

print()
if slower:
    print("candidate slower, worst first:")
    for n, r in sorted(slower, key=lambda x: -x[1]):
        print(f"  {n:<14} {r:.2f}x")
else:
    print("candidate is at parity or faster on every phase that cleared noise.")
if faster:
    print("candidate faster: " + ", ".join(f"{n} {1.0/r:.2f}x" for n, r in
                                           sorted(faster, key=lambda x: x[1])))
PY
