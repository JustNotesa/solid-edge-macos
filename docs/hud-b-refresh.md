# Spur (b): Wie das HUD das Neuzeichnen anstößt, und wo es unter Wine abreißt

Stand 2026-11-01. Statische Analyse (objdump, Python `-I`) plus Auswertung des vorhandenen Relay-Logs
`logs/mesa-zink-trace-mesa-22.0.3-p.log` (Ziehphase ab Zeile 1592872, Prozess `016c`). Kein eigener Wine-Lauf.
Adressen sind RVAs (Image-Basis 0x180000000), „belegt“ = im Code bzw. Log gelesen, „vermutet“ = Schluss ohne
vollständigen Beleg.

## Kurzfassung

1. **HUD_REFRESH wird nicht von FlushBuffers abgearbeitet, sondern vom HUDController selbst.** Jede Nachricht an das
   3D-Fenster läuft zuerst durch `JView`-vtable+0x80 (view 0x19740). Dieser Vorfilter reicht unbehandelte Nachrichten
   an **`IHUDController::OnMessage`** (Visual 0x1f8120) weiter. Bei HUD_REFRESH zeichnet `OnMessage` die betroffene
   Region **sofort** über den Hilite-Weg von `render.dll`, also über denselben Weg wie die funktionierende
   Hover-Hervorhebung.
2. **Diese Weitergabe gibt es nur, wenn JView+0x4d0 (der HUD-Controller der Ansicht) gesetzt ist** (view 0x19e09–0x19e14).
   JView+0x4d0 wird nur in view 0x19b0b–0x19c0a gesetzt, unter `JGetBufferRegionLevel(Merkmale) > 1`. Das ist genau
   die Würfel-Bedingung aus Spur (c).
   - **Vermutet, gut begründet:** Würfel und Rechteck haben **eine** gemeinsame Ursache: JView+0x4d0 bleibt 0.
   - Das Rechteck legt sich seinen Controller zwar über corevw selbst an (JCoreView+0x4408), und corevw postet auch
     HUD_REFRESH (gemessen: 4×). Aber die Nachricht erreicht den Controller nie, weil JView sie ohne +0x4d0 nicht
     weiterreicht. Danach schluckt sie `JView::WindowProc`.
3. Die Wine-typischen Fallen sind laut Log **ausgeschlossen**: Die registrierte Nachricht wird zugestellt
   (4× `window proc … msg=c1d6` an 000500DE), es gibt nur einen Thread, keinen PostThreadMessage und keinen Timer im
   Spiel. Die 44 `InvalidateRect` zielen nicht auf das 3D-Fenster, und es kommt kein `WM_PAINT` an 000500DE.
4. **Ein Relay-Lauf (nur user32/gdi32) entscheidet**, ob `HUDController::OnMessage` erreicht wird. Siehe Abschnitt 5.

## 1. Zuordnung der Messwerte (belegt)

Modulbasen aus den `trace:loaddll`-Zeilen des Logs (Prozess 016c; 21d0 ist ein zweiter Prozess):
Visual.dll 0x1030C0000, render.dll 0x1042B0000, corevw.dvx 0x109740000, view.dvx 0x159010000,
jengine.dll 0x100A20000, control.dll 0x101320000, ACASM.DCX 0x10A2F0000.

| Log | Modul+RVA | Bedeutung |
|---|---|---|
| `RegisterWindowMessageW(HUD_REFRESH)` → 0xC1D6, ret 1030d3cb1 | Visual+0x13cb1 | statischer Initialisierer (je Modul eigene Variable) |
| `PostMessageW(000500de, c1d6)` ret 10979eaea | corevw+0x5eaea | `IHUDTransient::Add` (0x5e970), Rechteck-Start |
| `PostMessageW(000500de, c1d6)` ret 10979f282 | corevw+0x5f282 | Gegenstück `Remove` (Funktion 0x5f110), bei WM_LBUTTONUP |
| `ReleaseCapture` ret 103508612 | Visual+0x448612 | in `JDisplayWindowProc`-Umfeld, bei WM_LBUTTONUP |
| `PostMessageW(…, c07f/c0c8/c0b3)` ret 10aa8ec21/10aa97791/10aa8fb37 | ACASM.DCX+0x79ec21/0x7a7791/0x79fb37 | `SetFocusToGraphicView`, `SelectToolNoObjectLocated`, `FenceObjectsLocated` |
| 1320× `KillTimer`, 104× `SetTimer(000500de)` | ACASM.DCX+0x79fe58…0x7a01e5 | Timer des Auswahlbefehls je Mausbewegung |
| Fensterprozedur von 000500DE = 0x159033E40 | view+0x23e40 | **000500DE ist das JView-Fenster (3D-Ansicht)** |

Nebenbefund (belegt): Der aktive Auswahlbefehl ist **`JAsmSmartLocateCmd` in ACASM.DCX** (RTTI
`.?AVJAsmSmartLocateCmd@@`, `.?AVHUDGraphic@@`, `.?AVHUDClient@@`, Quelle `aLocate\DEV\SMLOCATE.CPP`) und nicht
`JMSmartLocateCmd` in partDcx.dll. Der Aufbau ist gleich (HUDGraphic + HUDClient + IHUDTransient), die
partDcx-Adressen im Hauptdokument passen aber nicht zu diesem Lauf.

Ablauf je Zug (Log-Zeilen relativ zu 1592872): WM_LBUTTONDOWN (1825) → c07f → WM_TIMER 2 → c0c8 → **Add + Post
HUD_REFRESH (2520)** → rund 165× WM_NCHITTEST/WM_SETCURSOR/WM_MOUSEMOVE → WM_LBUTTONUP (7607) → ReleaseCapture →
**Remove + Post HUD_REFRESH** → c0b3. Die Nachricht c1d6 wird jedes Mal an view+0x23e40 zugestellt. Danach gibt es
**keinen** einzigen GL-Aufruf, kein `WM_PAINT` an 000500DE und kein `InvalidateRect` auf 000500DE.
Die 44 `InvalidateRect` gehen an Fenster der Toolkit-Oberfläche (ret ToolkitPro2410…+0x1bd8dc usw.).

## 2. Die Kette HUD_REFRESH → Zeichnen

```
corevw 0x5e970  IHUDTransient::Add: Container {…-8006}->Add, dann PeekMessageW(hwnd=JCoreView+0x1e40,
                HUD_REFRESH, PM_NOREMOVE) und, falls nichts ansteht, PostMessageW(hwnd, HUD_REFRESH, 0, 0)  [belegt]
        │  Nachrichtenschleife (Thread 016c) → DispatchMessage
        ▼
view 0x23f30    JView::WindowProc. Vor jeder Fallunterscheidung (0x242af–0x242cc):
                r = this->vtbl[0x80](this, hwnd, msg, wParam, lParam); r ≠ 0 → DefWindowProc, fertig   [belegt]
        ▼
view 0x19740    JView-vtable+0x80 („Vorfilter“, vtable .?AVJView@@ 0x5ee18 Slot +0x80)                [belegt]
                · 0x19b0b–0x19c0a: holt den HUD-Controller nach JView+0x4d0, nur bei
                  JGetBufferRegionLevel(JView+0x48c) > 1 (sonst bleibt +0x4d0 = 0; Details: hud-c-controller.md)
                · 0x19e09–0x19e14: **JView+0x4d0 == 0 → Sprung 0x1bdfb, Rückgabe „nicht behandelt“**
                · 0x1a32c ff.: Vergleich mit weiteren registrierten Nachrichten, Rest landet bei 0x1bb30
                · 0x1bb63–0x1bb92: JView+0x4d0 → IHUDController vtable+0x48 (hwnd, msg, wParam, lParam)
        ▼
Visual 0x1f9240 XIHUDController Slot +0x48 = Sprung auf HUDController-vtable+0x40                         [belegt]
Visual 0x1f8120 HUDController::OnMessage (vtable .?AVHUDController@@ 0x67d418 Slot +0x40)                 [belegt]
                msg == HUD_REFRESH (0x1f8263):
                · alle anstehenden HUD_REFRESH mit PeekMessageW(PM_REMOVE) entfernen (0x1f82a2)
                · GetUpdateRect(hwnd, NULL, FALSE) ≠ 0 → nichts tun (WM_PAINT zeichnet ohnehin alles) (0x1f82c6)
                · view->vtbl[0x38] → QI {cb038050-…} → QI {ff156300-…} = IJRHilite;
                  nur wenn dieses IJRHilite in der Controller-Liste +0x70 steht (also per Attach angemeldet):
                · IJRHilite->vtbl[0x90](id) (0x1f847e), Rückgabe 0x41001 („behandelt“)
                WM_MOUSEMOVE/Buttons/WM_MOUSEHOVER/-LEAVE: Treffertest der Widgets; bei Änderung selbst
                PostMessageW(HUD_REFRESH) (0x1f8f82/0x1f8fd0) oder direkt IJRHilite+0x90 (0x1f901e)
        ▼
render 0x1afd00 IJRHiliteImp@JRenderRec Slot +0x90 (JRenderRec+0x18, Konstruktor 0x1ae5ba):
                JRenderRec::SendRecorderOpcode(12, id−1) (0x1b1930)                                       [belegt]
        ▼
render 0x1a3d0  JCanvas::SendOpcode (JCanvas-vtable 0x1d79c8 Slot +0x60), Opcode 12 → 0x1a822
                → JCanvas+8 vtable+0x10 = 0x18ff0                                                          [belegt]
render 0x18ff0  Rechteck der Grafik holen; nur bei JCanvas+0x108 ≠ 0: JCanvas vtable+0xc0 = 0x1b310        [belegt]
render 0x1b310  Region sofort neu zeichnen (Backing-Store-Restore, Overlay, SwapBuffers 0x1b4a8)           [belegt: Aufrufe;
                                                                                                          Einzelheiten nicht analysiert]
```

Damit ist auch geklärt, **wer `FlushBuffers` aufruft**:
- `JCanvas::FlushBuffers` (0x15800, vtable+0x180) wird von `JCanvas::SendOpcode` bei **Opcode 5** (0x1a575–0x1a58c)
  und von JCanvas-vtable+0x140 (0x153d0, Puffer-Auffrischung, Schalter `ENABLE_LEGACY_BUFFER_REFRESH`) aufgerufen.
  Der Weg ist also derselbe Recorder-Opcode-Weg (`SendRecorderOpcode`), nur mit einem anderen Opcode. [belegt]
- FlushBuffers ist die **volle** Ausgabe. Es entfernt anstehende HUD_REFRESH nur, weil ein vollständiges Bild das HUD
  ohnehin enthält. Am Ende stellt es bei Bedarf `InvalidateRect` + `PostMessageW(DeferPaint)` ein (0x163ec–0x16423).
  **Es ist nicht der Verbraucher von HUD_REFRESH.** Die Annahme in `rechteck-und-wuerfel.md` 6.2, gezeichnet werde
  „erst über FlushBuffers“, ist damit überholt. [belegt]
- `DeferPaint` (0xC094) verarbeitet `JView::WindowProc` bei 0x246e1: Zähler JView+0x488, beim ersten Mal
  `InvalidateRect(hwnd, NULL, TRUE)`. Im Ziehbereich wird DeferPaint nicht gepostet (nur in der Ladephase,
  jengine+0x56110). [belegt]

## 3. Wo es abreißt

| Glied | Bewertung |
|---|---|
| Post von HUD_REFRESH (corevw Add/Remove) | **läuft** (Log) |
| Zustellung an JView::WindowProc (registrierte Nachricht, gleicher Thread) | **läuft** (Log: 4× `msg=c1d6` an view+0x23e40) |
| Vorfilter view 0x19740 → HUDController::OnMessage | **Hauptkandidat:** nur mit JView+0x4d0 ≠ 0. Ohne diesen Wert endet der Vorfilter bei 0x19e14, und die Nachricht fällt bis 0x26570 durch, wo sie geschluckt wird |
| `GetUpdateRect ≠ 0` blockiert | unwahrscheinlich: Im Ziehbereich kommt kein WM_PAINT an 000500DE, die Update-Region ist also leer |
| IJRHilite nicht in Controller-Liste +0x70 (kein Attach) | Folgefehler desselben Kandidaten: Das Attach (view 0x19e3f) hängt ebenfalls an JView+0x4d0 |
| JCanvas+0x108 == 0 | unwahrscheinlich: Dasselbe Feld prüft FlushBuffers, und die Szene wird ausgegeben |
| Zeichnen in 0x1b310 | unwahrscheinlich: dieselbe Maschinerie wie Hover, und Hover funktioniert |

**Stützendes Indiz (belegt, aber nicht beweisend):** Während der Mausbewegungen postet `HUDController::OnMessage`
selbst nie HUD_REFRESH (kein `PostMessageW` aus Visual+0x1f8f82 bzw. 0x1f8fd0, also ret 1032b8f88 bzw. 1032b8fd6).
Ohne Würfel-Widget wäre das allerdings auch bei laufendem OnMessage möglich.

**Warum genau Wine (vermutet, gehört zu Spur c):** `JGetBufferRegionLevel` verlangt Merkmal-Bit 16
(`JRENDER_HARDWARE_BACKINGSTORE`). Unter Mesa fehlen `WGL_ARB_buffer_region` und `GL_KTX_buffer_region`, siehe
`hud-c-controller.md` 3.3. Ergänzend zu Spur (c): JView liest die Merkmale nur einmal. Bei view 0x198f8 holt es
+0x48c nur, solange der Wert 0 ist. Der Startwert des Render-Managers 0x0100D007 ist aber ≠ 0. Liest JView die Merkmale
bei seiner ersten Nachricht, bevor render 0x194269 den endgültigen Wert schreibt, bleibt der Wert ohne Bit 16 für immer
im Zwischenspeicher. Wer Bit 16 per Vorschaltung nachrüstet, muss deshalb sicherstellen, dass es **vor** der ersten
JView-Nachricht feststeht, oder JView+0x48c muss neu gelesen werden.

Nicht zutreffend (belegt): eine Nachrichtenschleife ohne Idle-Lauf (im Ziehbereich laufen 11× `WM_IDLEUPDATECMDUI`,
OnIdle läuft also), `PostThreadMessage`, Zeichnen aus einem Nebenthread (alles auf 016c), Timerauflösung (das HUD nutzt
keinen Timer; die Timer gehören zu ACASM) und der `PM_QS_PAINT`-Filter (kommt nicht vor).

## 4. Vorschlag

Es gibt keine eigene Code-Änderung für Spur (b): Der Mechanismus ist intakt, solange JView+0x4d0 gesetzt ist. Die
Änderung gehört zu Spur (c), also Bit 16 bzw. Buffer-Region melden und emulieren, unter Beachtung des Zwischenspeichers
JView+0x48c (Abschnitt 3). Vorher bestätigt **ein** Relay-Lauf, dass beide Symptome an derselben Stelle abreißen.

## 5. Der eine Messlauf (Relay nur user32/gdi32)

Begründung: `HUDController::OnMessage` ruft bei jedem HUD_REFRESH `GetUpdateRect(hwnd, NULL, FALSE)` auf, und zwar
von Visual+0x1f82c6, Rücksprung **Visual+0x1f82cc**. Sonst ruft im 3D-Fenster nur FlushBuffers diese Funktion auf
(render+0x16571, Rücksprung render+0x16577); dort steht davor `GetClipBox` (gdi32, render+0x163c3).

Solid Edge beenden, Filter setzen:

```bash
cd ~/solidedge && source env.sh && "$WINESERVER" -k && "$WINE" reg add 'HKCU\Software\Wine\Debug' /v RelayInclude /t REG_SZ /d 'user32.GetUpdateRect;user32.PostMessageW;user32.InvalidateRect;gdi32.GetClipBox' /f
```

Starten wie beim letzten Lauf:

```bash
cd ~/solidedge && MEMDC_GL_FRONT_FLUSH=1 MESA_WRAPPER=1 MEMDC_GL_DEBUG=1 MESA_DIR=runtime/mesa-22.0.3-p SE_TESTDATEI='C:\Program Files\Siemens\Solid Edge 2026\Training\block.par' WINEDEBUG_EXTRA=+relay ./mesa-test.sh zink 150
```

Nach dem Laden `wc -l` (Wert A), zwei langsame Rechteck-Züge, erneut `wc -l` (Wert B). Danach die Basen prüfen
(Visual und render können anders liegen):

```bash
L=<Log>; grep -a "trace:loaddll" $L | grep -a -E 'Visual.dll|RENDER.DLL|view.dvx' | grep -a '^016c'
sed -n 'A,Bp' $L | grep -a '^016c' | grep -a -E 'GetUpdateRect|GetClipBox|PostMessageW\(000500de|msg=0x0000c1d6|msg=c1d6' | head -100
```

Erwartete Rücksprungadressen bei den Basen dieses Logs: OnMessage-HUD_REFRESH `ret=1032b82cc`, OnMessage-Mausweg
`ret=1032b8fe8`, FlushBuffers `GetUpdateRect ret=1042c6577` bzw. `GetClipBox ret=1042c63c9`.

| Befund im Ziehbereich | Bedeutung | Nächster Schritt |
|---|---|---|
| Nach jedem `msg=c1d6` an 000500DE **kein** `GetUpdateRect(000500de,…) ret=Visual+0x1f82cc` | OnMessage wird nie erreicht: **JView+0x4d0 = 0**, also dieselbe Ursache wie beim Würfel | Änderung aus Spur (c) (Bit 16 bzw. Buffer-Region), Zwischenspeicher +0x48c beachten |
| `GetUpdateRect … ret=Visual+0x1f82cc` mit `retval=00000000`, aber keine GL-Aktivität | OnMessage läuft, aber das IJRHilite steht nicht in der Controller-Liste (kein Attach) oder JCanvas+0x108 = 0 | statisch Visual 0x1f9740 (Attach) bzw. render 0x18ff0 weiter verfolgen |
| `GetUpdateRect … ret=Visual+0x1f82cc` mit `retval≠0` | Wine hält im 3D-Fenster eine nicht leere Update-Region, das HUD wird bewusst übersprungen | Update-Region-Behandlung (BeginPaint/ValidateRect) im WM_PAINT-Weg von JView bzw. winemac prüfen |

Filter danach entfernen:

```bash
cd ~/solidedge && source env.sh && "$WINE" reg delete 'HKCU\Software\Wine\Debug' /v RelayInclude /f
```

## Arbeitsdateien

Im Scratchpad `<scratch>/re-b/`:
- `rwm.py` (RegisterWindowMessage-Aufrufe → String und Variable), `xr.py` (Verwendungen globaler Variablen),
  `addr.py` + `mods.txt` (Log-Adresse → Modul+RVA);
- Listings: `v_26570.txt` (JView::WindowProc), `v_19740.txt` (JView-Vorfilter), `vis_1f8120.txt`
  (HUDController::OnMessage), `cv_5e970.txt`/`cv_5e400.txt`/`cv_5ed10.txt` (corevw HUD-Transient),
  `r_1a3d0.txt` (JCanvas::SendOpcode), `r_op12.txt`, `r_c0.txt`.
