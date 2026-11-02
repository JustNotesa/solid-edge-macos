#!/usr/bin/env bash
# Richtet Solid Edge 2026 Community Edition auf diesem Mac ein – ein Befehl, alles automatisch.
#
#   ./setup.sh                 einrichten, danach mit ./start.sh starten
#   ./setup.sh --app           zusätzlich „Solid Edge 2026.app“ unter /Programme anlegen (Start per Doppelklick)
#   ./setup.sh --dmg           zusätzlich eine DMG für weitere eigene Macs bauen (nicht öffentlich weitergeben!)
#   ./setup.sh --selbst-bauen  die zwei eigenen DLLs aus dem Quelltext bauen statt sie aus dem GitHub-Release zu laden
#
# Was du selbst tun musst: den Solid-Edge-Installer bei Siemens herunterladen (kostenlose Registrierung) und die
# ZIP-Datei in diesen Ordner oder nach ~/Downloads legen. Alles andere lädt und installiert das Skript.
# Dauer beim ersten Mal: ca. 1–1,5 Stunden (davon ~45 min .NET Framework). Mehrfach aufrufbar – erledigte
# Schritte werden übersprungen.
set -euo pipefail
cd "$(dirname "$0")"
ROOT="$PWD"

RELEASE="https://github.com/JustNotesa/solid-edge-macos/releases/download/v1.0"
WINE_URL="https://github.com/Gcenx/macOS_Wine_builds/releases/download/11.18/wine-staging-11.18-osx64.tar.xz"
DXVK_NAME="dxvk-macOS-async-v1.10.3-20230507"
DXVK_URL="https://github.com/Gcenx/DXVK-macOS/releases/download/v1.10.3-20230507/$DXVK_NAME.tar.gz"
SIEMENS_URL="https://resources.sw.siemens.com/en-US/download-solid-edge-community-edition"

APP=0; DMG=0; SELBST=0
for a in "$@"; do
  case "$a" in
    --app) APP=1 ;;
    --dmg) DMG=1 ;;
    --selbst-bauen) SELBST=1 ;;
    -h|--help) sed -n '2,13p' "$0"; exit 0 ;;
    *) echo "Unbekannte Option: $a (siehe ./setup.sh --help)" >&2; exit 2 ;;
  esac
done

schritt() { printf '\n\033[1;34m==> %s\033[0m\n' "$*"; }
ok()      { printf '    \033[32m✓\033[0m %s\n' "$*"; }
fehler()  { printf '\n\033[1;31mFehler:\033[0m %s\n' "$*" >&2; exit 1; }
laden()   { # laden <url> <ziel>
  [ -s "$2" ] && return 0
  curl -fL --retry 3 --progress-bar -o "$2.part" "$1" && mv "$2.part" "$2"
}

mkdir -p downloads runtime logs

# ---------------------------------------------------------------- 1. Mac prüfen
schritt "Mac prüfen"
[ "$(uname -s)" = Darwin ] || fehler "Dieses Skript ist für macOS."
if [ "$(uname -m)" = arm64 ]; then
  if ! arch -x86_64 /usr/bin/true 2>/dev/null; then
    echo "    Rosetta 2 fehlt – wird jetzt installiert (Apple-Lizenz wird dabei akzeptiert)."
    softwareupdate --install-rosetta --agree-to-license
  fi
  ok "Apple Silicon mit Rosetta 2"
else
  ok "Intel-Mac (nicht getestet, sollte aber funktionieren)"
fi
FREI=$(df -g "$ROOT" | awk 'NR==2 {print $4}')
[ "$FREI" -ge 35 ] || fehler "Zu wenig Speicher: $FREI GB frei, nötig sind mindestens 35 GB (besser 60 GB)."
ok "$FREI GB frei"

# ---------------------------------------------------------------- 2. Werkzeuge
schritt "Werkzeuge (Homebrew)"
command -v brew >/dev/null || fehler "Homebrew fehlt. Installieren: https://brew.sh – danach ./setup.sh erneut starten."
FEHLT=()
for p in winetricks p7zip; do brew list --formula "$p" >/dev/null 2>&1 || FEHLT+=("$p"); done
[ "$SELBST" = 1 ] && { brew list --formula mingw-w64 >/dev/null 2>&1 || FEHLT+=(mingw-w64); }
if [ ${#FEHLT[@]} -gt 0 ]; then brew install "${FEHLT[@]}"; fi
ok "winetricks, 7z$([ "$SELBST" = 1 ] && echo ", mingw-w64")"

# ---------------------------------------------------------------- 3. Solid-Edge-Installer
schritt "Solid-Edge-Installer"
MSI="installer/Solid Edge/Siemens Solid Edge 2026.msi"
if [ ! -f "$MSI" ]; then
  ZIP="$(ls -t "$ROOT"/Solid_Edge_Community_2026*.zip "$HOME"/Downloads/Solid_Edge_Community_2026*.zip 2>/dev/null | head -1 || true)"
  if [ -z "$ZIP" ]; then
    cat <<EOF

    Der Solid-Edge-Installer fehlt. So bekommst du ihn (kostenlos):
      1. Auf der Siemens-Seite registrieren und „Solid Edge Community Edition“ herunterladen
         (Datei „Solid_Edge_Community_2026_….zip“, ca. 5 GB).
      2. Die ZIP-Datei in diesen Ordner oder nach ~/Downloads legen.
      3. ./setup.sh erneut starten.
    Die Seite wird jetzt im Browser geöffnet.
EOF
    open "$SIEMENS_URL" 2>/dev/null || true
    exit 1
  fi
  echo "    Entpacke $(basename "$ZIP") …"
  mkdir -p installer
  unzip -q -o "$ZIP" -d installer
  [ -f "$MSI" ] || fehler "In $(basename "$ZIP") steht kein „Solid Edge/Siemens Solid Edge 2026.msi“ – andere Version?"
fi
ok "Installer bereit"

# ---------------------------------------------------------------- 4. Wine und DXVK
schritt "Wine Staging 11.18 (portabel, Gcenx)"
if [ ! -x "runtime/Wine Staging.app/Contents/Resources/wine/bin/wine" ]; then
  laden "$WINE_URL" downloads/wine-staging-11.18-osx64.tar.xz
  T="$(mktemp -d "$ROOT/runtime/.wine.XXXX")"
  tar -xJf downloads/wine-staging-11.18-osx64.tar.xz -C "$T"
  W="$(find "$T" -maxdepth 2 -name '*.app' -type d | head -1)"
  [ -n "$W" ] || fehler "Wine-Archiv enthält keine .app"
  rm -rf "runtime/Wine Staging.app" && mv "$W" "runtime/Wine Staging.app" && rm -rf "$T"
  xattr -dr com.apple.quarantine "runtime/Wine Staging.app" 2>/dev/null || true
fi
ok "Wine bereit"

schritt "DXVK-macOS 1.10.3"
if [ ! -f "runtime/dxvk/$DXVK_NAME/x64/d3d11.dll" ]; then
  laden "$DXVK_URL" "downloads/$DXVK_NAME.tar.gz"
  mkdir -p runtime/dxvk
  tar -xzf "downloads/$DXVK_NAME.tar.gz" -C runtime/dxvk
  [ -f "runtime/dxvk/$DXVK_NAME/x64/d3d11.dll" ] || fehler "DXVK-Archiv hat eine unerwartete Struktur"
fi
ok "DXVK bereit"

# ---------------------------------------------------------------- 5. Die zwei eigenen DLLs
schritt "Eigene DLLs (d2d1 mit SVG/geteiltem Gerät, opengl32-Vorschaltung)"
mkdir -p d2d1-build/out opengl-wrapper/out
if [ "$SELBST" = 1 ]; then
  [ -f d2d1-build/out/d2d1.dll ] || ./d2d1-build/build.sh
  # Die Vorschaltung baut install-opengl.sh automatisch, sobald mingw-w64 da ist.
  ok "werden aus dem Quelltext gebaut"
else
  laden "$RELEASE/SHA256SUMS" downloads/SHA256SUMS.release
  for f in d2d1.dll opengl32.dll; do
    laden "$RELEASE/$f" "downloads/$f"
    soll="$(awk -v f="$f" '$2==f {print $1}' downloads/SHA256SUMS.release)"
    ist="$(shasum -a 256 "downloads/$f" | awk '{print $1}')"
    [ -n "$soll" ] && [ "$soll" = "$ist" ] || fehler "Prüfsumme von $f stimmt nicht – Download löschen (downloads/$f) und erneut versuchen."
  done
  cp downloads/d2d1.dll d2d1-build/out/d2d1.dll
  cp downloads/opengl32.dll opengl-wrapper/out/opengl32.dll
  ok "fertig gebaut aus dem GitHub-Release geladen (Quelltext liegt in diesem Projekt)"
fi

# ---------------------------------------------------------------- 6. Installation
schritt "Solid Edge installieren"
source ./env.sh
if [ -f "$SE_EXE" ]; then
  ok "schon installiert ($WINEPREFIX) – übersprungen. Neu installieren: Ordner prefix/ löschen."
else
  echo "    Das dauert jetzt eine Weile (~1 h). Protokolle: logs/"
  ./install.sh 2>&1 | tee logs/install.log
  [ -f "$SE_EXE" ] || fehler "Installation fehlgeschlagen – siehe logs/install.log"
  ok "installiert"
fi

# ---------------------------------------------------------------- 7. App / DMG
if [ "$APP" = 1 ]; then
  schritt "App unter /Programme anlegen"
  ./build-dmg.sh --app
fi
if [ "$DMG" = 1 ]; then
  schritt "DMG bauen"
  ./build-dmg.sh
fi

schritt "Fertig"
cat <<EOF
    Solid Edge starten:   ./start.sh$([ "$APP" = 1 ] && printf '\n                          oder „Solid Edge 2026“ im Ordner Programme')
    Dateien:              im Solid-Edge-Dialog „Dokumente“, „Desktop“, „Downloads“ = deine Mac-Ordner,
                          Laufwerk Z: = ganze Mac-Festplatte
    Tastatur:             Cmd wirkt wie Strg (Cmd+C/V/Z/S), rechte Option = Alt
    Probleme:             README.md, ANLEITUNG-DMG.md (Abschnitt Fehlersuche), logs/
EOF
