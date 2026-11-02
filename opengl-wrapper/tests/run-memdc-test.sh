#!/usr/bin/env bash
# Startet memdc_test.exe unter Wine – einmal mit der opengl32-Vorschaltung, einmal (Gegenprobe) nur mit Mesa.
#   ./run-memdc-test.sh [llvmpipe|zink] [<mesa-ordner>]
# Nutzt den Prefix aus env.sh, berührt Solid Edge nicht (eigener Prozess, eigener Testordner).
set -uo pipefail
HIER="$(cd "$(dirname "$0")/.." && pwd)"
source "$HIER/../env.sh"
TREIBER="${1:-llvmpipe}"
MESA="${2:-$HIER/../runtime/mesa-22.0.3-p}/x64"
T="$HIER/out/test"

for variante in mit-vorschaltung nur-mesa; do
  rm -rf "$T" && mkdir -p "$T"
  cp "$HIER/out/memdc_test.exe" "$MESA/libgallium_wgl.dll" "$T/"
  [ -f "$MESA/libglapi.dll" ] && cp "$MESA/libglapi.dll" "$T/"
  if [ "$variante" != nur-mesa ]; then
    cp "$HIER/out/opengl32.dll" "$HIER/out/mesaogl32.dll" "$T/"
  else
    cp "$MESA/opengl32.dll" "$T/"
  fi
  echo "===== $variante ($TREIBER) ====="
  ( cd "$T" && GALLIUM_DRIVER="$TREIBER" MEMDC_GL_DEBUG=1 WINEDLLOVERRIDES="opengl32=n" \
      WINEDEBUG="-all,err+module" "$WINE" memdc_test.exe </dev/null 2>&1 ) | grep -av 'mvk-\|^	\|VK_KHR\|VK_EXT'
done
