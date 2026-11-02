#!/usr/bin/env bash
# Baut aus Wine-Runtime + fertig installiertem Prefix eine „Solid Edge 2026.app“ und packt sie in eine DMG.
#   ./build-dmg.sh         DMG nach dist/ (für weitere eigene Macs – nicht öffentlich weitergeben)
#   ./build-dmg.sh --app   nur die App nach /Programme (bzw. ~/Programme) legen, ohne DMG
# Die App wird klein gebaut (Starter, Wartefenster, Symbol, Deinstallations-App): macOS prüft beim ersten Öffnen jedes
# Byte im Bundle, bei 14 GB dauert das über eine Minute ohne jede Anzeige. Wine und die Windows-Umgebung liegen deshalb
# als „Inhalt“ daneben – bei --app im Arbeitsordner (…/SolidEdge2026/neu, APFS-Klon), in der DMG im versteckten Ordner
# .solidedge-inhalt. Der Starter übernimmt ihn beim ersten Start (mit Wartefenster und Fortschritt): Wine in die App,
# die Windows-Umgebung je nach gewähltem Modus in den Arbeitsordner (installiert) oder ebenfalls in die App (portabel).
# Env: SE_APP_ZIEL (Zielordner für --app), SE_DATA_DIR (Arbeitsordner) – für Tests.
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
NUR_APP=0; [ "${1:-}" = "--app" ] && NUR_APP=1
DATA="${SE_DATA_DIR:-$HOME/Library/Application Support/SolidEdge2026}"
if [ "$NUR_APP" = 1 ]; then INHALT="$DATA/neu"; else INHALT="$STAGE/.solidedge-inhalt"; fi
STAND="$(date +%Y%m%d-%H%M%S)-$RANDOM"   # Bau-Kennung: verbindet App und Inhalt
FORMAT="${DMG_FORMAT:-ULFO}"   # ULFO = LZFSE (schnell, gut), ULMO = LZMA (kleiner, deutlich langsamer), UDZO = zlib

[ -f "$SE_EXE" ] || { echo "Edge.exe fehlt – erst ./install.sh ausführen." >&2; exit 1; }

step "Wine beenden, damit der Prefix in Ruhe ist"
"$WINESERVER" -k 2>/dev/null || true
"$WINESERVER" -w 2>/dev/null || true

step "Bundle-Gerüst anlegen ($APP)"
rm -rf "$SE_ROOT/build" "$INHALT"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$INHALT"
cp "$SE_ROOT/app/launcher.sh" "$APP/Contents/MacOS/SolidEdge2026"
chmod +x "$APP/Contents/MacOS/SolidEdge2026"
cp "$SE_ROOT/app/Info.plist" "$APP/Contents/Info.plist"
cp "$SE_ROOT/app/warten.js" "$APP/Contents/Resources/warten.js"   # Wartefenster beim Start
# „Solid Edge deinstallieren.app“ – der Starter legt sie im Modus „Installiert“ neben die App.
D="$APP/Contents/Resources/Solid Edge deinstallieren.app"
mkdir -p "$D/Contents/MacOS" "$D/Contents/Resources"
cp "$SE_ROOT/app/deinstallieren.sh" "$D/Contents/MacOS/deinstallieren"
chmod +x "$D/Contents/MacOS/deinstallieren"
cp "$SE_ROOT/app/Deinstallieren-Info.plist" "$D/Contents/Info.plist"
# App-Symbol aus der installierten Edge.exe (das Siemens-Symbol liegt bewusst nicht im Repo).
"$SE_ROOT/app/icon-erzeugen.sh" "$SE_EXE" "$APP/Contents/Resources/AppIcon.icns" \
  || echo "Hinweis: kein App-Symbol erzeugt (7z fehlt?) – die App bekommt das Standardsymbol."

step "Wine-Runtime übernehmen (APFS-Klon)"
cp -cR "$SE_ROOT/runtime/Wine Staging.app/Contents/Resources/wine" "$INHALT/wine"

step "Prefix als Vorlage übernehmen (APFS-Klon) und aufräumen"
cp -cR "$WINEPREFIX" "$INHALT/prefix"
P="$INHALT/prefix"
U="$(basename "$(find "$P/drive_c/users" -mindepth 1 -maxdepth 1 -type d ! -name Public | head -1)")"
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

step "Persönliche Daten entfernen (Benutzername, Rechnername, Zuletzt-geöffnet-Listen, Mac-Pfade, Browser-Profile)"
# Exit-Code ≠ 0 = persönliche Daten noch im Prefix (Dateien stehen in der Ausgabe) → keine DMG bauen.
$NIEDRIG python3 -I "$SE_ROOT/app/prefix-anonymisieren.py" "$P" "$U" user \
  || { echo "Abbruch: persönliche Daten im Prefix (siehe oben) – app/prefix-anonymisieren.py erweitern." >&2; exit 1; }
echo "user" > "$APP/Contents/Resources/prefix-user.txt"

# Kennung zuletzt schreiben – erst dann gilt der Inhalt als vollständig.
echo "$STAND" > "$APP/Contents/Resources/stand.txt"
echo "$STAND" > "$INHALT/stand.txt"

if [ "$NUR_APP" = 1 ]; then
  ZIEL="${SE_APP_ZIEL:-/Applications}"; [ -w "$ZIEL" ] || { ZIEL="$HOME/Applications"; mkdir -p "$ZIEL"; }
  step "App nach $ZIEL legen"
  rm -rf "$ZIEL/$APP_NAME.app"
  mv "$APP" "$ZIEL/"
  rm -rf "$SE_ROOT/build"
  step "Fertig: $ZIEL/$APP_NAME.app (Inhalt in $INHALT – der erste Start übernimmt ihn)"
  exit 0
fi

step "Link auf /Programme"
ln -s /Applications "$STAGE/Applications"

step "DMG bauen ($FORMAT) – dauert"
mkdir -p "$DIST"
rm -f "$DMG"
$NIEDRIG hdiutil create -volname "$APP_NAME" -srcfolder "$STAGE" -fs APFS -format "$FORMAT" -ov "$DMG"
rm -rf "$SE_ROOT/build"

step "Fertig: $DMG ($(du -h "$DMG" | cut -f1))"
