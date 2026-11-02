#!/usr/bin/env bash
# OpenGL für Solid Edges 3D-Ansicht: Mesa 22.0.3 (pal1000/mesa-dist-win) mit Zink, nur für Edge.exe.
#
# Warum gerade so (Details im README, „OpenGL-Versuche mit Mesa“):
#  - Solid Edge verlangt ein Kompatibilitätsprofil (#version 150 compatibility); macOS bietet nur
#    GL 2.1 compat oder 3.2–4.1 core → mit Wines eigenem opengl32 bleibt die Ansicht leer.
#  - Zink rendert über Vulkan (winevulkan → MoltenVK → Metal) auf der GPU.
#  - Erst ab Mesa 22.1 gibt Zink über eine eigene Vulkan-Swapchain aus; Wines Mac-Treiber legt diese
#    Metal-View über alles (grauer Block, Menüs verdeckt), und Solid Edges GDI/OpenGL-Hybrid-Zeichnen
#    bleibt darunter unsichtbar. 22.0 liest das Bild zurück und gibt es per GDI aus → passt.
#  - Ab 25.2 verlangt Zink zudem „nullDescriptor“, das MoltenVK nicht hat.
#  - Mesa nennt eine interne Textur-Variable „sampler“ – in Metal ein Typname → Shader-Fehler,
#    Absturz in vkQueueSubmit. patches/patch-mesa-sampler-name.py benennt sie um.
set -euo pipefail
source "$(dirname "$0")/env.sh"

VER=22.0.3
MESA="$SE_ROOT/runtime/mesa-$VER"
PD="$(dirname "$SE_EXE")"
[ -d "$PD" ] || { echo "Solid Edge ist noch nicht installiert: $PD" >&2; exit 1; }

if [ ! -f "$MESA/x64/libgallium_wgl.dll" ]; then
  mkdir -p "$MESA"
  curl -sL -o "$MESA/m.7z" "https://github.com/pal1000/mesa-dist-win/releases/download/$VER/mesa3d-$VER-release-msvc.7z"
  7z x -y -o"$MESA" "$MESA/m.7z" >/dev/null
  rm -f "$MESA/m.7z"
fi

cp "$MESA/x64/libglapi.dll" "$PD/"
python3 -I "$SE_ROOT/patches/patch-mesa-sampler-name.py" "$MESA/x64/libgallium_wgl.dll" "$PD/libgallium_wgl.dll"

# opengl32-Vorschaltung (opengl-wrapper/): Mesa kann nicht in Bitmaps zeichnen (PFD_DRAW_TO_BITMAP), Solid Edge
# braucht das für Bildexport (View.SaveAsImage), Vorschaubilder u. a. Die Vorschaltung rendert solche Kontexte
# in ein verstecktes Hilfsfenster und kopiert das Ergebnis in die Bitmap; Mesas opengl32 heißt dann mesaogl32.dll.
WRAP="$SE_ROOT/opengl-wrapper/out"
if [ ! -f "$WRAP/opengl32.dll" ] && command -v x86_64-w64-mingw32-gcc >/dev/null; then
  "$SE_ROOT/opengl-wrapper/build.sh" "$MESA" >/dev/null
fi
if [ -f "$WRAP/opengl32.dll" ]; then
  cp "$WRAP/opengl32.dll" "$PD/opengl32.dll"
  cp "$MESA/x64/opengl32.dll" "$PD/mesaogl32.dll"
else
  echo "Hinweis: opengl32-Vorschaltung fehlt (brew install mingw-w64) – Bildexport/Vorschaubilder bleiben leer." >&2
  cp "$MESA/x64/opengl32.dll" "$PD/"
  rm -f "$PD/mesaogl32.dll"
fi

"$WINE" reg add 'HKCU\Software\Wine\AppDefaults\Edge.exe\DllOverrides' /v opengl32 /t REG_SZ /d "native,builtin" /f >/dev/null
"$WINE" reg delete 'HKCU\Software\Wine\Debug' /v RelayInclude /f >/dev/null 2>&1 || true
"$WINESERVER" -w
echo "OpenGL: Mesa $VER (Zink, GALLIUM_DRIVER aus env.sh) neben Edge.exe installiert."
