# Solid Edge 2026 Community Edition auf dem Mac – als eigene App bzw. DMG bauen

Diese Anleitung zeigt, wie man **Siemens Solid Edge 2026 Community Edition** auf einem Mac mit Apple Silicon
(M1–M4) lauffähig macht und daraus eine **„Solid Edge 2026.app“ in einer DMG** baut, die man auf seine
anderen Macs kopieren kann – ohne virtuelle Maschine, mit **GPU-Beschleunigung** für Oberfläche und 3D-Ansicht.

Grundlage ist [cryinkfly/Solid-Edge-on-Linux](https://codeberg.org/cryinkfly/Solid-Edge-on-Linux) (Wine-Prefix,
`winetricks win11`, Original-Installer) und die Linux-Fixes aus
[JustNotesa/solid-edge-wine-fixes](https://github.com/JustNotesa/solid-edge-wine-fixes). Auf dem Mac kommen
einige Probleme hinzu, die es unter Linux nicht gibt – vor allem bei der Grafik. Die Lösungen dafür sind hier
Schritt für Schritt beschrieben, die zugehörigen Skripte und Patches liegen in diesem Projekt.

> **Getestet:** MacBook Pro M3 Pro, 18 GB RAM, macOS 26, Solid Edge 2026 Community (Build 2510, Version
> 226.00.01.04). Teile, Skizzen, Extrusion, Bemaßung, Baugruppe und Blech wurden geöffnet bzw. angelegt;
> Langzeit-Praxis fehlt noch. Rückmeldungen willkommen.

---

> **Nur schnell eine laufende Version?** `git clone https://github.com/JustNotesa/solid-edge-macos.git && cd solid-edge-macos && ./setup.sh --app`
> – Installer-ZIP von Siemens vorher nach `~/Downloads` legen. Diese Anleitung erklärt, was dabei passiert.

## Inhalt

1. [Rechtliches – bitte zuerst lesen](#1-rechtliches--bitte-zuerst-lesen)
2. [Was am Ende herauskommt](#2-was-am-ende-herauskommt)
3. [Voraussetzungen](#3-voraussetzungen)
4. [Die Bausteine](#4-die-bausteine)
5. [Schritt für Schritt](#5-schritt-für-schritt)
6. [Was die Korrekturen machen (und warum)](#6-was-die-korrekturen-machen-und-warum)
7. [Fehlersuche](#7-fehlersuche)
8. [Bekannte Lücken](#8-bekannte-lücken)
9. [Quellen und Dank](#9-quellen-und-dank)

---

## 1. Rechtliches – bitte zuerst lesen

- **Die fertige DMG nicht öffentlich weitergeben.** Sie enthält die installierte Solid-Edge-Software und die
  persönliche Community-Lizenzdatei aus deinem Download. Die Community Edition ist kostenlos, aber nur für
  private, nicht-kommerzielle Nutzung und nur mit eigener Registrierung bei Siemens. Teile die **Anleitung und
  die Skripte**, nicht die DMG. Jede und jeder lädt den Installer selbst bei Siemens herunter.
- Wine (LGPL), DXVK (zlib), Mesa (MIT), nanosvg (zlib) und MoltenVK (Apache 2.0) sind freie Software; die
  eigenen Patches stehen unter denselben Lizenzen wie die jeweils gepatchte Software.
- Microsoft WebView2 Runtime und Visual C++ Redistributable werden aus den Original-Installern installiert.
- Solid Edge ist eine Marke von Siemens. Dieses Projekt hat nichts mit Siemens, Microsoft oder Apple zu tun.

---

## 2. Was am Ende herauskommt

- `SolidEdge2026-macOS.dmg` (~5,7 GB, LZFSE-komprimiert) mit einer `Solid Edge 2026.app`.
- Die App bringt alles mit: Wine, Grafik-Bibliotheken, einen fertig eingerichteten „Windows“-Ordner (Prefix)
  mit Solid Edge. Auf dem Ziel-Mac ist nur **Rosetta 2** nötig.
- Beim ersten Start kopiert die App den Prefix (~13 GB) nach
  `~/Library/Application Support/SolidEdge2026` und stellt ihn auf den aktuellen Benutzer um (Benutzername,
  Ordner Dokumente/Schreibtisch/Downloads, Laufwerk `Z:` = Mac-Festplatte).
- Leistung auf dem Test-Mac: Start in ~30 s, ~25 Threads, ~1,7 GB Speicher, Drehen/Zoomen flüssig.

---

## 3. Voraussetzungen

| Was | Wofür |
|---|---|
| Mac mit Apple Silicon, macOS 13 oder neuer | getestet mit macOS 26 |
| Rosetta 2 (`softwareupdate --install-rosetta`) | Wine läuft als x86_64-Programm |
| ~60 GB freier Speicher während des Baus | Prefix 13 GB, Installer 5 GB, DMG 6 GB, Kopien |
| Homebrew mit `winetricks`, `mingw-w64`, `p7zip` | .NET-Installation, Bau der zwei eigenen DLLs, Entpacken von Mesa |
| `python3` (bei macOS dabei) | kleine Byte-Patches |
| `Solid_Edge_Community_2026_2510.zip` | von [Siemens](https://resources.sw.siemens.com/en-US/download-solid-edge-community-edition) (Registrierung) |
| Zeit | ~1,5 h, davon ~45 min .NET 4.8 |

```bash
brew install winetricks mingw-w64 p7zip
```

> **Homebrew-Wine geht nicht:** Die Wine-Casks von Homebrew sind seit 01.09.2026 wegen Gatekeeper gesperrt.
> Deshalb ein portables Wine aus den Gcenx-Builds (siehe unten), das nicht installiert werden muss.

---

## 4. Die Bausteine

```
Solid Edge (Windows, x86_64)
 ├─ Oberfläche: Direct2D ──► d2d1.dll (eigener Build, Wine 11.21 + SVG + geteiltes Gerät)
 │                           └─► Direct3D 10/11 ──► DXVK 1.10.3 ──► Vulkan ──► MoltenVK ──► Metal (GPU)
 ├─ 3D-Ansicht: OpenGL ────► opengl32.dll (eigene Vorschaltung) ──► Mesa 22.0.3 Zink ──► Vulkan ──► MoltenVK ──► Metal (GPU)
 ├─ Startseite: WebView2 (Microsoft Edge Runtime, eigenes Direct3D von Wine)
 └─ alles in Wine Staging 11.18 (x86_64) unter Rosetta 2
```

| Baustein | Version | Quelle |
|---|---|---|
| Wine Staging | 11.18 | [Gcenx/macOS_Wine_builds](https://github.com/Gcenx/macOS_Wine_builds/releases) (portabel, in `runtime/Wine Staging.app`) |
| DXVK-macOS | 1.10.3 async (2023-05-07) | [Gcenx/DXVK-macOS](https://github.com/Gcenx/DXVK-macOS/releases) → `runtime/dxvk/` |
| Mesa für Windows | **22.0.3** (genau diese!) | [pal1000/mesa-dist-win](https://github.com/pal1000/mesa-dist-win/releases/tag/22.0.3) – lädt `install-opengl.sh` selbst |
| Wine-Quellen für d2d1 | 11.21 | [dl.winehq.org](https://dl.winehq.org/wine/source/11.x/) – lädt `d2d1-build/build.sh` selbst |
| nanosvg | aus solid-edge-wine-fixes | [JustNotesa/solid-edge-wine-fixes](https://github.com/JustNotesa/solid-edge-wine-fixes) `third_party/nanosvg` |
| WebView2 Runtime | Evergreen x64 | [Microsoft](https://go.microsoft.com/fwlink/?linkid=2124701) – lädt `install.sh` selbst |
| .NET Framework 4.8 | – | über `winetricks dotnet48` |

**Warum genau Mesa 22.0.3?** Neuere Mesa-Versionen geben das Zink-Bild über eine eigene Vulkan-Fläche aus, die
Wines Mac-Treiber über alle anderen Fenster legt (grauer Block, Menüs verdeckt). Ab 25.2 verlangt Zink außerdem
die Vulkan-Funktion `nullDescriptor`, die MoltenVK nicht hat. 22.0.3 liest das GPU-Bild zurück und gibt es
über GDI aus – das passt zu Solid Edges eigener Zeichenweise.

---

## 5. Schritt für Schritt

Alle Befehle im Projektordner ausführen. Schwere Schritte laufen auf Wunsch mit niedriger Priorität.

### 5.1 Projekt und Laufzeiten bereitlegen

1. Projekt (Skripte, Patches, Quelltexte) in einen Ordner legen, z. B. `~/solidedge`.
2. `Solid_Edge_Community_2026_2510.zip` in diesen Ordner legen und nach `installer/` entpacken
   (darin liegt `installer/Solid Edge/Siemens Solid Edge 2026.msi`).
3. Wine Staging 11.18 von Gcenx herunterladen und als `runtime/Wine Staging.app` ablegen.
   Da die Datei aus dem Internet kommt, einmal die Quarantäne entfernen:
   ```bash
   xattr -dr com.apple.quarantine "runtime/Wine Staging.app"
   ```
4. DXVK-macOS 1.10.3 nach `runtime/dxvk/dxvk-macOS-async-v1.10.3-20230507/` entpacken.
5. Die nanosvg-Header aus solid-edge-wine-fixes nach `downloads/se-wine-fixes/third_party/nanosvg/` legen.

`env.sh` setzt alle Pfade (`WINEPREFIX=prefix/`, Wine-Binary, `GALLIUM_DRIVER=zink`, Schalter der
Vorschaltung). Der Normenstandard für neue Dokumente ist `DIN Metric`, änderbar über `SE_STANDARD`.

### 5.2 Die zwei eigenen DLLs bauen

```bash
./d2d1-build/build.sh
```

```bash
./opengl-wrapper/build.sh runtime/mesa-22.0.3
```

- `d2d1-build/build.sh` lädt die Wine-11.21-Quellen, wendet zwei Patches an und baut nur `dlls/d2d1`:
  `0001` SVG-Dokumente mit nanosvg (aus solid-edge-wine-fixes), `0002` ein geteiltes Direct3D-Gerät statt eines
  neuen pro Icon (siehe Abschnitt 6).
- `opengl-wrapper/build.sh` baut die opengl32-Vorschaltung vor Mesa. Mesa selbst muss dafür einmal geladen
  sein – das erledigt `install-opengl.sh` (Schritt 5.3) automatisch; wer hier schon baut, lädt Mesa vorher von
  pal1000 nach `runtime/mesa-22.0.3/` (7z entpacken).

### 5.3 Alles installieren

```bash
./install.sh
```

Das Skript macht in dieser Reihenfolge:

1. **Prefix anlegen** (`wineboot`).
2. **.NET Framework 4.8** über winetricks (~45 min). Ein Wächter beendet hängende NGen-Prozesse
   (`mscorsvw.exe` hängt unter Wine endlos bei 100 % CPU); danach wird der NGen-Dienst abgeschaltet.
3. **Windows 11** als Windows-Version (`winetricks win11`).
4. **Mac-Tastatur:** beide Cmd = Strg (Cmd+C/V/Z wie am Mac), rechte Option = Alt, linke Option bleibt Option
   (für @ € { } auf der deutschen Tastatur).
5. **Menüs nicht über andere Programme legen:** `WindowsFloatWhenInactive=none` für Wines Mac-Treiber.
6. **Grafik:** DXVK (d3d10core/d3d11/dxgi) und die eigene `d2d1.dll` nach `system32`, Overrides auf `native`.
7. **WebView2 Runtime** still installieren; Solid Edge ignoriert die Umgebungsvariable für Browser-Optionen,
   deshalb werden `--no-sandbox --disable-gpu …` über die **WebView2-Policy** gesetzt; Edge-Update-Dienste aus.
8. **VC++ 2022** aus dem Installer-Paket, dann **Solid Edge** per `msiexec /qn` mit der mitgelieferten
   Community-Lizenz (Rückgabewert 103 unter Wine ist normal, maßgeblich ist, ob `Edge.exe` da ist).
9. **OpenGL** (`install-opengl.sh`): Mesa 22.0.3 laden, `libgallium_wgl.dll` mit
   `patches/patch-mesa-sampler-name.py` patchen, Vorschaltung + Mesa neben `Edge.exe`, Override nur für `Edge.exe`.
10. **Solid-Edge-Einstellungen:** kein IE-Startfenster (`mshtml`/`ieframe` für Edge.exe aus), kein
    „Ordered/Synchronous“-Popup, DIN-Vorlagen (`set-standard.sh`).

### 5.4 Testen

```bash
./start.sh
```

Prüfliste: Menüband mit Symbolen · Teil öffnen (z. B. `Training\block.par`) · 3D-Ansicht schattiert mit Kanten ·
Ansichtswürfel unten rechts mit Beschriftung beim Überfahren · Auswahlrechteck beim Ziehen ·
Rechtsklick-Kontextmenü · Skizze anlegen und „Skizze schließen“ · offenes Profil in einer Extrusion →
Profilfehlerassistent.

### 5.5 App und DMG bauen

```bash
./build-dmg.sh
```

- Baut `Solid Edge 2026.app`: Starter `app/launcher.sh` als `Contents/MacOS/SolidEdge2026`, `app/Info.plist`,
  Wine-Laufzeit und Prefix als Vorlage unter `Contents/Resources/` (per APFS-Klon, kostet keinen Platz).
- Räumt den Prefix auf (Temp, Logs, Laufwerks-Links, Diagnoseordner, Testdateien) und **entfernt persönliche
  Daten** (`app/prefix-anonymisieren.py`): Benutzername → `user` (der Starter setzt beim ersten Start den
  jeweiligen Mac-Benutzer ein), Zuletzt-geöffnet-Listen, Registry-Einträge mit Pfaden ins eigene Home
  (z. B. eigene Schriften), Installationsquelle, WebView2-Browserprofil, Nutzungsstatistik, Protokolle.
- Mit `--app` wird statt der DMG nur die App nach /Programme gelegt.
- Packt mit `hdiutil` als LZFSE (`DMG_FORMAT=ULMO` für LZMA: kleiner, aber deutlich langsamer).

Der Starter (`app/launcher.sh`) prüft Rosetta, kopiert beim ersten Start den Prefix, benennt das
Wine-Benutzerprofil um, passt die Registry an, setzt die Ordner-Links auf den Mac-Home-Ordner und startet
`Edge.exe` mit denselben Schaltern wie `env.sh`. Der Datenordner lässt sich zum Testen umlenken:

```bash
SE_DATA_DIR=/tmp/se-test "/Volumes/Solid Edge 2026/Solid Edge 2026.app/Contents/MacOS/SolidEdge2026"
```

**Immer einmal so testen** – Fehler, die nur beim allerersten Start auftreten, sieht man sonst nicht.

### 5.6 Auf dem anderen Mac

1. DMG öffnen, App nach /Programme ziehen.
2. Erster Start: **Rechtsklick → Öffnen** (App ist nicht von Apple signiert).
3. Ein paar Minuten warten (Kopie), danach startet Solid Edge.
4. Dateien öffnen/speichern: im Dialog „Dokumente“, „Desktop“, „Downloads“ (= Mac-Ordner) oder Laufwerk `Z:`.

---

## 6. Was die Korrekturen machen (und warum)

Das Muster bei fast allen Fehlern: Wine oder Mesa verhält sich in einem kleinen Detail anders als Windows, und
Solid Edge **schaltet die Funktion dann still ab**, statt eine Meldung zu zeigen.

| Symptom | Ursache | Korrektur |
|---|---|---|
| Solid Edge stürzt beim Start ab | Wines Direct3D kann auf dem Mac nur Feature-Level 9_3, Direct2D braucht 10_0 | DXVK; in d2d1 die Freigabe-Reihenfolge eines Render-Targets korrigiert (Use-after-free) |
| Symbole im Menüband fehlen | Wines d2d1 hat kein SVG | SVG-Dokumente mit nanosvg (Patch aus solid-edge-wine-fixes) |
| 2 000+ Threads, 48 GB Speicher, Hänger | Codejock legt je Symbol eine Direct2D-Factory an → je ein D3D-Gerät; DXVK 1.10.3 gibt sie wegen eines Referenzzyklus nie frei | d2d1 teilt ein Gerät je Thread (Aufräumen über FLS-Callback, nie unter dem Loader-Lock → kein Deadlock) |
| 3D-Ansicht leer | Solid Edge braucht OpenGL ≥ 3.2 im *Kompatibilitätsprofil*; macOS bietet nur 2.1 oder Core | Mesa 22.0.3 Zink (Vulkan → Metal); eine interne Variable heißt `sampler`, in Metal ein reservierter Typ → Shader-Fehler → Byte-Patch benennt sie um |
| grauer Block über allem (mit neueren Mesa) | Zinks Vulkan-Fläche liegt in Wines Mac-Treiber über allen Fenstern; Solid Edge zeichnet teils per GDI | Mesa 22.0.3 gibt per GDI aus |
| Bildexport/Vorschaubilder leer | Mesa kann nicht in Bitmaps zeichnen (`PFD_DRAW_TO_BITMAP`) | Vorschaltung zeichnet in ein verstecktes Fenster und kopiert ins Bitmap |
| Kein Ansichtswürfel, kein Auswahlrechteck | `view.dvx` liest `CRITICAL_SECTION.LockCount` mit Windows-Bedeutung (ab Vista); Wine zählt anders → Ansicht gilt als „nicht registriert“, das HUD bleibt aus | Vorschaltung entfernt beim Laden genau diese Prüfung (zwei bedingte Sprünge, Bytes werden vorher kontrolliert) |
| Würfelschrift/Bemaßungswerte fehlen | Mesas `wglUseFontOutlines` ist ein Platzhalter; Wines glu32-Zerlegung zerlegt E/F/W falsch | eigene Umsetzung mit eigener Flächenzerlegung (Nonzero-Füllregel) |
| „Skizze schließen“ tut nichts | Wines `OleSetMenuDescriptor` meldet beim zweiten Abmelden `E_FAIL`, Solid Edge bricht still ab | Vorschaltung macht in genau diesem Fall `S_OK` daraus |
| Startseite/Ansicht vom IE-Fenster verdeckt | altes IE-Startfenster liegt über der 3D-Ansicht | `mshtml`/`ieframe` für Edge.exe aus |

Die Korrekturen in der Vorschaltung sind über Umgebungsvariablen schaltbar (`MEMDC_GL_HUD_VIEW=fix`,
`MEMDC_GL_OLEMENU=fix`, Diagnose mit `MEMDC_GL_DEBUG=1`); **an den Solid-Edge-Dateien wird nichts verändert** –
alle Eingriffe passieren zur Laufzeit im Speicher und prüfen vorher die erwarteten Bytes.

Ausführliche Analysen (mit Adressen und Messungen) stehen in `docs/`.

---

## 7. Fehlersuche

- **Protokoll:** `logs/` (Installation) bzw. `~/Library/Application Support/SolidEdge2026/start.log` (App).
  Mehr Wine-Meldungen: `WINEDEBUG=err+all` vor den Start setzen.
- **Hängt beim Start, 0 % CPU:** im Log nach `wait timed out` suchen – das zeigt Deadlocks mit beteiligten
  Sperren.
- **Testläufe mit Diagnose:** `mesa-test.sh` startet Solid Edge mit einer Testdatei und optionalen Schaltern
  (Relay-Trace mit `TRACE=1 RELAY_INCLUDE='user32.…'` – **nie** auf KERNEL32/ntdll, das bringt Edge.exe zum Absturz).
- **Bilder des Fensters ohne Bildschirmfreigabe:** Der MCP-Server in `mcp/` kann Solid Edge per COM steuern und
  ein Bild des ganzen Fensters aus dem Prozess heraus aufnehmen (`fenster_bild`). Achtung: Das Eingabe-Werkzeug
  mit SendInput bewegt den echten Mauszeiger; besser `"methode":"post"`.
- **3D-Ansicht schwarz/grau:** prüfen, ob die Mesa-Dateien und die Vorschaltung neben `Edge.exe` liegen
  (`opengl32.dll`, `mesaogl32.dll`, `libgallium_wgl.dll`, `libglapi.dll`) und `GALLIUM_DRIVER=zink` gesetzt ist.
  Zum Vergleich CPU-Rendering: `GALLIUM_DRIVER=llvmpipe`.

---

## 8. Bekannte Lücken

- Die WebView2-Startseite („Starten“/„Lernen“) bleibt leer.
- Doppelklick auf `.par`/`.asm`/`.dft` im Finder öffnet sie noch nicht in Solid Edge.
- Die App ist nicht signiert/notarisiert (Rechtsklick → Öffnen beim ersten Start).
- Nur kurz getestet: Baugruppen, Blech, Zeichnungen, synchrone Konstruktion, Simulation, 3D-Druck.
- Wine/Mesa melden fehlende Vulkan-Funktionen (`logicOp`, `wideLines`, `depth_clip_enable`); in seltenen
  Fällen sind kleine Darstellungsfehler möglich.

---

## 9. Quellen und Dank

- [cryinkfly/Solid-Edge-on-Linux](https://codeberg.org/cryinkfly/Solid-Edge-on-Linux) – Grundidee
- [JustNotesa/solid-edge-wine-fixes](https://github.com/JustNotesa/solid-edge-wine-fixes) – WebView2-Einstellungen, SVG-Patch für d2d1
- [Gcenx](https://github.com/Gcenx) – Wine- und DXVK-Builds für macOS
- [pal1000/mesa-dist-win](https://github.com/pal1000/mesa-dist-win) – Mesa für Windows
- [WineHQ](https://www.winehq.org), [DXVK](https://github.com/doitsujin/dxvk), [Mesa](https://mesa3d.org),
  [MoltenVK](https://github.com/KhronosGroup/MoltenVK), [nanosvg](https://github.com/memononen/nanosvg)
