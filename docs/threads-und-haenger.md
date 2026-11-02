# Threadflut, CPU-Last und Hänger von Edge.exe (Stand 02.11.2026)

## Kurzfassung

- **Ursache:** Codejock ToolkitPro rastert jedes SVG-Symbol einzeln und legt dafür jedes Mal eine **neue
  Direct2D-Factory** an: Factory → WIC-Render-Target → `CreateSvgDocument` → zeichnen → alles freigeben. Beim Start
  sind das 204 Symbole. Wines d2d1 erzeugt **je Factory ein eigenes D3D10-Gerät**. Bei DXVK 1.10.3 wird dieses Gerät
  **nie freigegeben**, weil ein Referenzzyklus über `SwapDeviceContextState` es festhält. Jedes Gerät behält
  10 Threads und rund 220 MB Grafikspeicher.
- **Folge:** Nach dem Start hat Edge.exe 2 058 Threads, davon 2 040 von DXVK (204 Geräte), und einen
  Speicher-Footprint von **48 GB** auf einem Mac mit 18 GB RAM. Jedes Backstage-Menü, jedes neue Dokument und jede
  neue Umgebung bringt weitere Geräte, so kommt eine Sitzung auf über 3 100 Threads.
- **Korrektur:** eine selbst gebaute `d2d1.dll` (Wine 11.21 + SVG-Patch + neuer Patch 0002). Factories eines Threads
  teilen sich **ein** D3D-Gerät, das zwischengespeichert bleibt. Dazu kommen ein Cache für die Shader-Übersetzung, die
  Freigabe-Reihenfolge direkt im Quelltext und eine Statistik.
- **Ergebnis:** 2 058 → **29 Threads**, 48 GB → **1,7 GB** Footprint. Die Zeit für die 204 Symbole auf dem UI-Thread
  sinkt von 12,5 s auf **2,7 s**, die Startlast endet nach ~31 s statt ~51 s. Menüband, Symbole und 3D-Ansicht
  funktionieren unverändert.
- **Hänger:** In den Messläufen nicht nachgestellt. Am wahrscheinlichsten hängen sie mit dem **Speicherdruck** durch
  die Gerätelawine zusammen (Kompressor/Swap), nicht mit der Threadzahl an sich (siehe Abschnitt 5).

## 1. Messaufbau

- Start wie im Projekt üblich: `MESA_WRAPPER=1 MESA_DIR=runtime/mesa-22.0.3-p SE_TESTDATEI='…\Training\block.par'
  ./mesa-test.sh zink …` (Mesa 22.0.3 Zink + opengl32-Vorschaltung, DXVK-macOS 1.10.3 async).
- Threads: `ps -M -p <pid> | wc -l` alle 2 s. Threadnamen: `sample <pid> 1`, die Zeilen `Thread_…: <name>`.
  Die Stacks selbst sind unter Rosetta nicht symbolisiert („unknown binary“).
- Speicher: `footprint -p <pid>`, `top -l 1 -pid <pid> -stats mem,cmprs,th`, `vm_stat`.
- Aktivität (immer `methode: post`, der Mauszeiger des Users bleibt unberührt): 20 Klicks auf die Menüband-Reiter,
  jeweils mit Fensterbild (gemessen als Antwortzeit), Hover über 13 Menüband-Symbole, 8× „Modell drehen“ per COM
  (`ansicht drehen 45°`). Dazu Backstage („Datei“) und neue Dokumente (Blech, Baugruppe).
- Statistik in der neuen d2d1: `WINEDEBUG_EXTRA=+d2dstat` (mit `+timestamp` für Zeiten). Sie protokolliert Factories,
  D3D-Geräte, ID2D1Devices, DC/WIC/HWND-Render-Targets, SVG-Dokumente und Shader-Übersetzungen, jeweils mit laufenden
  Summen und dem Referenzzähler des D3D-Geräts an wichtigen Stellen.
- Messskripte (Scratch, nicht im Projekt): `monitor.sh`, `lauf.sh`, `bench.sh`, `icons.py`, `threadlast.c`.

Läufe: **A** = bisherige DLL (`downloads/se-wine-fixes/d2d1.fixed.dll`), **B** = neue DLL mit `D2D1_SHARED_DEVICE=0`
(Wine-Verhalten: ein Gerät je Factory), **C/D** = neue DLL mit Standardeinstellung (D = endgültiger Build aus
`d2d1-build/`).

## 2. Befund: Threads und Threadarten

Vorher (Lauf A, bisherige d2d1):

| Zeitpunkt | Threads | CPU-Zeit kumuliert |
|---|---|---|
| 15 s | 360 | 0:27 |
| 30 s | 1 445 | 1:24 |
| 45 s | 2 069 | 1:53 |
| 60 s / 90 s (Leerlauf, 0 % CPU) | 2 063 / 2 058 | 2:06 / 2:08 |
| nach 20 Reiterwechseln | 2 109 (vorübergehend) | 2:13 |
| nach Hover / 8× Drehen | 2 060 / 2 062 | 2:15 / 2:24 |

Threadarten im Leerlauf (`sample`): **1 428 `dxvk-shader`, 204 `dxvk-submit`, 204 `dxvk-queue`, 204 `dxvk-cs`**,
~16 namenlose (Wine-Hauptthreads, Solid Edge, Mesa/Zink), je 1 `wine_rpcrt4_server`, `wine_threadpool_worker`,
`com.apple.NSEventThread`, `com.apple.rosetta.exceptionserver`.

- Je DXVK-Gerät kommen 10 Threads zusammen: CS-Thread, Submit- und Queue-Thread sowie 7 State-Cache-Worker
  (DXVK-Log: „Using 7 compiler threads“ bei 12 Kernen). 204 Geräte ergeben genau 2 040 Threads.
- Die übrigen ~18 Threads gehören zu Wine, Solid Edge, Mesa/Zink und MoltenVK. Sie bleiben in allen Läufen gleich.
- Kurzzeitig 60–90 zusätzliche **`wine_threadpool_worker`**: Wines Threadpool bei COM/RPC (u. a. die Messbrücke)
  und Dokumentwechseln. Sie enden nach kurzer Leerlaufzeit von selbst und sind kein Leck.
- Wer legt die Geräte an? Ein Relay-Trace zeigt 204× `d3d10_1.D3D10CreateDevice1`, alle aus d2d1
  (`d2d_factory_get_device`, Rücksprung d2d1+0x241a3) und alle auf dem UI-Thread 016c. Die DXVK-Logs enthalten 204×
  `D3D11CoreCreateDevice` mit Feature-Level 10_0.
- Reiterwechsel, Hover und Drehen erzeugen keine neuen Geräte, weil alle Menüband-Symbole beim Start gerastert
  werden. Neue Geräte entstehen bei Backstage („Datei“, +1), neuem Blechteil (+4), Baugruppe (+22) und weiteren
  Umgebungen und Dialogen. So wächst eine Sitzung auf über 3 100 Threads.

## 3. Ursache (belegt)

### 3.1 Codejock: eine Factory pro SVG-Symbol

Die Statistik von Lauf B zeigt für jedes der 204 Symbole dieselbe Folge, alle Factories vom Typ
`D2D1_FACTORY_TYPE_SINGLE_THREADED` und alle auf Thread 016c:

```
factory+ → d3d_device+ → d2d_device+ → wic_rt+ → svg_doc+ → (Zeichnen, EndDraw/Present) → svg_doc- → factory- → d2d_device- → wic_rt-
```

Summen nach dem Start: je **204 angelegt und 204 freigegeben** für Factory, ID2D1Device, WIC-Render-Target und
SVG-Dokument. In d2d1 gibt es also **kein** Referenz-Leck: weder im DC-/WIC-Render-Target noch im SVG-Patch noch im
Release-Order-Patch.
Gegenprobe mit der d2d1 ohne SVG-Patch (`d2d1.dll.vor-svg`): **18 Threads**, kein einziges DXVK-Gerät. Ohne
funktionierendes `CreateSvgDocument` nimmt Codejock diesen Weg gar nicht. Der SVG-Patch verursacht das Problem also
nicht, er schaltet nur den Codepfad frei.

Nebenbefund: Solid Edge bzw. ToolkitPro holt `D2D1CreateFactory` über die **Ordinalzahl 1**. Eine d2d1 mit anders
nummerierten Exporten stürzt sofort ab (`D2D1ComputeMaximumScaleFactor(NULL)`). Der Build vergibt die Ordinalzahlen
deshalb wie Wine in der Reihenfolge der `.spec`-Datei.

### 3.2 Wine d2d1: ein D3D-Gerät je Factory

`dlls/d2d1/factory.c`, `d2d_factory_get_device()`: Jede Factory ruft beim ersten DC-, WIC- oder HWND-Render-Target
`D3D10CreateDevice1()` auf und behält das Gerät bis zu ihrer eigenen Freigabe. Unter Windows kostet eine Factory fast
nichts. Unter Wine mit DXVK bedeutet sie jedes Mal ein komplettes Vulkan-Gerät (MoltenVK/Metal) samt Threads,
Speicherblöcken und Shader-Übersetzung.

### 3.3 DXVK 1.10.3: Referenzzyklus, das Gerät wird nie zerstört

Referenzzähler des D3D-Geräts in Lauf B, aus der Statistik:

| Stelle | Zähler |
|---|---|
| nach `D3D10CreateDevice1` | 1 |
| nach Aufbau des WIC-Render-Targets (+1 Diagnose-Referenz) | 23 (bzw. 24) |
| beim Present | 28 |
| Factory gibt ihre Referenz frei | 2 (davon 1 Diagnose) |
| **nach allen d2d1-Freigaben inkl. Diagnose-Referenz** | **1** (204 von 204) |

Die letzte Referenz kommt aus DXVK selbst. In `src/d3d11/d3d11_context_imm.cpp` (v1.10.3) hält der
Immediate-Context das zuletzt gesetzte `ID3DDeviceContextState` als öffentliche COM-Referenz:

```cpp
Com<D3D11DeviceContextState> m_stateObject;          // d3d11_context_imm.h, v1.10.3
…
Com<D3D11DeviceContextState> oldState = std::move(m_stateObject);
…
m_stateObject = newState;                            // SwapDeviceContextState()
```

`D3D11DeviceContextState` ist ein `D3D11DeviceChild`. Dessen erste öffentliche Referenz erhöht den Zähler des
**Geräts** (`GetParentInterface()->AddRef()`). Der Immediate-Context gehört aber dem Gerät, damit entsteht der Zyklus
Gerät → Context → State-Objekt → Gerät. Wines d2d1 ruft `SwapDeviceContextState` bei jedem Zeichenvorgang auf
(`d2d_device_context_draw`). Das Gerät bleibt daher immer übrig. Im aktuellen DXVK ist das behoben:
`Com<D3D11DeviceContextState, false> m_stateObject` ist dort eine private Referenz. DXVK-macOS steht aber auf 1.10.3.

Wines eigenes d3d11 (wined3d) als Gegenprobe wurde nicht wiederholt: Es liefert auf dem Mac nur Feature-Level 9_3,
dann scheitert d2d1 schon beim Anlegen des Geräts (README, Versuchsprotokoll Grafik). Der Zyklus ist im
DXVK-Quelltext eindeutig belegt.

### 3.4 Kosten eines übrig gebliebenen Geräts

- 10 Threads, im Leerlauf ohne CPU-Last.
- **~220 MB Grafikspeicher** (DXVK-Speicherblöcke über MoltenVK/Metal; auf Apple Silicon ist das normaler RAM).
  `footprint`: alte d2d1 **48 GB** (IOAccelerator 38 GB, „Owned physical footprint (graphics)“ 7 GB), neue d2d1
  **1,7 GB**. Der Speicherkompressor hielt mit der alten DLL rund 3,1 Mio. Seiten (16 KB, ≈ 50 GB unkomprimiert)
  mehr als ohne Solid Edge, mit der neuen ≈ 70 000 Seiten (≈ 1,1 GB). Edge.exe selbst: 1 668 MB komprimiert (alt)
  gegen 633 MB (neu). Swap war mit der alten DLL zu 4,6 von 5 GB belegt.
- Je Symbol ein neues Vulkan-Gerät plus die Übersetzung der d2d1-Shader: **61 ms** pro Symbol auf dem UI-Thread
  (Median 48 ms, Spitze 1,2 s), zusammen 12,5 s beim Start. Lauf B hat dabei schon den Shader-Cache. Die
  ursprüngliche DLL übersetzt die HLSL-Shader zusätzlich bei jedem ID2D1Device neu und liegt eher darüber.

## 4. Korrektur

Eigene `d2d1.dll`, gebaut aus den Quellen von Wine 11.21 (`d2d1-build/build.sh`, mingw-w64, ohne Wine-Buildsystem).
Sie enthält:

1. `0001-d2d1-implement-svg-documents-with-nanosvg.patch` (unverändert, aus `downloads/se-wine-fixes/`).
2. **`d2d1-build/patches/0002-d2d1-geteiltes-d3d-geraet.patch`**:
   - **Ein D3D-Gerät je Thread** (`dlls/d2d1/shared.c`): `d2d_factory_get_device()` fragt einen Cache, Schlüssel ist
     die Thread-ID des Aufrufers. Der Cache hält eine eigene Referenz, damit das Gerät das Muster „Factory anlegen,
     ein Symbol, alles freigeben“ überlebt. Alles, was ein Thread mit D2D macht, läuft ohnehin nacheinander.
     Factories verschiedener Threads behalten getrennte Geräte. **Multi-threaded Factories** behalten wie bisher ein
     eigenes Gerät, weil ihre Render-Targets nur die Sperre ihrer eigenen Factory nehmen. Endet ein Thread, gibt ein
     FLS-Callback (`FlsAlloc`) seine Cache-Referenz frei: Er hängt den Eintrag nur aus, die eigentliche Freigabe läuft
     später im Threadpool. `DllMain` fasst den Cache nicht an. Ist ein Gerät entfernt worden
     (`GetDeviceRemovedReason`), wird ein neues angelegt. Sperrregeln siehe Abschnitt 8.
   - **Shader-Cache:** Die konstanten HLSL-Shader von d2d1 (6 Stück) werden einmal je Prozess mit `D3DCompile`
     übersetzt statt bei jedem ID2D1Device, also bei jedem Render-Target.
   - **Freigabe-Reihenfolge im WIC-Render-Target** (DXGI-Oberfläche vor dem inneren Render-Target) jetzt im
     Quelltext. Damit ist der Byte-Patch `patches/patch-d2d1.py` für diese DLL überflüssig.
   - **Statistik** `WINEDEBUG=+d2dstat` (ohne den Kanal keine Ausgabe, keine zusätzlichen Referenzen).
   - Schalter `D2D1_SHARED_DEVICE` (Umgebung): `thread` (Standard), `process` (ein Gerät für alle
     Single-threaded-Factories), `0` (Wine-Verhalten, nur für Vergleiche).

Warum nicht DXVK reparieren? Selbst ohne den Zyklus würde jedes Symbol ein Vulkan-Gerät samt Threads und
Speicherblöcken anlegen und wieder zerstören, rund 60 ms je Symbol. Ein geteiltes, dauerhaftes Gerät ist ohnehin der
schnelle Weg. DXVK-macOS neu zu bauen wäre aufwendig. Auch DXVK-Optionen (`dxvk.numCompilerThreads`,
`dxvk.enableStateCache`) verringern nur die Threads je Gerät, nicht die Zahl der Geräte und nicht ihren Speicher.
Darauf wurde verzichtet. Ein Restzyklus bleibt: Das eine geteilte Gerät je Thread wird nie zerstört. Das ist gewollt
und ohne Nachteil.

Grenze: Eine Single-threaded-Factory darf ihre Objekte nur nacheinander verwenden. Würde ein Programm Factories
auf Thread A anlegen und sie dann **gleichzeitig** auf zwei anderen Threads benutzen, teilten sich diese ein Gerät.
Solid Edge tut das nicht (alle 204 + 27 Factories auf Thread 016c). Für den Notfall: `D2D1_SHARED_DEVICE=0`.

## 5. Messergebnisse vorher/nachher

| | A: alte d2d1 | B: neu, `D2D1_SHARED_DEVICE=0` | D: neu (Standard) |
|---|---|---|---|
| Threads nach 15 / 30 / 45 s | 360 / 1 445 / 2 069 | 524 / 1 763 / 2 144 | 116 / 124 / **30** |
| Threads im Leerlauf (60–90 s) | 2 058 | 2 059 | **29** |
| DXVK-Geräte | 204 | 204 | **1** |
| Threads nach Reiterwechseln, Hover und 8× Drehen | 2 062 | 2 062 | **31** |
| Footprint nach dem Start | 48 GB | – | **1,76 GB** |
| nach Backstage, Blechteil und Baugruppe (+27 Factories) | hochgerechnet +27 Geräte ≈ +270 Threads, +6 GB | – | weiterhin 1 Gerät, 2,0 GB |
| Zeit für 204 Symbole auf dem UI-Thread | ≥ 12,5 s | 12,5 s (61 ms/Symbol) | **2,7 s (13 ms/Symbol)** |
| Startlast (CPU > 5 %) endet nach | 51 s | 49 s | **31 s** |
| CPU-Zeit bis Leerlauf | 2:08 min | 2:05 min | 1:56 min |
| Fensterbild nach Reiterklick, Mittel / Max | 0,26 / 0,42 s | 0,24 / 0,30 s | 0,25 / 0,31 s |
| COM „drehen 45°“, Mittel | 2,83 s | 2,68 s | 2,69 s |

- Die **CPU-Spitzen beim Start** (6–8 Kerne) bleiben, die gesamte CPU-Zeit sinkt kaum. Diese Last kommt aus dem
  Start von Solid Edge selbst (Rosetta-übersetzter Code, .NET, Laden der DLLs), nicht aus Direct2D/DXVK. Mit der
  Korrektur ist sie nur früher vorbei. Im Leerlauf liegt die CPU-Last in allen Läufen bei 0 %.
- Die Antwortzeiten bei Reiterwechsel, Fensterbild und COM bleiben gleich: Diese Aktionen legen keine Geräte an.
  Teurer war vorher alles, was neue Symbole rastert (Start, Backstage, neue Umgebungen und Dokumente).

### Hänger einordnen

In keinem Messlauf gab es einen Hänger. Einordnung:

- **Threadzahl an sich: kaum Einfluss.** Mikromessung `threadlast.exe` im selben Prefix, mit 30 bzw. 2 100 ruhenden
  Threads:

  | Operation | 30 Threads | 2 100 Threads |
  |---|---|---|
  | Thread anlegen + beenden | 0,21 ms | 0,34 ms |
  | DLL laden + freigeben | 0,49 ms | 0,51 ms |
  | ausführbaren Speicher anlegen, ausführen, freigeben | 0,014 ms | 0,039 ms |
  | Code umschreiben und ausführen | 0,008 ms | 0,034 ms |
  | alle Threads anhalten und fortsetzen | 1,2 ms | 98 ms |
  | Thread-Liste (Toolhelp) | 3,5 ms | 76 ms |

  Das reicht nicht für Hänger von über einer Minute.
- **Speicherdruck: sehr wahrscheinlich.** Mit der alten DLL belegt Edge.exe 48 GB Footprint auf 18 GB RAM, in einer
  Sitzung mit 3 100 Threads (≈ 310 Geräte) sogar rund 70 GB. Das meiste liegt komprimiert im Kompressor, Swap war
  fast voll. Greift Solid Edge auf ausgelagerte oder komprimierte Seiten zu (Metal-Residency beim Abschicken von
  Command-Buffern, GC, Fensterwechsel), steht der UI-Thread, bis das System die Seiten zurückholt. COM-Aufrufe und
  Fensteraufnahmen laufen über denselben UI-Thread und antworten in dieser Zeit ebenfalls nicht. Dazu kommen beim
  Rastern neuer Symbole 60 ms bis 1,2 s je Gerät, alles auf dem UI-Thread. Die Korrektur beseitigt beides.
- **Falls trotzdem wieder ein Hänger auftritt** (mit der neuen DLL): währenddessen aufzeichnen

  ```bash
  P=$(pgrep -f 'Program/Edge\.exe' | head -1)
  sample $P 5 -file ~/Desktop/haenger-sample.txt
  sudo spindump $P 10 -file ~/Desktop/haenger-spindump.txt   # optional, braucht Passwort
  footprint -p $P | head -20; vm_stat; sysctl vm.swapusage
  ps -M -p $P | wc -l
  ```

  Rosetta-Stacks sind in `sample` nicht symbolisiert. Aussagekräftig sind dort aber die nativen Wartestellen
  (`semaphore_wait`, `psynch`, `IOAccelerator`) und die Speicherwerte.

## 6. Installation

```bash
./mesa-test.sh stopp               # Solid Edge beenden
./d2d1-build/build.sh --install    # baut d2d1-build/out/d2d1.dll und kopiert sie nach prefix/…/system32/d2d1.dll
```

- Das Skript lädt einmalig `downloads/wine-11.21.tar.xz` (~47 MB, SHA-256 geprüft) und bei Bedarf die
  nanosvg-Header (`downloads/se-wine-fixes/third_party/nanosvg/`). Es braucht `brew install mingw-w64` und baut in
  ~25 s.
- Die vorherige DLL bleibt als `system32/d2d1.dll.vor-geteilt` liegen. **Zurück:** diese Datei zurückkopieren
  (oder `downloads/se-wine-fixes/d2d1.fixed.dll`).
- **`install.sh` (geändert):** Liegt `d2d1-build/out/d2d1.dll` vor, kopiert es diese direkt nach `system32`. Sonst gilt
  der bisherige Weg (`patch-d2d1.py` auf `downloads/se-wine-fixes/d2d1.dll` bzw. Wines d2d1). Der Byte-Patch würde auf
  die neue DLL nicht passen und wird für sie auch nicht gebraucht. Die DLL-Overrides (`d2d1=native`) bleiben gleich.
  `env.sh` ist unverändert. Die DMG übernimmt die DLL mit dem Prefix.
- Prüfen: `WINEDEBUG_EXTRA=+d2dstat ./mesa-test.sh zink 60`, danach im Log
  `grep -c 'd3d_device+' logs/mesa-zink-*.log` → 1 (vorher 204), und `ps -M -p <pid> | wc -l` → ~30.

## 7. Dateien

| Pfad | Inhalt |
|---|---|
| `d2d1-build/build.sh` | Build und Installation der d2d1 |
| `d2d1-build/patches/0002-d2d1-geteiltes-d3d-geraet.patch` | geteiltes Gerät, Shader-Cache, Freigabe-Reihenfolge, Statistik (neue Datei `shared.c`) |
| `d2d1-build/out/d2d1.dll` | gebaute DLL (in system32 installiert) |
| `downloads/wine-11.21.tar.xz` | Wine-Quellen (nur `include/`, `dlls/d2d1`, `libs/uuid`, `libs/dxguid` werden benutzt) |
| `downloads/se-wine-fixes/third_party/nanosvg/` | nanosvg-Header (zlib-Lizenz) aus solid-edge-wine-fixes |
| `install.sh` | bevorzugt die eigene DLL (siehe oben) |

## 8. Nachtrag: Deadlock beim Erststart (behoben am 02.11.2026)

**Fehler der ersten Fassung von Patch 0002:** Beim ersten Start einer frisch aus dem DMG kopierten Umgebung blieb
Solid Edge im Startfenster stehen, bei 0 % CPU. Das Log (`err+all`) zeigt einen Lock-Order-Deadlock (ABBA):

- Thread 0024 hielt `d2d_shared_cs` und legte **unter dieser Sperre** das D3D-Gerät an (`D3D10CreateDevice1` →
  DXVK lädt Module und startet Threads). Dafür brauchte er `loader_section`.
- Thread 01b0 beendete sich gerade. Er hielt also `loader_section` und rief im `DllMain(DLL_THREAD_DETACH)` der d2d1
  das Aufräumen des Thread-Geräts auf. Dafür brauchte er `d2d_shared_cs`.
- Danach warteten acht weitere Threads auf `loader_section`. Beim Erststart passiert mehr gleichzeitig
  (WebView2, .NET, leere Caches), deshalb trat es dort auf.

**Nachgestellt:** `d2dstress.exe` (Scratch) startet kurzlebige Threads, die gleichzeitig Factories und
WIC-Render-Targets anlegen. Parallel laden andere Threads DLLs und entladen sie wieder und starten bzw. beenden
Threads. Mit der alten DLL hängt der Test in Runde 0; das Log zeigt dieselbe Kette (`d2d_shared_cs` ↔
`loader_section`).

**Behebung (in Patch 0002 enthalten), Regeln:**

1. `DllMain` ist wieder genau wie in Wine (nur `DLL_PROCESS_ATTACH`), ohne eigene Sperren und ohne Freigaben.
2. Unter `d2d_shared_cs` passiert nur Listenarbeit: Schlüssel vergleichen, ein- und aushängen, `AddRef`. Das Anlegen
   eines Geräts (auch die Prüfung mit `GetDeviceRemovedReason`) läuft **ohne** Sperre. Danach wird das Gerät unter
   der Sperre eingesetzt. Hat ein anderer Thread gewonnen (nur bei `process`), wird das eigene Gerät außerhalb der
   Sperre wieder freigegeben.
3. Das Thread-Ende behandelt ein FLS-Callback. ntdll ruft FLS-Callbacks in `LdrShutdownThread` auf, **bevor** es
   `loader_section` nimmt (`RtlProcessFlsData`, geprüft in `dlls/ntdll/loader.c` von Wine 11.21). Der Callback hängt
   den Eintrag nur aus. `Release` läuft über `TrySubmitThreadpoolCallback`, außerhalb jeder Sperre.

Damit hält kein Thread jemals beide Sperren zugleich, in welcher Reihenfolge auch immer.

**Tests nach der Behebung:**

| Test | Ergebnis |
|---|---|
| `d2dstress.exe 3 4`, alte DLL | **hängt** in Runde 0 (30× „wait timed out“) |
| `d2dstress.exe 3 4` und 5× `4 6`, neue DLL | alle fertig nach 3–6 s, 12 bzw. 24 Geräte angelegt und alle 12 bzw. 24 über FLS/Threadpool freigegeben, 0 Timeouts |
| Erststart aus frisch kopierter DMG-Umgebung (3×, `dmgtest3`–`dmgtest5`, neue DLL vor dem Start eingesetzt) | jedes Mal gestartet, 198 Symbole, **1** D3D-Gerät, 25–26 Threads, Footprint 1,26 GB, 0 Timeouts |
| Entwicklungs-Prefix mit block.par und Aktivität | Leerlauf 28–31 Threads, 1 Gerät, 1,76 GB, 204 Symbole in 2,3 s (11 ms je Symbol), 0 Timeouts |

Hinweis zum DMG: Er enthält noch die DLL **mit** dem Deadlock. Er muss mit `build-dmg.sh` neu gebaut werden, nachdem
`./d2d1-build/build.sh --install` die neue DLL in den Prefix gelegt hat.
