#!/usr/bin/env bash
# Baut aus Wine-Runtime + fertig installiertem Prefix eine „Solid Edge 2026.app“ und packt sie in eine DMG.
# Voraussetzung: ./install.sh ist durchgelaufen und Solid Edge startet mit ./start.sh.
# Installer-Ordner und ZIP werden NICHT eingepackt.
set -euo pipefail
source "$(dirname "$0")/env.sh"

step() { printf '\n==> %s\n' "$*"; }

APP_NAME="Solid Edge 2026"
DIST="$SE_ROOT/dist"
STAGE="$SE_ROOT/build/dmg-root"
APP="$STAGE/$APP_NAME.app"
DMG="$DIST/SolidEdge2026-macOS.dmg"
FORMAT="${DMG_FORMAT:-ULFO}"   # ULFO = LZFSE (schnell, gut), ULMO = LZMA (kleiner, deutlich langsamer), UDZO = zlib

[ -f "$SE_EXE" ] || { echo "Edge.exe fehlt – erst ./install.sh ausführen." >&2; exit 1; }

step "Wine beenden, damit der Prefix in Ruhe ist"
"$WINESERVER" -k 2>/dev/null || true
"$WINESERVER" -w 2>/dev/null || true

step "Bundle-Gerüst anlegen ($APP)"
rm -rf "$SE_ROOT/build"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$SE_ROOT/app/launcher.sh" "$APP/Contents/MacOS/SolidEdge2026"
chmod +x "$APP/Contents/MacOS/SolidEdge2026"
cp "$SE_ROOT/app/Info.plist" "$APP/Contents/Info.plist"
[ -f "$SE_ROOT/app/AppIcon.icns" ] && cp "$SE_ROOT/app/AppIcon.icns" "$APP/Contents/Resources/"

step "Wine-Runtime übernehmen (APFS-Klon)"
cp -cR "$SE_ROOT/runtime/Wine Staging.app/Contents/Resources/wine" "$APP/Contents/Resources/wine"

step "Prefix als Vorlage übernehmen (APFS-Klon) und aufräumen"
cp -cR "$WINEPREFIX" "$APP/Contents/Resources/prefix"
P="$APP/Contents/Resources/prefix"
U="$(basename "$(find "$P/drive_c/users" -mindepth 1 -maxdepth 1 -type d ! -name Public | head -1)")"
echo "$U" > "$APP/Contents/Resources/prefix-user.txt"
echo "${SE_EXE#$WINEPREFIX/drive_c/}" > "$APP/Contents/Resources/edge-path.txt"
rm -f "$P/.se-ready"
# Temp, Logs, Caches, Installer-Cache und Laufwerks-Links (zeigen auf diesen Rechner) entfernen.
rm -rf "$P/drive_c/users/$U/AppData/Local/Temp/"* \
       "$P/drive_c/users/$U/Temp/"* \
       "$P/drive_c/windows/temp/"* \
       "$P/drive_c/windows/Installer/"*.tmp \
       "$P/drive_c/windows/Logs/"* \
       "$P/drive_c/ProgramData/Package Cache" \
       "$P/dosdevices" \
       "$P/drive_c/shaders" "$P/drive_c/secapture" "$P/drive_c/setrace" "$P/drive_c/skizze-test.par"   # Diagnose (Mesa-Shader-Mitschnitt) bzw. MCP-Bildwerkzeug
find "$P/drive_c/users/$U" -maxdepth 1 -type l -delete
find "$P/drive_c" -iname '*.log' -path '*Temp*' -delete 2>/dev/null || true

step "Link auf /Programme"
ln -s /Applications "$STAGE/Applications"

step "DMG bauen ($FORMAT) – dauert"
mkdir -p "$DIST"
rm -f "$DMG"
$NIEDRIG hdiutil create -volname "$APP_NAME" -srcfolder "$STAGE" -fs APFS -format "$FORMAT" -ov "$DMG"
rm -rf "$SE_ROOT/build"

step "Fertig: $DMG ($(du -h "$DMG" | cut -f1))"
