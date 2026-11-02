#!/usr/bin/env bash
# Dock-Name „Solid Edge 2026“ statt „wine“ bzw. „x86_64-unix“:
#   app/wine-umbenennen.sh <wine-ordner> <Wine-Info.plist> [<AppIcon.icns>]
# Wines ntdll startet sich immer als <ordner von ntdll.so>/wine neu (realpath). macOS nennt den Prozess nach dem
# Ordner, in dem dieser Lader liegt: ohne Info.plist „wine“, mit Info.plist den Ordnernamen. Deshalb heißt der echte
# Ordner lib/wine/x86_64-unix jetzt „Solid Edge 2026.app“ (flaches App-Bundle mit Info.plist und Symbol); unter dem
# alten Namen bleibt ein Verweis. Weil der Ordner nicht mehr auf /x86_64-unix endet, sucht Wine alles eine Ebene
# tiefer (ntdll/unix/loader.c, init_paths: dll_dir = ntdll_dir): dafür die Verweise x86_64-unix (auf sich selbst),
# x86_64-windows, i386-windows im Bundle sowie lib/bin und lib/share. Mehrfach aufrufbar (macht nichts doppelt).
set -euo pipefail
W="$1"; PLIST="$2"; ICON="${3:-}"
L="$W/lib/wine"; NAME="Solid Edge 2026.app"; A="$L/$NAME"
if [ -d "$L/x86_64-unix" ] && [ ! -L "$L/x86_64-unix" ]; then
  rm -f "$L/x86_64-unix/Info.plist" "$L/x86_64-unix/AppIcon.icns"   # Zwischenstand (Info.plist im alten Ordner)
  mv "$L/x86_64-unix" "$A"
  ln -s "$NAME" "$L/x86_64-unix"
fi
[ -d "$A" ] || { echo "wine-umbenennen: $A fehlt" >&2; exit 1; }
[ -e "$A/x86_64-unix" ]    || ln -s . "$A/x86_64-unix"
[ -e "$A/x86_64-windows" ] || ln -s ../x86_64-windows "$A/x86_64-windows"
[ -e "$A/i386-windows" ] || { [ -d "$L/i386-windows" ] && ln -s ../i386-windows "$A/i386-windows"; } || true
[ -e "$W/lib/bin" ]   || ln -s ../bin "$W/lib/bin"
[ -e "$W/lib/share" ] || ln -s ../share "$W/lib/share"
cmp -s "$PLIST" "$A/Info.plist" || cp "$PLIST" "$A/Info.plist"
if [ -n "$ICON" ] && [ -f "$ICON" ]; then cmp -s "$ICON" "$A/AppIcon.icns" || cp "$ICON" "$A/AppIcon.icns"; fi
