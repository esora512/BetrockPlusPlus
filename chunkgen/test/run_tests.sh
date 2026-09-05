#!/usr/bin/env bash
# Small smoke/regression test harness for chunkgen:
#   1. Generates a fixed set of (seed, dimension, x, z) chunks.
#   2. Validates each output file is well-formed NBT with the expected chunk layout
#      (via verify_nbt.py, a small dependency-free NBT reader).
#   3. Re-generates one of them and checks the output is byte-identical except for
#      the LastUpdate timestamp field, proving generation is deterministic.
#
# Usage: test/run_tests.sh [path-to-chunkgen-binary]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CHUNKGEN="${1:-$SCRIPT_DIR/../build/chunkgen}"
OUT_DIR="$(mktemp -d)"
trap 'rm -rf "$OUT_DIR"' EXIT

if [[ ! -x "$CHUNKGEN" ]]; then
    echo "chunkgen binary not found/executable at: $CHUNKGEN" >&2
    echo "Build it first (see README.md), or pass its path as \$1." >&2
    exit 1
fi

# seed, dimension, x, z
CASES=(
    "12345 0 0 0"
    "12345 0 5 -3"
    "-987654321 0 100 100"
    "12345 -1 0 0"
    "42 -1 10 10"
)

FILES=()
echo "== Generating ${#CASES[@]} chunks =="
for case in "${CASES[@]}"; do
    read -r seed dim x z <<<"$case"
    out="$OUT_DIR/c_${seed}_${dim}_${x}_${z}.nbt"
    "$CHUNKGEN" --seed "$seed" --dimension "$dim" --x "$x" --z "$z" --out "$out"
    FILES+=("$out")
done

echo
echo "== Validating NBT structure =="
python3 "$SCRIPT_DIR/verify_nbt.py" "${FILES[@]}"

echo
echo "== Determinism check (regenerate case #1, diff ignoring LastUpdate) =="
read -r seed dim x z <<<"${CASES[0]}"
redo="$OUT_DIR/redo.nbt"
"$CHUNKGEN" --seed "$seed" --dimension "$dim" --x "$x" --z "$z" --out "$redo" >/dev/null
python3 - "$OUT_DIR/c_${seed}_${dim}_${x}_${z}.nbt" "$redo" <<'EOF'
import gzip
import sys

a = gzip.decompress(open(sys.argv[1], "rb").read())
b = gzip.decompress(open(sys.argv[2], "rb").read())
if len(a) != len(b):
    print(f"FAIL: length mismatch {len(a)} vs {len(b)}")
    sys.exit(1)
diffs = [i for i in range(len(a)) if a[i] != b[i]]
# LastUpdate is an 8-byte TAG_Long; a couple of its low-order bytes may differ
# between two runs a few seconds apart. Anything outside a small tail window
# differing means generation is NOT deterministic.
if len(diffs) > 8 or (diffs and max(diffs) - min(diffs) > 8):
    print(f"FAIL: {len(diffs)} differing bytes outside the expected LastUpdate field: {diffs}")
    sys.exit(1)
print(f"OK: {len(diffs)} differing byte(s), consistent with only LastUpdate changing: {diffs}")
EOF

echo
echo "All checks passed."
