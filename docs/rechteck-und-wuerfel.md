# Auswahlrechteck und Ansichtswürfel – statische Analyse

Stand 2026-11-01. Grundlage ist ausschließlich statische Analyse der installierten Binärdateien
(`prefix/drive_c/Program Files/Siemens/Solid Edge 2026/Program`): Exporte, RTTI (`.?AV`/`.?AU`), Strings, `.pdata`,
Kreuzreferenzen (objdump + Python mit `-I`). Kein neuer Lauf. Alle Adressen sind RVAs (Image-Basis jeweils 0x180000000).

## Kurzfassung

1. **Würfel und Rechteck laufen über dieselbe Schicht: das HUD („Heads-Up Display“) des 3D-Fensters.**
   - Der Würfel unten rechts ist das Widget `HUDCamera` in `Visual.dll`. Es wird von `HUDController` verwaltet, und
     `view.dvx` (`JView`) legt es an.
   - Das Auswahlrechteck der Teileumgebung zeichnet `JMSmartLocateCmd` in `partDcx.dll` als **HUD-Transient-Grafik**
     über die Schnittstelle `IJRenderLite` von `render.dll`.
   - Beide hängen als Szenenknoten vom Typ **0x4E** in der **OVERLAY-Liste** der Szene (`GSCENE_ENUM_OVERLAY` = 0x10).
     `render.dll` zeichnet sie im Overlay-Durchgang von `JRenderGL`.
2. **Die Hover-Hervorhebung gehört nicht zum HUD.** Sie läuft über den Hilite-/Backing-Store-Pfad. Das passt zum
   Befund: Hover geht, Würfel und Rechteck fehlen beide.
3. Mehrere bisher verdächtigte Ursachen sind statisch ausgeschlossen (Abschnitt 4).
4. **Offen ist ein Glied:** ob der Overlay-Durchgang in unserer Umgebung ausgeführt wird und ob sein Ergebnis auf den
   Bildschirm gelangt. Dazu gehören die Reihenfolge gegenüber dem Backing-Store-Sichern und dem `glCopyPixels` nach
   FRONT sowie der Zeichenpuffer.
   - Das lässt sich statisch nur mit unverhältnismäßigem Aufwand klären: `render.dll` `JRenderGL::Display` umfasst über
     2000 Zeilen mit virtuellen Aufrufen über Tabellen.
   - Deshalb gibt es noch **keine** belegte Code-Änderung. Abschnitt 5 nennt die eine Messung, die das Glied schließt.
     Für jedes ihrer Ergebnisse ist die Änderung vorab festgelegt.

## 1. Ansichtswürfel

### 1.1 Identifikation

- **`SEClasses.dll`** enthält eine Namenstabelle der CLSIDs. Jeder Eintrag ist 0x18 Byte groß: `{GUID 16 Byte, Zeiger auf
  den Namen}`.
  - Die frühere Zuordnung war um einen Eintrag verschoben.
  - Die Tabelle wird beim Start über `SEClasses!JRegisterSolidEdgeClasses` (0x2a1b0) mit
    `JUtil!JRegisterInprocServer(Modulname, Tabelle, Anzahl)` in SEs **eigenen Klassen-Cache** eingetragen
    (`JUtil!JCoCreateInstance` 0x23d70 → `JCoGetClassObject` → Cache 0x21d10, erst danach Registry).

  | Name | CLSID | Modul (aus der Registrierung) |
  |---|---|---|
  | `CLSID_QuickViewCube` | `{E01D000D-2640-4C25-9772-80D62E3BDBC9}` | `vwcmds.dcx` |
  | `CLSID_QuickViewCubeOptions` | `{E01D000D-2640-4C25-9773-80D62E3BDBC9}` | `vwcmds.dcx` |
  | `CLSID_HUDController` | `{19600806-ABCD-1234-8000-080036B4D602}` | `visual.dll` |

  Keine dieser CLSIDs steht in `prefix/system.reg` oder `user.reg`. Das ist **kein** Fehler: auch `corevw.dvx`, `view.dvx`
  und `vwcmds.dcx` haben dort keinen Eintrag und laufen trotzdem, weil SE seinen eigenen Cache nutzt.
- **`VWCMDS.DCX` → Klasse `QuickViewCube`** (RTTI, vtable 0xfe578) ist ein `DefineViewCommandBase`-Befehl (CMDBASE.CPP),
  also der Befehl für Statusleiste und Menüband. Er ist **nicht** der Würfel im Grafikfenster.
- **Der Würfel im Fenster** heißt intern „CommonViewTool“ bzw. „QVC“.
  - Einstellungen: `sprfhlp.dll!JSecPrefsCommonViewTool` (`PrefSets\Options-CommonViewTool`: Show, Size, Position,
    Color, Opacity, Transparent, Triad).
  - Im Prefix (`user.reg`, Abschnitt `PrefSets\Options-CommonViewTool`) stehen **Show=1**, Size=0x80, Position=2,
    Triad=1, Transparent=1, Opacity=0x7f.
  - Gezeichnet wird er von `Visual.dll` `HUDCamera`: RTTI `.?AUHUDCamera@@`, vtables 0x67a7c0 sowie 0x67a810 (IJGraphic,
    Offset 0x48) und `.?AVXIHUDCamera@HUDCamera@@` 0x67a710.

### 1.2 Erzeugung (view.dvx, `JView`)

- **Konstruktor** (Funktion 0xf9a0): bettet bei JView+0x4a0 eine `HUDMemberGraphic` samt `HUDClient` ein (0xfa96) und
  bei JView+0x4e0 die `JSecPrefsCommonViewTool` (0xfae4).
- **Einstellungen laden** (Funktion 0x1dad0):
  - 0x1de7e–0x1defb liest Show, Size, Triad, Color, Opacity, Position und Transparent nach JView+0x630…0x650.
  - 0x1df35 bildet die Widget-Flags JView+0x644: Bit 31 = versteckt (Show=0), Bit 0 = ohne Triade, Bit 1 = nicht
    transparent.
  - Debugwerte `QVC_COLOR_AQUA`, `QVC_SIZE_TINY` und `QVC_DEBUG_LEVEL` werden nur gelesen, wenn ein Debugschalter
    gesetzt ist.
- **HUD-Controller holen** (Funktion 0x19740, 0x19b0b–0x19c0a). Er wird nur geholt, wenn **alle** folgenden
  Bedingungen gelten:
  - JView+0x90 ≠ 0, JView+0x4d8 = 0, die Ansichtsart ≠ 4, JView+0x498 = 0x80070490 (noch nicht versucht),
    JView+0x4d0 = 0 und die Merkmale JView+0x48c ≠ 0;
  - **`Visual!JGetBufferRegionLevel(Merkmale) > 1`** (0x19b64). Laut `Visual.dll` 0x44c680 liefert das 2 nur bei
    Merkmal **Bit 16** (`JRENDER_HARDWARE_BACKINGSTORE`) und `DEBUG\ENABLE_BUFFER_REGIONS` (Standard 1).
  - Danach: `QueryInterface` auf JView+0x78 (= `JCoreView`, corevw.dvx) nach `{…-8000}` → JView+0x4d0.
  - Die Merkmale kommen aus JView+0xe8 über vtable+0x30 (0x19905). Laut Renderer-Dump hat das 3D-Fenster Bit 16, die
    Bedingung sollte also erfüllt sein. Dass JView+0x48c genau diesen Wert hält, ist aber nicht direkt belegt.
- **JCoreView-QueryInterface** (corevw.dvx 0x1a2e7–0x1a375): legt den Controller beim ersten Aufruf über
  `JCoCreateInstance(CLSID_HUDController)` an und speichert ihn bei +0x4408. Das HRESULT steht bei +0x4400.
- **Widget anlegen und einstellen** (view.dvx 0x19c43–0x19cba sowie 0x1e0fa–0x1e35a):
  - IHUDController vtable+0x68 sucht das Widget per CLSID `{…-8002}`, vtable+0x58 legt es an, falls Show ≠ 0.
  - Danach werden Größe (+0x30), Deckkraft (+0x60), Flags (+0x20), Position (+0x50) und drei Farben (+0x80) gesetzt.
- **Einhängen in die Szene:** Der HUDController meldet sich mit Grafiktyp **0x4E** beim Grafik-Besitzer der Ansicht an
  (Visual 0x1f9b68, vtable+0x98 mit `edx=0x4E`). `render.dll` sortiert 0x4E-Knoten in die Szenenliste GScene+0x4000
  (0xe4875–0xe488f). Diese Liste ist **`GSCENE_ENUM_OVERLAY` (0x10)**: Visual `GScene::Begin` 0x1038a0 bildet 0x10 auf
  +0x4000 ab.

### 1.3 Zeichnen

```
render.dll  JRenderGL (z. B. 0xe5430): GScene-Durchläufe 0xdb520(…, 0x3 PRIMARY|TRANSLUCENT),
            danach 0xdb520(…, 0x10 OVERLAY), danach 0xdb520(…, 0x20)       [auch 0xe21c0, 0xe6490 mit 0x10]
render.dll  0xdb520 Knotenschleife: Typ 0x4E → 0xd9470 (0xdc9d1–0xdc9fe)
render.dll  0xd9470 („JRenderGL::Display“, JRGL.CPP): ctx+0x224 |= 0x80000000 (+Bit 0/1), JRC vtable+0x368(4,4),
            Knoten→IJGraphic::Display(ctx) (vtable+0x28), JRC vtable+0x368(0,4)
Visual.dll  HUDController::Display 0x1f7d30: verlangt QI ctx+0x198 → {6EE587BC-44F8-11CE-…}; läuft über alle
            Widgets, QI {…-8001} und IJGraphic {FB690900-…}, ruft deren Display (+0x28)
Visual.dll  HUDCamera::Display 0x1e05e0 (etwa 4500 Zeilen)
```

`HUDCamera::Display` zeichnet mit **OpenGL im Sofortmodus** in den gerade aktiven Kontext und Zeichenpuffer:
glViewport/glScissor auf die Ecke, glBegin/glVertex3f(v), Licht, Material, glColor und glBlendFunc. Das sind rund 150
GL-Aufrufe über die verzögert gebundene GL-Tabelle von Visual (Auflöser 0xeebb0: zuerst
`GetProcAddress(opengl32)`, sonst `wglGetProcAddress`, sonst der Import; core-Funktionen werden also immer gefunden).
Es gibt kein GDI, kein D2D und kein `glClear`.

Bedingungen in `HUDCamera::Display`:
- Feld this−0x34 ≥ 0 (0x1e067b). Vermutlich das Widget-Flagwort, in das +0x20 das Bit 31 „versteckt“ schreibt;
  das ist nicht einzeln belegt;
- ctx ≠ 0 und `(ctx+0x224 & 0xF00) == 0` (0x1e068e);
- `JGetRuntimePreference(3, 0x08000000) == 0` (0x1e06af). Das Bit ist der Debug-Schalter „NORMALMAP“
  (Visual 0x43d2aa, `JToggleRuntimePreference`), standardmäßig aus;
- `QueryInterface` auf ctx+0x198 nach `{6EE587BC-…}` ist erfolgreich (0x1da3e0);
- Hilfsfunktion `0x45ce50(ctx, Widget-Kennung this−4)` ≠ −1 (0x1e06e1). Die genaue Bedeutung ist nicht aufgelöst.

`ENABLE_HUD` (Visual 0x1742c8, Standard 0) setzt nur Laufzeit-Bit 3/0x8000. Das wird nirgends abgefragt; es ist also
**kein** Schalter für Würfel oder Rechteck.

### 1.4 Wo es bei uns scheitern kann

| Stelle | Bewertung |
|---|---|
| Fehlende Registry-CLSIDs | **ausgeschlossen** (SE-eigener Klassen-Cache, siehe 1.1) |
| Einstellung Show | **ausgeschlossen** (Show=1 im Prefix) |
| `ENABLE_HUD` | **ausgeschlossen** (wird nicht abgefragt) |
| JView-Bedingung `JGetBufferRegionLevel > 1` (view 0x19b64) | wahrscheinlich erfüllt (Bit 16 laut Dump); nicht direkt am JView-Wert belegt |
| Overlay-Durchgang nicht ausgeführt oder nicht sichtbar | **offen**, Hauptkandidat, gemeinsam mit dem Rechteck |

## 2. Auswahlrechteck

Teileumgebung, Befehl „Auswählen“ = `JMSmartLocateCmd` (partDcx.dll, CJMSMLOC.CPP, RTTI 0xb32550).
`JMSmartLocateCmd` erbt bei +0x458 von `HUDGraphic` (vtable 0xb32ab0) und legt im Konstruktor 0x401f20 einen
`HUDClient` an.

### 2.1 Ablauf ab Mausklick

1. **`JMSmartLocateCmd::OnLButtonDown`** (0x412560):
   - speichert den Punkt in +0x468 und +0x470 (Rechteck-Ecken);
   - liest `GetKeyState(VK_MENU)` (0x4133c9): mit Alt wird es ein Lasso (+0x1e21=1);
   - prüft `GetCapture()` und ruft `SetCapture(Ansichtsfenster)` (0x4133eb–0x41340c).
   - Mit Alt bzw. Option ist das ein anderer Modus. Auf dem Mac ist links Option ≠ Alt
     (`LeftOptionIsAlt=n`), das stört also nicht.
2. **`OnMouseMove`** (0x414670): speichert den aktuellen Punkt in +0x470. Ist das Rechteck aktiv (Zustand 3/4), wird der
   Punkt auf das Fensterrechteck begrenzt (`GetWindowRect`).
3. **`JMSmartLocateCmd::LocateMouseMove`** (0x40fac0). **Hier beginnt das Rechteck** (0x40fb3f–0x40fbd6). Bedingungen:
   - `wParam & MK_LBUTTON` (aus der Nachricht, **kein** `GetAsyncKeyState`);
   - Auswahlmodus +0x1328 ∈ {0, 3};
   - Zustand +0x4b8 = 0, Startpunkt gültig, dasselbe Fenster;
   - **Manhattan-Abstand |dx|+|dy| > 5 Pixel** (0x40fba9).

   Dann wird der Zustand +0x4b8 auf **3** gesetzt und `0x416280` aufgerufen. Das ist eine `QueryInterface` auf die
   Ansicht nach IHUDTransient `{…-8005}` und `->Add(this+0x458)`.
   - Die JView-`QueryInterface` reicht `{…-8005}` an JCoreView weiter (view 0x20e64 → 0x215a4).
   - JCoreView reicht weiter an den HUDController-Container `{…-8006}` und stellt `HUD_REFRESH` ein (corevw
     0x5e970–0x5eae4: `PeekMessage`, dann `PostMessage`).
4. **Zeichnen:** `HUDGraphic::Draw` von JMSmartLocateCmd (vtable+0x28 → **0x407bb0**):
   - `QueryInterface` auf die Ansicht nach `{…-8005}`, dann „ist registriert“ (+0x20) ≠ 0;
   - Zustand +0x4b8 == 3;
   - `QueryInterface` auf ctx+0x198 nach **IJRenderLite** `{600DF00D-2014-1960-0806-0800369B4102}`;
   - `Begin` (+0x18), Farbe aus den Einstellungen setzen (+0xee4/+0xee8/+0xeec, Alpha +0x1b04; Option +0x8f0:
     innen / überlappend / richtungsabhängig, x2<x1 = kreuzend), Füllung (+0x30) bzw. Umriss (+0x28),
     **DrawRect** (+0x40) über (x1,y1)-(x2,y2) aus +0x468…+0x474, `End` (+0x20).
5. **IJRenderLite** ist `render.dll` `JRenderManager`+0x310 (vtable 0x1f4ed0, Konstruktor 0x17a0db).
   - `QueryInterface` (0x1859b5) gibt es nur zurück, wenn `ENABLE_RENDER_LITE` = 1 (Standard 1, 0x105a0) und
     `ENABLE_PROXY_WRAPPER` = 0 (Standard 0, 0x105d0).
   - `Begin` (0xafe20) legt einen Stapel an (0x3a0 Byte) und holt Kamera und Viewport. Gezeichnet wird gesammelt bei
     `End` bzw. im Overlay-Durchgang.

Die Zeichenroutinen `JRubberBand`/`JRubberBandBase` (Displst.dll, CMDSPT2D.DLL, i2d.dcx, dtldcx.dcx) und
`render.dll` `RUBBERBAND`/`FAST_RUBBERBAND`/`DISABLE_FAST_RUBBERBAND` gehören zu den **2D-Umgebungen** (Zeichnung,
Skizze). `partDcx.dll` hat keinen `JRubberBand`. Der Schalter `DISABLE_FAST_RUBBERBAND` betrifft das 3D-Rechteck also
nicht.

Ein drittes HUD-Rechteck hat `JView` selbst (view 0x2535f–0x2565d: Zoomfenster mit X-Taste). Es läuft genauso über
`HUDMemberGraphic`, IHUDTransient und IJRenderLite (view 0x17890).

### 2.2 Wo es bei uns scheitern kann

| Stelle | Bewertung |
|---|---|
| Rechteck-Start (MK_LBUTTON, > 5 px, `SetCapture`) | sehr wahrscheinlich erfüllt: Die Rechteck-Auswahl wirkt nach dem Loslassen, also wird der Zustand gesetzt. Ob jeder Zwischenschritt einzeln läuft, ist nicht belegt |
| `GetAsyncKeyState` | wird hier **nicht** benutzt |
| IJRenderLite verfügbar | Standard-Schalter erlauben es (siehe 2.1, Punkt 5) |
| Overlay-Durchgang nicht ausgeführt oder nicht sichtbar | **offen**, derselbe Kandidat wie beim Würfel |

## 3. Nebenbefund `render.dll`+0x575fc (`SetROP2`)

Die Funktion 0x57520 setzt den Zeichenmodus zurück: über die GL-Tabelle `glLogicOp(GL_COPY 0x1503)` und
`glDisable(GL_COLOR_LOGIC_OP 0xBF2)` sowie `SetROP2(hdc, R2_COPYPEN)`. Das ist **keine** GDI-Zeichnung ins
3D-Fenster. Die frühere Vermutung „GDI-Overlay wird von der Zink-Ausgabe überschrieben“ trifft auf diese Stelle nicht
zu.

## 4. Statisch ausgeschlossen

- Fehlende COM-Registrierung (HUDController und QuickViewCube stehen im SE-Klassen-Cache).
- Einstellung „Würfel aus“ (Show=1).
- Debugschalter `ENABLE_HUD`, `NORMALMAP` und `DISABLE_FAST_RUBBERBAND` (keine Wirkung auf diese Pfade bzw.
  standardmäßig passend).
- Ein Prüfweg über `GetAsyncKeyState` beim Rechteck.
- GDI-, D2D- und Layered-Window-Overlays für Würfel und Rechteck: HUDCamera nutzt nur GL, das Rechteck nur IJRenderLite.

## 5. Overlay-Durchgang, offenes Glied und der eine Bestätigungslauf

### 5.1 Was über den Overlay-Durchgang belegt ist

`render.dll` 0xe5430, Ende des JRenderGL-Bilds (bis „JRenderGL::EndDisplay“ 0xe60db):

- **0xe5713:** Der gesamte Block läuft nur bei JRenderGL+0x3370 = 0 (Merker „Ende läuft schon“).
- **0xe5728–0xe578a:** Zuerst kommt der Hauptdurchgang `GScene::Begin(0x3f)`. Er läuft entweder über den Cache-Pfad
  0xd5190 oder über 0xdb520 mit PRIMARY|TRANSLUCENT (0x3), wenn +0x3b20 = 0 und +0x4268 = 0.
- **0xe5799–0xe57e0:** Danach der Overlay-Durchgang. Er läuft **nur, wenn +0x3b2c = 0 und die Overlay-Liste
  GScene+0x4000 nicht leer ist**. Dann `Begin(0x10)`, 0xdb520(…, 0x10) und bei Typ 0x4E 0xd9470 (HUD). +0x3b2c dient
  während des Durchgangs als Sperre.
- **0xe57e7–0xe5816:** Danach die Liste 0x20.
- Erst danach folgen die Schatten-/Rand-Durchgänge und das Ende-Protokoll. Ab 0xe6216 kommt bei Render-Flag 0x10000000
  eine Pufferkopie mit SRCCOPY 0xCC0020 (Quelle 0x400, Ziel 0x404).

Das Overlay wird also **im selben Bild nach der Szene und in denselben Zeichenpuffer** gezeichnet. Ob es danach mit in
den Backing Store bzw. nach FRONT gelangt, hängt davon ab, ob der Hauptweg aus diesem Ende-Pfad kommt oder aus einem
späteren Region-Restore. Das ist statisch nicht entschieden: Das Ladeprotokoll zeigt Blit BACK→FBO,
`glDrawBuffer(FRONT)`, `glCopyPixels`, `glFinish`, aber ohne Zuordnung zum Aufrufer.

### 5.2 Warum ein Lauf nötig ist

Für beide Merkmale bleibt dieselbe Frage, und sie lässt sich nur zur Laufzeit beantworten: **Läuft
`HUDCamera::Display` bzw. die IJRenderLite-Ausgabe des Rechtecks, und in welchen Puffer, relativ zum letzten
`glCopyPixels` nach FRONT?** Alles Vorherige ist belegt oder ausgeschlossen. Ohne diese Antwort wäre jede Änderung
geraten.

### 5.3 Vorbereitete Vorschaltung (nur Protokoll)

`opengl-wrapper/out/opengl32.dll`, SHA256 `8240f33d…0128`:
- Neu ist **Aufrufer-Zuordnung**: Jede neue Aufrufstelle von glBegin, glDrawBuffer, glCopyPixels und glBlitFramebuffer
  wird einmal als `Modul+RVA` mit dem aktuellen Zeichenpuffer protokolliert
  (`Aufrufer glBegin: Visual.dll+0x1e1086 (Zeichenpuffer BACK)`).
- In der Zeitreihe `Aktivität/s:` gibt es neu `glBegin aus HUDCamera` (Visual RVA 0x1e05e0–0x1e6300) und
  `davon in FRONT`.

### 5.4 Der eine Lauf

Solid Edge vorher beenden. Der Relay-Filter stört nicht, weil ohne `+relay` keine Relay-Ausgabe entsteht.

```bash
cd ~/solidedge && source env.sh && "$WINESERVER" -k
```

```bash
cd ~/solidedge && MEMDC_GL_FRONT_FLUSH=1 MESA_WRAPPER=1 MEMDC_GL_DEBUG=1 MESA_DIR=runtime/mesa-22.0.3-p SE_TESTDATEI='C:\Program Files\Siemens\Solid Edge 2026\Training\block.par' ./mesa-test.sh zink 150
```

Ablauf:
1. Nach dem Laden `wc -l` auf das Log (Wert A).
2. Der User zieht zweimal langsam ein Rechteck auf (je etwa 3 Sekunden).
3. Erneut `wc -l` (Wert B).

Auswertung:

```bash
L=~/solidedge/logs/mesa-zink-mesa-22.0.3-p.log; grep -a -E "Aufrufer|Aktivität/s" $L | head -200
```

### 5.5 Entscheidung und daraus folgende Änderung

| Befund | Bedeutung | Gezielte Änderung |
|---|---|---|
| **Kein** `Aufrufer glBegin: Visual.dll+0x1e0…–0x1e6…`, `glBegin aus HUDCamera` bleibt 0 | Das Overlay wird nicht gezeichnet: Sperre +0x3370/+0x3b2c, Overlay-Liste leer oder HUDController nicht angehängt | keine Grafikänderung; statisch weiter bei view 0x19b0b (JView+0x48c) bzw. render +0x3b2c |
| HUDCamera zeichnet in **BACK**, danach kein neues `glCopyPixels` nach FRONT im selben Takt | Overlay landet nur im Rückpuffer, der Hauptweg gibt aus dem Backing Store aus | Vorschaltung: nach Overlay-Zeichnen in BACK beim nächsten `glFinish` BACK→FRONT ausgeben (eine Funktion, per Schalter) |
| HUDCamera zeichnet in **FRONT** (`davon in FRONT` > 0) und bleibt mit `MEMDC_GL_FRONT_FLUSH=1` unsichtbar | Die Vorderpuffer-Ausgabe von Mesa 22.0.3 über GDI verliert sie | Mesa-Seite: `stw_st_framebuffer_flush_front` erzwingen (Patch in `mesa-22.0.3-p`) |
| Fürs Rechteck: neue `Aufrufer`-Stellen aus `render.dll` erscheinen nur während des Ziehens | Das Rechteck wird gezeichnet; Puffer wie oben | dieselbe Zeile wie beim Würfel |

## 6. Nach dem Entscheidungslauf (DLL 8240f33d): Fall 1

Befund des Koordinators: Es gibt **kein** glBegin aus HUDCamera, weder beim Laden noch beim Ziehen. Beim Ziehen gibt es
**überhaupt keine** GL-Aufrufe. Im Log stehen nur 9 Aufrufer, alle beim Laden: Visual 0x3c40ff, 0x3dc82c, 0x497fd9,
0x3dc951, 0x3dca49, 0x39b112, Blit 0x20ec15 sowie `glDrawBuffer(FRONT)` 0x3859bc und `glCopyPixels` 0x3859dc.

### 6.1 Folgerungen (belegt)

- **Die Szene selbst zeichnet ohne glBegin.** Sie läuft über VBOs und Arrays, deshalb gibt es keinen
  glBegin-Aufrufer. Würde HUDCamera laufen, müsste mindestens ein `Aufrufer glBegin: Visual.dll+0x1e…` auftauchen.
- **Würfel:** Auch in den Bildern der Ladephase wurde HUDCamera nicht gezeichnet. Diese Bilder sind vollständig
  ausgegeben worden: Blit, `glDrawBuffer(FRONT)`, `glCopyPixels` und `glFinish` sind zu sehen. Der Würfel fehlt also
  **nicht** wegen der Ausgabe, sondern weil der HUD-Knoten **im Overlay-Durchgang nicht vorkommt**. Mögliche Gründe:
  - Die Overlay-Liste GScene+0x4000 ist leer;
  - die Sperre +0x3b2c ist gesetzt;
  - oder der HUDController ist nicht angehängt bzw. sein Widget nicht angelegt.
- **Rechteck:** Während des Ziehens wird gar kein Bild gezeichnet. Das Neuzeichnen wird also nicht angestoßen oder
  nicht ausgeführt.

### 6.2 Wie das HUD Neuzeichnen anstößt (statisch belegt)

- Die Anmeldung `partDcx 0x416280` geht über `IJView::QueryInterface({…-8005})` → view 0x20e64 → 0x215a4 an JCoreView
  (JView+0x78).
- **corevw 0x5e970** ist IHUDTransient::Add. Es arbeitet in dieser Reihenfolge:
  1. Holt den HUDController (+0x4408) und **legt ihn bei Bedarf an** (QueryInterface auf sich selbst `{…-8000}` →
     0x1a31c → `JCoCreateInstance(CLSID_HUDController)`, HRESULT bei +0x4400).
  2. Holt den Transient-Container `{…-8006}` und ruft `Add` (+0x10).
  3. Prüft `PeekMessageW(hwnd=+0x1e40, HUD_REFRESH)` und ruft, falls keine solche Nachricht ansteht,
     **`PostMessageW(hwnd, HUD_REFRESH, 0, 0)`** (0x5eae4).
- **HUD_REFRESH ist eine registrierte Fensternachricht** (`RegisterWindowMessageW(L"HUD_REFRESH")`, in jedem Modul
  eigens registriert).
  - JView::WindowProc (view 0x26570) **schluckt** sie: Nachricht == view-Variable 0x1800803e8 → 0x26603, kein Rundruf
    an Befehle, kein Zeichnen.
  - Visual `JDisplayWindowProc` stellt sie nur selbst ein (0x44839e, 0x44ec36, 0x45c98a, 0x45ce2b,
    JProcessKeyin 0x4968e2).
  - Wirklich abgearbeitet wird sie erst beim **Leeren der Zeichenfläche**: `render.dll` **`JCanvas::FlushBuffers`**
    0x15800 (vtable `.?AVJCanvas@@` Slot 0x180) bzw. **`JCanvasDBS::FlushBuffers`** 0x2cf40 (Slot 0x1b0) entfernen
    anstehende HUD_REFRESH mit `PeekMessageW(PM_REMOVE)` (0x15aab, 0x2cfce) und zeichnen dann.
  - HUD_REFRESH ist also nur ein Weck- bzw. Merker-Mechanismus. Gezeichnet wird über FlushBuffers. Wer FlushBuffers
    aufruft, ist statisch noch nicht gefunden: virtueller Aufruf über die Canvas-vtable.
- Beim Ziehen meldet `OnMouseMove` (partDcx 0x414670) nur neue Koordinaten (+0x470). Danach folgen `GetWindowRect`
  (0x4146f1, nur im Rechteck-Zustand 3/4) und `KillTimer` (in `LocateMouseMove`). Ein eigener Zeichenauftrag pro
  Bewegung ist nicht zu sehen. Das Neuzeichnen hängt am HUD_REFRESH bzw. FlushBuffers-Mechanismus.

### 6.3 Antworten auf (a), (b) und (c)

- **(a)** Für das Rechteck wird der Overlay-Durchgang nicht angestoßen, es entsteht gar kein Bild. Beim Würfel fehlt
  der Knoten auch in Bildern, die tatsächlich gezeichnet werden. **Gemeinsamer Kandidat:** Der HUDController kommt
  nicht zustande, `JCoCreateInstance(CLSID_HUDController)` in corevw 0x1a364 scheitert. Dann:
  - stellt `Add` kein HUD_REFRESH ein (corevw 0x5ea2a: Controller null → kein Add, kein Post);
  - gibt es keinen Overlay-Knoten, also keinen Würfel;
  - bleibt FlushBuffers ohne Anlass, also gibt es beim Ziehen kein Bild.

  Das ist **vermutet**, nicht belegt. Die SE-Registrierung „visual.dll“ ist belegt; ob das Anlegen gelingt, nicht.
- **(b)** `PostMessageW` mit der registrierten Nachricht HUD_REFRESH, kein Timer, kein `InvalidateRect`. Eingestellt
  wird an das Fenster JCoreView+0x1e40. Ein Thread-Problem (`wglMakeCurrent` über Threads) ist statisch nicht zu
  erkennen: FlushBuffers liest nur `GLcontext::wglGetCurrentContext` (render 0x15a5b) und meldet sich mit
  `JDebugBuffers`. Prüfen lässt sich das erst, wenn klar ist, ob FlushBuffers überhaupt läuft.
- **(c)** Die `glLogicOp`-Aufrufe der Ladephase sind `glLogicOp(GL_COPY 0x1503)` + `glDisable(GL_COLOR_LOGIC_OP)`, also
  Modus-Zurücksetzen (render 0x57520 und weitere Stellen). Dahinter folgt kein `glGetError` und keine
  Fähigkeitsprüfung. Mit dem HUD hängt das nicht zusammen.

### 6.4 Nebenbefund: viele DXVK-Geräte

Das laufende Edge.exe (per `sample` angesehen) hat **2057 Threads**, davon **204** `dxvk-submit` und je ein
`dxvk-queue`/`dxvk-cs`/`dxvk-shader`-Satz. Es gibt also rund 200 D3D11-Geräte, vermutlich je D2D-Zeichenvorgang eines
neu angelegten Geräts (d2d1 über DXVK). Das ist kein Zeichenfehler, aber ein Leck an Ressourcen und Threads. Gesondert
prüfen.

### 6.5 Der eine Lauf (Relay nur user32, kein KERNEL32)

Er entscheidet zwischen „HUD_REFRESH wird beim Rechteck-Start eingestellt“ (dann fehlt der FlushBuffers-Anstoß) und
„es wird nie eingestellt“ (dann scheitert die Anlage des HUDControllers bzw. die Transient-Anmeldung).

Solid Edge beenden und den Relay-Filter setzen:

```bash
cd ~/solidedge && source env.sh && "$WINESERVER" -k && "$WINE" reg add 'HKCU\Software\Wine\Debug' /v RelayInclude /t REG_SZ /d 'user32.RegisterWindowMessageW;user32.PostMessageW;user32.SetCapture;user32.ReleaseCapture;user32.GetWindowRect;user32.KillTimer;user32.SetTimer;user32.InvalidateRect;user32.RedrawWindow;user32.UpdateWindow' /f
```

Starten:

```bash
cd ~/solidedge && MEMDC_GL_FRONT_FLUSH=1 MESA_WRAPPER=1 MEMDC_GL_DEBUG=1 MESA_DIR=runtime/mesa-22.0.3-p SE_TESTDATEI='C:\Program Files\Siemens\Solid Edge 2026\Training\block.par' WINEDEBUG_EXTRA=+relay ./mesa-test.sh zink 150
```

Ablauf:
1. Nach dem Laden `wc -l` auf das Log (Wert A).
2. Zwei langsame Rechteck-Züge.
3. Erneut `wc -l` (Wert B).

Auswertung:
1. Den Wert von HUD_REFRESH ermitteln:
   `grep -a -A1 'RegisterWindowMessageW(.*HUD_REFRESH' LOG | grep -a Ret | head -3`
   Die Rückgabe `retval=0000Cxxx` ist die Nachrichtennummer, sie ist je Prozess gleich.
2. Im Bereich A..B: `grep -a -E 'Call user32\.(PostMessageW|SetCapture|ReleaseCapture|GetWindowRect|SetTimer|InvalidateRect)|Aktivität|Aufrufer' | head -200`

Entscheidung:

| Befund | Bedeutung | Gezielte Änderung bzw. nächster Schritt |
|---|---|---|
| `GetWindowRect` beim Ziehen (Rechteck-Zustand erreicht), **kein** `PostMessageW(…, <HUD_REFRESH>, …)` | Transient-Anmeldung bzw. Anlage des HUDControllers scheitert (corevw 0x1a364 / 0x5ea2a) | statisch: `Visual!DllGetClassObject` 0x506f59 → HUDController-Konstruktor 0x1f73a0 auf Wine-abhängige Aufrufe prüfen |
| `PostMessageW(…, <HUD_REFRESH>, …)` vorhanden, danach keine `Aktivität` | Die Nachricht steht an, FlushBuffers läuft nicht | statisch: Aufrufer von Canvas-vtable+0x180/+0x1b0 suchen (Leerlauf bzw. Nachrichtenschleife) |
| kein `GetWindowRect` beim Ziehen | Der Rechteck-Zustand wird gar nicht erreicht (der > 5-px-Weg bzw. `MK_LBUTTON` kommt nicht an) | Mausnachrichten aus winemac prüfen |

Relay-Filter danach entfernen:

```bash
cd ~/solidedge && source env.sh && "$WINE" reg delete 'HKCU\Software\Wine\Debug' /v RelayInclude /f
```

## 7. Spur (a): Warum der Overlay-Durchgang nie läuft

Arbeitsteilung: Spur (b), der Refresh-Anstoß, steht in `docs/hud-b-refresh.md`, Spur (c), `glLogicOp`, in
`docs/hud-c-logicop.md`. Dieser Abschnitt behandelt nur (a).

### 7.1 Belegte Kette, von hinten nach vorne

1. **Overlay-Durchgang** (render 0xe5799–0xe57e0): läuft nur, wenn `GScene+0x4000` (OVERLAY-Liste) nicht leer ist
   (`size()` 0xe57a9, Sprung bei 0 nach 0xe57e7) und die Sperre +0x3b2c = 0 ist.
   - +0x3b2c wird unmittelbar vor dem Durchgang auf 1 gesetzt (0xe57b4) und danach auf r14 zurückgesetzt (0xe57e0).
     Es ist eine **Wiedereintrittssperre**, keine dauerhafte Sperre. Dass r14 hier 0 ist, ist vermutet; derselbe
     Registerwert setzt +0x37fc zurück.
2. **Die Liste enthält HUD-Knoten (Typ 0x4E) nur über `IHUDController::Attach`.**
   - Visual vtable `XIHUDController` Slot 7 (+0x38) = 0x1f9ca0 → 0x1f9740.
   - 0x1f9740 läuft über die Ansicht: QI `{85261840-…}` → +0x78 → +0x68(1) → +0x18 → +0x38 → QI `{CB038050-…}` →
     QI `{FF156300-…}` → QI `{6EE587BC-…}`.
   - Am Ende steht `->vtable+0x98(0x4E, IJGraphic {F073D730-…})` (0x1f9b68). Nur dort entsteht der Overlay-Knoten.
3. **Attach wird nur an einer Stelle aufgerufen: view.dvx 0x19e3f** (in JView-Funktion 0x19740). Bedingungen:
   - JView+0x4d0 (Controller) ≠ 0;
   - JView+0x490 = 0 und +0x4d8 = 0;
   - Ansichtsart (lokal −0x38) ≠ 4.
   - Das Ergebnis (Cookie) landet in JView+0x490.
4. **JView+0x4d0 wird nur bei view 0x19b0b–0x19c0a gesetzt.** Bedingungen: JView+0x90 ≠ 0, +0x4d8 = 0, Art ≠ 4,
   +0x498 = 0x80070490, +0x4d0 = 0, Merkmale +0x48c ≠ 0 und **`JGetBufferRegionLevel(JView+0x48c) > 1`** (0x19b64).
5. **JView+0x48c** wird (korrigiert in 7.6: bei jedem Aufruf, solange 0) gefüllt (0x198f8–0x19918, nur solange 0) und danach nie wieder (die einzigen
   Schreibstellen sind der Konstruktor 0xfa6b und diese).
   - Quelle: JView+0xe8 → `JCoreView::IJRenderSetupImp::GetSupportedFeatures` (corevw 0x5b100, IRENDSTP.CPP).
   - Das ist `render.dll` `JRenderManager::CIJRenderSetupImp` Slot 6 = 0x192da0: `*out = this->+0xce8`.
   - +0xce8 erhält seinen Endwert erst in `InitializeRenderer`. Der Modulaufruf bei 0x193de1 bekommt `&+0xce8` als
     Ausgabe; danach folgt bei 0x193e4b ggf. `|= 0x20`.
6. **Gegenstück beim Rechteck:** IHUDTransient über JCoreView (corevw 0x5e970) legt zwar bei Bedarf den Controller an
   (0x1a364), ruft aber **kein** Attach. Ohne JViews Attach gibt es keinen 0x4E-Knoten. Dann läuft auch für das
   Rechteck kein Overlay-Durchgang, selbst wenn neu gezeichnet würde.

### 7.2 Folgerung

Würfel und Rechteck fehlen beide, weil `IHUDController::Attach` (view 0x19e3f) nicht stattfindet. Das passt zu allen
Befunden: Selbst die vollständig gezeichneten Bilder der Ladephase enthalten kein HUDCamera-glBegin.

Statisch sind folgende Ursachen möglich (vermutet, Reihenfolge nach Wahrscheinlichkeit):
- **(i)** JView+0x48c ohne Bit 16. Die Merkmale werden beim ersten Durchlauf von 0x19740 einmalig zwischengespeichert;
  ist +0xce8 dann noch nicht der Endwert oder fehlt Bit 16, ist die HUD-Bedingung dauerhaft falsch.
- **(ii)** Ansichtsart = 4.
- **(iii)** Eine der QIs in Attach (0x1f9740) scheitert.

Bit 16 ist laut Renderer-Dump im Endwert vorhanden. Ob JView genau diesen Wert hat, ist nicht belegt.

### 7.3 Einzelner Bestätigungslauf mit eingebauter gezielter Änderung

`opengl-wrapper/out/opengl32.dll`, SHA256 `29f56554…d258`, hat einen neuen Schalter **`MEMDC_GL_HUD_BRL`**:
- `log`: Beim ersten `wglMakeCurrent` wird `Visual!JGetBufferRegionLevel` (Export) durch eine gleichwertige
  Nachbildung ersetzt (Logik aus Visual 0x44c680, `ENABLE_BUFFER_REGIONS` über Visuals `JGetDebugBOOL`). Die
  Nachbildung protokolliert Merkmale, Ergebnis und Aufrufer. Der HUD-Aufruf ist `view.dvx+0x19b6a` und wird immer
  protokolliert.
- `force`: wie `log`, liefert aber **nur für den Aufruf aus view.dvx+0x19b6a** mindestens 2. **Das ist die gezielte
  Änderung für Ursache (i).**

Der Lauf (Solid Edge vorher beenden):

```bash
cd ~/solidedge && source env.sh && "$WINESERVER" -k
```

```bash
cd ~/solidedge && MEMDC_GL_HUD_BRL=force MEMDC_GL_FRONT_FLUSH=1 MESA_WRAPPER=1 MEMDC_GL_DEBUG=1 MESA_DIR=runtime/mesa-22.0.3-p SE_TESTDATEI='C:\Program Files\Siemens\Solid Edge 2026\Training\block.par' ./mesa-test.sh zink 150
```

Ablauf: Bild ansehen (Würfel unten rechts?), dann ein Rechteck ziehen. Auswertung:

```bash
grep -a -E "HUD-Bedingung|JGetBufferRegionLevel|HUDCamera|Aktivität" ~/solidedge/logs/mesa-zink-mesa-22.0.3-p.log | head -60
```

| Befund | Bedeutung |
|---|---|
| `… = 0` oder `= 1, Aufrufer view.dvx+0x19b6a → HUD-Bedingung, erzwungen 2`, danach `glBegin aus HUDCamera` > 0 bzw. Würfel sichtbar | **Ursache (i) bestätigt**, die Änderung wirkt. Die protokollierten Merkmale zeigen, welches Bit fehlt; als Nächstes klären, warum (Zeitpunkt oder Merkmale aus InitializeRenderer) |
| `= 2 … → HUD-Bedingung` (nicht erzwungen) und trotzdem kein HUDCamera | Die Bedingung ist erfüllt; Ursache (ii) oder (iii). Statisch weiter bei 0x19e09 (Art) bzw. 0x1f9740 |
| keine Zeile mit `view.dvx+0x19b6a` | 0x19740 erreicht die Bedingung nie, weil eine frühere Abfrage greift (+0x90, +0x4d8, Art = 4, +0x498). Statisch weiter bei 0x19b0b |
| Zeile `HUD-Bedingung … ersetzt` fehlt | Der Ersatz wurde nicht eingerichtet (Visual.dll bei `wglMakeCurrent` nicht geladen): Fehler der Vorschaltung, mir melden |

### 7.4 Ergebnis des Laufs mit `force` (DLL 29f56554) und nächster Schritt

- Der Ersatz ist eingerichtet. `JGetBufferRegionLevel(0x1011d23)` = 2, aber nur Aufrufe aus `render.dll`
  (+0x1344c, +0x18775, +0x40ba5, +0x40bb3), **nie aus view.dvx+0x19b6a**. Es ist kein Würfel zu sehen.
- Die Merkmale 0x1011d23 enthalten Bit 16. **Ursache (i) ist widerlegt.** Die Abfrage `JGetBufferRegionLevel` wird gar
  nicht erreicht.

Alle Abfragen in JView vfunc+0x80 = view 0x19740 (vtable 0x5ee18, Slot 16) **vor** 0x19b64:

| Stelle | Abfrage | Herkunft des Feldes |
|---|---|---|
| 0x1982c | `0xb7e0(this, 0)` ≠ NULL (JView in globaler Ansichten-Tabelle 0x180086538) | Anmeldung der Ansicht |
| 0x1983a | JView+0x4d8 = 0 | wird bei 0x19e30 nach dem HUD-Attach auf 1 gesetzt |
| 0x19849 | JView+0x1d8 = 0 | noch nicht aufgelöst |
| 0x19b0b | JView+0x90 ≠ 0 | Ansichts-Schnittstelle (QI in 0x16bxx) |
| 0x19b1a | JView+0x4d8 = 0 | wie oben |
| 0x19b29 | **Ansichtsart ≠ 4** | `(+0x90)->vtable+0x48(&art, &unk)` bei 0x19956 |
| 0x19b33 | JView+0x498 = 0x80070490 | Konstruktor 0xfa79; wird 0, sobald der Controller geholt ist |
| 0x19b45 | JView+0x4d0 = 0 | Controller |
| 0x19b54 | JView+0x48c ≠ 0 | einmalig aus `GetSupportedFeatures` (0x198f8) |

Art = 4 führt bei 0x19d63 in einen eigenen Zweig: Er ruft auf einem anderen Objekt vtable+0x80 auf (0x19dfb) und hängt
**nie** einen HUD an. Der Verdacht, dass Art = 4 die Ursache ist, ist vermutet, nicht belegt.

### 7.5 Ein Lauf: JView-Felder zur Laufzeit

`opengl-wrapper/out/opengl32.dll`, SHA256 `ed5af690…858a`, hat einen neuen Schalter **`MEMDC_GL_HUD_VIEW=1`**:
- Er ersetzt den Eintrag in der JView-vtable (view.dvx RVA 0x5ee18+0x80, nur wenn er auf 0x19740 zeigt) durch einen
  Stub. Der Stub sichert rcx, rdx, r8 und r9, protokolliert und springt dann ins Original.
- Er protokolliert pro Aufruf (nur bei Änderung) `+90`, `art` (über denselben Aufruf vtable+0x48 wie das Original),
  `+1d8`, `+4d8`, `+498`, `+4d0`, `+48c`, `+490` und `show`.
- Bleibt die Zeile `JView vfunc+0x80 … #` ganz aus, wird 0x19740 nie aufgerufen.

Solid Edge beenden:

```bash
cd ~/solidedge && source env.sh && "$WINESERVER" -k
```

Starten:

```bash
cd ~/solidedge && MEMDC_GL_HUD_VIEW=1 MEMDC_GL_HUD_BRL=log MESA_WRAPPER=1 MEMDC_GL_DEBUG=1 MESA_DIR=runtime/mesa-22.0.3-p SE_TESTDATEI='C:\Program Files\Siemens\Solid Edge 2026\Training\block.par' ./mesa-test.sh zink 150
```

Ablauf: laden, Maus einmal über die 3D-Ansicht bewegen, ein Rechteck ziehen. Auswertung:

```bash
grep -a -E "JView|HUD-Bedingung|vtable passt" ~/solidedge/logs/mesa-zink-mesa-22.0.3-p.log | head -40
```

| Befund | Bedeutung, nächster Schritt |
|---|---|
| `art=4` | Die Ansicht meldet Art 4, der HUD-Zweig ist ausgeschlossen. Statisch klären, was Art 4 bedeutet und woher sie kommt (`(+0x90)->vtable+0x48`) |
| `+1d8≠0` oder `+4d8≠0` vor jedem Attach | Dieses Feld sperrt. Statisch die Schreibstellen klären |
| `+90=0` | Die Ansichts-Schnittstelle fehlt (QI bei 0x16bxx scheitert) |
| `+498` ≠ 0x80070490 und `+4d0=0` | Ein früherer Versuch hat das HRESULT überschrieben, deshalb kein neuer Versuch |
| keine `JView vfunc+0x80`-Zeile, aber „wird protokolliert“ steht im Log | 0x19740 wird nicht aufgerufen. Der Auslöser fehlt, das wäre Spur (b) |
| Absturz kurz nach „wird protokolliert“ | Der Stub bzw. der zusätzliche vtable+0x48-Aufruf verträgt sich nicht. Dann ohne Art erneut, mir melden |

### 7.6 Befund JView-Felder (DLL ed5af690) und gezielte Änderung

Messung (Koordinator):
- Die 3D-JView `46047B08` hat `+90=490D0148 art=0 +1d8=0 +4d8=0 +498=0x80070490 +4d0=0 +48c=0`, unverändert
  bis #24.
- **Die Abfrage bei 0x19b54 (`+0x48c ≠ 0`) schlägt fehl.** Deshalb wird `JGetBufferRegionLevel` nie aufgerufen, kein
  Controller geholt, kein Attach ausgeführt, und es gibt weder Würfel noch Rechteck-Overlay.

Korrektur zu 7.1, Punkt 5 (belegt, view 0x198f8–0x19915): +0x48c wird **nicht einmalig** gelesen, sondern **bei
jedem Aufruf neu, solange es 0 ist**: `(+0xe8)->vtable+0x30(&+0x48c)`. Weil es bis #24 bei 0 bleibt, liefert dieser
Aufruf dauerhaft 0 oder scheitert. Es ist also kein Zeitproblem.

Kette des Aufrufs:
- JView+0xe8 = `JCoreView`+0x90 (`IJRenderSetupImp`, corevw 0x0d223; QI `{9064842A-…}` bei corevw 0x1a982).
- `GetSupportedFeatures` (corevw 0x5b100) holt über `this−0x88` → QI `{85261840-…}` → vtable+0x18(1, &x) → QI
  `{9064842A-…}` → vtable+0x30 ein Render-Setup.
- Bei `render.dll` endet das in `JRenderProxy` (vtable `IJRenderSetupProxy`, Slot 6 = 0x17e5f0, leitet weiter) bzw.
  `JRenderManager::CIJRenderSetupImp` Slot 6 = 0x192da0: `*out = this->+0xce8`.
- `render.dll` selbst übergibt an `JGetBufferRegionLevel` die Merkmale 0x1011d23. Woher diese stammen (JRC+0x3c laut
  README) und warum +0xce8 bzw. das per vtable+0x18(1) gewählte Objekt 0 liefert, ist statisch nicht
  abschließend belegt.
- Vermutet: vtable+0x18(1) liefert ein anderes bzw. nicht initialisiertes Render-Setup, oder `InitializeRenderer`
  schreibt die Merkmale unter Wine nicht nach +0xce8. Der Modulaufruf 0x193de1 bekommt `&+0xce8`; wenn das Modul auf
  dem Zink-Pfad nicht schreibt, bleibt 0.

**Gezielte Änderung `MEMDC_GL_HUD_VIEW=fix`** (DLL `fc46d33f…1e17`). Im bestehenden Zwischenstück vor JView
vfunc+0x80, nur wenn +0x90 gesetzt ist und +0x48c = 0:
1. **Gleicher Aufruf wie das Original:** `(+0xe8)->vtable+0x30(&wert)`. Liefert er einen Wert ≠ 0, wird dieser
   eingetragen. Protokolliert werden HRESULT und Wert, das klärt nebenbei den Grund.
2. **Sonst** die Merkmale, die `render.dll` zuletzt selbst an `JGetBufferRegionLevel` übergeben hat (mit Bit 16),
   gesammelt vom `JGetBufferRegionLevel`-Ersatz. Der ist bei `fix` automatisch im Modus `log` aktiv, also gleichwertig.
   Der Wert wird nicht fest eingetragen, sondern vom Renderer zur Laufzeit übernommen.

Danach läuft das Original unverändert weiter: `JGetBufferRegionLevel` > 1 → Controller → Widget → Attach (0x19e3f) →
0x4E-Knoten in der Overlay-Liste.

Solid Edge beenden:

```bash
cd ~/solidedge && source env.sh && "$WINESERVER" -k
```

Starten:

```bash
cd ~/solidedge && MEMDC_GL_HUD_VIEW=fix MEMDC_GL_FRONT_FLUSH=1 MESA_WRAPPER=1 MEMDC_GL_DEBUG=1 MESA_DIR=runtime/mesa-22.0.3-p SE_TESTDATEI='C:\Program Files\Siemens\Solid Edge 2026\Training\block.par' ./mesa-test.sh zink 150
```

Auswertung:

```bash
grep -a -E "fix|JView|HUD-Bedingung|HUDCamera|Aktivität" ~/solidedge/logs/mesa-zink-mesa-22.0.3-p.log | head -60
```

Erwartet:
1. `fix: GetSupportedFeatures(...) → hr …, Merkmale 0` (das ist der Grund), danach `+0x48c ← 0x1011d23`;
2. `JGetBufferRegionLevel(0x1011d23) = 2, Aufrufer view.dvx+0x19b6a → HUD-Bedingung`;
3. eine JView-Zeile mit `+4d0≠0`, `+490≠0`, `+4d8=1`;
4. `glBegin aus HUDCamera` > 0 und der Würfel im Fensterbild.

Fürs Rechteck zusätzlich: Beim Ziehen erscheinen `Aktivität`-Zeilen. Fehlen sie trotz Attach, liegt das restliche
Problem beim Anstoß (Spur b).

### 7.7 Ursache gefunden: Wine-Semantik von `CRITICAL_SECTION.LockCount` in view.dvx

**Kontrollfluss von 0x19740 bis 0x19b64 (vollständig, belegt).** Vor der HUD-Abfrage gibt es nur diese Rücksprünge:

| Stelle | Bedingung | Sprungziel |
|---|---|---|
| 0x1981d | TLS-Init-Wächter | 0x1bf5c, kehrt zurück |
| **0x19834** | **`0xb7e0(this,0)` == NULL** | 0x1be62 = `return 0` |
| 0x19843 | +0x4d8 ≠ 0 | 0x1be62 |
| 0x19852 | +0x1d8 ≠ 0 | 0x1be62 |
| 0x198d4 | QI `{…e588}` auf +0x90 scheitert mit etwas anderem als E_NOINTERFACE | 0x1bed1 (Fehler) |
| 0x199f5 | QI `{…ead8}` scheitert ebenso | 0x1bfb2 (Fehler) |

Alle späteren Sprünge (0x19b14–0x19b6d) führen nur nach 0x19d43, nicht aus der Funktion. Der fix-Lauf hat +0x48c
nachgeladen, das Original selbst hat es nie getan. Damit hat das Original 0x198f8 nie erreicht. Bei +0x4d8 = +0x1d8 = 0
bleibt nur **0x19834**.

**Was `0xb7e0(this, modus)` tut (view.dvx 0xb7e0, belegt):**
- Es ist die Tabelle der lebenden Ansichten: `std::set<JView*>` bei 0x180086538, geschützt durch das Sperrobjekt
  0x180086500 = `{int ok; CRITICAL_SECTION cs}`.
- Initialisierung über 0xf740 (`InitializeCriticalSectionAndSpinCount(&+8, 0xFFF)`).
- `modus = 1` trägt ein (JView-Konstruktor view 0xfb5b), `−1` trägt aus (Destruktor 0x14bc3), `0` sucht
  (0x1982c und 0x21a6b).
- Vor jedem Zugriff:
  ```
  0xb83e  call 0x16740          ; EnterCriticalSection(&lock.cs) (0x1675c), Wächter = &lock
  0xb84c  cmpl $0,0x10(%rax)    ; lock+0x10 = cs+8 = CRITICAL_SECTION.LockCount
  0xb850  je   0xb991           ; == 0 → ohne Eintragen/Suchen „nicht gefunden“ (NULL) zurückgeben
  ```
  Dasselbe Muster steht bei 0xb4fe/0xb50c/0xb510 in 0xb4a0.
- **Windows (ab Vista):** Eine gehaltene, unumkämpfte Sperre hat `LockCount = −2` (≠ 0), die Abfrage greift also nie.
- **Wine** behält die NT4-Semantik: `RtlEnterCriticalSection` macht `InterlockedIncrement(&LockCount)` von −1 aus,
  eine gehaltene Sperre hat also `LockCount = 0`. Damit liefert **jeder** Aufruf von 0xb7e0 NULL:
  - der JView-Konstruktor trägt **nie** ein;
  - JView vfunc+0x80 (0x19740) bricht bei 0x19834 immer sofort ab;
  - kein `GetSupportedFeatures`, kein Controller, kein Attach, also weder Würfel noch Rechteck-Overlay.

  Außerdem fallen in 0x19740 Raw-/Touch-Eingabe, die Weitergabe der HUD-Ereignisse und Weiteres weg, und 0x21a6b sucht
  ebenso vergeblich.
- Ein Scan aller SE-Module nach demselben Code-Muster (`call; mov rax,[rsp+x]; cmp dword [rax+0x10],0; je`) findet es
  nur in view.dvx an den zwei Stellen. Treffer in `ProESEServerSide.dll` und `VoxelSolver_Main.exe` sind wohl
  unabhängig.

**Gezielte Änderung (behebt die Ursache, nicht das Symptom).** `MEMDC_GL_HUD_VIEW=fix`, DLL `4f12d982…88fd`:
1. Sobald view.dvx geladen ist (beim nächsten `wglMakeCurrent`), werden die beiden bedingten Sprünge `je` bei
   view.dvx+0xb510 und +0xb850 durch einen 6-Byte-NOP ersetzt. Vorher werden die Bytes geprüft
   (`83 78 10 00 0f 84 3b 01 00 00`). Das ergibt genau das Windows-Verhalten: Nach `EnterCriticalSection` wird immer
   weitergemacht.
2. Weil der JView-Konstruktor schon vorher gelaufen und sein Eintrag ins Leere gegangen sein kann, prüft das
   Zwischenstück vor vfunc+0x80 mit demselben Aufruf wie das Original, `0xb7e0(this,0)`. Ist das NULL, trägt es
   einmal nach, wie der Konstruktor es getan hätte: `0xb7e0(this,1)`.
3. Das Nachladen von +0x48c aus 7.6 bleibt als Absicherung bestehen. Es greift nicht mehr, wenn das Original selbst
   0x198f8 erreicht.

Das Protokoll zeigt pro JView `b7e0=…`. In jedem Modus wird zuerst gesucht, auch ohne `fix`; ohne `fix` bestätigt
das Ergebnis NULL die Ursache.

Solid Edge beenden:

```bash
cd ~/solidedge && source env.sh && "$WINESERVER" -k
```

Starten:

```bash
cd ~/solidedge && MEMDC_GL_HUD_VIEW=fix MEMDC_GL_FRONT_FLUSH=1 MESA_WRAPPER=1 MEMDC_GL_DEBUG=1 MESA_DIR=runtime/mesa-22.0.3-p SE_TESTDATEI='C:\Program Files\Siemens\Solid Edge 2026\Training\block.par' ./mesa-test.sh zink 150
```

Auswertung:

```bash
grep -a -E "fix|JView|HUD-Bedingung|HUDCamera|Aktivität" ~/solidedge/logs/mesa-zink-mesa-22.0.3-p.log | head -60
```

Erwartet:
1. `fix: view.dvx+0xb510 … entfernt`, ebenso `+0xb850`;
2. `fix: JView … nachgetragen (… suchen → <JView>)`;
3. JView-Zeile mit `b7e0=<JView>`, danach `+4d0≠0`, `+490≠0`, `+4d8=1`;
4. `JGetBufferRegionLevel(0x1011d23) = 2, Aufrufer view.dvx+0x19b6a → HUD-Bedingung`;
5. `glBegin aus HUDCamera` > 0, Würfel im Fensterbild;
6. beim Rechteck-Ziehen `Aktivität`-Zeilen.

Schlägt Punkt 2 fehl (`suchen → 0`), greift der Patch nicht: Bytes oder Zeitpunkt prüfen.

### 7.8 Abgleich mit Spur (b) (`docs/hud-b-refresh.md`)

- Spur (b) zeigt: JView::WindowProc (view 0x242c3) leitet jede Nachricht an das 3D-Fenster über vfunc+0x80
  (0x19740). HUD_REFRESH geht nur bei JView+0x4d0 ≠ 0 an `HUDController::OnMessage` (Visual 0x1f8120) und von dort
  zum Neuzeichnen (`JCanvas::SendOpcode(12)` → render 0x1b310). Weil 0x19740 bei 0x19834 abbricht (7.7), ist +0x4d0
  nie gesetzt. **Das Rechteck hängt also an derselben Ursache wie der Würfel.** Die Änderung `MEMDC_GL_HUD_VIEW=fix`
  sollte beide zurückbringen.
- Korrektur zu Abschnitt 2: In der aktiven Umgebung (Baugruppe) ist der Auswahlbefehl `JAsmSmartLocateCmd` in
  **ACASM.DCX**, nicht `JMSmartLocateCmd` in partDcx. Der Mechanismus ist derselbe: ACASM.DCX enthält ebenfalls die
  IIDs IJRenderLite `{600DF00D-…}` und IHUDTransient `{…-8005}` (Scan in 3). Die partDcx-Adressen in 2.1 gelten für
  die Teile-Umgebung.

## Werkzeuge

Die Hilfsskripte liegen nur im Sitzungs-Scratchpad (`…/scratchpad/re/`):
- `pe.py`: Sektionen, Exporte, Importe, `.pdata`, RTTI und vtables;
- `dis.py`: Funktion per `.pdata` disassemblieren, mit Importen, Exporten, Strings, GUIDs, vtables und der GL-Tabelle
  von Visual (`gltab.pkl`) beschriftet;
- `vt.py`: vtable ausgeben;
- `seclasses2.pkl`: CLSID → (Name, Modul) aus `SEClasses.dll`.
