#!/usr/bin/env bash
# -----------------------------------------------------------------------------
#  OJU - macOS build (VST3 + AU + Standalone, universal arm64 + x86_64)
#
#  Needs: Xcode (or the Xcode Command Line Tools), CMake 3.22+, Git.
#         brew install cmake
#
#  Usage: ./build-mac.sh            build Release
#         ./build-mac.sh install    build, then copy into ~/Library/Audio/Plug-Ins
# -----------------------------------------------------------------------------
set -euo pipefail
cd "$(dirname "$0")"

command -v cmake >/dev/null || { echo "CMake not found: brew install cmake"; exit 1; }

GENERATOR=()
if xcodebuild -version >/dev/null 2>&1; then
    GENERATOR=(-G Xcode)
fi

cmake -S . -B build-mac "${GENERATOR[@]}" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13
cmake --build build-mac --config Release --parallel

OUT="build-mac/OJU_artefacts/Release"
# Ad-hoc sign so Apple Silicon will load locally built plugins (replace with your
# Developer ID when you ship - see docs/SHIPPING.md).
for b in "$OUT/VST3/OJU.vst3" "$OUT/AU/OJU.component" "$OUT/Standalone/OJU.app"; do
    [ -e "$b" ] && codesign --force --deep --sign - "$b" >/dev/null 2>&1 || true
done

echo
echo "Built OK ($(lipo -archs "$OUT/VST3/OJU.vst3/Contents/MacOS/OJU" 2>/dev/null || echo universal))"
echo "  VST3        $OUT/VST3/OJU.vst3"
echo "  AU          $OUT/AU/OJU.component"
echo "  Standalone  $OUT/Standalone/OJU.app"

if [ "${1:-}" = "install" ]; then
    mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components
    rm -rf ~/Library/Audio/Plug-Ins/VST3/OJU.vst3 ~/Library/Audio/Plug-Ins/Components/OJU.component
    cp -R "$OUT/VST3/OJU.vst3" ~/Library/Audio/Plug-Ins/VST3/
    cp -R "$OUT/AU/OJU.component" ~/Library/Audio/Plug-Ins/Components/
    # make Logic / GarageBand notice the new AU
    killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
    echo "Installed to ~/Library/Audio/Plug-Ins (VST3 + Components)."
    echo "Check the AU with:  auval -v aufx Ojue Mbyj"
fi
