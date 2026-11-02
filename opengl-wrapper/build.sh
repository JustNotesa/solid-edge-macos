#!/usr/bin/env bash
# Baut die opengl32-Vorschaltung (PFD_DRAW_TO_BITMAP-Emulation) mit mingw-w64 und legt sie zusammen mit der
# umbenannten Mesa-opengl32 (mesaogl32.dll) in einen Ausgabeordner.
#   ./build.sh [<mesa-ordner>]      Standard: ../runtime/mesa-22.0.3-p
# Voraussetzung: brew install mingw-w64   (liefert x86_64-w64-mingw32-gcc)
set -euo pipefail

HIER="$(cd "$(dirname "$0")" && pwd)"
MESA="${1:-$HIER/../runtime/mesa-22.0.3-p}"
AUS="$HIER/out"
CC="${CC:-x86_64-w64-mingw32-gcc}"
NIEDRIG="$HOME/.claude/tools/niedrig.sh"
[ -x "$NIEDRIG" ] || NIEDRIG=""

command -v "$CC" >/dev/null || { echo "$CC fehlt – erst: brew install mingw-w64" >&2; exit 1; }
[ -f "$MESA/x64/opengl32.dll" ] || { echo "Mesa-opengl32 fehlt: $MESA/x64/opengl32.dll" >&2; exit 1; }

mkdir -p "$AUS"
python3 -I "$HIER/gen-def.py" "$MESA/x64/opengl32.dll" "$AUS/opengl32.def"
$NIEDRIG "$CC" -O2 -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type -Wno-missing-field-initializers -shared \
  -o "$AUS/opengl32.dll" "$HIER/memdc_gl.c" "$AUS/opengl32.def" -I"$HIER" \
  -lgdi32 -luser32 -static-libgcc
cp "$MESA/x64/opengl32.dll" "$AUS/mesaogl32.dll"
# Testprogramm (tests/run-memdc-test.sh)
$NIEDRIG "$CC" -O2 -Wall -Wno-missing-field-initializers -o "$AUS/memdc_test.exe" "$HIER/tests/memdc_test.c" \
  -lopengl32 -lgdi32 -static-libgcc

echo "Fertig: $AUS/opengl32.dll + $AUS/mesaogl32.dll"
echo "Einsetzen (neben Edge.exe, zusätzlich zu libgallium_wgl.dll/libglapi.dll):"
echo "  cp \"$AUS/opengl32.dll\" \"$AUS/mesaogl32.dll\" \"<Program-Ordner>/\""
