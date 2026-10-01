#!/usr/bin/env bash
# Build the macOS installer for OJU (VST3 + AU + Standalone): one .pkg, double-click to install.
#
#   installer/mac/make-pkg.sh 2.0.0
#
# Signed + notarised (for selling): set DEV_APP / DEV_INST to your Developer ID certificates
# and store a notarytool profile once:
#   xcrun notarytool store-credentials OJU_NOTARY --apple-id you@example.com --team-id TEAMID
#   DEV_APP="Developer ID Application: Your Name (TEAMID)" \
#   DEV_INST="Developer ID Installer: Your Name (TEAMID)" installer/mac/make-pkg.sh 2.0.0
#
# Without them (CI test builds) the package is unsigned: macOS asks once before opening it
# (see docs/INSTALL.md). The plugins it installs load normally.
#
# OUT points at the built plugins (default: build-mac/OJU_artefacts/Release).
set -euo pipefail
cd "$(dirname "$0")/../.."
VERSION="${1:-2.0.0}"
OUT="${OUT:-build-mac/OJU_artefacts/Release}"
STAGE="${STAGE:-build-mac/pkg}"
DEV_APP="${DEV_APP:-}"
DEV_INST="${DEV_INST:-}"
HERE=installer/mac
rm -rf "$STAGE" && mkdir -p "$STAGE"/{vst3,au,app,res} dist

cp -R "$OUT/VST3/OJU.vst3" "$STAGE/vst3/"
cp -R "$OUT/AU/OJU.component" "$STAGE/au/"
cp -R "$OUT/Standalone/OJU.app" "$STAGE/app/"

# 1. Sign the bundles: Developer ID with hardened runtime when available, else ad-hoc
for b in "$STAGE/vst3/OJU.vst3" "$STAGE/au/OJU.component" "$STAGE/app/OJU.app"; do
    if [ -n "$DEV_APP" ]; then
        codesign --force --deep --timestamp --options runtime --sign "$DEV_APP" "$b"
    else
        codesign --force --deep --sign - "$b"
    fi
done

# 2. One component package per format. The AU package refreshes the Audio Unit cache
#    afterwards, so Logic / GarageBand see OJU without a restart.
#    Bundles are not relocatable: always install to the standard folders, even if an old
#    copy of OJU sits somewhere else on the Mac.
component_plist() {   # root -> plist
    pkgbuild --analyze --root "$1" "$2" >/dev/null
    local i=0
    while plutil -replace "$i.BundleIsRelocatable" -bool NO "$2" 2>/dev/null; do i=$((i + 1)); done
}
for part in vst3 au app; do component_plist "$STAGE/$part" "$STAGE/$part.plist"; done

pkgbuild --root "$STAGE/vst3" --component-plist "$STAGE/vst3.plist" --install-location "/Library/Audio/Plug-Ins/VST3" \
         --identifier com.madebyjoseph.oju.vst3 --version "$VERSION" "$STAGE/OJU-VST3.pkg"
pkgbuild --root "$STAGE/au" --component-plist "$STAGE/au.plist" --install-location "/Library/Audio/Plug-Ins/Components" \
         --scripts "$HERE/scripts" \
         --identifier com.madebyjoseph.oju.au --version "$VERSION" "$STAGE/OJU-AU.pkg"
pkgbuild --root "$STAGE/app" --component-plist "$STAGE/app.plist" --install-location "/Applications" \
         --identifier com.madebyjoseph.oju.app --version "$VERSION" "$STAGE/OJU-App.pkg"

# 3. Product archive with OJU's welcome and finish pages
sed "s/@VERSION@/$VERSION/g" "$HERE/distribution.xml" > "$STAGE/distribution.xml"
sed "s/@VERSION@/$VERSION/g" "$HERE/resources/welcome.html" > "$STAGE/res/welcome.html"
cp "$HERE/resources/conclusion.html" "$STAGE/res/"
PKG="dist/OJU-$VERSION-macOS.pkg"
SIGN=()
[ -n "$DEV_INST" ] && SIGN=(--sign "$DEV_INST")
productbuild --distribution "$STAGE/distribution.xml" --package-path "$STAGE" \
             --resources "$STAGE/res" ${SIGN[@]+"${SIGN[@]}"} "$PKG"

# 4. Notarise and staple (signed builds only)
if [ -n "$DEV_INST" ]; then
    xcrun notarytool submit "$PKG" --keychain-profile OJU_NOTARY --wait
    xcrun stapler staple "$PKG"
fi
echo "Ready: $PKG"
