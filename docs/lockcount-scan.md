# Scan: Kontrollfluss hängt von CRITICAL_SECTION-Feldern ab (Wine-Semantik)

Stand 2026-11-02. Rein statische Analyse, keine Wine-Läufe. Anlass ist der belegte Fehler in `view.dvx`
(`docs/rechteck-und-wuerfel.md` 7.7). Gesucht wurde dasselbe Muster in allen PE-Modulen von Solid Edge 2026.

## 1. Ergebnis in Kürze

- Echte, unter Wine abweichende Prüfungen gibt es nur an **19 Stellen in 6 Modulen**. Alle prüfen
  `CRITICAL_SECTION.LockCount == 0` direkt nach `EnterCriticalSection` und springen dann mit `je` in den
  „nicht gesperrt“-Zweig.
- **Im laufenden Solid-Edge-Prozess sind davon nur 3 Module geladen:** `view.dvx` (schon behoben), `Visual.dll`
  (Speicher-Statistik-Thread) und `JUtil.dll` (Diagnosefenster „STOPWATCH“). `SEEC.dll`, `SETAL.dll` und
  `SEPreview.ocx` waren im protokollierten Lauf (`re-b/mods.txt`) nicht geladen.
- **Für „Skizze schließen“ gibt es keinen Kandidaten.** Die Module, die beim Verlassen des Profil- bzw.
  Skizzenmodus beteiligt sind (partDcx/partDax/partDex, profDex/profDax/profhlp, jengine, control, cmdspt,
  CMDSPT2D, hcnstr, jmcmddcx, VWCMDS/vwscmds, corevw, separt.dnx, acmdutl, ToolkitPro, Visual, render, JUtil,
  SEClasses, sprfhlp, Edge.exe, acasm) wurden vollständig geprüft. Sie haben entweder gar keinen CRITICAL_SECTION-Import
  (die Profil-/Befehls-Module nutzen nur SRWLock über die CRT) oder alle Enter-Stellen sind sauber
  (Tabelle 4). Der Fehler beim Schließen der Skizze hat also nach diesem Scan **eine andere Ursache**.
- Das Rechtsklick-Kontextmenü wird laut Zwischenstand schon durch den view.dvx-Fix behoben. Weitere Treffer
  dazu gibt es nicht.

## 2. Hintergrund: Was unter Wine anders ist

| Zustand | Windows ab Vista: LockCount | Wine (NT4-Semantik): LockCount |
|---|---|---|
| frei | −1 | −1 |
| gehalten, keine Wartenden | −2 (Bit 0 = 0 heißt gesperrt) | 0 |
| gehalten, n Wartende | −2 − 4·n | n |

`RecursionCount`, `OwningThread` und `LockSemaphore` verhalten sich auf beiden Systemen gleich. Unter Wine weichen also
nur Prüfungen auf `LockCount` im gehaltenen Zustand ab, etwa `== 0`, `< −1` oder `(x & 1) == 0` mit Wartenden.

## 3. Methode und Abdeckung

Skripte in `<scratch>/re-lock/`:

1. `stats.py`: Alle Dateien unter `Program/` (rekursiv, 3610) auf PE geprüft. Ergebnis: 1183 x64-Module mit
   Importen, davon 427 mit Import von `(Rtl)(Try)EnterCriticalSection` (aus kernel32, api-ms-win-core-synch-* oder ntdll).
   451 32-Bit-PEs wurden übersprungen, weil sie nicht im 64-Bit-Prozess laufen.
2. `sites.py`: Alle `call [rip+IAT]` und `jmp [rip+IAT]` auf Enter, TryEnter, Leave und Init gezählt:
   8720 + 37 direkte Enter/TryEnter-Aufrufe und 634 Tail-jmp-Thunks.
3. `blob.py`: Ermittelt Wrapper in 3 Ebenen: kleine Funktionen (≤ 0x80 bzw. 0x60 Byte), die Enter aufrufen, Tail-jmp-Thunks
   und Wrapper von Wrappern, z. B. `view.dvx 0x16740` und `ToolkitPro CXTPLockGuard`. Daraus entstehen insgesamt
   **124 506 Aufrufstellen** (direkt und über Wrapper). Je Stelle wurde ein Codefenster (Funktionsanfang bzw. −0xC0 bis
   +0x120) in einen Blob geschrieben und **einmal** mit `objdump -M intel` disassembliert.
4. `ana.py` / `ana2.py`: Symbolische Auswertung je Stelle. Die Skripte verfolgen Register, Stack-Slots,
   Speicher-Ladevorgänge und globale RIP-Adressen. Damit bestimmen sie die CS-Adresse im Argument `rcx`. Bei Wrappern
   wird der Wrapper-Körper mit ausgeführt, damit etwa Wächterobjekte erkannt werden, die einen Zeiger auf das Sperrobjekt
   speichern. Danach suchen sie jeden Speicherzugriff auf CS+0x8, +0xC, +0x10 oder +0x18:
   - `ana.py`: bis zu 30 Befehle nach dem Aufruf, Abbruch beim nächsten call/jmp/ret;
   - `ana2.py`: bis zu 80 Befehle, auch über weitere Aufrufe hinweg (flüchtige Register werden verworfen), dazu alle
     Zugriffe **vor** dem Aufruf im selben Fenster.

   Die CS-Adresse blieb bei 1168 von 124 506 Stellen unbestimmt. In geladenen Modulen betrifft das 11 Stellen in
   Visual/render: bedingte Enter-Funktionen, die einen Zeiger zurückgeben, von Hand geprüft und unauffällig.
5. Zusätzlich gibt es einen groben Musterfilter, unabhängig von der CS-Verfolgung (`loose.json`): `cmp/test DWORD [..], 0/−1/−2/1`
   mit `jcc` in den ersten 8 Befehlen nach jedem Enter. Ungeklärte Fälle mit passendem Versatz wurden von Hand geprüft:
   keine weiteren Treffer.
6. `expw.py` / `impw.py`: **Modulübergreifende** Wrapper, also 284 exportierte Lock-Funktionen, z. B.
   `sealloc!JSEAllocCriticalSection::ScopedEnterAndLeave`, `ToolkitPro!CXTPLockGuard::LockThread`, DCubed `mutex::lock`,
   `libtcsoacommon!ScopedCriticalSection`. Ihre 1882 Aufrufstellen in allen x64-Modulen wurden auf
   `cmp/test DWORD [reg+8|C|10|18]` mit `jcc` geprüft: **0 Treffer**. `ScopedEnterAndLeave` importiert niemand.
   view.dvx 0x16740 ist eine inlinierte Kopie desselben Wächters (`JSEAllocCriticalSection` = `{bool ok; CRITICAL_SECTION cs}`
   bei +8, daher LockCount = Sperrobjekt+0x10).

Prüfung der Methode: Beide bekannten view.dvx-Stellen (0xb50c, 0xb84c) werden gefunden.

## 4. Treffer

### 4.1 Echte Treffer (Wine-abhängig)

| Modul | geladen | Aufruf → Prüfung | Funktion / Zweck | Feld | Bedingung | Wine-abhängig | Folge unter Wine |
|---|---|---|---|---|---|---|---|
| view.dvx | ja | 0xb4fe → 0xb50c | 0xb4a0, Tabelle lebender JViews (std::set, Sperre 0x86500) | LockCount (Sperrobjekt+0x10) | `== 0 → je 0xb651` | **ja** | schon behoben (7.7) |
| view.dvx | ja | 0xb83e → 0xb84c | 0xb7e0, JView-Tabelle eintragen/suchen | LockCount | `== 0 → je 0xb991` (NULL) | **ja** | schon behoben (7.7): HUD, Würfel, Rechteck, Kontextmenü |
| Visual.dll | ja | 0x439845 → 0x439854 | Thread 0x439680: Speicherüberwachung (GetSystemInfo, GlobalMemoryStatusEx, GetGuiResources, VirtualQuery; Schlüssel `"mem[1]:commit"`), globale CS 0x8efbf0 | LockCount (0x8efbf8) | `== 0 → je 0x439a1f` (Leave, Sleep 2000, nächste Runde) | **ja** | Speicherstatistik wird nie in die Tabelle (0x8efc18) eingetragen. Keine Endlosschleife und kein Hänger, die Schleife läuft sowieso alle 2 s. Harmlos, kein UI-Bezug. |
| JUtil.dll | ja | 0xa28eb → 0xa28fa | 0xa1f40 = WindowProc des Diagnosefensters „STOPWATCH“ (STOPWATCH.INI), Taste `R` (0x52) | LockCount (0x17c568, CS 0x17c560) | `== 0 → je 0xa2908` (Reset 0xa3cf0 entfällt) | **ja** | Stoppuhr-Reset wirkt nicht. Nur Diagnose. |
| JUtil.dll | ja | 0xa2927 → 0xa2931 | dito, Leertaste (0x20) | LockCount | `== 0 → je 0xa29c4` (Start/Stopp per QueryPerformanceCounter entfällt) | **ja** | Stoppuhr startet/stoppt nicht. Nur Diagnose. |
| SEPreview.ocx | nein | 0x17bdb6 → 0x17bdc3 | Speicherüberwachungs-Thread wie in Visual (`"mem[1]:commit"`) | LockCount (global 0x511570) | `== 0 → je 0x17beb0` | **ja** | Statistik fehlt. Harmlos. |
| SEEC.dll | nein | 0x55ed75 → 0x55ed86 | 0x55ed30: Zustandsautomat mit Events (SetEvent/ResetEvent), CS = Objekt+8 | LockCount | `== 0 → je 0x55ee5f` | **ja** | Zustandswechsel und Events unterbleiben (Teamcenter-Anbindung) |
| SEEC.dll | nein | 0x55eba2, 0x55ec83, 0x55eff2, 0x55f142, 0x55f2bf (Wächter 0x55e560) → 0x55ebad, 0x55ec8e, 0x55effd, 0x55f150, 0x55f2cd | Methoden derselben Klasse | LockCount ([Wächter]+8) | `== 0 → je` | **ja** | dito |
| SETAL.dll | nein | 0x197fe5 → 0x197ff6, sowie 0x197e0a/0x197ef3/0x198262/0x1983b2/0x19852f (Wächter 0x1955a0) → 0x197e15/0x197efe/0x19826d/0x1983c0/0x19853d | gleicher Code wie SEEC (statisch gelinkte Bibliothek) | LockCount | `== 0 → je` | **ja** | dito, nur mit Teamcenter |

Das Muster ist überall gleich: Ein Wächterobjekt (`mov [rsp+x],1` als EH-Zustand) sperrt, dann folgt `if (cs.LockCount)`. Das
entspricht einem inlinierten „ist gesperrt?“ mit Vista-Annahme. Unter Wine ist der Wert 0, also wird der gesperrte Zweig
übersprungen.

### 4.2 Treffer der Auswertung, von Hand verworfen (keine CS-Feldzugriffe)

| Modul | Stelle | Grund |
|---|---|---|
| asmds.dll | 0x16b50, 0x6329a/0x6329e | Lauf über Tail-`jmp LeaveCriticalSection` hinaus in die nächste Funktion |
| IEACAD.dll | 0x283a46/0x283ada/0x283ae5 | CS-Zeiger liegt in `[rdi+0xab0]`, Zugriff auf Objektfeld `+0xab8` (Byte-Flag) |
| IEACAD.dll | 0x112f80a ff. | Register neu geladen, kein CS |
| libpoco.dll | 0x116e4b u. a. | `lock inc [rbx+8]` = Referenzzähler eines anderen Objekts |
| partDax/partDex/AEASM/DOCEXT/CAMPro/SEEC/autosketch/view.dvx (0x4c6b7, 0x4caa7) | `movups [rsp+0x70]` | Nullsetzen einer Stack-CS **vor** `InitializeCriticalSectionEx` (statische Bibliotheksroutine) |
| dcu2d7x/dcux2d*/dcuxcl* | `mov rax,[rax+0x18]` | Lauf über Tail-`jmp` hinaus in die nächste Funktion |
| TD_DbRoot, D3DCompiler_47, CCM_MeshToolkit, Conlin, ieasse, StdParts, PartFinder, DynMotionSolver, CrossCadWare, libgallium_wgl, ProESEServerSide | verschiedene | Register zwischenzeitlich anders belegt oder Strukturkopie, kein CS-Feld |

### 4.3 Abdeckung der für „Skizze schließen“ relevanten Module

Alle sind im Lauf geladen, außer `cnstr2d.dax` und `ProfileReader.dll`.

| Modul | Enter-Importe | geprüfte Stellen (direkt / über Wrapper) | Treffer |
|---|---|---|---|
| partDcx.dll | 11 | 11 / 0 | 0 |
| partDax.dll | 12 | 12 / 0 | 0 (nur 4.2) |
| partDex.dll | 13 | 13 / 2 | 0 (nur 4.2) |
| partSvc / partutl / partbrp | 1 / 1 / 3 | 1/0, 1/2, 3/0 | 0 |
| jengine.dll | 16 | 16 / 4 | 0 |
| control.dll | 16 | 16 / 3 | 0 |
| acmdutl.dll | 6 | 6 / 2 | 0 |
| view.dvx | 13 | 13 / 2 | nur die 2 bekannten |
| Visual.dll | 127 | 126 / 71 | 1 (Speicherstatistik) |
| render.dll | 57 | 57 / 10 | 0 |
| JUtil.dll | 6 | 6 / 5 | 2 (STOPWATCH) |
| ToolkitPro2410vc170x64U.dll (Menüband/XTPToolBar) | 20 | 17 / 282 | 0 |
| profDex, profDax, profhlp, cmdspt, CMDSPT2D, hcnstr, jmcmddcx, VWCMDS.DCX, vwscmds.dcx, corevw.dvx, separt.dnx, SEClasses, sprfhlp, FWKAUTO, GEOM2DSVC, acasm.dcx, partDcxdm, partmnk, partPS, Edge.exe | **0** (nur `AcquireSRWLockExclusive` aus der CRT bzw. gar nichts) | – | 0, Muster dort gar nicht möglich |

## 5. Vorschlag für Byte-Patches (im Stil von view.dvx)

Ersatz: bedingten Sprung durch NOP gleicher Länge ersetzen, also `0f 84 xx xx xx xx` → `66 0f 1f 44 00 00` und
`74 xx` → `66 90`. Vorher die erwarteten Bytes prüfen. Alle Module haben ImageBase 0x180000000; RVA = Adresse − Basis.
Die Dateiversätze gelten für die Originaldateien. Zur Laufzeit wird an `Modulbasis + RVA` gepatcht.

| Priorität | Modul | RVA jcc | Dateiversatz | erwartet | Ersatz | vorangehendes `cmp` (Prüfbytes) |
|---|---|---|---|---|---|---|
| erledigt | view.dvx | 0xb510 | 0xa910 | `0f 84 3b 01 00 00` | `66 0f 1f 44 00 00` | 0xb50c `83 78 10 00` |
| erledigt | view.dvx | 0xb850 | 0xac50 | `0f 84 3b 01 00 00` | `66 0f 1f 44 00 00` | 0xb84c `83 78 10 00` |
| optional | Visual.dll | 0x43985b | 0x438c5b | `0f 84 be 01 00 00` | `66 0f 1f 44 00 00` | 0x439854 `83 3d 9d 63 4b 00 00` |
| optional | JUtil.dll | 0xa2901 | 0xa1d01 | `74 05` | `66 90` | 0xa28fa `83 3d 67 9c 0d 00 00` |
| optional | JUtil.dll | 0xa2938 | 0xa1d38 | `0f 84 86 00 00 00` | `66 0f 1f 44 00 00` | 0xa2931 `83 3d 30 9c 0d 00 00` |
| nur mit Teamcenter | SEEC.dll | 0x55ed8a | 0x55e18a | `0f 84 cf 00 00 00` | `66 0f 1f 44 00 00` | 0x55ed86 `83 7b 08 00` |
| nur mit Teamcenter | SEEC.dll | 0x55ebb1 / 0x55ec92 / 0x55f001 / 0x55f154 / 0x55f2d1 | 0x55dfb1 / 0x55e092 / 0x55e401 / 0x55e554 / 0x55e6d1 | `74 4d` / `74 74` / `74 25` / `74 25` / `74 4b` | `66 90` | jeweils `83 78 08 00` 4 Byte davor |
| nur mit Teamcenter | SETAL.dll | 0x197ffa | 0x1973fa | `0f 84 cf 00 00 00` | `66 0f 1f 44 00 00` | 0x197ff6 `83 7b 08 00` |
| nur mit Teamcenter | SETAL.dll | 0x197e19 / 0x197f02 / 0x198271 / 0x1983c4 / 0x198541 | 0x197219 / 0x197302 / 0x197671 / 0x1977c4 / 0x197941 | `74 4d` / `74 74` / `74 25` / `74 25` / `74 4b` | `66 90` | jeweils `83 78 08 00` 4 Byte davor |
| nicht nötig | SEPreview.ocx | 0x17bdca | 0x17b1ca | `0f 84 e0 00 00 00` | `66 0f 1f 44 00 00` | 0x17bdc3 `83 3d a6 57 39 00 00` |

Für „Skizze schließen“ bringt keiner dieser Patches etwas, auch die optionalen nicht (Statistik bzw. Diagnosefenster).

**Grundsätzliche Alternative:** Statt einzelner Patches könnte Wines `RtlEnterCriticalSection` die Vista-Semantik
nachbilden (LockCount im gehaltenen Zustand ≠ 0). Das ist ein Eingriff in ntdll mit Risiko für Wine selbst.
Nach diesem Scan lohnt er sich nicht, weil es nur die oben genannten Stellen gibt.

## 6. Grenzen des Scans

- Nicht erfasst sind Enter-Aufrufe über Funktionszeiger, die zur Laufzeit gesetzt werden (28 `mov reg,[IAT]`-Ladevorgänge,
  nicht einzeln geprüft), virtuelle Lock-Methoden (`ILock`-vtables) und per `GetProcAddress` geholte Adressen. Ebenso
  wenig erfasst: Lesezugriffe auf CS-Felder in Funktionen ohne Enter in Reichweite, z. B. ein separates `IsLocked()`, das
  nur auf das Objekt zugreift.
- Wrapper wurden nur bis 0x80 Byte und 3 Ebenen erkannt. Größere Lock-Funktionen gelten als direkte Stellen. Ihre
  Aufrufer wurden nur über den groben Musterfilter (3.5) und die Export-Prüfung (3.6) erfasst.
- Weitere Unterschiede zwischen Wine und Windows (SRWLock-Interna, Condition Variables, Nachrichtenreihenfolge) waren
  nicht Gegenstand dieses Scans. Für „Skizze schließen“ ist als Nächstes eine Laufzeitspur des Befehlswegs nötig:
  XTPToolBar → WM_COMMAND → Befehl im Profil-Modul (profDex/partDcx) → Recompute/Verlassen der Umgebung. Sie zeigt, wo
  der Ablauf still endet.
