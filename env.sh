# Gemeinsame Umgebung der Solid-Edge-Kompatibilitätsschicht (wird von install.sh/start.sh geladen).
# Nachbau von cryinkfly/Solid-Edge-on-Linux für macOS (Apple Silicon über Rosetta 2).

SE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Laufzeit: staging = WineHQ Staging 11.18 + DXVK-macOS + gepatchtes d2d1 (Standard, läuft)
#           gptk    = Gcenx' Game-Porting-Toolkit-Wine 7.7 mit D3DMetal – Direct2D braucht aber D3D10,
#                     das D3DMetal nicht kann, und DXVK geht dort nicht (Wine ohne Vulkan) → nur zum Vergleich
SE_RUNTIME="${SE_RUNTIME:-staging}"
case "$SE_RUNTIME" in
  gptk)
    WINE_BIN="$SE_ROOT/runtime/gptk/Game Porting Toolkit.app/Contents/Resources/wine/bin"
    export WINE="$WINE_BIN/wine64"
    export WINEPREFIX="$SE_ROOT/prefix-gptk"
    ;;
  staging)
    WINE_BIN="$SE_ROOT/runtime/Wine Staging.app/Contents/Resources/wine/bin"
    export WINE="$WINE_BIN/wine"
    export WINEPREFIX="$SE_ROOT/prefix"
    ;;
esac

export PATH="$WINE_BIN:$PATH"
export WINESERVER="$WINE_BIN/wineserver"
export WINEDEBUG="${WINEDEBUG:--all}"
# Schnellere Synchronisation über Mach-Semaphoren (macOS-Pendant zu esync/fsync).
export WINEMSYNC=1
export WINEESYNC=1

# OpenGL der 3D-Ansicht (Mesa neben Edge.exe, siehe install-opengl.sh):
#   zink = GPU über Vulkan/MoltenVK/Metal (Standard), llvmpipe = CPU-Renderer (Rückfall bei Grafikfehlern)
export GALLIUM_DRIVER="${GALLIUM_DRIVER:-zink}"
# opengl32-Vorschaltung (opengl-wrapper/): Ansichten-Tabelle von view.dvx an Wines CRITICAL_SECTION-Semantik
# anpassen (sonst kein HUD: kein Ansichtswürfel, kein Auswahlrechteck). Schalter siehe README.
export MEMDC_GL_HUD_VIEW="${MEMDC_GL_HUD_VIEW:-fix}"
# „Skizze schließen“: Wines OleSetMenuDescriptor(NULL, …) meldet E_FAIL, wenn kein Menü-Hook mehr da ist; Windows nicht.
# Die Vorschaltung macht daraus für jengine.dll S_OK, sonst bleibt Solid Edge im Skizzenmodus (docs/skizze-schliessen.md).
export MEMDC_GL_OLEMENU="${MEMDC_GL_OLEMENU:-fix}"

# Zeichnungs-/Normenstandard für neue Dokumente (Ordnername unter Template\, z. B. "ISO Metric", "ANSI Inch").
SE_STANDARD="${SE_STANDARD:-DIN Metric}"

SE_INSTALLER="$SE_ROOT/installer/Solid Edge"
SE_EXE="$WINEPREFIX/drive_c/Program Files/Siemens/Solid Edge 2026/Program/Edge.exe"
SE_LOGS="$SE_ROOT/logs"
mkdir -p "$SE_LOGS"

# Schwere Arbeit niedrig priorisieren (Ton/Bedienung haben Vorrang).
NIEDRIG="$HOME/.claude/tools/niedrig.sh"
[ -x "$NIEDRIG" ] || NIEDRIG=""
