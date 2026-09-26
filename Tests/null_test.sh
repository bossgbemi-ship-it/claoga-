#!/usr/bin/env bash
# -----------------------------------------------------------------------------
#  OJU v1 <-> v2 null test (regression guard for "protect what works")
#
#  Renders every dry take through the frozen OJU v1 build and through the
#  current build with all v2 modules off (by loading the v1 preset), and fails
#  unless every output is bit-identical.
#
#  Usage: Tests/null_test.sh [build-dir]
#  Env:   V1_REF    git ref of v1 (default: the OJU v1 release commit)
#         JUCE_DIR  optional local JUCE checkout (skips the download)
# -----------------------------------------------------------------------------
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/build-null}"
V1_REF="${V1_REF:-d94ea2211ee1f780b1bcb39dedd7f6a9cce012ae}"   # OJU v1 (tag oju-v1.0.0)
V1_DIR="$BUILD/v1-src"
WORK="$BUILD/null-work"

rm -rf "$WORK"
mkdir -p "$BUILD" "$WORK/takes" "$WORK/out"
if [ ! -d "$V1_DIR/Source" ]; then
    git -C "$ROOT" worktree add --detach "$V1_DIR" "$V1_REF" >/dev/null 2>&1 \
      || { rm -rf "$V1_DIR"; git -C "$ROOT" archive "$V1_REF" Source | (mkdir -p "$V1_DIR" && tar -x -C "$V1_DIR"); }
fi

EXTRA=()
[ -n "${JUCE_DIR:-}" ] && EXTRA+=(-DFETCHCONTENT_SOURCE_DIR_JUCE="$JUCE_DIR")
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DOJU_BUILD_TESTS=ON -DOJU_V1_SOURCE_DIR="$V1_DIR" ${EXTRA[@]+"${EXTRA[@]}"} >/dev/null
cmake --build "$BUILD" --config Release --target OJURender OJURenderV1 OJUTests --parallel 4 >/dev/null

find_bin() { find "$BUILD" -type f \( -name "$1" -o -name "$1.exe" \) -path "*artefacts*" | head -1; }
R1="$(find_bin OJURenderV1)"; R2="$(find_bin OJURender)"; T="$(find_bin OJUTests)"

# Synthetic takes (always) + your own takes from testdata/takes
"$T" --write-vocal "$WORK/takes/synth_48k_mono.wav" 12 48000 1 >/dev/null
"$T" --write-vocal "$WORK/takes/synth_44k_stereo.wav" 12 44100 2 >/dev/null
TAKES=("$WORK"/takes/*.wav)
shopt -s nullglob
for f in "$ROOT"/testdata/takes/*.wav "$ROOT"/testdata/takes/*.WAV; do TAKES+=("$f"); done

# v1 configurations: default preset, and presets made by v1's own Listen in several styles/modes
CONFIGS=("default|" "afro-natural|--style 0 --mode 0 --listen" "trap-extreme|--style 1 --mode 1 --listen"
         "soul-natural|--style 4 --mode 0 --listen")
BLOCKS=(512 333)

pass=0; fail=0
for take in "${TAKES[@]}"; do
  name="$(basename "${take%.*}")"
  for cfg in "${CONFIGS[@]}"; do
    label="${cfg%%|*}"; opts="${cfg#*|}"
    for block in "${BLOCKS[@]}"; do
      base="$WORK/out/${name}_${label}_${block}"
      # shellcheck disable=SC2086
      "$R1" --in "$take" --out "$base.v1.wav" --block "$block" $opts --save-state "$base.v1state" >/dev/null
      "$R2" --in "$take" --out "$base.v2.wav" --block "$block" --load-state "$base.v1state" >/dev/null
      if result="$("$R2" --compare "$base.v1.wav" "$base.v2.wav")"; then
        pass=$((pass+1)); echo "  [null] $name / $label / block $block: $result"
      else
        fail=$((fail+1)); echo "  [FAIL] $name / $label / block $block: $result"
      fi
    done
  done
done

echo
echo "Null test: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
