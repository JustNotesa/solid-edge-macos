#!/usr/bin/env bash
# Erzeugt das App-Symbol (AppIcon.icns) aus dem Programmsymbol der installierten Edge.exe.
# Das Siemens-Symbol wird nicht im Repo mitgeliefert, sondern bei jedem Bau lokal aus der eigenen Installation gelesen.
#   app/icon-erzeugen.sh <Edge.exe> <ziel.icns>
set -euo pipefail
EXE="$1"; ZIEL="$2"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT

# Erste Symbolgruppe = Programmsymbol (so zeigt es auch der Windows-Explorer an).
GRUPPE="$(7z l "$EXE" | awk '/\.rsrc\/GROUP_ICON\// {print $NF}' | sort -t/ -k3 -n | head -1)"
[ -n "$GRUPPE" ] || { echo "Kein Symbol in $EXE gefunden" >&2; exit 1; }
7z e -y -o"$T" "$EXE" "$GRUPPE" '.rsrc/ICON/*' >/dev/null

# GRPICONDIR lesen und den größten Eintrag (Breite 0 = 256 px) nehmen.
ID="$(python3 -I - "$T/$(basename "$GRUPPE")" <<'PY'
import struct, sys
d = open(sys.argv[1], "rb").read()
n = struct.unpack_from("<H", d, 4)[0]
best = None
for i in range(n):
    w, h, _, _, _, bits, size, rid = struct.unpack_from("<BBBBHHIH", d, 6 + 14 * i)
    w = w or 256
    if best is None or (w, bits) > best[0]:
        best = ((w, bits), rid)
print(best[1])
PY
)"
QUELLE="$T/$ID"; [ -f "$QUELLE" ] || QUELLE="$T/$ID.ico"
file -b "$QUELLE" | grep -q PNG || { echo "Symbol $ID ist kein PNG" >&2; exit 1; }

SET="$T/AppIcon.iconset"; mkdir -p "$SET"
for g in 16 32 128 256; do
  sips -z "$g" "$g" "$QUELLE" --out "$SET/icon_${g}x${g}.png" >/dev/null
  d=$((g * 2)); [ "$d" -le 256 ] && sips -z "$d" "$d" "$QUELLE" --out "$SET/icon_${g}x${g}@2x.png" >/dev/null
done
cp "$QUELLE" "$SET/icon_256x256.png"
iconutil -c icns "$SET" -o "$ZIEL"
echo "App-Symbol: $ZIEL"
