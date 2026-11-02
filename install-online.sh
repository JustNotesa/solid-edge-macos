#!/usr/bin/env bash
# Ein-Befehl-Installation von Solid Edge 2026 Community Edition auf dem Mac:
#
#   curl -fsSL https://raw.githubusercontent.com/JustNotesa/solid-edge-macos/main/install-online.sh | bash
#
# Ablauf: Homebrew und Rosetta 2 bei Bedarf installieren (fragt einmal nach dem Mac-Passwort), dieses Projekt
# nach ~/solid-edge-macos laden, die Siemens-Downloadseite öffnen und warten, bis der Installer in „Downloads“
# liegt, dann Solid Edge installieren, „Solid Edge 2026“ in den Ordner Programme legen, aufräumen und starten.
# Umgebungsvariablen: SE_ZIEL (Projektordner, Standard ~/solid-edge-macos), SE_ZWEIG (Git-Zweig, Standard main).
set -euo pipefail

REPO="JustNotesa/solid-edge-macos"
ZWEIG="${SE_ZWEIG:-main}"
ZIEL="${SE_ZIEL:-$HOME/solid-edge-macos}"

schritt() { printf '\n\033[1;34m==> %s\033[0m\n' "$*"; }
fehler()  { printf '\n\033[1;31mFehler:\033[0m %s\n' "$*" >&2; exit 1; }

[ "$(uname -s)" = Darwin ] || fehler "Nur für macOS."
exec </dev/tty 2>/dev/null || true   # bei „curl … | bash“ Tastatureingaben (Passwort) vom Terminal lesen

schritt "Homebrew"
if ! command -v brew >/dev/null; then
  for b in /opt/homebrew/bin/brew /usr/local/bin/brew; do [ -x "$b" ] && eval "$("$b" shellenv)" && break; done
fi
if ! command -v brew >/dev/null; then
  echo "    Homebrew wird installiert (dabei wirst du einmal nach deinem Mac-Passwort gefragt) …"
  /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
  for b in /opt/homebrew/bin/brew /usr/local/bin/brew; do [ -x "$b" ] && eval "$("$b" shellenv)" && break; done
  command -v brew >/dev/null || fehler "Homebrew-Installation fehlgeschlagen."
fi
echo "    ✓ $(brew --version | head -1)"

schritt "Projekt laden nach $ZIEL"
if [ -d "$ZIEL/.git" ] && command -v git >/dev/null; then
  git -C "$ZIEL" pull -q --ff-only || echo "    (Aktualisieren übersprungen – lokale Änderungen?)"
elif [ -f "$ZIEL/setup.sh" ]; then
  echo "    ✓ vorhanden"
else
  mkdir -p "$ZIEL"
  curl -fsSL "https://codeload.github.com/$REPO/tar.gz/refs/heads/$ZWEIG" | tar -xz -C "$ZIEL" --strip-components 1
fi
[ -x "$ZIEL/setup.sh" ] || fehler "setup.sh fehlt in $ZIEL"

cd "$ZIEL"
exec ./setup.sh --app --aufraeumen --starten
