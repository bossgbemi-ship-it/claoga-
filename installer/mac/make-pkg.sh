#!/usr/bin/env bash
# Build a signed, notarised macOS installer for OJU (VST3 + AU + Standalone).
# Prerequisites: ./build-mac.sh has been run; you have "Developer ID Application" and
# "Developer ID Installer" certificates in your keychain and a notarytool keychain profile:
#   xcrun notarytool store-credentials OJU_NOTARY --apple-id you@example.com --team-id TEAMID
#
# Usage: DEV_APP="Developer ID Application: Your Name (TEAMID)" \
#        DEV_INST="Developer ID Installer: Your Name (TEAMID)" \
#        installer/mac/make-pkg.sh 1.0.0
set -euo pipefail
cd "$(dirname "$0")/../.."
VERSION="${1:-1.0.0}"
OUT=build-mac/OJU_artefacts/Release
STAGE=build-mac/pkg
rm -rf "$STAGE" && mkdir -p "$STAGE"/{vst3,au,app} dist

# 1. Sign the bundles with hardened runtime (required for notarisation)
for b in "$OUT/VST3/OJU.vst3" "$OUT/AU/OJU.component" "$OUT/Standalone/OJU.app"; do
    codesign --force --deep --timestamp --options runtime --sign "$DEV_APP" "$b"
done

cp -R "$OUT/VST3/OJU.vst3" "$STAGE/vst3/"
cp -R "$OUT/AU/OJU.component" "$STAGE/au/"
cp -R "$OUT/Standalone/OJU.app" "$STAGE/app/"

# 2. One component package per format
pkgbuild --root "$STAGE/vst3" --install-location "/Library/Audio/Plug-Ins/VST3" \
         --identifier com.madebyjoseph.oju.vst3 --version "$VERSION" "$STAGE/OJU-VST3.pkg"
pkgbuild --root "$STAGE/au" --install-location "/Library/Audio/Plug-Ins/Components" \
         --identifier com.madebyjoseph.oju.au --version "$VERSION" "$STAGE/OJU-AU.pkg"
pkgbuild --root "$STAGE/app" --install-location "/Applications" \
         --identifier com.madebyjoseph.oju.app --version "$VERSION" "$STAGE/OJU-App.pkg"

# 3. Product archive, signed with the Installer certificate
productbuild --synthesize --package "$STAGE/OJU-VST3.pkg" --package "$STAGE/OJU-AU.pkg" \
             --package "$STAGE/OJU-App.pkg" "$STAGE/distribution.xml"
productbuild --distribution "$STAGE/distribution.xml" --package-path "$STAGE" \
             --sign "$DEV_INST" "dist/OJU-$VERSION-macOS.pkg"

# 4. Notarise and staple
xcrun notarytool submit "dist/OJU-$VERSION-macOS.pkg" --keychain-profile OJU_NOTARY --wait
xcrun stapler staple "dist/OJU-$VERSION-macOS.pkg"
echo "Ready: dist/OJU-$VERSION-macOS.pkg"
