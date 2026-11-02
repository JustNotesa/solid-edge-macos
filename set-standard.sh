#!/usr/bin/env bash
# Stellt die Standardvorlagen von Solid Edge auf $SE_STANDARD um (Vorgabe aus env.sh: "DIN Metric").
# Solid Edge muss dafür beendet sein (Werte liegen in HKCU, Solid Edge überschreibt sie beim Beenden).
set -euo pipefail
source "$(dirname "$0")/env.sh"

if ps -axo command= | grep -q '[P]rogram/Edge.exe'; then
  echo "Solid Edge läuft noch – bitte erst beenden." >&2
  exit 1
fi

TPL_WIN='C:\Program Files\Siemens\Solid Edge 2026\Template'
TPL_UNIX="$WINEPREFIX/drive_c/Program Files/Siemens/Solid Edge 2026/Template"
klein="$(echo "$SE_STANDARD" | tr '[:upper:]' '[:lower:]')"
[ -d "$TPL_UNIX/$SE_STANDARD" ] || { echo "Vorlagenordner fehlt: $TPL_UNIX/$SE_STANDARD" >&2; exit 1; }

KEY='HKCU\Software\Siemens\Solid Edge\Version 226\Global\Templates'
setze() { "$WINE" reg add "$KEY" /v "$1" /t REG_SZ /d "$2" /f >/dev/null; }

setze Standard          "$SE_STANDARD"
setze PartTradTemplate  "$TPL_WIN\\$SE_STANDARD\\$klein part.par"
setze AsmTradTemplate   "$TPL_WIN\\$SE_STANDARD\\$klein assembly.asm"
setze DraftTemplate     "$TPL_WIN\\$SE_STANDARD\\$klein draft.dft"
setze PsmTradTemplate   "$TPL_WIN\\$SE_STANDARD\\$klein sheet metal.psm"
setze WeldAsmTemplate   "$TPL_WIN\\$SE_STANDARD\\$klein weldment.asm"
if [ -f "$TPL_UNIX/Symbol/$klein symbol.dft" ]; then
  setze DraftSymbolTemplate "$TPL_WIN\\Symbol\\$klein symbol.dft"
fi
"$WINESERVER" -w
echo "Standard auf $SE_STANDARD gestellt."
