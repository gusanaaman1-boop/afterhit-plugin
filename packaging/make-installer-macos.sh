#!/usr/bin/env bash
#
# Builds the macOS delivery for AFTERHIT, from a clean Release build:
#
#   dist/AFTERHIT-<ver>-macOS.zip
#       AFTERHIT-<ver>.pkg            installer (VST3 / AU / Standalone, each deselectable)
#       UNINSTALL-AFTERHIT.command    double-click to remove everything it installed
#       AFTERHIT-MANUAL.md            controls, presets, notes
#
# pkgbuild / productbuild ship with macOS: nothing downloaded, nothing paid.
# NOT signed or notarised (paid certificate) - macOS may ask for right-click >
# Open the first time. DAW-loaded plug-ins are unaffected.

set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
VERSION="$(sed -n 's/^project(AfterHit VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
ID="com.naaman.afterhit"
ART="$ROOT/build/AfterHit_artefacts/Release"
STAGE="$ROOT/build-installer"
DIST="$ROOT/dist"
OUT="AFTERHIT-$VERSION-macOS"

echo "AFTERHIT $VERSION - macOS installer"

cmake --build build --config Release --target AfterHit_VST3 AfterHit_AU AfterHit_Standalone -j >/dev/null

for f in VST3/AFTERHIT.vst3/Contents/MacOS/AFTERHIT AU/AFTERHIT.component/Contents/MacOS/AFTERHIT \
         Standalone/AFTERHIT.app/Contents/MacOS/AFTERHIT; do
    [ -e "$ART/$f" ] || { echo "MISSING: $ART/$f"; exit 1; }
    case "$(lipo -archs "$ART/$f")" in *arm64*x86_64*|*x86_64*arm64*) ;; *) echo "NOT UNIVERSAL: $f"; exit 1;; esac
done

rm -rf "$STAGE" "$DIST"
mkdir -p "$STAGE/roots/vst3/Library/Audio/Plug-Ins/VST3" "$STAGE/roots/au/Library/Audio/Plug-Ins/Components" \
         "$STAGE/roots/app/Applications" "$STAGE/pkgs" "$STAGE/resources" "$STAGE/$OUT" "$DIST"
cp -R "$ART/VST3/AFTERHIT.vst3"      "$STAGE/roots/vst3/Library/Audio/Plug-Ins/VST3/"
cp -R "$ART/AU/AFTERHIT.component"   "$STAGE/roots/au/Library/Audio/Plug-Ins/Components/"
cp -R "$ART/Standalone/AFTERHIT.app" "$STAGE/roots/app/Applications/"

for c in vst3 au app; do
    pkgbuild --quiet --root "$STAGE/roots/$c" --identifier "$ID.$c" --version "$VERSION" \
             --install-location / "$STAGE/pkgs/$c.pkg"
done

cat > "$STAGE/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>AFTERHIT $VERSION - by Gussa Naaman</title>
    <organization>com.naaman</organization>
    <options customize="always" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <welcome file="welcome.html" mime-type="text/html"/>
    <choices-outline><line choice="vst3"/><line choice="au"/><line choice="app"/></choices-outline>
    <choice id="vst3" title="VST3" description="For Cubase, Live, Reaper, Bitwig."><pkg-ref id="$ID.vst3"/></choice>
    <choice id="au" title="Audio Unit" description="For Logic Pro and GarageBand."><pkg-ref id="$ID.au"/></choice>
    <choice id="app" title="Standalone application" description="AFTERHIT without a host."><pkg-ref id="$ID.app"/></choice>
    <pkg-ref id="$ID.vst3" version="$VERSION" onConclusion="none">vst3.pkg</pkg-ref>
    <pkg-ref id="$ID.au"   version="$VERSION" onConclusion="none">au.pkg</pkg-ref>
    <pkg-ref id="$ID.app"  version="$VERSION" onConclusion="none">app.pkg</pkg-ref>
</installer-gui-script>
XML

cat > "$STAGE/resources/welcome.html" <<HTML
<html><body style="font-family:-apple-system;font-size:13px">
<p><b>AFTERHIT $VERSION</b> - the hit stays sharp and close; the space blooms after it.<br>
by <b>Gussa Naaman</b></p>
<p>Installs to /Library/Audio/Plug-Ins (VST3, Audio Unit) and /Applications.</p>
<p>This build is <b>not signed or notarised</b>. macOS may warn the first time you open the
standalone application; the plug-ins load normally in a host.</p>
<p>To remove it later, double-click <b>UNINSTALL-AFTERHIT.command</b> from the same zip.</p>
</body></html>
HTML

productbuild --quiet --distribution "$STAGE/distribution.xml" --package-path "$STAGE/pkgs" \
             --resources "$STAGE/resources" "$STAGE/$OUT/AFTERHIT-$VERSION.pkg"

# --- uninstaller -----------------------------------------------------------------
cat > "$STAGE/$OUT/UNINSTALL-AFTERHIT.command" <<'SH'
#!/bin/bash
# Removes AFTERHIT (installer copies in /Library and /Applications, and any
# developer copies in ~/Library). Asks for your password for the system folders.
T=( "/Library/Audio/Plug-Ins/VST3/AFTERHIT.vst3" "/Library/Audio/Plug-Ins/Components/AFTERHIT.component"
    "/Applications/AFTERHIT.app" "$HOME/Library/Audio/Plug-Ins/VST3/AFTERHIT.vst3"
    "$HOME/Library/Audio/Plug-Ins/Components/AFTERHIT.component" )
echo "AFTERHIT uninstaller"; found=0
for t in "${T[@]}"; do
  if [ -e "$t" ]; then found=1
    if [ -w "$(dirname "$t")" ]; then rm -rf "$t"; else sudo rm -rf "$t"; fi
    [ -e "$t" ] && echo "  COULD NOT REMOVE  $t" || echo "  removed  $t"
  fi
done
for id in com.naaman.afterhit.vst3 com.naaman.afterhit.au com.naaman.afterhit.app; do
  pkgutil --pkg-info "$id" >/dev/null 2>&1 && sudo pkgutil --forget "$id" >/dev/null && echo "  forgot receipt  $id"
done
[ "$found" = 0 ] && echo "  nothing was installed."
echo; read -n 1 -s -r -p "Done. Press any key to close."
SH
chmod +x "$STAGE/$OUT/UNINSTALL-AFTERHIT.command"

{ cat README.md; echo; echo "## Parameters and presets"; echo; cat docs/PARAMETERS.md; } > "$STAGE/$OUT/AFTERHIT-MANUAL.md"

# --- verify ------------------------------------------------------------------------
for c in vst3 au app; do
    n=$(pkgutil --payload-files "$STAGE/pkgs/$c.pkg" | wc -l | tr -d ' ')
    echo "  $c.pkg: $n files"; [ "$n" -gt 1 ] || { echo "EMPTY COMPONENT $c"; exit 1; }
done

(cd "$STAGE" && ditto -c -k --norsrc --noextattr --keepParent "$OUT" "$DIST/$OUT.zip")
unzip -l "$DIST/$OUT.zip" | tail -n +1 | grep -E "pkg|command|MANUAL"
ls -lh "$DIST"
