#!/usr/bin/env bash
# Richtet Fensteraufnahme und Eingabe ein: baut (falls nötig) secapture-v2.dll + secapture-inject.exe und kopiert beide nach
# C:\secapture im Prefix. Mit „--inject“ wird die DLL gleich in das laufende Edge.exe geladen.
#
# Wine Staging 11.18 wertet AppInit_DLLs nicht aus (kernel32 hat nur den Stub LoadAppInitDlls, user32 liest den
# Registry-Wert nie). Deshalb lädt secapture-inject.exe die DLL per CreateRemoteThread + LoadLibraryW – ohne Neustart,
# einmal pro Solid-Edge-Sitzung. Das MCP-Tool „fenster_bild“ erledigt das selbst, wenn die DLL noch nicht antwortet.
set -euo pipefail
HIER="$(cd "$(dirname "$0")" && pwd)"
source "$HIER/../../env.sh"
DLL=secapture-v2.dll   # Protokollversion wie in secapture.c
if [ ! -f "$HIER/$DLL" ] || [ "$HIER/secapture.c" -nt "$HIER/$DLL" ] || [ "$HIER/secapture-inject.c" -nt "$HIER/secapture-inject.exe" ]; then
  "$HIER/build.sh"
fi
ZIEL="$WINEPREFIX/drive_c/secapture"
mkdir -p "$ZIEL"
cp "$HIER/secapture-inject.exe" "$ZIEL/"
# Die DLL ist gesperrt, solange sie in Edge.exe geladen ist – dann die vorhandene behalten.
cp "$HIER/$DLL" "$ZIEL/" 2>/dev/null || echo "Hinweis: $DLL in Benutzung, vorhandene Kopie bleibt."
echo "installiert nach $ZIEL"
if [ "${1:-}" = "--inject" ]; then
  "$WINE" 'C:\secapture\secapture-inject.exe' </dev/null 2>/dev/null
fi
