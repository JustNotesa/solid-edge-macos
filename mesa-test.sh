#!/usr/bin/env bash
# Testet Solid Edges 3D-Ansicht mit Mesa für Windows (pal1000/mesa-dist-win 26.2.4, runtime/mesa/) statt
# Wines opengl32 – nur für Edge.exe (DLLs neben Edge.exe + AppDefaults-Override).
#
#   ./mesa-test.sh llvmpipe [sek]   CPU-Renderer (OpenGL 4.5 Kompatibilität) – nur Diagnose
#   ./mesa-test.sh zink [sek]       GPU: OpenGL über Vulkan (winevulkan → MoltenVK → Metal), Version vorgetäuscht
#   ./mesa-test.sh aus              Mesa entfernen, wieder Wines opengl32
#
# Öffnet direkt die Teilvorlage des eingestellten Standards, damit sofort eine 3D-Ansicht entsteht.
# Solid Edge bleibt danach offen (für Screenshots); Beenden: ./mesa-test.sh stopp
set -uo pipefail
source "$(dirname "$0")/env.sh"

MODUS="${1:-llvmpipe}"
WARTE="${2:-90}"
PD="$(dirname "$SE_EXE")"
MESA="${MESA_DIR:-$SE_ROOT/runtime/mesa-22.0.3-p}/x64"   # genutzte Fassung (Zink + sampler-Patch)
SHADER_UNIX="$WINEPREFIX/drive_c/shaders"
LOG="$SE_LOGS/mesa-$MODUS$([ "${TRACE:-0}" = 1 ] && echo -trace)$([ "${MESA_PATCH:-0}" = 1 ] && echo -patch)$([ -n "${MESA_DIR:-}" ] && echo "-$(basename "$MESA_DIR")").log"
OVR='HKCU\Software\Wine\AppDefaults\Edge.exe\DllOverrides'

# Alle Wine-Aufrufe von der Konsole entkoppeln: der dabei gestartete wineserver erbt sonst stdout,
# und z. B. „./mesa-test.sh … | tail“ würde nie enden.
w()  { "$WINE" "$@" </dev/null >/dev/null 2>&1; }
ws() { "$WINESERVER" "$@" </dev/null >/dev/null 2>&1; }

ws -k || true
sleep 1

case "$MODUS" in
  stopp) exit 0 ;;
  aus)
    rm -f "$PD/opengl32.dll" "$PD/libgallium_wgl.dll" "$PD/libglapi.dll"
    w reg delete "$OVR" /v opengl32 /f || true
    w reg delete 'HKCU\Software\Wine\Debug' /v RelayInclude /f || true
    ws -w
    echo "Mesa entfernt."
    exit 0 ;;
  llvmpipe)
    export GALLIUM_DRIVER=llvmpipe ;;
  zink)
    export GALLIUM_DRIVER=zink
    # Annahme: Solid Edge nutzt real keine Geometry-/Tessellation-Shader, dann reicht das Melden der Version.
    export MESA_GL_VERSION_OVERRIDE="${MESA_GL_VERSION_OVERRIDE:-4.4COMPAT}"
    export MESA_GLSL_VERSION_OVERRIDE="${MESA_GLSL_VERSION_OVERRIDE:-440}"
    export MESA_LOADER_DRIVER_OVERRIDE=zink ;;
  *) echo "Unbekannter Modus: $MODUS" >&2; exit 2 ;;
esac

cp "$MESA/opengl32.dll" "$PD/"
# Mesa bis 23.x lagert die GL-Einsprünge in libglapi.dll aus; neuere Builds haben sie nicht mehr.
rm -f "$PD/libglapi.dll"; [ -f "$MESA/libglapi.dll" ] && cp "$MESA/libglapi.dll" "$PD/"
if [ "${MESA_PATCH:-0}" = 1 ]; then
  # Zweites SetPixelFormat auf dasselbe Fenster nicht mehr ablehnen (siehe patches/patch-mesa-setpixelformat.py).
  [ -f "$MESA/libgallium_wgl.patched.dll" ] || \
    python3 -I "$SE_ROOT/patches/patch-mesa-setpixelformat.py" "$MESA/libgallium_wgl.dll" "$MESA/libgallium_wgl.patched.dll"
  cp "$MESA/libgallium_wgl.patched.dll" "$PD/libgallium_wgl.dll"
else
  cp "$MESA/libgallium_wgl.dll" "$PD/"
fi
w reg add "$OVR" /v opengl32 /t REG_SZ /d "native,builtin" /f
# MESA_WRAPPER=1: opengl32-Vorschaltung (opengl-wrapper/) für OpenGL in Speicher-DCs (Ansichtswürfel, SaveAsImage);
# Mesas eigenes opengl32 liegt dann als mesaogl32.dll daneben.
rm -f "$PD/mesaogl32.dll"
if [ "${MESA_WRAPPER:-0}" = 1 ]; then
  cp "$SE_ROOT/opengl-wrapper/out/opengl32.dll" "$SE_ROOT/opengl-wrapper/out/mesaogl32.dll" "$PD/"
fi

# TRACE=1: protokolliert (RELAY_INCLUDE oder) die Pixelformat-Aufrufe (gdi32 → opengl32) samt Rückgabewerten.
DBG="err+all,fixme-all,-ole,-dbghelp,-ntlm,-kerberos,-d2d,-ntdll"
# WINEDEBUG_EXTRA: zusätzliche Kanäle, z. B. "+fps,warn+vulkan,+macdrv" (Present-Rate, Swapchain-Warnungen, Mac-Views)
[ -n "${WINEDEBUG_EXTRA:-}" ] && DBG="$DBG,$WINEDEBUG_EXTRA"
if [ "${TRACE:-0}" = 1 ]; then
  w reg add 'HKCU\Software\Wine\Debug' /v RelayInclude /t REG_SZ \
    /d "${RELAY_INCLUDE:-gdi32.ChoosePixelFormat;gdi32.SetPixelFormat;gdi32.GetPixelFormat;gdi32.DescribePixelFormat;gdi32.SwapBuffers;user32.GetDC;user32.ReleaseDC}" /f
  DBG="$DBG,+relay,+loaddll"
else
  w reg delete 'HKCU\Software\Wine\Debug' /v RelayInclude /f || true
fi
mkdir -p "$SHADER_UNIX"
find "$SHADER_UNIX" -type f -name '*.shader_test' -delete

export MESA_SHADER_CAPTURE_PATH='C:\shaders'
export GALLIUM_HUD="${GALLIUM_HUD:-fps,cpu}"     # Einblendung im 3D-Fenster: zeigt, dass Mesa rendert + Leistung
export MESA_DEBUG=1

# SE_TESTDATEI: anderes Dokument öffnen, z. B. ein Beispielteil mit Geometrie aus Training\ (block.par, long_arm.par …).
TEIL="${SE_TESTDATEI:-C:\\Program Files\\Siemens\\Solid Edge 2026\\Template\\$SE_STANDARD\\$(echo "$SE_STANDARD" | tr '[:upper:]' '[:lower:]') part.par}"
echo "Starte Solid Edge mit Mesa/$MODUS, öffne: $TEIL"
# Auch die Hintergrund-Subshell von der Konsole lösen, sonst hält sie die Pipe zu „| tail" offen, bis Solid Edge endet.
( cd "$PD" && WINEDEBUG="$DBG" nohup "$WINE" "$SE_EXE" "$TEIL" </dev/null >"$LOG" 2>&1 & ) </dev/null >/dev/null 2>&1

for _ in $(seq "$WARTE"); do
  sleep 1
  grep -aq 'Unhandled' "$LOG" 2>/dev/null && break
done

echo "--- Ergebnis nach ${WARTE}s ---"
if pgrep -qf 'Program/Edge\.exe'; then echo "Edge.exe läuft"; else echo "Edge.exe läuft NICHT"; fi
echo "Geladene Mesa-DLL neben Edge.exe: $(ls "$PD" | grep -ciE '^(opengl32|libgallium_wgl)\.dll$')/2"
echo "--- Auffälliges im Log ($LOG) ---"
grep -av '^	\|mvk-info\|mvk-warn\|^info' "$LOG" | grep -aiE 'mesa|zink|llvmpipe|gallium|MESA_|GL_|glsl|shader|Unhandled|vulkan|error' | sort | uniq -c | sort -rn | head -25
if [ "${TRACE:-0}" = 1 ]; then
  echo "--- Pixelformat-Aufrufe (erste 40) ---"
  grep -aE 'PixelFormat|SwapBuffers' "$LOG" | grep -aE 'Call|Ret' | head -40
  echo "--- Welche opengl32 geladen? ---"
  grep -aiE 'Loaded.*(opengl32|libgallium)' "$LOG" | head -5
fi
N=$(find "$SHADER_UNIX" -name '*.shader_test' | wc -l | tr -d ' ')
echo "--- Mitgeschnittene Shader: $N ---"
if [ "$N" -gt 0 ]; then
  echo "GLSL-Versionen:"; grep -ahoE '^#version [0-9]+( [a-z]+)?' "$SHADER_UNIX"/*.shader_test | sort | uniq -c
  echo "Shader-Stufen:";  grep -ahoE '^\[(vertex|fragment|geometry|tessellation [a-z]+|compute) shader\]' "$SHADER_UNIX"/*.shader_test | sort | uniq -c
  echo "Geometry/Tess-Hinweise:"; grep -alE 'EmitVertex|layout ?\( ?(triangles|lines|points)|gl_TessLevel|layout ?\( ?vertices' "$SHADER_UNIX"/*.shader_test | wc -l
  echo "GL-Version laut Shader-Test-Kopf:"; grep -ahE '^GL(SL)? >= ' "$SHADER_UNIX"/*.shader_test | sort | uniq -c
fi
if [ -n "${WINEDEBUG_EXTRA:-}" ]; then
  echo "--- Present-Rate (+fps, letzte 8) ---"
  grep -a 'trace:fps' "$LOG" | tail -8
  echo "--- Vulkan-Warnungen (warn+vulkan) ---"
  grep -a 'warn:vulkan' "$LOG" | sed -E 's/^[0-9a-f]{4}://' | sort | uniq -c | sort -rn | head -10
  echo "--- Mac-Treiber: Client-Surfaces / Fensterrechtecke ---"
  grep -aE 'trace:macdrv:macdrv_client_surface|trace:win:client_surface_create|created [0-9a-f]+/' "$LOG" | sed -E 's/^[0-9a-f]{4}://' | sort | uniq -c | head -15
fi
echo "Solid Edge bleibt offen – Screenshot machen, danach: ./mesa-test.sh stopp"
