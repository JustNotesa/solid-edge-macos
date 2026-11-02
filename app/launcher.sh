#!/usr/bin/env bash
# Starter der „Solid Edge 2026.app“ (liegt im Bundle als Contents/MacOS/SolidEdge2026).
# Beim ersten Start wird der mitgelieferte Prefix (read-only im Bundle/DMG) nach
# ~/Library/Application Support/SolidEdge2026 kopiert und auf den aktuellen Benutzer umgestellt.
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

meldung() { osascript -e "display notification \"$1\" with title \"Solid Edge 2026\"" >/dev/null 2>&1 || true; }

mkdir -p "$DATA"
exec >>"$LOG" 2>&1
echo "=== $(date) Start (Benutzer $ME, Daten $DATA)"

if ! arch -x86_64 /usr/bin/true 2>/dev/null; then
  osascript -e 'display alert "Solid Edge 2026" message "Rosetta 2 fehlt. Bitte im Terminal ausführen: softwareupdate --install-rosetta"' >/dev/null 2>&1 || true
  exit 1
fi

if [ ! -f "$PREFIX/.se-ready" ]; then
  meldung "Erster Start: Umgebung wird eingerichtet (einige Minuten) …"
  rm -rf "$PREFIX.tmp"
  # cp -c = APFS-Klon, wenn Quelle und Ziel auf demselben Volume liegen; sonst normale Kopie.
  cp -cR "$RES/prefix" "$PREFIX.tmp" 2>/dev/null || { rm -rf "$PREFIX.tmp"; ditto "$RES/prefix" "$PREFIX.tmp"; }
  chmod -R u+w "$PREFIX.tmp"

  # Wine-Benutzerprofil auf den aktuellen macOS-Benutzer umstellen.
  if [ "$TEMPLATE_USER" != "$ME" ]; then
    [ -d "$PREFIX.tmp/drive_c/users/$TEMPLATE_USER" ] && mv "$PREFIX.tmp/drive_c/users/$TEMPLATE_USER" "$PREFIX.tmp/drive_c/users/$ME"
    for reg in "$PREFIX.tmp"/user.reg "$PREFIX.tmp"/userdef.reg "$PREFIX.tmp"/system.reg; do
      [ -f "$reg" ] || continue
      LC_ALL=C sed -i '' \
        -e "s/\\\\\\\\users\\\\\\\\$TEMPLATE_USER/\\\\\\\\users\\\\\\\\$ME/g" \
        -e "s/\\\\\\\\Users\\\\\\\\$TEMPLATE_USER/\\\\\\\\Users\\\\\\\\$ME/g" \
        -e "s/\"USERNAME\"=\"$TEMPLATE_USER\"/\"USERNAME\"=\"$ME\"/g" \
        "$reg"
    done
  fi

  # Profilordner-Links (Desktop, Dokumente …) auf den echten Home-Ordner setzen.
  UDIR="$PREFIX.tmp/drive_c/users/$ME"
  for pair in "Desktop:Desktop" "Documents:Documents" "Downloads:Downloads" "Music:Music" "Pictures:Pictures" "Videos:Movies"; do
    win="${pair%%:*}"; mac="${pair##*:}"
    if [ -L "$UDIR/$win" ] || [ ! -e "$UDIR/$win" ]; then
      rm -f "$UDIR/$win"
      [ -d "$HOME/$mac" ] && ln -s "$HOME/$mac" "$UDIR/$win" || mkdir -p "$UDIR/$win"
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
fi

EDGE="$PREFIX/drive_c/$(cat "$RES/edge-path.txt")"
cd "$(dirname "$EDGE")"
exec "$WINE_BIN/wine" "$EDGE" "$@"
