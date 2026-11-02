#!/usr/bin/env bash
# Starter der „Solid Edge 2026.app“ (liegt im Bundle als Contents/MacOS/SolidEdge2026).
# Die App wird klein ausgeliefert (macOS prüft beim allerersten Öffnen jedes Byte im Bundle – mit 13 GB über eine
# Minute ohne jede Anzeige). Wine und die Windows-Umgebung („Inhalt“, siehe build-dmg.sh) übernimmt der Starter erst
# beim ersten Start bzw. nach einem Update: aus ~/Library/Application Support/SolidEdge2026/neu (setup.sh) oder aus der
# geöffneten DMG (/Volumes/*/.solidedge-inhalt). Was danach ins Bundle kommt, prüft macOS nicht erneut.
# Zwei Modi, gewählt beim ersten Start (Knöpfe im Wartefenster, gespeichert in Contents/Resources/modus.txt):
#   installiert  Wine in die App (Contents/Resources/wine), Windows-Umgebung (Prefix = Solid-Edge-Einstellungen),
#                start.log und status.txt im Arbeitsordner ~/Library/Application Support/SolidEdge2026;
#                neben die App kommt „Solid Edge deinstallieren.app“. Updates behalten die Einstellungen.
#   portabel     alles in der App (Wine wie oben, Prefix und Protokolle in Contents/Resources/inhalt) – außerhalb
#                bleibt nichts. Ein Update (neue App) bringt eine frische Windows-Umgebung mit.
# Während des Starts zeigt ein kleines Wartefenster (warten.js) an, dass etwas passiert; es schließt sich, sobald
# Solid Edge sein erstes Fenster zeigt.
# Umgebungsvariablen: SE_MODUS=installiert|portabel überspringt die Frage (setup.sh, Tests);
# SE_NUR_EINRICHTEN=1 nur einrichten, nicht starten (setup.sh); SE_DATA_DIR anderer Arbeitsordner (Tests).
set -euo pipefail

# Bis das Wartefenster steht, möglichst keine Hilfsprogramme starten: der Starter läuft unter Rosetta, dort kostet
# jeder Prozessstart spürbar Zeit (deshalb read/cd/${…} statt cat/dirname/pwd).
cd "${0%/*}/.."; APP_CONTENTS="$PWD"
APP="${APP_CONTENTS%/*}"
RES="$APP_CONTENTS/Resources"
lies() { local z=""; { IFS= read -r z || true; } < "$1" 2>/dev/null || true; printf '%s' "$z"; }   # erste Zeile

# Arbeitsordner (Modus installiert) per Env-Variable umbiegbar (für Tests).
DATA="${SE_DATA_DIR:-$HOME/Library/Application Support/SolidEdge2026}"
WINE_BIN="$RES/wine/bin"
read -r STAND < "$RES/stand.txt" || true
read -r TEMPLATE_USER < "$RES/prefix-user.txt" || true
ME="${USER:-$(id -un)}"
DEINST="Solid Edge deinstallieren.app"

meldung() {   # Text per argv, damit Anführungszeichen im Text nichts kaputt machen
  osascript -e 'on run argv' -e 'display alert "Solid Edge 2026" message (item 1 of argv)' -e 'end run' "$1" \
    </dev/null >/dev/null 2>&1 || true
}
falscher_ort() {
  meldung "Solid Edge kann nicht von hier gestartet werden ($1).

Bitte „Solid Edge 2026“ erst im Finder in den Ordner Programme ziehen und dort öffnen. Wenn du die App aus einer DMG hast: die DMG dabei geöffnet lassen – der erste Start holt sich den Inhalt von dort."
  exit 1
}

if [ "$HOSTTYPE" != x86_64 ] && ! arch -x86_64 /usr/bin/true 2>/dev/null; then   # läuft er unter Rosetta, ist es da
  meldung "Rosetta 2 fehlt. Bitte im Terminal ausführen: softwareupdate --install-rosetta"
  exit 1
fi

# Direkt aus der DMG oder aus der App-Translocation (heruntergeladene App, die nicht verschoben wurde) geht es nicht:
# dort kann der Starter nichts in die App legen.
case "$APP" in */AppTranslocation/*) falscher_ort "die App läuft aus einem schreibgeschützten Zwischenordner von macOS" ;; esac
[ -d "${APP%/*}/.solidedge-inhalt" ] && falscher_ort "die App liegt noch in der DMG"

# Modus: gespeichert in der App > SE_MODUS > bestehende Installation im Arbeitsordner (auch alte ohne Modus) > fragen.
MODUS=""; [ -f "$RES/modus.txt" ] && { read -r MODUS < "$RES/modus.txt" || true; }
if [ "$MODUS" != installiert ] && [ "$MODUS" != portabel ]; then
  MODUS=""
  case "${SE_MODUS:-}" in installiert|portabel) MODUS="$SE_MODUS" ;; esac
  [ -z "$MODUS" ] && [ -f "$DATA/prefix/.se-ready" ] && MODUS=installiert
  [ -z "$MODUS" ] && [ -n "${SE_NUR_EINRICHTEN:-}" ] && MODUS=installiert
fi
arbeit_setzen() {
  if [ "$MODUS" = portabel ]; then ARBEIT="$RES/inhalt"; else ARBEIT="$DATA"; fi
  PREFIX="$ARBEIT/prefix"
}
ARBEIT=""; PREFIX=""
[ -n "$MODUS" ] && arbeit_setzen

# Ist alles da und passt zu dieser App? Sonst muss Inhalt übernommen werden.
NOETIG=0; ASTAND=""
[ -n "$ARBEIT" ] && [ -f "$ARBEIT/stand.txt" ] && { read -r ASTAND < "$ARBEIT/stand.txt" || true; }
if [ -z "$MODUS" ] || [ ! -x "$WINE_BIN/wine" ] || [ ! -f "$PREFIX/.se-ready" ] \
   || [ "$ASTAND" != "$STAND" ]; then NOETIG=1; fi
QUELLE=""
if [ "$NOETIG" = 1 ]; then
  for q in "$DATA/neu" /Volumes/*/.solidedge-inhalt; do
    [ -f "$q/stand.txt" ] && [ "$(lies "$q/stand.txt")" = "$STAND" ] && { QUELLE="$q"; break; }
  done
  if [ -z "$QUELLE" ] && ! { [ -n "$MODUS" ] && [ -f "$PREFIX/.se-ready" ] \
                             && { [ -x "$WINE_BIN/wine" ] || [ -x "$DATA/wine/bin/wine" ]; }; }; then
    meldung "Für den ersten Start fehlt der Inhalt der App (Windows-Umgebung mit Solid Edge). Wenn du die App aus einer DMG hast: die DMG-Datei doppelklicken, damit sie geöffnet ist, und Solid Edge erneut starten. Wenn du sie mit setup.sh gebaut hast: setup.sh erneut ausführen."
    exit 1
  fi
fi
if { [ "$NOETIG" = 1 ] || [ "$MODUS" = portabel ]; } && [ ! -w "$RES" ]; then
  case "$APP" in /Volumes/*) falscher_ort "die App liegt auf einem schreibgeschützten Laufwerk" ;;
                 *) falscher_ort "in die App kann nicht geschrieben werden" ;; esac
fi

# Protokoll und Status für das Wartefenster. Ist der Modus noch offen, erst in einen Zwischenordner – nach der Wahl
# wandern beide in den Arbeitsordner, im Zwischenordner bleiben Verweise für das Fenster (es räumt sie am Ende weg).
WAHL=""
if [ -n "$MODUS" ]; then
  [ -d "$ARBEIT" ] || mkdir -p "$ARBEIT"; LOG="$ARBEIT/start.log"; STATUS="$ARBEIT/status.txt"
else
  ZW="$(mktemp -d "${TMPDIR:-/tmp}/solidedge-start.XXXXXX")"
  LOG="$ZW/start.log"; STATUS="$ZW/status.txt"; WAHL="$ZW/wahl.txt"
fi
exec >>"$LOG" 2>&1

# Status: eine Zeile je Schritt, „⏳“ läuft, „✓“ erledigt.
: > "$STATUS"
schritt() { printf '⏳  %s\n' "$*" >> "$STATUS"; echo "--- $*"; }
fertig()  { LC_ALL=C sed -i '' '$ s/^⏳/✓ /' "$STATUS" 2>/dev/null || true; }
echo "✓   macOS hat die App geprüft und gestartet" >> "$STATUS"

# Wartefenster. $$ bleibt nach dem exec unten die PID von Edge.exe – daran erkennt das Fenster, wann es gehen kann.
FENSTER=""
if [ -z "${SE_NUR_EINRICHTEN:-}" ] && [ -f "$RES/warten.js" ]; then
  if [ "$NOETIG" = 0 ]; then T1="Solid Edge startet …"
  elif [ -n "$MODUS" ] && [ -f "$PREFIX/.se-ready" ]; then T1="Solid Edge wird aktualisiert …"
  else T1="Solid Edge wird zum ersten Mal eingerichtet …"; fi
  # Nativ starten, nicht unter Rosetta wie dieser Starter: startet schneller, und die Knöpfe der Wahl
  # (ObjC.registerSubclass) hängen unter Rosetta endlos.
  NATIV=(); [ -d /Library/Apple/usr/libexec/oah ] && NATIV=(arch -arm64)   # oah = Rosetta, nur auf Apple Silicon
  ${NATIV[@]+"${NATIV[@]}"} osascript -l JavaScript "$RES/warten.js" "$$" "$T1" "$STATUS" "$LOG" $WAHL </dev/null >/dev/null 2>&1 &
  FENSTER=$!
fi
echo "=== $(date) Start (Benutzer $ME, App $APP, Modus ${MODUS:-offen}, Arbeitsordner ${ARBEIT:-offen})"

# Modus erfragen (Knöpfe im Wartefenster).
if [ -z "$MODUS" ]; then
  if [ -z "$FENSTER" ]; then MODUS=installiert
  else
    echo "Frage nach dem Modus (installiert/portabel) …"
    while [ ! -s "$WAHL" ]; do
      kill -0 "$FENSTER" 2>/dev/null || { echo "Wartefenster beendet, keine Wahl – Abbruch."; exit 1; }
      sleep 0.2
    done
    MODUS="$(cat "$WAHL")"
    case "$MODUS" in installiert|portabel) ;; *) echo "Abgebrochen."; exit 0 ;; esac
  fi
  echo "Gewählt: $MODUS"
  arbeit_setzen
  mkdir -p "$ARBEIT"
  cat "$LOG" >> "$ARBEIT/start.log"; cat "$STATUS" > "$ARBEIT/status.txt"
  rm -f "$LOG" "$STATUS"
  ln -s "$ARBEIT/start.log" "$LOG"; ln -s "$ARBEIT/status.txt" "$STATUS"
  LOG="$ARBEIT/start.log"; STATUS="$ARBEIT/status.txt"
  exec >>"$LOG" 2>&1
  echo "Arbeitsordner: $ARBEIT"
fi

# Ordner kopieren und dabei den Fortschritt in die Ausgabe schreiben. Auf demselben Volume per APFS-Klon (sofort).
kopieren() {   # quelle ziel
  rm -rf "$2"
  if [ "$(stat -f %d "$1")" = "$(stat -f %d "$(dirname "$2")")" ] && cp -cR "$1" "$2" 2>/dev/null; then return 0; fi
  rm -rf "$2"
  local gesamt; gesamt="$(du -sk "$1" | cut -f1)"
  ditto "$1" "$2" & local pid=$!
  while kill -0 "$pid" 2>/dev/null; do
    sleep 2
    awk -v k="$(du -sk "$2" 2>/dev/null | cut -f1)" -v g="$gesamt" \
      'BEGIN { if (g > 0) printf "kopiert: %.1f von %.1f GB (%d %%)\n", k/1048576, g/1048576, 100*k/g }'
  done
  wait "$pid"
}
# Ordner übernehmen: aus neu/ verschieben (auf demselben Volume nur Umbenennen), sonst kopieren. Erst nach .tmp, dann
# umbenennen – ein abgebrochener Start hinterlässt so nichts Halbes.
uebernehmen() {   # quelle ziel
  rm -rf "$2.tmp"
  if [ "$VERSCHIEBEN" = 1 ] && [ "$(stat -f %d "$1")" = "$(stat -f %d "$(dirname "$2")")" ]; then mv "$1" "$2.tmp"
  else kopieren "$1" "$2.tmp"; fi
  # Kopie aus einer heruntergeladenen DMG trägt das Quarantäne-Merkmal – für die eigene Kopie entfernen.
  xattr -dr com.apple.quarantine "$2.tmp" 2>/dev/null || true
}

# Name des Windows-Benutzerprofils im Prefix (drive_c/users/<name>): der vorhandene Profilordner (bei Installationen
# vor Nov. 2026 der Mac-Benutzername), sonst der Name aus der Vorlage („user“). Wine bekommt ihn beim Start (USER,
# WINEUSERNAME) – der Mac-Benutzername muss so nirgends in der Windows-Umgebung stehen.
profil() {   # prefix
  local d n=""
  for d in "$1/drive_c/users/$TEMPLATE_USER" "$1/drive_c/users/$ME" "$1"/drive_c/users/*; do
    [ -d "$d" ] && [ ! -L "$d" ] && [ "$(basename "$d")" != Public ] && { n="$(basename "$d")"; break; }
  done
  echo "${n:-$TEMPLATE_USER}"
}

# Profilordner-Links (Desktop, Dokumente …) auf den Home-Ordner des aktuellen Mac-Benutzers setzen. Auch bei jedem
# Start: eine portable App kann auf einem anderen Mac oder bei einem anderen Benutzer laufen.
ordner_verknuepfen() {   # prefix
  local udir pair win mac
  udir="$1/drive_c/users/$(profil "$1")"
  for pair in "Desktop:Desktop" "Documents:Documents" "Downloads:Downloads" "Music:Music" "Pictures:Pictures" "Videos:Movies"; do
    win="${pair%%:*}"; mac="${pair##*:}"
    if [ -L "$udir/$win" ] && [ "$(readlink "$udir/$win")" = "$HOME/$mac" ]; then continue; fi
    if [ -L "$udir/$win" ] || [ ! -e "$udir/$win" ]; then
      rm -f "$udir/$win"
      [ -d "$HOME/$mac" ] && ln -s "$HOME/$mac" "$udir/$win" || mkdir -p "$udir/$win"
      echo "C:\\users\\${udir##*/}\\$win -> ~/$mac"
    fi
  done
}

# „Solid Edge deinstallieren.app“ neben die App legen (nur Modus installiert). Sie bekommt die Pfade mit.
deinstaller_anlegen() {
  local quelle="$RES/$DEINST" ziel; ziel="$(dirname "$APP")/$DEINST"
  [ -d "$quelle" ] || return 0
  if [ ! -w "$(dirname "$APP")" ]; then echo "Hinweis: $(dirname "$APP") nicht beschreibbar – keine Deinstallations-App."; return 0; fi
  rm -rf "$ziel.tmp"
  ditto "$quelle" "$ziel.tmp"
  printf '%s\n%s\n' "$APP" "$DATA" > "$ziel.tmp/Contents/Resources/pfade.txt"
  xattr -dr com.apple.quarantine "$ziel.tmp" 2>/dev/null || true
  rm -rf "$ziel"; mv "$ziel.tmp" "$ziel"
  echo "Deinstallations-App: $ziel"
}

if [ "$NOETIG" = 1 ] && [ -z "$QUELLE" ]; then
  # Kein passender Inhalt, aber eine funktionierende Installation: weiter damit. Alter Aufbau (Wine im Arbeitsordner):
  # Wine in die App verschieben.
  echo "Inhalt zu Stand $STAND nicht gefunden – starte mit der vorhandenen Installation."
  if [ ! -x "$WINE_BIN/wine" ]; then
    schritt "Wine in die App verschieben"
    VERSCHIEBEN=1; uebernehmen "$DATA/wine" "$RES/wine"; rm -rf "$RES/wine"; mv "$RES/wine.tmp" "$RES/wine"
    fertig
  fi
  echo "$MODUS" > "$RES/modus.txt"
  [ "$MODUS" = installiert ] && deinstaller_anlegen
elif [ "$NOETIG" = 1 ]; then
  echo "Inhalt: $QUELLE (Stand $STAND)"
  [ "$QUELLE" = "$DATA/neu" ] && VERSCHIEBEN=1 || VERSCHIEBEN=0

  schritt "Wine (Windows-Umgebung für den Mac) in die App übernehmen"
  uebernehmen "$QUELLE/wine" "$RES/wine"
  rm -rf "$RES/wine"; mv "$RES/wine.tmp" "$RES/wine"
  fertig

  if [ ! -f "$PREFIX/.se-ready" ]; then
    if [ "$VERSCHIEBEN" = 1 ]; then
      schritt "Windows-Umgebung mit Solid Edge übernehmen (nur beim ersten Start)"
    else
      schritt "Windows-Umgebung mit Solid Edge aus der DMG kopieren (~13 GB, nur beim ersten Start, einige Minuten) – die DMG bis dahin geöffnet lassen"
    fi
    uebernehmen "$QUELLE/prefix" "$PREFIX"
    chmod -R u+w "$PREFIX.tmp"
    fertig

    schritt "Ordner Dokumente, Schreibtisch und Downloads mit dem Mac verknüpfen"
    ordner_verknuepfen "$PREFIX.tmp"

    # Laufwerke: nur C: und Z: (= /).
    rm -rf "$PREFIX.tmp/dosdevices"
    mkdir -p "$PREFIX.tmp/dosdevices"
    ln -s ../drive_c "$PREFIX.tmp/dosdevices/c:"
    ln -s / "$PREFIX.tmp/dosdevices/z:"

    rm -rf "$PREFIX"
    mv "$PREFIX.tmp" "$PREFIX"
    touch "$PREFIX/.se-ready"
    echo "Prefix eingerichtet: $PREFIX"
    fertig
  else
    # Update der App: nur die eigenen Grafik-DLLs in den bestehenden Prefix übernehmen
    # (der Rest – Einstellungen, Dokumente – bleibt unangetastet).
    schritt "Grafik-Bibliotheken aktualisieren (GPU über Metal)"
    PROG="drive_c/$(dirname "$(cat "$RES/edge-path.txt")")"
    for f in "$PROG/opengl32.dll" "$PROG/mesaogl32.dll" "$PROG/libgallium_wgl.dll" "$PROG/libglapi.dll" \
             drive_c/windows/system32/d2d1.dll drive_c/windows/system32/d3d11.dll drive_c/windows/system32/dxgi.dll \
             drive_c/windows/system32/d3d10core.dll; do
      if [ -f "$QUELLE/prefix/$f" ] && ! cmp -s "$QUELLE/prefix/$f" "$PREFIX/$f"; then
        cp "$QUELLE/prefix/$f" "$PREFIX/$f"; echo "aktualisiert: $f"
      fi
    done
    fertig
  fi

  echo "$STAND" > "$ARBEIT/stand.txt"
  echo "$MODUS" > "$RES/modus.txt"
  [ "$VERSCHIEBEN" = 1 ] && rm -rf "$DATA/neu"
  if [ "$MODUS" = installiert ]; then
    # Alter Aufbau: Wine lag im Arbeitsordner – jetzt in der App.
    [ -d "$DATA/wine" ] && { rm -rf "$DATA/wine"; echo "altes $DATA/wine entfernt"; }
    deinstaller_anlegen
  else
    rmdir "$DATA" 2>/dev/null || true   # portabel: außerhalb der App bleibt nichts (nur wenn leer)
  fi
fi
[ "$NOETIG" = 0 ] && ordner_verknuepfen "$PREFIX"   # schnell, wenn alles stimmt

# Dock-Name „Solid Edge 2026“ statt „wine“ (app/wine-umbenennen.sh): build-dmg.sh baut Wine schon so; hier für Wine
# aus älteren Inhalten nachholen. Schnell, wenn schon erledigt.
if [ -x "$RES/wine-umbenennen.sh" ] && [ -w "$RES/wine/lib/wine" ] \
   && { [ ! -L "$RES/wine/lib/wine/x86_64-unix" ] || ! cmp -s "$RES/Wine-Info.plist" "$RES/wine/lib/wine/Solid Edge 2026.app/Info.plist"; }; then
  "$RES/wine-umbenennen.sh" "$RES/wine" "$RES/Wine-Info.plist" "$RES/AppIcon.icns" && echo "Dock-Name eingerichtet"
fi
[ -n "${SE_NUR_EINRICHTEN:-}" ] && { echo "Eingerichtet ($MODUS)."; exit 0; }

export PATH="$WINE_BIN:$PATH"
export WINEPREFIX="$PREFIX"
# Windows-Benutzer = Profil im Prefix, nicht der Mac-Name. Wines ntdll liest den Namen aus USER (WINEUSERNAME leitet
# es selbst daraus ab und gibt es nur an die Windows-Seite weiter) – beides setzen.
WINEUSERNAME="$(profil "$PREFIX")"; export WINEUSERNAME USER="$WINEUSERNAME"
export WINEDEBUG="${WINEDEBUG:--all}"
export WINEMSYNC=1
export GALLIUM_DRIVER="${GALLIUM_DRIVER:-zink}"   # 3D-Ansicht über die GPU (Mesa-Zink), llvmpipe = CPU
# opengl32-Vorschaltung (opengl-wrapper/): Ansichten-Tabelle von view.dvx an Wines CRITICAL_SECTION-Semantik
# anpassen (sonst kein HUD: kein Ansichtswürfel, kein Auswahlrechteck). Schalter siehe README.
export MEMDC_GL_HUD_VIEW="${MEMDC_GL_HUD_VIEW:-fix}"
# Wines OleSetMenuDescriptor meldet beim zweiten Entfernen E_FAIL → „Skizze schließen“ bricht ab (docs/skizze-schliessen.md).
export MEMDC_GL_OLEMENU="${MEMDC_GL_OLEMENU:-fix}"
unset SE_MODUS SE_NUR_EINRICHTEN

EDGE="$PREFIX/drive_c/$(cat "$RES/edge-path.txt")"
cd "$(dirname "$EDGE")"
schritt "Windows-Umgebung (Wine) starten und Solid Edge laden – der Solid-Edge-Startbildschirm folgt gleich"
exec "$WINE_BIN/wine" "$EDGE" "$@"
