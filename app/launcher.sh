#!/usr/bin/env bash
# Starter der „Solid Edge 2026.app“ (liegt im Bundle als Contents/MacOS/SolidEdge2026).
# Beim ersten Start wird der mitgelieferte Prefix (read-only im Bundle/DMG) nach
# ~/Library/Application Support/SolidEdge2026 kopiert und auf den aktuellen Benutzer umgestellt.
# Während des Starts zeigt ein kleines Wartefenster (warten.js) an, dass etwas passiert; es schließt sich, sobald
# Solid Edge sein erstes Fenster zeigt. SE_NUR_EINRICHTEN=1: nur den Arbeitsordner einrichten, nicht starten
# (setup.sh nutzt das, damit der erste Start schneller geht).
set -euo pipefail

APP_CONTENTS="$(cd "$(dirname "$0")/.." && pwd)"
RES="$APP_CONTENTS/Resources"
WINE_BIN="$RES/wine/bin"

# Zielordner per Env-Variable umbiegbar (für Tests).
DATA="${SE_DATA_DIR:-$HOME/Library/Application Support/SolidEdge2026}"
PREFIX="$DATA/prefix"
LOG="$DATA/start.log"
TEMPLATE_USER="$(cat "$RES/prefix-user.txt")"
ME="${USER:-$(id -un)}"

export PATH="$WINE_BIN:$PATH"
export WINEPREFIX="$PREFIX"
export WINEDEBUG="${WINEDEBUG:--all}"
export WINEMSYNC=1
export GALLIUM_DRIVER="${GALLIUM_DRIVER:-zink}"   # 3D-Ansicht über die GPU (Mesa-Zink), llvmpipe = CPU
# opengl32-Vorschaltung (opengl-wrapper/): Ansichten-Tabelle von view.dvx an Wines CRITICAL_SECTION-Semantik
# anpassen (sonst kein HUD: kein Ansichtswürfel, kein Auswahlrechteck). Schalter siehe README.
export MEMDC_GL_HUD_VIEW="${MEMDC_GL_HUD_VIEW:-fix}"
# Wines OleSetMenuDescriptor meldet beim zweiten Entfernen E_FAIL → „Skizze schließen“ bricht ab (docs/skizze-schliessen.md).
export MEMDC_GL_OLEMENU="${MEMDC_GL_OLEMENU:-fix}"

mkdir -p "$DATA"
exec >>"$LOG" 2>&1
echo "=== $(date) Start (Benutzer $ME, Daten $DATA)"

if ! arch -x86_64 /usr/bin/true 2>/dev/null; then
  osascript -e 'display alert "Solid Edge 2026" message "Rosetta 2 fehlt. Bitte im Terminal ausführen: softwareupdate --install-rosetta"' >/dev/null 2>&1 || true
  exit 1
fi

# Status für das Wartefenster: eine Zeile je Schritt, „⏳“ läuft, „✓“ erledigt.
STATUS="$DATA/status.txt"
: > "$STATUS"
schritt() { printf '⏳  %s\n' "$*" >> "$STATUS"; echo "--- $*"; }
fertig()  { LC_ALL=C sed -i '' '$ s/^⏳/✓ /' "$STATUS" 2>/dev/null || true; }
echo "✓   macOS hat die App geprüft und gestartet" >> "$STATUS"

# Wartefenster. $$ bleibt nach dem exec unten die PID von Edge.exe – daran erkennt das Fenster, wann es gehen kann.
if [ -z "${SE_NUR_EINRICHTEN:-}" ] && [ -f "$RES/warten.js" ]; then
  if [ -f "$PREFIX/.se-ready" ]; then T1="Solid Edge startet …"; else T1="Solid Edge wird zum ersten Mal eingerichtet …"; fi
  osascript -l JavaScript "$RES/warten.js" "$$" "$T1" "$STATUS" "$LOG" </dev/null >/dev/null 2>&1 &
fi

if [ ! -f "$PREFIX/.se-ready" ]; then
  schritt "Arbeitsordner anlegen: Windows-Umgebung mit Solid Edge kopieren (~13 GB, nur beim ersten Start)"
  rm -rf "$PREFIX.tmp"
  # cp -c = APFS-Klon, wenn Quelle und Ziel auf demselben Volume liegen; sonst normale Kopie. -v zeigt jede Datei
  # in der Ausgabe des Wartefensters, damit man sieht, dass es vorangeht.
  cp -cRv "$RES/prefix" "$PREFIX.tmp" 2>/dev/null | awk 'NR % 50 == 0' \
    || { rm -rf "$PREFIX.tmp"; ditto -V "$RES/prefix" "$PREFIX.tmp" 2>&1 | awk 'NR % 50 == 0'; }
  [ -d "$PREFIX.tmp/drive_c" ] || { rm -rf "$PREFIX.tmp"; ditto -V "$RES/prefix" "$PREFIX.tmp" 2>&1 | awk 'NR % 50 == 0'; }
  chmod -R u+w "$PREFIX.tmp"
  fertig

  # Wine-Benutzerprofil auf den aktuellen macOS-Benutzer umstellen.
  schritt "Windows-Benutzer auf „$ME“ umstellen"
  if [ "$TEMPLATE_USER" != "$ME" ]; then
    [ -d "$PREFIX.tmp/drive_c/users/$TEMPLATE_USER" ] && mv "$PREFIX.tmp/drive_c/users/$TEMPLATE_USER" "$PREFIX.tmp/drive_c/users/$ME"
    for reg in "$PREFIX.tmp"/user.reg "$PREFIX.tmp"/userdef.reg "$PREFIX.tmp"/system.reg; do
      [ -f "$reg" ] || continue
      LC_ALL=C sed -i '' \
        -e "s/\\\\\\\\users\\\\\\\\$TEMPLATE_USER/\\\\\\\\users\\\\\\\\$ME/g" \
        -e "s/\\\\\\\\Users\\\\\\\\$TEMPLATE_USER/\\\\\\\\Users\\\\\\\\$ME/g" \
        -e "s/\"USERNAME\"=\"$TEMPLATE_USER\"/\"USERNAME\"=\"$ME\"/g" \
        "$reg"
      echo "angepasst: $(basename "$reg")"
    done
  fi
  fertig

  # Profilordner-Links (Desktop, Dokumente …) auf den echten Home-Ordner setzen.
  schritt "Ordner Dokumente, Schreibtisch und Downloads mit dem Mac verknüpfen"
  UDIR="$PREFIX.tmp/drive_c/users/$ME"
  for pair in "Desktop:Desktop" "Documents:Documents" "Downloads:Downloads" "Music:Music" "Pictures:Pictures" "Videos:Movies"; do
    win="${pair%%:*}"; mac="${pair##*:}"
    if [ -L "$UDIR/$win" ] || [ ! -e "$UDIR/$win" ]; then
      rm -f "$UDIR/$win"
      [ -d "$HOME/$mac" ] && ln -s "$HOME/$mac" "$UDIR/$win" || mkdir -p "$UDIR/$win"
      echo "C:\\users\\$ME\\$win -> ~/$mac"
    fi
  done

  # Laufwerke: nur C: und Z: (= /).
  rm -rf "$PREFIX.tmp/dosdevices"
  mkdir -p "$PREFIX.tmp/dosdevices"
  ln -s ../drive_c "$PREFIX.tmp/dosdevices/c:"
  ln -s / "$PREFIX.tmp/dosdevices/z:"

  rm -rf "$PREFIX"
  mv "$PREFIX.tmp" "$PREFIX"
  touch "$PREFIX/.se-ready"
  echo "Prefix eingerichtet."
  fertig
fi
# Nach einem Update der App die eigenen Grafik-DLLs in den bestehenden Arbeitsordner übernehmen
# (der Rest des Arbeitsordners – Einstellungen, Dokumente – bleibt unangetastet).
schritt "Grafik-Bibliotheken prüfen (GPU über Metal)"
PROG="drive_c/$(dirname "$(cat "$RES/edge-path.txt")")"
for f in "$PROG/opengl32.dll" "$PROG/mesaogl32.dll" "$PROG/libgallium_wgl.dll" "$PROG/libglapi.dll" \
         drive_c/windows/system32/d2d1.dll drive_c/windows/system32/d3d11.dll drive_c/windows/system32/dxgi.dll \
         drive_c/windows/system32/d3d10core.dll; do
  [ -f "$RES/prefix/$f" ] && ! cmp -s "$RES/prefix/$f" "$PREFIX/$f" && cp "$RES/prefix/$f" "$PREFIX/$f" && echo "aktualisiert: $f"
done
fertig
[ -n "${SE_NUR_EINRICHTEN:-}" ] && exit 0

EDGE="$PREFIX/drive_c/$(cat "$RES/edge-path.txt")"
cd "$(dirname "$EDGE")"
schritt "Windows-Umgebung (Wine) starten und Solid Edge laden – der Solid-Edge-Startbildschirm folgt gleich"
exec "$WINE_BIN/wine" "$EDGE" "$@"
