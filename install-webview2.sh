#!/usr/bin/env bash
# Installiert die Microsoft Edge WebView2 Runtime (Evergreen Standalone x64) still in den Wine-Prefix.
# Quelle: https://go.microsoft.com/fwlink/?linkid=2124701 (signiert von Microsoft Corporation).
# Wird von install.sh aufgerufen, lässt sich aber auch einzeln nachholen.
set -euo pipefail
source "$(dirname "$0")/env.sh"

WV2="$SE_ROOT/downloads/MicrosoftEdgeWebView2RuntimeInstallerX64.exe"
[ -f "$WV2" ] || { echo "Installer fehlt: $WV2" >&2; exit 1; }

# WebView2 (Chromium) läuft unter Wine nur mit Windows-7-Kennung und ohne Sandbox stabil.
"$WINE" reg add "HKCU\\Software\\Wine\\AppDefaults\\msedgewebview2.exe" /v Version /t REG_SZ /d win7 /f >/dev/null
"$WINE" reg add "HKCU\\Environment" /v WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS /t REG_SZ /d "--no-sandbox" /f >/dev/null
# Solid Edge ignoriert WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS – die Chromium-Schalter wirken nur über die
# WebView2-Policy (Erkenntnis aus JustNotesa/solid-edge-wine-fixes).
WV2_ARGS='--no-sandbox --disable-gpu --disable-gpu-compositing --disable-direct-composition --disable-d3d11 --disable-software-rasterizer --in-process-gpu'
for h in HKCU HKLM; do
  "$WINE" reg add "$h\\Software\\Policies\\Microsoft\\Edge\\WebView2\\AdditionalBrowserArguments" /v Edge.exe /t REG_SZ /d "$WV2_ARGS" /f >/dev/null
done
# WebView2 zeichnet über DirectComposition (CreateSwapChainForComposition) – kann DXVK 1.10 nicht,
# die Startseite bliebe weiß. Deshalb nutzt nur msedgewebview2.exe Wines eigenes Direct3D.
for f in d3d11 dxgi d2d1 d3d10core; do
  "$WINE" reg add "HKCU\\Software\\Wine\\AppDefaults\\msedgewebview2.exe\\DllOverrides" /v "$f" /t REG_SZ /d builtin /f >/dev/null
done

$NIEDRIG "$WINE" "$WV2" /silent /install >"$SE_LOGS/webview2-install.log" 2>&1 \
  || echo "WebView2-Installer meldet Rückgabewert $? – siehe logs/webview2-install.log"

# Edge-Update-Dienste laufen sonst dauerhaft weiter (blockieren „wineserver -w“ und kosten Leistung).
for svc in edgeupdate edgeupdatem MicrosoftEdgeElevationService; do
  "$WINE" reg add "HKLM\\System\\CurrentControlSet\\Services\\$svc" /v Start /t REG_DWORD /d 4 /f >/dev/null 2>&1 || true
done
"$WINE" reg delete "HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Run" /v MicrosoftEdgeAutoLaunch /f >/dev/null 2>&1 || true
"$WINE" reg delete "HKLM\\Software\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Run" /v MicrosoftEdgeUpdate /f >/dev/null 2>&1 || true
"$WINESERVER" -k || true

APPDIR="$WINEPREFIX/drive_c/Program Files (x86)/Microsoft/EdgeWebView/Application"
if ls "$APPDIR"/*/msedgewebview2.exe >/dev/null 2>&1; then
  echo "WebView2 installiert: $(ls "$APPDIR" | grep -E '^[0-9.]+$' | head -1)"
else
  echo "WebView2 nicht gefunden unter $APPDIR" >&2
  exit 1
fi
