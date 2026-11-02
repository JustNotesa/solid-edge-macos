#!/usr/bin/env bash
# Richtet die Wine-Umgebung ein und installiert Solid Edge Community 2026 darin.
# Wie bei cryinkfly: eigener Prefix, winetricks win11, dann der Original-Installer.
# Zusätzlich: VC++ 2022 und .NET 4.8 (Solid Edge braucht beide), Installation still (ohne Klick-Dialoge).
set -euo pipefail
source "$(dirname "$0")/env.sh"

step() { printf '\n==> %s\n' "$*"; }
winpfad() { "$WINE" winepath -w "$1" 2>/dev/null; }

# NGen (.NET-Vorkompilierung) hängt unter Wine oft endlos bei 100 % CPU und blockiert damit den
# .NET-Installer. Dieser Wächter beendet NGen-Prozesse, die länger als 5 Minuten laufen.
ngen_waechter() {
  # Läuft als Hintergrund-Subshell und erbt sonst „set -e“/pipefail: Findet grep (noch) kein NGen, würde sich der
  # Wächter in der ersten Runde still beenden.
  set +e +o pipefail
  while true; do
    ps -axo pid=,etime=,command= | grep -E '(mscorsvw|ngen)\.exe' | grep -v grep | while read -r pid et _; do
      min=$(echo "$et" | awk -F'[-:]' '{n=NF; print (n>=3 ? $(n-2)*60 : 0) + $(n-1)}')
      [ "$min" -ge 5 ] && kill "$pid" 2>/dev/null && echo "NGen-Prozess $pid beendet (lief $et)"
    done
    sleep 30
  done
}

step "Wine-Prefix anlegen ($WINEPREFIX, Laufzeit $SE_RUNTIME)"
$NIEDRIG "$WINE" wineboot -u
"$WINESERVER" -w

step "winetricks: .NET Framework 4.8 (dauert ca. 30–45 Min)"
# Wine Mono (shared install) trägt sich als .NET 4.7 ein – in der 64- UND der 32-Bit-Registry-Ansicht. winetricks
# entfernt Mono, die Deinstallation bricht aber am fehlenden Framework\v4.0.30319\diasymreader.dll ab und lässt die
# Schlüssel stehen; winetricks löscht danach nur die 64-Bit-Ansicht. Der 32-Bit-.NET-4-Installer liest dann
# Wow6432Node, meldet „bereits installiert“ und beendet sich mit Erfolg, ohne etwas zu tun → dotnet48 bricht ab
# („ngen.exe not found“). Deshalb beide Ansichten vorher leeren.
for k in 'HKLM\Software\Microsoft\NET Framework Setup\NDP\v4' 'HKLM\Software\Wow6432Node\Microsoft\NET Framework Setup\NDP\v4'; do
  "$WINE" reg delete "$k" /f >/dev/null 2>&1 || true
done
"$WINESERVER" -w
ngen_waechter & WAECHTER=$!
trap 'kill $WAECHTER 2>/dev/null || true' EXIT
WINE="$WINE" $NIEDRIG winetricks -q --unattended dotnet48 2>&1 | tee "$SE_LOGS/winetricks-dotnet48.log"

step ".NET-Optimierungsdienst (NGen) abschalten"
"$WINESERVER" -k || true
for svc in clr_optimization_v4.0.30319_32 clr_optimization_v4.0.30319_64; do
  "$WINE" reg add "HKLM\\System\\CurrentControlSet\\Services\\$svc" /v Start /t REG_DWORD /d 4 /f || true
done
"$WINESERVER" -w

step "winetricks: Windows-Version auf Windows 11"
WINE="$WINE" winetricks -q win11

step "Mac-Tastatur: Cmd = Strg (Cmd+C/V/Z wie am Mac), rechte Option = Alt, linke Option bleibt für @ € { }"
for v in LeftCommandIsCtrl RightCommandIsCtrl RightOptionIsAlt; do
  "$WINE" reg add "HKCU\\Software\\Wine\\Mac Driver" /v "$v" /t REG_SZ /d y /f
done
"$WINE" reg add "HKCU\\Software\\Wine\\Mac Driver" /v LeftOptionIsAlt /t REG_SZ /d n /f
# „Immer oben“-Fenster (Mini-Werkzeugleisten, Kontextmenüs) nicht über andere Mac-Programme legen,
# wenn Solid Edge im Hintergrund ist (Standard „nonfullscreen“ lässt sie über allem schweben).
"$WINE" reg add "HKCU\\Software\\Wine\\Mac Driver" /v WindowsFloatWhenInactive /t REG_SZ /d none /f

step "Absturzdialog von Wine abschalten (Backtrace landet im Log)"
"$WINE" reg add "HKCU\\Software\\Wine\\WineDbg" /v ShowCrashDialog /t REG_DWORD /d 0 /f || true

step "Grafik: DXVK-macOS (Direct3D 10/11 → Vulkan → Metal) und gepatchtes Direct2D"
# Wines eigenes Direct3D schafft auf macOS nur Feature-Level 9_3, Direct2D braucht 10_0.
DXVK="$SE_ROOT/runtime/dxvk/dxvk-macOS-async-v1.10.3-20230507"
for f in d3d10core d3d11 dxgi; do
  cp "$DXVK/x64/$f.dll" "$WINEPREFIX/drive_c/windows/system32/"
  cp "$DXVK/x32/$f.dll" "$WINEPREFIX/drive_c/windows/syswow64/"
done
# d2d1: Freigabe-Reihenfolge korrigiert (sonst Use-after-free mit DXVK), siehe patches/patch-d2d1.py.
# Bevorzugt das d2d1 mit SVG-Unterstützung (nanosvg, Wine 11.21) aus JustNotesa/solid-edge-wine-fixes –
# ohne SVG bleiben die Menüband-Symbole leer.
# Erste Wahl: eigener Build (d2d1-build/, Wine 11.21 + SVG + ein geteiltes D3D-Gerät je Thread statt je Factory,
# Freigabe-Reihenfolge schon im Quelltext) – sonst ~200 nie freigegebene DXVK-Geräte, siehe docs/threads-und-haenger.md.
D2D1_EIGEN="$SE_ROOT/d2d1-build/out/d2d1.dll"
if [ -f "$D2D1_EIGEN" ]; then
  cp "$D2D1_EIGEN" "$WINEPREFIX/drive_c/windows/system32/d2d1.dll"
else
  D2D1_QUELLE="$SE_ROOT/downloads/se-wine-fixes/d2d1.dll"
  [ -f "$D2D1_QUELLE" ] || D2D1_QUELLE="$WINE_BIN/../lib/wine/x86_64-windows/d2d1.dll"
  python3 -I "$SE_ROOT/patches/patch-d2d1.py" "$D2D1_QUELLE" "$WINEPREFIX/drive_c/windows/system32/d2d1.dll"
fi
for f in d3d10core d3d11 dxgi d2d1; do
  "$WINE" reg add "HKCU\\Software\\Wine\\DllOverrides" /v "$f" /t REG_SZ /d native /f
done
"$WINESERVER" -w

step "Microsoft Edge WebView2 Runtime (Solid Edge verlangt sie beim Start)"
WV2="$SE_ROOT/downloads/MicrosoftEdgeWebView2RuntimeInstallerX64.exe"
if [ ! -f "$WV2" ]; then
  mkdir -p "$SE_ROOT/downloads"
  curl -sL -o "$WV2" "https://go.microsoft.com/fwlink/?linkid=2124701"
fi
"$SE_ROOT/install-webview2.sh"

step "Visual C++ 2022 Redistributable (aus dem Installer-Paket)"
$NIEDRIG "$WINE" "$SE_INSTALLER/ISSetupPrerequisites/MS VC++ 2022 Redist (x64)/VC_redist.x64.exe" /install /quiet /norestart || true
"$WINESERVER" -w

step "Solid Edge 2026 installieren (still, Log: logs/msi-install.log)"
$NIEDRIG "$WINE" msiexec /i "$SE_INSTALLER/Siemens Solid Edge 2026.msi" /qn /norestart \
  LICENSEDIR="$(winpfad "$SE_INSTALLER/LicenseFile")" \
  /l*v "$(winpfad "$SE_LOGS/msi-install.log")" \
  || echo "msiexec meldet Rückgabewert $? (unter Wine oft 103 trotz Erfolg) – maßgeblich ist, ob Edge.exe da ist."
"$WINESERVER" -w

if [ -f "$SE_EXE" ]; then
  step "OpenGL für die 3D-Ansicht: Mesa-Zink (GPU)"
  "$SE_ROOT/install-opengl.sh"

  step "Solid Edge: kein IE-Startfenster (verdeckt sonst die 3D-Ansicht), kein Ordered/Synchronous-Popup"
  # Aus JustNotesa/solid-edge-wine-fixes.
  "$WINE" reg add 'HKCU\Software\Wine\AppDefaults\Edge.exe\DllOverrides' /v mshtml /t REG_SZ /d "" /f
  "$WINE" reg add 'HKCU\Software\Wine\AppDefaults\Edge.exe\DllOverrides' /v ieframe /t REG_SZ /d "" /f
  "$WINE" reg add 'HKCU\Software\Siemens\Solid Edge\Version 226\PrefSets\Options-General' \
    /v DoNotShowOrderedDefaultDialog /t REG_BINARY /d 06000000000000000100000000000000 /f

  step "Normenstandard für neue Dokumente: $SE_STANDARD"
  "$SE_ROOT/set-standard.sh"
  step "Fertig. Starten mit: ./start.sh"
else
  echo "Edge.exe nicht gefunden – siehe $SE_LOGS/msi-install.log" >&2
  exit 1
fi
