# Solid Edge Community 2026 auf macOS (Wine-Kompatibilitätsschicht)

Nachbau von [cryinkfly/Solid-Edge-on-Linux](https://codeberg.org/cryinkfly/Solid-Edge-on-Linux) für macOS auf
Apple Silicon. Solid Edge läuft als Windows-Programm in Wine, Wine selbst als x86_64 über Rosetta 2.
Alles läuft auf der GPU: Oberfläche (Direct2D/Direct3D) über DXVK → MoltenVK → Metal, 3D-Ansicht (OpenGL) über
Mesa-Zink → Vulkan → MoltenVK → Metal.

## Schnellstart: ein Befehl

Terminal öffnen (Programme → Dienstprogramme → Terminal), diese Zeile einfügen, Enter:

```bash
curl -fsSL https://raw.githubusercontent.com/JustNotesa/solid-edge-macos/main/install-online.sh | bash
```

Mehr ist nicht zu tun – außer zwei Dingen, die das Skript dir zeigt:

1. Es fragt, wie Solid Edge eingerichtet wird: **Installiert** (Enter, empfohlen – Einstellungen bleiben bei
   Updates, zum Entfernen gibt es „Solid Edge deinstallieren“) oder **Portabel** (alles steckt in der App, App
   löschen = alles weg).
2. Es öffnet die **Siemens-Seite**. Dort kostenlos registrieren und **„Solid Edge Community Edition“**
   herunterladen (`Solid_Edge_Community_2026_….zip`, ~5 GB). Die Datei einfach in **Downloads** lassen.
3. Das Terminal wartet, bis der Download fertig ist, und macht dann allein weiter (~30 Minuten).
4. Am Ende liegt **„Solid Edge 2026“ im Ordner Programme** und startet automatisch.

Ggf. fragt das Skript einmal nach deinem Mac-Passwort (für Homebrew bzw. Rosetta 2, falls noch nicht vorhanden).
Voraussetzungen: Mac mit Apple Silicon, ~40 GB freier Speicher während der Installation (danach ~15 GB).
Läuft etwas schief: den Befehl einfach noch einmal ausführen – erledigte Schritte werden übersprungen.

Wer lieber selbst klont: `git clone https://github.com/JustNotesa/solid-edge-macos.git && cd solid-edge-macos && ./setup.sh --app`
(Optionen: `./setup.sh --help`). Hintergründe: [ANLEITUNG-DMG.md](ANLEITUNG-DMG.md).

## Stand (02.11.2026)

Funktioniert: Installation, Start, Menüband mit allen Symbolen, 3D-Ansicht auf der GPU, Ansichtswürfel mit
Beschriftung, Auswahlrechteck, Hover-Hervorhebung, Bemaßungswerte, Kontextmenüs, Skizze schließen inkl.
Profilfehlerassistent, Bildexport (`View.SaveAsImage`), DIN-Vorlagen, Mac-Tastatur, Öffnen/Speichern in die
Mac-Ordner, DMG für andere Macs. Bekannte Lücke: Startseite („Starten“/„Lernen“) bleibt leer.

| Problem | Ursache | Lösung | Details |
|---|---|---|---|
| 3D-Ansicht leer | Solid Edge braucht OpenGL ≥ 3.2 *Kompatibilitätsprofil*, macOS hat nur 2.1 oder Core | Mesa 22.0.3 Zink (Ausgabe über GDI, kein `nullDescriptor` nötig) + Umbenennung der Shader-Variable `sampler` (in Metal reserviert) | `install-opengl.sh`, unten „OpenGL-Versuche“ |
| Oberfläche stürzt ab | Wines Direct3D schafft nur Feature-Level 9_3, Direct2D braucht 10_0 | DXVK + d2d1 mit korrigierter Freigabe-Reihenfolge | „Grafik: warum DXVK + Patch“ |
| Symbole fehlen | Wines d2d1 kann kein SVG | d2d1 mit nanosvg aus [JustNotesa/solid-edge-wine-fixes](https://github.com/JustNotesa/solid-edge-wine-fixes) | `d2d1-build/` |
| 2 000 Threads, 48 GB Speicher, Hänger | je Symbol ein D3D-Gerät, das DXVK 1.10.3 durch einen Referenzzyklus nie freigibt | eigene d2d1 mit einem geteilten Gerät je Thread (ohne Loader-Lock-Deadlock) | `docs/threads-und-haenger.md` |
| Bildexport/Vorschaubilder leer | Mesa kann nicht in Bitmaps zeichnen (`PFD_DRAW_TO_BITMAP`) | opengl32-Vorschaltung rendert in verstecktes Fenster und kopiert zurück | `opengl-wrapper/` |
| kein Würfel, kein Auswahlrechteck | `view.dvx` prüft `CRITICAL_SECTION.LockCount` mit Windows-Semantik, Wine zählt anders → HUD aus | Vorschaltung entfernt die Prüfung beim Laden (`MEMDC_GL_HUD_VIEW=fix`) | `docs/rechteck-und-wuerfel.md`, `docs/lockcount-scan.md` |
| Würfelschrift/Bemaßungswerte fehlen | Mesas `wglUseFontOutlines` ist ein Platzhalter, Wines glu32-Zerlegung fehlerhaft | eigene Umsetzung mit eigener Flächenzerlegung | `opengl-wrapper/font_outlines.c` |
| „Skizze schließen“ tut nichts | Wines `OleSetMenuDescriptor` meldet beim zweiten Abmelden `E_FAIL` | Vorschaltung fängt genau diesen Fall ab (`MEMDC_GL_OLEMENU=fix`) | `docs/skizze-schliessen.md` |
| Beenden hängt (Fenster weg, Edge.exe und Dock-Symbol bleiben) | render.dll löscht seine GL-Kontexte erst in `DLL_PROCESS_DETACH`; Mesa wartet dort auf seinen Arbeits-Thread, den `ExitProcess` schon beendet hat | Vorschaltung koppelt GL beim Prozessende von Mesa ab (Standard, `MEMDC_GL_EXIT=0` schaltet ab) | `docs/beenden-haenger.md` |
| WebView2 fehlt / Optionen wirkungslos | Solid Edge verlangt WebView2, ignoriert die Umgebungsvariable | Runtime still installieren, Optionen per Policy | `install-webview2.sh` |
| Menüs schweben über anderen Programmen | Wines Mac-Treiber hält „immer oben“-Fenster auch im Hintergrund oben | `WindowsFloatWhenInactive=none` | `install.sh` |

## Aufbau

| Pfad | Inhalt |
|---|---|
| `install-online.sh` | Ein-Befehl-Installation (lädt das Projekt, ruft `setup.sh --app --aufraeumen --starten`) |
| `setup.sh` | Einrichtung: Prüfungen, Downloads, DLLs (Release oder `--selbst-bauen`), Installation, App/DMG |
| `install.sh`, `install-opengl.sh`, `install-webview2.sh`, `set-standard.sh` | die eigentliche Installation in den Wine-Prefix |
| `env.sh`, `start.sh` | Umgebung, Start ohne App |
| `app/` (`launcher.sh`, `warten.js`, `deinstallieren.sh`, `Info.plist`, `Deinstallieren-Info.plist`, `prefix-anonymisieren.py`, `icon-erzeugen.sh`), `build-dmg.sh` | App-Bundle (klein ausgeliefert; Wine und Prefix kommen beim ersten Start dazu, installiert oder portabel), Deinstallations-App und DMG |
| `opengl-wrapper/` | eigene opengl32-Vorschaltung vor Mesa (Quelltext, `build.sh`, Tests) |
| `d2d1-build/` | eigene d2d1.dll aus Wine-11.21-Quellen + Patches |
| `patches/` | kleine Byte-Patches (Mesa `sampler`, d2d1-Freigabe-Reihenfolge) |
| `mcp/`, `tools/` | MCP-Server zur Steuerung von Solid Edge durch KI-Assistenten (COM, Fensterbild, Eingabe), Diagnose |
| `docs/` | Analyseberichte zu den einzelnen Fehlern |
| `mesa-test.sh` | Testläufe mit Diagnose-Schaltern |
| *nicht im Repo:* `installer/`, `runtime/`, `prefix/`, `dist/` | entstehen beim Einrichten (Siemens-Installer, Wine/DXVK/Mesa, Prefix, DMG) |

## Benutzung (Entwicklung)

```bash
git clone https://github.com/JustNotesa/solid-edge-macos.git && cd solid-edge-macos && ./setup.sh
```

```bash
./start.sh
```

- `./setup.sh --app` legt zusätzlich die App nach /Programme, `--dmg` baut eine DMG für weitere eigene Macs
  (persönliche Daten werden entfernt; **nicht öffentlich weitergeben** – sie enthält Solid Edge und die Lizenz),
  `--selbst-bauen` baut die zwei DLLs aus dem Quelltext (braucht `brew install mingw-w64`).
- Modus der App: `--app` fragt im Terminal (Enter = Installiert); `--installiert` bzw. `--portabel` (schließen
  `--app` ein) legen ihn ohne Rückfrage fest. Details: [ANLEITUNG-DMG.md](ANLEITUNG-DMG.md) Abschnitt 2.
- Dateien öffnen/speichern: Im Solid-Edge-Dialog „Dokumente“, „Desktop“ oder „Downloads“ (= gleichnamige
  Mac-Ordner) oder über `Z:\Users\<name>\…` beliebige Orte.
- Ausführlich: [ANLEITUNG-DMG.md](ANLEITUNG-DMG.md).

## Entwicklungsprotokoll

Die folgenden Abschnitte dokumentieren den Weg dorthin (Versuche, Messungen, verworfene Ansätze).

### OpenGL-Versuche mit Mesa

- Mesa für Windows 26.2.4 ([pal1000/mesa-dist-win](https://github.com/pal1000/mesa-dist-win), x64 release-msvc)
  liegt in `runtime/mesa/`. Der Build enthält llvmpipe und Zink und kennt `MESA_GL_VERSION_OVERRIDE`,
  `MESA_GLSL_VERSION_OVERRIDE`, `MESA_SHADER_CAPTURE_PATH`, `ZINK_DEBUG`.
- `mesa-test.sh llvmpipe|zink|aus|stopp` setzt `opengl32.dll` + `libgallium_wgl.dll` nur neben `Edge.exe`
  (Override `AppDefaults\Edge.exe\DllOverrides opengl32=native,builtin`), öffnet die Teilvorlage direkt,
  blendet `GALLIUM_HUD=fps,cpu` ein und schneidet alle Shader mit (`C:\shaders\*.shader_test`) – danach Auswertung
  von GLSL-Versionen und ob Geometry-/Tessellation-Shader vorkommen.
- **llvmpipe, Lauf 1 (01.11.2026):** Mesas `opengl32` wird geladen, aber Solid Edge hat nie einen Kontext:
  `logs/mesa-llvmpipe.log` enthält nur `Mesa: error: GL User Error: glDisable/glMaterialfv/glColorMask/
  glDebugMessageInsert … called without a rendering context`, 0 mitgeschnittene Shader.
  Vermutete Ursache: Wines `gdi32.SetPixelFormat` reicht jeden Aufruf direkt an das geladene `opengl32` (Mesa)
  weiter. Mesas `DrvSetPixelFormat` lehnt einen zweiten Aufruf auf dasselbe Fenster ab (gibt nur bei Pbuffern
  TRUE zurück, sonst FALSE – im Disassembly: `cmpl $0x1,%ebx; sete %sil`), Windows bzw. Wines eigenes OpenGL
  akzeptieren ihn bei gleichem Format (vgl. [prospero-win PR #731](https://github.com/mpereiraesaa/prospero-win/pull/731)).
  Vorbereitet: `patches/patch-mesa-setpixelformat.py` (liefert dort immer TRUE), aktivierbar mit `MESA_PATCH=1`;
  Beweis-Lauf mit `TRACE=1` (Relay-Trace der gdi32-Pixelformat-Aufrufe).
- **Trace-Ergebnis:** Die Vermutung war falsch – der SetPixelFormat-Patch ändert nichts. Die **Hauptansicht rendert
  mit llvmpipe** (Achsenkreuz und Hintergrund sichtbar). Die Fehler kommen von Nebenkontexten:
  `gdi32.ChoosePixelFormat(…) retval=0` → `SetPixelFormat(…,0,…) retval=0`, Aufrufer `Visual.dll+0x90b4a`.
  Disassembly dieser Funktion: Solid Edge prüft `GetObjectType(hdc) == OBJ_MEMDC` und fordert für **Speicher-DCs
  (Offscreen-Bitmaps)** `dwFlags = 0x68` (`PFD_DRAW_TO_BITMAP | PFD_SUPPORT_OPENGL | PFD_GENERIC_FORMAT`), für
  Fenster `0x425` (`DOUBLEBUFFER | DRAW_TO_WINDOW | SUPPORT_OPENGL | SWAP_COPY`). Wunschliste: 32-Bit-Farbe,
  Accum 64/48, Tiefe 24, Stencil 8/0, 2 Aux-Puffer; Solid Edge sucht selbst unter den ersten 256 Formaten und fällt
  sonst auf ChoosePixelFormat zurück. Mesas WGL kann nur in Fenster (und Pbuffer) zeichnen, **kein
  `PFD_DRAW_TO_BITMAP`** – das liefert unter Windows nur Microsofts GDI-Software-OpenGL. Ein Byte-Patch am Chooser
  hilft daher nicht (Mesa bräuchte dafür ein Fenster). Folge: Offscreen-Bilder (vermutlich Vorschaubilder/Thumbnails,
  ggf. Drucken/Bild speichern) fehlen; die 3D-Arbeitsansicht ist nicht betroffen.
- Shader-Mitschnitt blieb leer – im leeren Teil zeichnet Solid Edge offenbar ohne eigene GLSL-Programme
  (Achsenkreuz/Hintergrund). Test mit Geometrie: `SE_TESTDATEI='C:\…\Training\block.par'`.

- **Zink (Mesa 23.1.9 + Sampler-Namens-Patch):** läuft ohne Fehler, 3D-Fläche bleibt hellgrau. Log
  `logs/mesa-zink-mesa-23.1.9-p.log`: alle drei Swapchains gehören Zink (danach jeweils dessen Pipeline-Warnung),
  1512×816 → beim Andocken des PathFinders 1483×673, alle auf derselben `WineMetalView` → kein Konflikt mit DXVK.
  Wine-Quelltext (win32u/winemac, 10.18): Die Metal-View eines Kindfensters wird **versteckt** angelegt und erst in
  `client_surface_present` (bei jedem `vkQueuePresentKHR`) sichtbar; ihre Lage wird nur neu berechnet, wenn das
  GL-Fenster selbst verschoben wird oder sein **direkter** Elternteil (`update_children_window_state` geht nur eine
  Ebene tief) – verschiebt sich ein Großelternfenster (MDI-Dokumentfenster maximiert), bleibt die View an der alten
  Stelle (passt zum „weißen Rechteck mittig“ mit Wines eigenem OpenGL).
  Nächster Test: `WINEDEBUG_EXTRA=+fps,warn+vulkan,+macdrv` zeigt, ob/wie oft Zink präsentiert und wie Wine die
  Views setzt.

- **Zink präsentiert nur 3–4×** (+fps), danach zeichnet Solid Edge ohne `SwapBuffers` (Modus „GDI/OpenGL Hybrid
  (Backing store)“, `JRDrawHybrid` in `render.dll`). Die Schalter dafür liest `Visual.dll` über
  `JRenderRegistryBOOL(name, Standard 0)` aus `HKCU\<JGetCurrentUserPath>\Render` – aber **nur**, wenn
  `JGetDebugBOOL("ENABLE_RENDER_REGISTRY")` wahr ist (Debug-Schlüssel `HKCU\Software\Siemens\Solid Edge\Version 226\DEBUG`,
  DWORD-Werte wie unter `DEBUG\LOGGING\SYSTEM`). Bekannte Render-Werte (alle Standard 0): `UseSwapBuffers`,
  `UseHybridSwapBuffers`, `UseSingleSwapLayerBuffers`, `UseSeperateSwapLayerBuffers`, `ENABLE_TRIAD_CACHE`,
  `ENABLE_COLOR_LOGIC_OP`, `TraceBufferLogic`, `DisplayExtensionWarnings`.
  Test: `ENABLE_RENDER_REGISTRY=1` + `Render\UseHybridSwapBuffers=1` (+ `UseSwapBuffers=1`).

- **Grauer Block über allem (Zink ≥ 22.1):** Zink präsentiert über Kopper → Vulkan-Swapchain → Metal-View des
  Mac-Treibers, die über allen Win32-Inhalten liegt (ignoriert Clipping/Z-Reihenfolge) und das letzte Bild zeigt;
  Solid Edge zeichnet danach über GDI/ohne SwapBuffers.
  Quelltext-Befund: `wgl_require_gdi_compat` (nur Mesa 26.x) filtert **nur Pixelformate** auf `PFD_SUPPORT_GDI`,
  der Ausgabeweg von Zink bleibt Kopper (`stw_st.c`: `resource_create_drawable`, `zink_flush_frontbuffer` →
  `zink_kopper_present_queue`) – hilft also nicht. `nullDescriptor` ist ab Mesa 25.2 Pflicht.
  **Mesa ≤ 22.0** dagegen: `wgl.c` erzeugt Zink mit dem GDI-Software-Winsys (`zink_create_screen(winsys)`),
  `zink_flush_frontbuffer` liest per `pipe_texture_map` zurück, kopiert ins Displaytarget und gibt es mit
  `displaytarget_display` per GDI aus – GPU-Rendern, GDI-Ausgabe, keine Vulkan-Fensterfläche/Metal-View.
  Kopper kam mit 22.1. 22.0 hat außerdem eigene MoltenVK-Unterstützung (`zink_internal_setup_moltenvk`) und keine
  nullDescriptor-Pflicht. → `runtime/mesa-22.0.3/` (pal1000 22.0.3 msvc, enthält auch `test_wgl.exe`).
  Der „sampler“-String sitzt in 22.0.3 in der GLSL-Basistyp-Namensliste (nicht wie in 23.1 in der NIR-Modusliste) –
  erst ohne Patch testen.

**KosmicKrisp** (LunarG, Vulkan-Treiber direkt auf Metal, in Mesa): Vulkan-1.4-konform, braucht macOS 26
(Metal 4) und Apple Silicon. Tessellation ist eingebaut, **Geometry-Shader sind (Stand Nov. 2026) nicht als
fertig gemeldet** – nur auf der Roadmap. Haken für uns: Wine läuft auf dem Mac als x86_64 unter Rosetta, seine
Vulkan-Brücke kann nur eine x86_64-Treiberbibliothek laden; KosmicKrisp gibt es fertig nur für arm64
(LunarG-SDK, MacPorts `mesa +kosmickrisp`). Ein x86_64-Build wäre selbst zu bauen und ist nirgends erprobt.

### Offscreen-OpenGL (Ansichtswürfel, SaveAsImage, Vorschaubilder): `opengl-wrapper/`

Solid Edge rendert diese per OpenGL in **Speicher-DCs** (`PFD_DRAW_TO_BITMAP | PFD_SUPPORT_OPENGL |
PFD_GENERIC_FORMAT`, Formatwahl in `Visual.dll+0x90b4a`); die `SetPixelFormat`-Aufrufer liegen in `Visual.dll` bei
`+0xe2b83` (Code von `GTriad`, Würfel/Achsenkreuz), `+0x3c26d6` (`JRC::CreateContext`, Offscreen-Renderkontext) und
`+0x5379d9` (eigener Thread). Mesas WGL kann nur Fenster/Pbuffer → ChoosePixelFormat 0, SetPixelFormat(…, 0) FALSE.
`opengl-wrapper/memdc_gl.c` ist eine eigene `opengl32.dll` vor Mesa (Mesas `opengl32.dll` als `mesaogl32.dll`
daneben, 367 Exporte als Forwarder, 12 abgefangen). Für Speicher-DCs: Fensterformat statt Bitmapformat,
Kontext auf einem versteckten Hilfsfenster in Bitmapgröße, Rückkopie per `glReadPixels` in die DIB-Section
(bzw. `SetDIBitsToDevice` bei DDB/anderen Formaten) bei `glFlush`, `glFinish`, `SwapBuffers`, Kontextwechsel und
Löschen. Bau: `opengl-wrapper/build.sh` (mingw-w64), Test ohne Solid Edge: `opengl-wrapper/tests/run-memdc-test.sh`.
Protokoll: `MEMDC_GL_DEBUG=1`.

**Schalter der Vorschaltung** (alle per Umgebungsvariable; ohne gesetzte Variable keine Ausgabe und kein Eingriff):

| Variable | Wirkung | Auslieferung |
|---|---|---|
| `MEMDC_GL_HUD_VIEW=fix` | **Würfel- und Rechteck-Fix.** `view.dvx` prüft nach `EnterCriticalSection` die `CRITICAL_SECTION.LockCount` auf ≠ 0 (0xb50c/0xb84c). Windows ab Vista hat bei gehaltener Sperre −2, Wine 0, deshalb wird die 3D-Ansicht nie in die Ansichten-Tabelle (`0xb7e0`) eingetragen und das HUD (Würfel, Auswahlrechteck, HUD_REFRESH) nie angehängt. Der Fix ersetzt beide `je` durch NOPs (Bytes vorher geprüft) und trägt bereits erzeugte Ansichten einmal nach (`0xb7e0(this,1)`). Details: `docs/rechteck-und-wuerfel.md` §7.7 | **an** |
| `MEMDC_GL_OLEMENU=fix` | **„Skizze schließen“-Fix.** Wines `ole32!OleSetMenuDescriptor(NULL, …)` liefert `E_FAIL`, wenn für den Thread kein Menü-Hook mehr installiert ist; Windows nicht. jengine ruft beim Verlassen der Skizzen-/Profilumgebung (in-place-OLE) `SetMenu(NULL)` zweimal, der zweite Fehler bricht `HProfiles::_EndSketch` ab und Solid Edge bleibt im Skizzenmodus. Der Fix biegt jengines Importeintrag auf eine Hülle um, die beim Entfernen `E_FAIL` zu `S_OK` macht. Details: `docs/skizze-schliessen.md` | **an** |
| `MEMDC_GL_EXIT` | **Beenden-Fix**, ohne Variable aktiv (`0` schaltet ab). Solid Edge (render.dll, `JRC::~JRC` → `uglDeleteContext` → `wglMakeCurrent(NULL, NULL)`) räumt seine OpenGL-Kontexte erst in `DLL_PROCESS_DETACH` auf. `ExitProcess` hat dann Mesas Threaded-Context-Thread schon beendet, der Flush wartet für immer. Sobald `RtlDllShutdownInProgress()` gilt, gibt die Vorschaltung nichts mehr an Mesa weiter: wgl-Aufrufe melden Erfolg, die GL-Dispatch-Tabelle des Threads wird auf Mesas Leerlauf-Tabelle gesetzt (FLS-Callback zu Beginn von `LdrShutdownProcess`). Details: `docs/beenden-haenger.md` | **an** |
| `MEMDC_GL_HUD_VIEW=1` | nur Diagnose: JView-Felder pro Nachricht protokollieren (braucht `MEMDC_GL_DEBUG`) | aus |
| `MEMDC_GL_FRONT_FLUSH=1` | `glFlush` nach Zeichnen/Kopieren in FRONT und beim Verlassen von FRONT. Laut Code überflüssig: jede Ausgabe nach FRONT läuft über `Visual!uglCopyBuffer` (0x38a800, einziger `glCopyPixels`-Aufrufer), der danach `JFinish` → `glFinish` ruft (0x38aa29) | aus (nach A/B-Test) |
| `MEMDC_GL_HUD_BRL=log\|force` | Diagnose: `Visual!JGetBufferRegionLevel` durch Nachbildung ersetzen und protokollieren (`force`: für view.dvx mindestens 2) | aus |
| `MEMDC_GL_DEBUG=1` | Protokoll auf stderr, einschließlich Zeitreihe `Aktivität/s`, Aufrufer (Modul+RVA), HUDCamera-Zählung und Blit-Protokoll | aus |
| `MEMDC_GL_DUMP_RENDERERS=<s>` | `render.dll!g_DumpRenderers()` alle s Sekunden | aus |
| `MEMDC_GL_VENDOR` / `MEMDC_GL_RENDERER` | `glGetString` vortäuschen (ohne Wirkung auf Solid Edge) | aus |

**Würfelbeschriftung (`wglUseFontOutlinesA/W`, `opengl-wrapper/font_outlines.c`):**
- Mesas `wglUseFontOutlinesW` ist nur ein Platzhalter (`xor eax,eax; ret`). Visual legt die Beschriftung des
  Würfels aber damit an: Visual 0x1f590d, über `GLcontext::wglUseFontOutlinesW`, das den opengl32-Import aufruft;
  je Zeichen `count` 1, `WGL_FONT_POLYGONS`, GLYPHMETRICSFLOAT je Zeichen. Bei Fehlschlag bleibt die Liste 0 und der
  Würfel ohne Text.
- Die Vorschaltung setzt beide Funktionen selbst um, **nach Wines `dlls/opengl32/wgl.c`
  (`wglUseFontOutlines_common`; © 1999 Lionel Ulmer, 2005 Raphael Junqueira; LGPL 2.1+)**:
  - `GetGlyphOutline(GGO_NATIVE)` mit Einheitsmatrix bei Schriftgröße 1024 (em);
  - quadratische Splines per Bézier-Unterteilung bis `deviation` (≤ 0 → 1/1024);
  - je Zeichen eine Display-Liste mit `glFrontFace(GL_CCW)` und abschließendem `glTranslated` um den Vorschub.
  - `WGL_FONT_POLYGONS` wird **ohne glu32** zerlegt. Wines glu32-Tessellator zerlegt konkave Konturen falsch: „E“,
    „F“, „W“ wurden zu Dreiecken, belegt im memdc-Test mit Überdeckung ~50 %, unabhängig von COMBINE und Füllregel.
    Stattdessen kommt eine Trapez-Zerlegung nach der Nonzero-Regel von TrueType zum Einsatz:
    - waagerechte Streifen zwischen allen Eckpunkt- und Schnittpunkt-Höhen;
    - geschnittene Kanten nach x sortiert und nach Umlaufzahl gepaart;
    - je Spanne zwei `GL_TRIANGLES` gegen den Uhrzeigersinn.

    Das deckt Löcher (O, B, R, A), konkave und überlappende Konturen ab. Ein nativer Gegentest gegen eine
    Punkt-in-Polygon-Referenz (E, F, W, O mit Loch, überlappende Rechtecke) war ohne Abweichung; nur bei W lagen
    10 Pixel genau auf Kanten.
  - `WGL_FONT_LINES` zeichnet `GL_LINE_LOOP`s.
  - `extrusion` wird wie bei Wine nicht ausgewertet (flache Zeichen); unterstützt sind nur `TT_PRIM_LINE` und
    `TT_PRIM_QSPLINE`.
- Test: `opengl-wrapper/tests/run-memdc-test.sh` zeichnet 'W' als Fläche und als Umriss in den Speicher-DC und prüft
  die Pixel. Außerdem vergleicht er E, W, F, H, B, O, R und A (Arial und Tahoma, normal und fett) per Überdeckung mit
  GDI-`TextOut`; verlangt sind Schnitt/Vereinigung ≥ 75 %. In der Gegenprobe „nur-mesa“ scheitern die
  Schriftprüfungen erwartungsgemäß, weil Mesa dort nur einen Platzhalter hat.

**Buffer Regions** (`opengl-wrapper/bufregion.c`): Solid Edge sichert den gerenderten Hintergrund mit
`GL_KTX_buffer_region` (fragt per `wglGetProcAddress` `glDrawBufferRegion[ARB|EXT]` ab, prüft keinen
Erweiterungs-String) und zeichnet darüber die dynamischen Einblendungen (Vorhervorhebung, Ansichtswürfel). Mesa
hat das nicht → Einblendungen fehlen. Die Vorschaltung liefert `glBufferRegionEnabled/NewBufferRegion/
DeleteBufferRegion/ReadBufferRegion/DrawBufferRegion` und `wglCreate/Delete/Save/RestoreBufferRegion` (jeweils auch
mit ARB/EXT) und ergänzt `GL_KTX_buffer_region` bzw. `WGL_ARB_buffer_region` in den Erweiterungs-Strings. Umsetzung
auf der GPU: je Region ein FBO in Fenstergröße (RGBA8 bzw. DEPTH24_STENCIL8), Speichern/Wiederherstellen per
`glBlitFramebuffer` in Fensterkoordinaten; Scissor, Schreibmasken, Rasterizer-Discard, sRGB, FBO-/Renderbuffer-
Bindungen und Lese-/Zeichenpuffer werden gesichert und wiederhergestellt.
Befund danach: Solid Edge holt die KTX-Funktionen über Lazy-Binding-Thunks (Fallback: leerer Stub) und fragt nur
`glDrawBufferRegion` als Verfügbarkeitsprüfung ab; `JRC::SaveBufferRegion/RestoreBufferRegion` sind Stubs
(`E_NOTIMPL`). Ob Buffer Regions genutzt werden, entscheidet `JGetBufferRegionLevel(Merkmale)`: Stufe 2 bei Merkmal
Bit 16 (`JRENDER_HARDWARE_BACKINGSTORE`) und `DEBUG\ENABLE_BUFFER_REGIONS` (Standard 1). Die Renderer-Merkmale hängen
an der Herstellererkennung: `glGetString(GL_VENDOR)` wird gegen NVIDIA/ATI/AMD/INTEL/VMWARE/MICROSOFT geprüft,
ein Text (vermutlich `GL_RENDERER`) zusätzlich gegen QUADRO/GEFORCE/RADEON … – Mesa („Collabora Ltd“,
„Mesa/X.org“) ist unbekannt. Test: `MEMDC_GL_VENDOR` / `MEMDC_GL_RENDERER` täuschen die Texte vor (Standard: aus).
Ergebnis: Vortäuschen ändert nichts – Bit 16 hängt nicht am Hersteller. Belegte Kette (statisch):
- `render.dll!JRenderManager::CIJRenderSetupImp::InitializeRenderer` (RVA 0x193460–0x193ff4, Argumente hWnd, hDC,
  `dwFeatureHints`) bildet die Merkmale aus den **Hints des Aufrufers**: Hints 0 → 0x1000017 (GDI-Zweig), 0x20 →
  0x100D027 usw.; Bit 16 der Hints wird gelöscht (`btrl $0x10` bei 0x193567), außer Bit 25/26 sind gesetzt.
- Bit 16 wird nur im OpenGL-Zweig (Hints & 0x20) gesetzt: bei 0x19382e–0x193877 `|= 0x1011000`, wenn
  `GetObjectType(hDC)` ∉ {4 METADC, 12 ENHMETADC}, `Visual.dll!SupportsOpenGL(hDC, Merkmale)` TRUE (Pixelformat
  vorhanden und beschreibbar) und hWnd ≠ 0 – und nur, wenn vorher keiner der Sonderfälle greift
  ((Merkmale & 0xC000) = 0xC000, Bit 14, (Merkmale & 0x1080000) = 0x1080000). Weitere Setzstellen: 0x1938d3
  (`|= 0x80010000` bei Bit 31 ohne Bit 26), 0x19390c (Hints 0x2000022 → 0x82010022), 0x193b01 (`|= 0x80010020`).
- `JRC::ChoosePixelFormat` übernimmt die Merkmale nur (`JRC+0x3c`), `JAutoConfigureRendererFeatures` ändert nur
  Bits 13–15 und 24; `JGetBufferRegionLevel(Merkmale)` braucht Bit 16.
- Laufzeitbeleg: `MEMDC_GL_DUMP_RENDERERS=<s>` ruft `render.dll!g_DumpRenderers()` alle s Sekunden auf; die
  Ausgabe (`Primary=…,HWND=[…],Features=[…],Type=[…]`) erscheint mit `WINEDEBUG=+debugstr` im Log.
- Regionen: `Visual.dll` wählt das Verfahren bei 0x20e350 (`glRegionMode`, aus `GLcontext::glCreateRegion`):
  `glBlitFramebuffer` vorhanden → **Verfahren 3 (FBO-Blit)**, sonst KTX (1), sonst `wglCreateBufferRegionARB` (2),
  sonst 5. Mesa hat `glBlitFramebuffer` → KTX/WGL-Buffer-Regions werden nie gebraucht.
- Hover-Lauf (Blit-Protokoll): beim Überfahren stellt Solid Edge Tiefe und Farbe per Blit FBO → BACK wieder her,
  zeichnet, sichert BACK → FBO und wechselt mit `glDrawBuffer(GL_FRONT)` in den Vorderpuffer; `SwapBuffers` wird
  nie aufgerufen. Mesa 22.0.3 gibt den Vorderpuffer nur in `glFlush`/`glFinish` aus (`st_manager_flush_frontbuffer`,
  nur wenn der FRONT-Renderbuffer seit der letzten Ausgabe `defined` ist → `stw_st_framebuffer_flush_front` →
  `zink_flush_frontbuffer` → GDI). `MEMDC_GL_FRONT_FLUSH=1` löst genau diesen Weg aus, sobald in FRONT
  geblittet/kopiert/gezeichnet wurde bzw. FRONT verlassen wird; das Protokoll zeigt, ob Solid Edge in der
  FRONT-Phase selbst `glFlush`/`glFinish` ruft.
- Auswahlrechteck (Rubber-Band): `render.dll` (Funktion 0x1af520) wählt den Puffertyp; `DISABLE_FAST_RUBBERBAND`
  (Debug-BOOL, Standard **1**) bildet die schnellen Typen 3/4 auf die langsamen 1/2 ab, `FORCE_FAST_RUBBERBAND`
  (Standard 0) umgekehrt. Kandidaten für das Zeichnen: GDI-XOR (`SetROP2` + `Polyline`/`Rectangle`/`LineTo` sind in
  render.dll/Visual.dll importiert), GL-XOR (`glLogicOp` in Visual.dll/igl.dll), `JRDrawHybrid::rdraw_BeginXORHilite`
  /`rdraw_DrawLineStringGhost`. Messung: Vorschaltung protokolliert `glLogicOp`/`GL_COLOR_LOGIC_OP`, Relay auf
  gdi32/user32-Zeichenfunktionen.
  Messlauf 1: beim Ziehen kein GDI-XOR, kein logicOp; die Vorschaltung protokollierte damals nur die ersten N
  Aufrufe je Funktion – GL-Aktivität war damit nicht ausgeschlossen. Seither: `Aktivität/s:`-Zeitreihe (flush,
  finish, swap, blit, drawbuf FRONT/BACK, logicop, copypixels, glBegin Linien/Flächen/Punkte, Ops in FRONT).
  Ladephase: Zinks GDI-Ausgabe = `StretchDIBits`; `render.dll+0x575fc` (`SetROP2(R2_COPYPEN)`) gehört zu einem
  Modus-Zurücksetzen (`glLogicOp(GL_COPY)` + `glDisable(GL_COLOR_LOGIC_OP)` + `SetROP2`), ist also **kein**
  GDI-Overlay.
  Statische Analyse (`docs/rechteck-und-wuerfel.md`): Im Teil-Modus zeichnet der Auswahlbefehl
  (`partDcx!JMSmartLocateCmd`) das Rechteck als HUD-Transient über `IJRenderLite` (render.dll). Der Würfel ist
  `Visual!HUDCamera` (GL-Sofortmodus). Beide sind Overlay-Knoten (Typ 0x4E, `GSCENE_ENUM_OVERLAY`).
  `JRubberBand`/`FAST_RUBBERBAND` gehören zu den 2D-Umgebungen.
- Hover: `render.dll` liest `HiliteAsWireframe` (BOOL) und `HiliteRenderMode` (DWORD, Standard 1 bzw. 3) über
  `JRenderRegistryBOOL/DWORD` – nur wirksam mit `DEBUG\ENABLE_RENDER_REGISTRY`.

### Menüband-Symbole mit SVG-d2d1 (nanosvg) – Restfehler

- Quelle der Symbole: `Program/Images/Ribbon/Dark/*_<ID>.svg` (437 Dateien), die ID ist die Befehls-/Text-ID aus
  `ResDLLs/0007/Ribbon.drx` (z. B. 125 „Bemaßen“ → `GRP-GROUP-DIMENSION_125.svg`, 126 „Muster“). Dazu ~1000
  SVG-Ressourcen (Typ `SVG`) in den `.drx`-Dateien für Dialoge/Bildlisten.
- nanosvg-Test (`clang` + Header aus solid-edge-wine-fixes, gleiche Parameter wie der d2d1-Patch): alle 437 parsen,
  keine leeren Ergebnisse. Nicht unterstützte Merkmale: `clip-path` 40× (nur Eckenrundung fehlt – eine Umformung
  „Clip-Form statt Fläche“ verschlechtert diese Symbole, daher nicht Standard), `<text>` 3×, `mask` 2×; in den
  Ressourcen zusätzlich `<image>` 11×, `filter` 3×, `<use>` 2×. `patches/svg-vereinfachen.py` (style→inline,
  `<use>` auflösen, Clip nur mit `--clip`) ändert im Menüband 0 Dateien, bei den Ressourcen 2.
- „Bemaßen“ (125) rendert mit nanosvg bei 64/32/24/16 px vollständig → kein SVG-Problem; vermutlich gesperrter
  Zustand (Bemaßungsbefehle nur in der Skizze aktiv) bzw. dessen Darstellung.
- „Teilkonfigurationen“ (647) hat **keine** Datei in `Images/Ribbon` und keine SVG-Ressource mit dieser ID –
  das Symbol kommt aus einer anderen Quelle (Laufzeit-Trace nötig).

### Grafik: warum DXVK + Patch

Solid Edge zeichnet Teile der Oberfläche mit Direct2D. Wines Direct2D verlangt immer ein **Direct3D-10**-Gerät
(`D3D10CreateDevice1`, Feature-Level 10_0). Wines eigenes Direct3D (wined3d) schafft auf macOS nur 9_3
(OpenGL 4.1 ohne `ARB_polygon_offset_clamp`; MoltenVK ohne Geometry-Shader). DXVK liefert 10/11.
Mit DXVK 1.10.3 gab es aber einen Use-after-free: `d2d_wic_render_target_Release` gibt erst den D2D-Kontext
(und damit das letzte Geräte-Objekt) frei, danach die DXGI-Oberfläche; DXVK 1.10 hält das Gerät nicht über die
Oberfläche am Leben. `patches/patch-d2d1.py` tauscht die beiden Freigaben in einer Kopie von `d2d1.dll`, die als
native DLL in `system32` liegt (DLL-Overrides `d3d10core,d3d11,dxgi,d2d1=native` in der Registry).

**Seit 02.11.2026: eigener d2d1-Build** (`d2d1-build/build.sh --install`, Wine 11.21 + SVG + Patch 0002). Codejock legt je
SVG-Symbol eine neue D2D-Factory an, Wines d2d1 je Factory ein D3D-Gerät, und DXVK 1.10.3 gibt diese Geräte nie frei
(Referenzzyklus über `SwapDeviceContextState`). Ergebnis waren 204 Geräte, 2 058 Threads und 48 GB Footprint. Jetzt gibt
es ein geteiltes Gerät je Thread: 29 Threads, 1,7 GB. Die Freigabe-Reihenfolge steckt im Quelltext, `patch-d2d1.py` ist
dafür nicht mehr nötig. Details: `docs/threads-und-haenger.md`.

### Versuchsprotokoll Grafik

| Versuch | Ergebnis |
|---|---|
| wined3d OpenGL (Standard) | Feature-Level 9_3 → `d2d_factory_get_device Failed to create device` → Absturz in WIC (`BitmapClipper_Initialize` mit Nullzeiger) |
| `WINE_D3D_CONFIG=renderer=vulkan` | ebenfalls 9_3 (MoltenVK ohne Geometry-Shader) → gleicher Absturz |
| `csmt=0x0` | keine Änderung |
| GPU-Kennung NVIDIA (`VideoPciVendorID/DeviceID`) | keine Änderung (Grenze kommt aus den GL-Fähigkeiten) – zurückgenommen |
| DXVK-macOS 1.10.3 (d3d11+d3d10core+dxgi nativ) | D2D bekommt ein Gerät; danach Use-after-free: Lesezugriff auf `0xfeeefeeefeeefeee` in DXVK `d3d11.dll+0x135e40`, gerufen aus `d2d1.dll+0x4307d` (`d2d_wic_render_target_Release`) |
| `AppInit_DLLs=vulkan-1.dll` | wird von Wine 11 nicht mehr unterstützt |
| DXVK-macOS „repack“ (mit Wines dxgi) | `d2d_factory_get_device … hr 0x887a0004` (DXGI_ERROR_UNSUPPORTED) |
| GPTK-Wine 7.7 + D3DMetal 3.0 | Splash erscheint (GDI), aber D2D scheitert ebenfalls (`hr 0x80004005`): D3DMetal kann nur D3D11/12, Wines d2d1 braucht D3D10. Außerdem Lizenzdialog „License directory not found“ und auf dem geklonten Wine-11-Prefix Dialog „rundll32.exe – This application could not be started“ (Prefix-Downgrade 11 → 7.7) |
| GPTK-Wine + DXVK | `Wine was built without Vulkan support` → geht nicht |
| **Wine 11 + DXVK + gepatchtes d2d1** | **läuft** – kein Absturz, Solid Edge fragt nach WebView2 |

### D3DMetal-Lizenz

Die Datei `D3DMetal.framework/Versions/A/Resources/LICENSE` im Gcenx-Build enthält nur den BSD-2-Hinweis der
mitgenutzten Bibliothek xxHash, keine Bedingungen von Apple. Apples eigene Lizenz für das Game Porting Toolkit
liegt dem Gcenx-Build nicht bei (sie gehört zum Download auf developer.apple.com); die Weitergabe von D3DMetal ist
damit nicht geklärt. Die DMG nutzt D3DMetal nicht.
