#!/usr/bin/env bash
# Deinstallations-App „Solid Edge deinstallieren.app“ (liegt im Bundle als Contents/MacOS/deinstallieren).
# Legt die Haupt-App („Solid Edge 2026.app“, enthält Wine), den Arbeitsordner (~/Library/Application Support/
# SolidEdge2026: Windows-Umgebung mit den Solid-Edge-Einstellungen, stand.txt, start.log …) und sich selbst in den
# Papierkorb (nur Modus „Installiert“; portabel steckt alles in der App) –
# nichts wird endgültig gelöscht, erst das Leeren des Papierkorbs entfernt es. Die Zeichnungen des Benutzers liegen
# in Dokumente/Schreibtisch/Downloads (in der Windows-Umgebung nur verknüpft) und bleiben unangetastet.
# Welche Pfade gemeint sind, steht in Contents/Resources/pfade.txt (Zeile 1: Haupt-App, Zeile 2: Arbeitsordner);
# die Datei schreibt der Starter. Fehlt sie, gelten die Standardpfade.
# SE_OHNE_RUECKFRAGE=1: keine Dialoge – Liste und Meldungen nach stdout, ohne Bestätigung (für Tests und Skripte);
# läuft Solid Edge noch, Abbruch mit exit 1. SE_DIALOG_AUFGEBEN=<Sekunden>: Dialoge schließen sich nach dieser Zeit
# von selbst (gilt dann als „Abbrechen“; nur für Tests).
set -uo pipefail

# Aus einer Umgebung mit gesetztem WINEPREFIX gestartet (z. B. Terminal)? Dann ohne neu starten – sonst hielte die
# Prüfung „läuft Solid Edge noch?“ unten dieses Skript selbst für Solid Edge (ps -E zeigt die Start-Umgebung).
if [ -n "${WINEPREFIX+x}" ]; then exec env -u WINEPREFIX /bin/bash "$0" "$@"; fi

TITEL="Solid Edge deinstallieren"
SELBST="$(cd "$(dirname "$0")/../.." && pwd)"
PFADE="$SELBST/Contents/Resources/pfade.txt"
STANDARD_DATA="$HOME/Library/Application Support/SolidEdge2026"
OHNE_RUECKFRAGE="${SE_OHNE_RUECKFRAGE:-}"

# --- Dialoge (Texte immer per argv an AppleScript, nie in den Quelltext einsetzen – wegen Anführungszeichen) ----
# frage <text> <abbrechen-knopf> <ok-knopf>: gibt 0 zurück, wenn der ok-Knopf gewählt wurde.
frage() {
  local antwort
  antwort="$(osascript - "$TITEL" "$1" "$2" "$3" "${SE_DIALOG_AUFGEBEN:-0}" 2>/dev/null <<'EOF'
on run argv
  set {titel, txt, nein, ja, aufgeben} to argv
  if (aufgeben as integer) > 0 then
    set r to display dialog txt with title titel buttons {nein, ja} default button 1 cancel button 1 with icon caution giving up after (aufgeben as integer)
  else
    set r to display dialog txt with title titel buttons {nein, ja} default button 1 cancel button 1 with icon caution
  end if
  if gave up of r then return ""
  return button returned of r
end run
EOF
)" || return 1
  [ "$antwort" = "$3" ]
}
# frage_weiter <text> <abbrechen-knopf> <ok-knopf>: wie frage, aber mit dem ok-Knopf als Standard (für „Erneut prüfen“).
frage_weiter() {
  local antwort
  antwort="$(osascript - "$TITEL" "$1" "$2" "$3" "${SE_DIALOG_AUFGEBEN:-0}" 2>/dev/null <<'EOF'
on run argv
  set {titel, txt, nein, ja, aufgeben} to argv
  if (aufgeben as integer) > 0 then
    set r to display dialog txt with title titel buttons {nein, ja} default button 2 cancel button 1 with icon caution giving up after (aufgeben as integer)
  else
    set r to display dialog txt with title titel buttons {nein, ja} default button 2 cancel button 1 with icon caution
  end if
  if gave up of r then return ""
  return button returned of r
end run
EOF
)" || return 1
  [ "$antwort" = "$3" ]
}
# meldung <text>: Hinweis mit „OK“ (bzw. nach stdout bei SE_OHNE_RUECKFRAGE).
meldung() {
  if [ -n "$OHNE_RUECKFRAGE" ]; then printf '%s\n' "$1"; return 0; fi
  osascript - "$TITEL" "$1" "${SE_DIALOG_AUFGEBEN:-0}" >/dev/null 2>&1 <<'EOF' || true
on run argv
  set {titel, txt, aufgeben} to argv
  if (aufgeben as integer) > 0 then
    display dialog txt with title titel buttons {"OK"} default button 1 giving up after (aufgeben as integer)
  else
    display dialog txt with title titel buttons {"OK"} default button 1
  end if
end run
EOF
}

# --- Pfade bestimmen und prüfen -----------------------------------------------------------------------------------
APP=""; DATA=""
if [ -f "$PFADE" ]; then
  APP="$(sed -n 1p "$PFADE")"
  DATA="$(sed -n 2p "$PFADE")"
fi
[ -n "$APP" ] || APP="$(dirname "$SELBST")/Solid Edge 2026.app"
[ -n "$DATA" ] || DATA="$STANDARD_DATA"
APP="${APP%/}"; DATA="${DATA%/}"

# Haupt-App nur, wenn sie wirklich unsere ist (.app mit dem Starter SolidEdge2026).
case "$APP" in
  /*.app) [ -f "$APP/Contents/MacOS/SolidEdge2026" ] && [ "$APP" != "$SELBST" ] || APP="" ;;
  *) APP="" ;;
esac

# Arbeitsordner nur, wenn er nach unserem aussieht und kein allgemeiner Ordner ist (Home, Library, / …).
data_ok() {
  local echt home
  case "$1" in /?*) ;; *) return 1 ;; esac
  [ -d "$1" ] || return 1
  echt="$(cd "$1" 2>/dev/null && pwd -P)" || return 1
  home="$(cd "$HOME" 2>/dev/null && pwd -P)" || home="$HOME"
  case "$echt" in
    /|/Users|/Applications|/Library|/System|/Volumes|/private|/private/var|/private/tmp|/tmp) return 1 ;;
    "$home"|"$home/Library"|"$home/Library/Application Support"|"$home/Documents"|"$home/Desktop"|"$home/Downloads") return 1 ;;
  esac
  case "$home/" in "$echt/"*) return 1 ;; esac          # enthält den Home-Ordner
  [ "$echt" = "$(cd "$SELBST" && pwd -P)" ] && return 1
  [ -d "$1/prefix" ] || [ -f "$1/stand.txt" ] || [ -f "$1/start.log" ]
}
data_ok "$DATA" || DATA=""

# Anzeige: ~ statt Home-Ordner.
anzeige() { case "$1" in "$HOME"/*) printf '~/%s' "${1#"$HOME"/}" ;; *) printf '%s' "$1" ;; esac; }
# Größe in KB (du -sk) → „13,2 GB“, „712 MB“ (Dezimal-Einheiten wie im Finder).
groesse() {
  LC_ALL=C awk -v k="${1:-0}" 'BEGIN {
    b = k * 1024
    if (b >= 1e9) s = sprintf("%.1f GB", b / 1e9)
    else if (b >= 1e6) s = sprintf("%d MB", b / 1e6 + 0.5)
    else s = sprintf("%d KB", b / 1e3 + 0.5)
    sub(/\./, ",", s); print s }'
}

# Diese App selbst nur mitnehmen, wenn sie verschiebbar ist (nicht auf einer DMG, nicht von macOS „verlegt“).
SELBST_MIT=""
case "$SELBST" in
  */AppTranslocation/*|/Volumes/*) ;;
  *) [ -w "$(dirname "$SELBST")" ] && SELBST_MIT=1 ;;
esac

# --- Nichts gefunden ------------------------------------------------------------------------------------------------
if [ -z "$APP" ] && [ -z "$DATA" ]; then
  TEXT="Solid Edge 2026 wurde nicht gefunden – weder die App noch ihr Arbeitsordner ($(anzeige "$STANDARD_DATA")). Es gibt nichts zu deinstallieren."
  if [ -n "$OHNE_RUECKFRAGE" ] || [ -z "$SELBST_MIT" ]; then meldung "$TEXT"; exit 0; fi
  frage "$TEXT

Soll diese Deinstallations-App in den Papierkorb?" "Abbrechen" "In den Papierkorb legen" || exit 0
  ZIELE=("$SELBST")
else
  # --- Größen (parallel, der Arbeitsordner hat viele Dateien) und Bestätigung --------------------------------------
  TMP="$(mktemp -d "${TMPDIR:-/tmp}/se-deinst.XXXXXX")"
  [ -n "$APP" ]  && { du -sk "$APP"  2>/dev/null | tail -1 | cut -f1 > "$TMP/app";  } &
  [ -n "$DATA" ] && { du -sk "$DATA" 2>/dev/null | tail -1 | cut -f1 > "$TMP/data"; } &
  wait
  G_APP="$(groesse "$(cat "$TMP/app" 2>/dev/null)")"
  G_DATA="$(groesse "$(cat "$TMP/data" 2>/dev/null)")"
  rm -f "$TMP/app" "$TMP/data"; rmdir "$TMP" 2>/dev/null

  ZIELE=(); LISTE=""
  [ -n "$APP" ]  && { ZIELE+=("$APP");  LISTE+="• Solid Edge 2026 – $(anzeige "$APP") ($G_APP)"$'\n'; }
  [ -n "$DATA" ] && { ZIELE+=("$DATA"); LISTE+="• Windows-Umgebung mit den Solid-Edge-Einstellungen – $(anzeige "$DATA") ($G_DATA)"$'\n'; }
  [ -n "$SELBST_MIT" ] && { ZIELE+=("$SELBST"); LISTE+="• diese Deinstallations-App"$'\n'; }

  TEXT="Folgendes kommt in den Papierkorb:

${LISTE}
Deine Zeichnungen in Dokumente, Schreibtisch und Downloads bleiben erhalten.
Alles landet im Papierkorb – erst wenn du ihn leerst, wird es endgültig entfernt."
  if [ -n "$OHNE_RUECKFRAGE" ]; then
    printf '%s\n' "$TEXT"
  else
    frage "$TEXT" "Abbrechen" "In den Papierkorb legen" || exit 0
  fi

  # --- Läuft Solid Edge noch aus DIESEM Arbeitsordner? (Prozess-Umgebungen, nur lesen – nie beenden) -------------
  laeuft() {
    [ -n "$DATA" ] || return 1
    local muster="WINEPREFIX=$DATA/prefix" echt mit_env ohne_env
    echt="WINEPREFIX=$(cd "$DATA" 2>/dev/null && pwd -P)/prefix"   # derselbe Ordner ohne Symlinks
    # Prozessliste zuerst ganz einlesen, dann durchsuchen: so taucht das grep nicht selbst in der Liste auf, und
    # „grep -q“ bricht keine laufende ps-Pipe ab (mit pipefail gälte das sonst als „nicht gefunden“).
    mit_env="$(ps -E -ww -U "$(id -u)" -o pid=,command= 2>/dev/null)"
    ohne_env="$(ps -ww -U "$(id -u)" -o command= 2>/dev/null)"
    # Treffer nur mit Wortgrenze: danach Leerzeichen, „/ “ oder Zeilenende (sonst passte auch „…/prefix2“) –
    # dafür an jede Zeile ein Leerzeichen anhängen (Festtext-Suche per index, Muster über ENVIRON statt -v,
    # damit Backslashes im Pfad nicht umgedeutet werden). ps -E zeigt die Umgebung nicht bei allen Prozessen: die
    # Windows-Programme überschreiben den Bereich mit ihrer Windows-Befehlszeile, Apple-Programme verbergen ihn.
    # Sichtbar bleibt sie beim wineserver, der läuft, solange irgendein Programm dieser Windows-Umgebung läuft.
    SE_M1="$muster" SE_M2="$echt" awk '
      { z = $0 " "
        if (index(z, ENVIRON["SE_M1"] " ") || index(z, ENVIRON["SE_M1"] "/ ") ||
            index(z, ENVIRON["SE_M2"] " ") || index(z, ENVIRON["SE_M2"] "/ ")) f = 1 }
      END { exit !f }' <<<"$mit_env" && return 0
    # Zusätzlich: Programme, die direkt aus dem Arbeitsordner bzw. mit dem Wine der App laufen (wineserver, wine,
    # Edge.exe per Unix-Pfad; …/SolidEdge2026/wine bei Installationen vor Nov. 2026).
    grep -qF -e "$DATA/wine/" -e "$DATA/prefix/" ${APP:+-e "$APP/Contents/Resources/wine/"} <<<"$ohne_env"
  }
  while laeuft; do
    TEXT_L="Solid Edge läuft noch. Bitte Solid Edge beenden (alle Fenster schließen bzw. Datei → Beenden) und dann „Erneut prüfen“ klicken."
    if [ -n "$OHNE_RUECKFRAGE" ]; then echo "Solid Edge läuft noch. Bitte Solid Edge beenden – es wurde nichts entfernt."; exit 1; fi
    frage_weiter "$TEXT_L" "Abbrechen" "Erneut prüfen" || exit 0
    sleep 1
  done
fi

# --- In den Papierkorb: zuerst über den Finder (mit „Zurücklegen“), sonst /usr/bin/trash -------------------------
FEHLER="$(osascript - "${ZIELE[@]}" 2>&1 >/dev/null <<'EOF'
on run argv
  set liste to {}
  repeat with p in argv
    set end of liste to ((POSIX file (contents of p)) as alias)
  end repeat
  tell application "Finder" to delete liste
end run
EOF
)"
RC=$?
if [ "$RC" -ne 0 ]; then
  echo "Finder: $FEHLER" >&2
  # Was der Finder schon verschoben hat, ist weg – nur den Rest weitergeben.
  REST=()
  for z in "${ZIELE[@]}"; do [ -e "$z" ] && REST+=("$z"); done
  if [ "${#REST[@]}" -gt 0 ]; then
    if [ -x /usr/bin/trash ] && /usr/bin/trash "${REST[@]}"; then
      :
    else
      case "$FEHLER" in
        *-1743*|*"nicht berechtigt"*|*"Not authorized"*)
          GRUND="macOS hat der App nicht erlaubt, den Finder zu steuern. Bitte in Systemeinstellungen → Datenschutz & Sicherheit → Automation bei „$TITEL“ den Finder erlauben und die App erneut öffnen." ;;
        *)
          GRUND="Fehler: $FEHLER
Falls macOS den Zugriff auf den Finder verweigert hat: Systemeinstellungen → Datenschutz & Sicherheit → Automation → „$TITEL“ → Finder erlauben." ;;
      esac
      LISTE_R=""; for z in "${REST[@]}"; do LISTE_R+="• $(anzeige "$z")"$'\n'; done
      meldung "Solid Edge konnte nicht in den Papierkorb gelegt werden. Es wurde nichts gelöscht.

$GRUND

Alternativ diese Objekte selbst im Finder in den Papierkorb ziehen:
$LISTE_R"
      exit 1
    fi
  fi
fi

if [ -z "$APP" ] && [ -z "$DATA" ]; then
  meldung "Die Deinstallations-App liegt jetzt im Papierkorb."
else
  meldung "Solid Edge 2026 liegt jetzt im Papierkorb. Erst wenn du den Papierkorb leerst, wird es endgültig entfernt und der Speicherplatz frei.
Deine Zeichnungen in Dokumente, Schreibtisch und Downloads sind erhalten geblieben."
fi
exit 0
