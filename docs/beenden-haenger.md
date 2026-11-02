# Beenden hängt: Fenster weg, Edge.exe bleibt (Stand 02.11.2026)

## Kurzfassung

- **Symptom:** Nach „Beenden“ verschwindet das Fenster, Edge.exe bleibt mit 6–7 Threads bei 0 % CPU stehen, das
  Dock-Symbol bleibt für immer.
- **Ursache (belegt):** Solid Edge räumt seine OpenGL-Kontexte in einem **statischen Destruktor von `RENDER.DLL`**
  auf, also erst in deren `DLL_PROCESS_DETACH` während `ExitProcess`:
  `JRC::~JRC` → `JRC::RemoveAll` → `Visual!uglDeleteContext` → `wglMakeCurrent(NULL, NULL)`.
  Zu diesem Zeitpunkt hat `ExitProcess` alle anderen Threads schon beendet (Wine: `RtlExitUserProcess` →
  `NtTerminateProcess(0)`, erst danach `LdrShutdownProcess`). Mesa 22.0.3 muss beim Lösen des Kontexts den
  Befehlsstrom seines **Threaded Context** (Zink) leeren und wartet in `cnd_wait` (`SleepConditionVariableCS` →
  `RtlWaitOnAddress` → `NtWaitForAlertByThreadId`) auf seinen Arbeits-Thread. Den gibt es nicht mehr, also wartet
  der Hauptthread für immer, mit Loader-Lock.
- **Der Mac-Treiber ist nur Folge:** Wer danach im Dock „Beenden“ wählt oder Cmd+Q drückt, schickt ein Quit-Apple-Event.
  winemac fragt die Windows-Seite (`WM_QUERYENDSESSION`), deren Thread hängt aber schon in `ExitProcess`. Deshalb
  wartet der macOS-Hauptthread in `_handleAEQuit` → `_shouldTerminate`. Genau das zeigte das `sample` beim User.
- **Korrektur:** in der opengl32-Vorschaltung (`opengl-wrapper/memdc_gl.c`, Schalter `MEMDC_GL_EXIT`, Standard an).
  Sobald ntdll das Prozessende meldet (`RtlDllShutdownInProgress()`), geht kein Aufruf mehr an Mesa. Die GL-Dispatch-
  Tabelle des Threads wird auf Mesas Leerlauf-Tabelle gesetzt, wgl-Aufrufe melden Erfolg.
- **Ergebnis:** Alle Beenden-Wege enden in 2–5 s (vorher: Hänger auf allen Wegen, die `ExitProcess` nehmen).

## 1. Nachstellen

Entwicklungs-Prefix, Start wie `start.sh` mit `Training\block.par`, nach 70 s beenden, dann `ps`/`sample`:

| Weg | Wie ausgelöst | ohne Korrektur |
|---|---|---|
| COM `Application.Quit` | `mcp/solidedge_mcp.py --test com_zugriff '{"pfad":"Quit"}'` | **hängt**, 8 Threads |
| Schließen (`WM_CLOSE`) | an das Hauptfenster `EngineFrame` gepostet | **hängt**, 7 Threads |
| danach Dock-Quit | Quit-Apple-Event (`NSRunningApplication terminate`) | hängt weiter, `sample`: `_handleAEQuit` → `_shouldTerminate`, `NtWaitForAlertByThreadId` – wie beim User |
| Dock-Quit direkt, Dokument unverändert | Apple-Event | endet: winemac ruft nach `WM_ENDSESSION` selbst `TerminateProcess` |

Der Dock-Quit eines laufenden Solid Edge endet also über `TerminateProcess` ohne DLL-Abmeldung. Alle anderen Wege
(Datei → Beenden, Schließkreuz, COM) laufen über `exit()` → `CorExitProcess` → `ExitProcess` und hängen. Der Hänger beim
User ist die Folge davon: Erst schließen (Fenster weg, Prozess bleibt), danach im Dock „Beenden“ (kein Antwortpartner
mehr).

## 2. Beleg: wartender Thread und Aufrufkette

`WINEDEBUG=+module` zeigt die Abmeldungen des Hauptthreads 0024 bei `ExitProcess`. Die letzte Zeile ist

```
0024:trace:module:MODULE_InitDLL (00000001042B0000 L"RENDER.DLL",PROCESS_DETACH,0000000000000001) - CALL
```

ohne `RETURN`. Es gibt keine Meldung „section … wait timed out“: Wines Critical Sections blockieren beim Prozessende
nie (`RtlpWaitForCriticalSection` kehrt bei `RtlDllShutdownInProgress()` sofort zurück). Es muss also eine
Bedingungsvariable, eine SRW-Sperre oder `WaitOnAddress` sein.

Aufrufkette mit `tools/stackscan.c` (neu, siehe 6). Das Programm liest im hängenden Prozess die Register des Threads und
die Rücksprungadressen auf seinem Stack aus. Gekürzt, die Rahmen sind heuristisch, einzelne veraltete Einträge sind
entfernt:

```
rip ntdll.dll NtWaitForAlertByThreadId+0x14
    ntdll.dll RtlWaitOnAddress+0x17b
    ntdll.dll RtlSleepConditionVariableCS+0x36
    kernelbase.dll SleepConditionVariableCS+0x2c
    libgallium_wgl.dll +0x46947b / +0x95823 / … / +0x3192ae      (Mesa: Flush, Warten auf Fence)
    libgallium_wgl.dll stw_make_current+0x4fe
    mesaogl32.dll wglMakeCurrent+0xc
    opengl32.dll (Vorschaltung) wglMakeCurrent
    Visual.dll ?uglDeleteContext@@YAHPEAUHGLRC__@@@Z+0x3b         (= wglMakeCurrent(NULL, NULL), Visual 0x38bb75)
    Visual.dll ?RemoveAll@JRC@@QEAAXXZ+0x553
    Visual.dll ??1JRC@@QEAA@XZ+0xea
    RENDER.DLL +0x1bce89 (onexit-Tabelle der CRT, statische Objekte)
    ntdll.dll LdrShutdownProcess+0x9c
    ntdll.dll RtlExitUserProcess+0x28
    kernel32.dll ExitProcess+0xa
    mscoree.dll CorExitProcess+0x10d
    ucrtbase.dll _o_exit+0xaf
    Edge.exe +0x17ac
```

`uglDeleteContext` (Visual 0x38bb40) ruft nacheinander `JRenderCleanup(rc, 0)` und `wglMakeCurrent(NULL, NULL)` auf
(Import 0x180649c38). Der zweite Aufruf löst den aktuellen Kontext, `stw_make_current` leert ihn und wartet.

**Gegenprobe, welcher Mesa-Thread fehlt:** Mit `GALLIUM_THREAD=0` (kein Threaded Context, keine Zink-Flush-Queue)
und **ohne** Korrektur endet `WM_CLOSE` sauber nach 3 s. Mesa wartet also auf seinen eigenen Arbeits-Thread. Als
dauerhafte Lösung taugt der Schalter nicht, weil er Mesas Threaded Context für das ganze Rendern abschaltet.

Mesa weiß um das Problem: Sein eigenes `DllMain` räumt bei `DLL_PROCESS_DETACH` mit `lpReserved != NULL` gerade
deshalb nicht auf (Kommentar in `libgl_gdi.c`: Die Reihenfolge, in der Threads beim Prozessende verschwinden, ist nicht
verlässlich). Solid Edges render.dll ruft aber vorher, aus ihrem eigenen Detach, normale GL-Funktionen auf. Mit einem
Windows-Herstellertreiber geht das gut, mit Mesas Threaded Context nicht.

## 3. Ausgeschlossene Kandidaten

| Kandidat | Befund |
|---|---|
| eigene d2d1 (geteiltes Gerät, FLS-Callback) | Ihr FLS-Callback läuft am Anfang von `LdrShutdownProcess` und kehrt zurück (Rahmen `d2d1 … RtlProcessFlsData` liegt tiefer im Stack, der Thread ist schon weiter). `DllMain` der d2d1 tut beim Detach nichts. Sie ist nicht beteiligt, keine Änderung. |
| DXVK 1.10.3 | Mit der Korrektur laufen die Abmeldungen des Hauptthreads bis `ntdll.dll` durch (341 `PROCESS_DETACH` statt 202 bis zum Hänger), darunter `D3D11.DLL`, `dxgi.dll`, `d2d1.dll`, `libgallium_wgl.dll` und `winemac.drv`. Kein Hänger. |
| Mesa/Zink-Queue, `vkDestroyDevice` beim Entladen | Mesa räumt beim Prozessende selbst nicht auf (siehe oben). Der Hänger entsteht nur, weil Solid Edge vorher GL aufruft. |
| Vorschaltung (Hilfsfenster, eigene Threads) | Die Vorschaltung startet keine Threads. Sie reicht nur den Aufruf von render.dll weiter (`wglMakeCurrent` im Stack). |
| WebView2-Kindprozesse | nicht im Stack. Nach dem Ende bleiben keine Prozesse im Prefix übrig. |
| Wines Mac-Treiber | nur Folge (Abschnitt 1) |

## 4. Korrektur

`opengl-wrapper/memdc_gl.c`, Abschnitt „Prozessende“:

1. Beim Laden holt die Vorschaltung `RtlDllShutdownInProgress` (ntdll) sowie `_glapi_set_dispatch` und
   `_glapi_set_context` (Mesas `libglapi.dll`, bei neueren Builds ersatzweise aus `libgallium_wgl.dll`). Außerdem
   legt sie einen FLS-Index mit Callback an.
2. `wglMakeCurrent` mit Kontext merkt den Thread im FLS-Index vor.
3. **Prozessende:** ntdll setzt `process_detaching` und ruft danach, noch vor allen `DllMain`-Aufrufen, die
   FLS-Callbacks des beendenden Threads (`LdrShutdownProcess` → `RtlProcessFlsData`, Wine 11.21 `loader.c`). Der Callback
   sieht `RtlDllShutdownInProgress() == TRUE` und setzt für diesen Thread `_glapi_set_dispatch(NULL)` (= Mesas
   Leerlauf-Tabelle) und `_glapi_set_context(NULL)`. Das sind nur Thread-Zeiger in libglapi, kein Aufruf in den
   Treiber. Danach tun auch die direkt durchgereichten `gl*`-Funktionen nichts mehr, `glGetString` liefert NULL.
4. Zusätzlich prüfen alle abgefangenen Einsprünge, die sonst in Mesa landen würden (`wglMakeCurrent`,
   `wglDeleteContext`, `wglShareLists`, `wglSwapBuffers`, `wglSwapLayerBuffers`, `glFlush`, `glFinish`), zuerst
   `RtlDllShutdownInProgress()`. Ist es wahr, koppeln sie ab (falls noch nicht geschehen) und melden Erfolg, ohne Mesa
   aufzurufen.
5. Schalter `MEMDC_GL_EXIT=0` stellt das alte Verhalten her (nur für Vergleiche). Ohne Variable ist die Korrektur an,
   damit sie auch in der installierten App wirkt, deren Starter die Variable nicht kennt. `MEMDC_GL_DEBUG=1` protokolliert
   `Prozessende (FLS): GL von Mesa abgekoppelt …`.

**Warum das sicher ist:**

- Die Korrektur greift nur, wenn ntdll das Prozessende schon begonnen hat. Zu diesem Zeitpunkt hat Solid Edge seinen
  eigenen Abschluss hinter sich: Dokumente sind gespeichert bzw. verworfen (Speichern-Dialog), die Lizenz ist abgemeldet
  (`addinlm.dll` meldet sich früher ab), `exit()` hat die atexit-Handler von Edge.exe ausgeführt, .NET ist beendet
  (`CorExitProcess`). Im normalen Betrieb ändert sich nichts.
- Übersprungen wird nur das Aufräumen von GL-Kontexten, also GPU-Speicher, Vulkan-Objekte und Hilfsfenster. Das gibt
  das Betriebssystem mit dem Prozess ohnehin frei. Dateien schreibt dieser Code nicht.
- Alle übrigen DLL-Abmeldungen laufen unverändert weiter. Bei einem harten `TerminateProcess` gingen sie verloren.
  Deshalb wurde dieser Weg nicht gewählt.
- Mesas eigenes Verhalten ist dasselbe: Es räumt beim Prozessende nicht auf. Die Korrektur sorgt nur dafür, dass auch
  die Aufrufe von Solid Edge selbst Mesa in dieser Phase nicht mehr erreichen.

## 5. Tests

Mit Korrektur (Entwicklungs-Prefix, außer wo angegeben):

| Weg | Ergebnis |
|---|---|
| COM `Application.Quit` | endet nach 5 s, alle `PROCESS_DETACH` bis ntdll, keine Restprozesse |
| `WM_CLOSE` an das Hauptfenster | endet nach 4 s |
| Schließkreuz / Alt+F4 (`WM_SYSCOMMAND SC_CLOSE`), normaler Start ohne Diagnose | endet nach 3 s |
| Datei → Beenden (`StartCommand PartFileExit` = 57665) bei geändertem Dokument, Dialog „Änderungen speichern?“ → „Nein“ | endet nach 3 s |
| Dock-Quit (Quit-Apple-Event) | endet nach 2 s |
| Cmd+Q | Menüpunkt „Beenden“ des App-Menüs (`terminate:`), derselbe Weg wie der Dock-Quit. Nicht eigens ausgelöst, weil echte Tastendrücke den parallel arbeitenden User stören würden. |
| **installierte App** (`/Applications/Solid Edge 2026.app`, Prefix `~/Library/Application Support/SolidEdge2026`, neue DLL eingesetzt): `WM_CLOSE` / Dock-Quit | endet nach 4 s / 2 s |
| ohne Korrektur (`MEMDC_GL_EXIT=0`), `WM_CLOSE` | hängt (Gegenprobe) |
| ohne Korrektur, `GALLIUM_THREAD=0`, `WM_CLOSE` | endet (Gegenprobe zum Mesa-Thread) |
| normaler Start, Fensterbild | Menüband, 3D-Ansicht (block.par), Ansichtswürfel mit Beschriftung; Auswahlrechteck beim Ziehen sichtbar |
| `opengl-wrapper/tests/run-memdc-test.sh` | 1 Fehler (Arial 400 „W“ Überdeckung 1007/1234). Mit der alten Vorschaltung (HEAD) identisch, also keine Folge dieser Änderung. Die Prozessende-Meldung erscheint auch hier. |

## 6. Werkzeug `tools/stackscan.c`

`stackscan.exe <pid> [tid …]` (Windows-PID/TID, hex für TID) hält den Thread kurz an, liest `CONTEXT` und die
Stack-Grenzen aus dem TEB und gibt alle Stack-Werte aus, die auf eine Rücksprungadresse in einem geladenen Modul zeigen
(vorne ein `call`). Sie erscheinen als Modul+RVA, dazu der nächstgelegene Export. Anders als `winedbg` braucht es
keinen Debugger-Thread im Zielprozess und funktioniert deshalb auch, während der Loader-Lock gehalten wird.

```bash
x86_64-w64-mingw32-gcc -O2 -o stackscan.exe tools/stackscan.c -lpsapi
"$WINE" stackscan.exe 32          # Edge.exe hat unter Wine meist die PID 32 (0x20); TID des Hauptthreads 0024
```

Die Rahmen sind heuristisch, ältere Rücksprungadressen bleiben auf dem Stack stehen. Plausibel ist eine Kette, wenn
jeweils der Aufrufer an dieser Stelle die nächste Funktion aufruft (z. B. per Disassembly geprüft).

**Vorsicht beim Prefix der installierten App:** Wine-Programme dort nie aus einem Arbeitsordner unter `~/Documents`,
`~/Desktop` oder `~/Downloads` starten. Der wineserver der App öffnet dann diesen Ordner, macOS fragt nach (TCC-Dialog
„Solid Edge 2026 möchte auf Dateien im Ordner Dokumente zugreifen“), und bis zur Antwort steht der ganze wineserver.

## 7. Auslieferung

- `opengl-wrapper/build.sh` baut die DLL, `mesa-test.sh` (`MESA_WRAPPER=1`) und `install-opengl.sh` kopieren
  `opengl-wrapper/out/opengl32.dll` neben Edge.exe.
- Im Entwicklungs-Prefix und im Prefix der installierten App
  (`~/Library/Application Support/SolidEdge2026/prefix/…/Program/opengl32.dll`, alte Datei daneben als
  `opengl32.dll.vor-beenden-fix`) ist die neue DLL eingesetzt.
- **Noch offen:** `setup.sh` lädt ohne `--selbst-bauen` die `opengl32.dll` aus dem GitHub-Release v1.0. Dieser Stand
  hat die Korrektur nicht, das Release und `SHA256SUMS` müssen neu hochgeladen werden. Die Vorlage im App-Bundle
  (`/Applications/Solid Edge 2026.app/Contents/Resources/prefix`) und die DMG enthalten ebenfalls noch die alte DLL:
  mit `./setup.sh --app` bzw. `build-dmg.sh` neu bauen.
