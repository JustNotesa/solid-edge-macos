#!/usr/bin/env bash
# Baut Wines d2d1.dll (Wine 11.21) als native x86_64-DLL mit mingw-w64 – ohne Wine-Buildsystem.
# Patches:
#   0001  SVG-Dokumente mit nanosvg (JustNotesa/solid-edge-wine-fixes, liegt in downloads/se-wine-fixes/)
#   0002  ein geteiltes D3D-Gerät je Thread statt je Factory, Shader nur einmal übersetzen,
#         Freigabe-Reihenfolge im WIC-Render-Target (ersetzt patches/patch-d2d1.py), Statistik (+d2dstat)
#         → docs/threads-und-haenger.md
#
#   ./build.sh             baut out/d2d1.dll
#   ./build.sh --install   baut und kopiert nach prefix/…/system32/d2d1.dll (Solid Edge vorher beenden)
#
# Voraussetzungen: brew install mingw-w64 (gcc + widl), python3. Lädt einmalig die Wine-Quellen
# (wine-11.21.tar.xz, ~47 MB) nach downloads/.
set -euo pipefail

HIER="$(cd "$(dirname "$0")" && pwd)"
SE_ROOT="$(cd "$HIER/.." && pwd)"
WINE_VER=11.21
TARBALL="$SE_ROOT/downloads/wine-$WINE_VER.tar.xz"
TARBALL_SHA256=5a40f593432d81fd3a1646db503f21ce42b8894d6d80c34c3a48b886edc54e3d
FIXES="$SE_ROOT/downloads/se-wine-fixes"
ARBEIT="$HIER/work"
AUS="$HIER/out"
CC=x86_64-w64-mingw32-gcc
WIDL=x86_64-w64-mingw32-widl
NIEDRIG="$HOME/.claude/tools/niedrig.sh"
[ -x "$NIEDRIG" ] || NIEDRIG=""

for t in $CC $WIDL x86_64-w64-mingw32-dlltool; do
  command -v $t >/dev/null || { echo "$t fehlt – erst: brew install mingw-w64" >&2; exit 1; }
done

# 1. Quellen
if [ ! -f "$TARBALL" ]; then
  mkdir -p "$(dirname "$TARBALL")"
  curl -fL -o "$TARBALL.part" "https://dl.winehq.org/wine/source/11.x/wine-$WINE_VER.tar.xz"
  mv "$TARBALL.part" "$TARBALL"
fi
echo "$TARBALL_SHA256  $TARBALL" | shasum -a 256 -c - >/dev/null || { echo "Prüfsumme von $TARBALL falsch" >&2; exit 1; }

NANOSVG="$FIXES/third_party/nanosvg"
if [ ! -f "$NANOSVG/nanosvg.h" ] || [ ! -f "$NANOSVG/nanosvgrast.h" ]; then
  mkdir -p "$NANOSVG"
  for f in nanosvg.h nanosvgrast.h LICENSE.txt; do
    curl -fsSL -o "$NANOSVG/$f" "https://raw.githubusercontent.com/JustNotesa/solid-edge-wine-fixes/HEAD/third_party/nanosvg/$f"
  done
fi

rm -rf "$ARBEIT"
mkdir -p "$ARBEIT" "$AUS"
tar -xJf "$TARBALL" -C "$ARBEIT" "wine-$WINE_VER/include" "wine-$WINE_VER/dlls/d2d1" "wine-$WINE_VER/libs/uuid" \
  "wine-$WINE_VER/libs/dxguid"
W="$ARBEIT/wine-$WINE_VER"

# 2. Patches (0001 aus JustNotesa/solid-edge-wine-fixes, wird bei Bedarf geladen)
if [ ! -f "$FIXES/0001-d2d1-implement-svg-documents-with-nanosvg.patch" ]; then
  mkdir -p "$FIXES"
  curl -fsSL -o "$FIXES/0001-d2d1-implement-svg-documents-with-nanosvg.patch" \
    "https://raw.githubusercontent.com/JustNotesa/solid-edge-wine-fixes/HEAD/patches/0001-d2d1-implement-svg-documents-with-nanosvg.patch"
fi
( cd "$W" && patch -p1 -s < "$FIXES/0001-d2d1-implement-svg-documents-with-nanosvg.patch" \
          && patch -p1 -s < "$HIER/patches/0002-d2d1-geteiltes-d3d-geraet.patch" )
cp "$NANOSVG/nanosvg.h" "$NANOSVG/nanosvgrast.h" "$W/dlls/d2d1/"

# 3. Header aus den IDL-Dateien (einige IDL-Fragmente lassen sich allein nicht übersetzen – egal)
echo "Erzeuge Header …"
( cd "$W/include" && for f in *.idl; do
    $NIEDRIG $WIDL --nostdinc -I. -D__WINESRC__ -h -o "${f%.idl}.h" "$f" >/dev/null 2>&1 || true
  done )

# 4. Übersetzen (Wines eigene CRT-Header, gelinkt gegen ucrtbase wie Wines PE-Builds)
CFLAGS="-O2 -nostdinc -I$W/include/msvcrt -I$W/include -I$W/dlls/d2d1 -D__WINESRC__ -D_UCRT -fno-builtin
  -fshort-wchar -fno-strict-aliasing -fno-omit-frame-pointer -Wall -Wno-attributes -Wno-unused -Wno-format
  -ffile-prefix-map=$W=wine-$WINE_VER"
# -ffile-prefix-map: __FILE__ (assert-Meldungen, Namen der Critical Sections) enthielte sonst den absoluten
# Bau-Pfad samt Mac-Benutzernamen → wird zu wine-11.21/dlls/d2d1/….c. Debuginfo entfernt das -s beim Linken.
OBJ="$ARBEIT/obj"; mkdir -p "$OBJ"
echo "Übersetze …"
for f in "$W"/dlls/d2d1/*.c; do
  $NIEDRIG $CC $CFLAGS -c "$f" -o "$OBJ/$(basename "${f%.c}").o"
done
for f in "$W"/libs/uuid/*.c "$W"/libs/dxguid/*.c; do
  $NIEDRIG $CC $CFLAGS -c "$f" -o "$OBJ/lib_$(basename "${f%.c}").o"
done

# 5. Linken: Exporte mit Wines Ordinalzahlen (Solid Edge importiert D2D1CreateFactory über Ordinal 1!),
#    Importbibliotheken für Wines Debug-Funktionen (ntdll) und d3d10_1 fehlen in mingw → selbst erzeugen.
{ echo "LIBRARY d2d1.dll"; echo "EXPORTS"
  grep '^@' "$W/dlls/d2d1/d2d1.spec" | awk '{sub(/\(.*/,"",$3); print "    " $3 " @" NR}'; } > "$OBJ/d2d1.def"
printf 'LIBRARY ntdll.dll\nEXPORTS\n__wine_dbg_get_channel_flags\n__wine_dbg_header\n__wine_dbg_output\n__wine_dbg_strdup\n' \
  > "$OBJ/winedbg.def"
printf 'LIBRARY d3d10_1.dll\nEXPORTS\nD3D10CreateDevice1\n' > "$OBJ/d3d10_1.def"
x86_64-w64-mingw32-dlltool -d "$OBJ/winedbg.def" -l "$OBJ/libwinedbg.a"
x86_64-w64-mingw32-dlltool -d "$OBJ/d3d10_1.def" -l "$OBJ/libd3d10_1.a"
$NIEDRIG $CC -shared -s -o "$AUS/d2d1.dll" "$OBJ"/*.o "$OBJ/d2d1.def" -L"$OBJ" -lwinedbg -ld3d10_1 \
  -lgdi32 -luser32 -ladvapi32 -ld3dcompiler_47 -ldwrite -lxmllite -lole32 -lkernel32 -lucrtbase -static-libgcc
rm -rf "$ARBEIT"
# Keine lokalen Pfade/Benutzernamen in der verteilten DLL (LC_ALL=C + /usr/bin/grep: ugrep zählt Binärdateien falsch)
if LC_ALL=C /usr/bin/grep -a -q -e "$HOME" -e "$SE_ROOT" -e '/Users/' "$AUS/d2d1.dll"; then
  echo "Fehler: $AUS/d2d1.dll enthält lokale Pfade" >&2; exit 1
fi
echo "Fertig: $AUS/d2d1.dll"

if [ "${1:-}" = "--install" ]; then
  source "$SE_ROOT/env.sh"
  ZIEL="$WINEPREFIX/drive_c/windows/system32/d2d1.dll"
  if pgrep -qf 'Program/Edge\.exe'; then echo "Solid Edge läuft – erst beenden (./mesa-test.sh stopp)." >&2; exit 1; fi
  [ -f "$ZIEL" ] && [ ! -f "$ZIEL.vor-geteilt" ] && cp "$ZIEL" "$ZIEL.vor-geteilt"
  cp "$AUS/d2d1.dll" "$ZIEL"
  echo "Installiert: $ZIEL (vorherige Datei: $ZIEL.vor-geteilt)"
fi
