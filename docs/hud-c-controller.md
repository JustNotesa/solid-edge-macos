# Spur (c): glLogicOp, HUDController-Anlage, Bedingungen für den Würfel-Knoten

Stand 2026-11-01. Nur statische Analyse (objdump, Python `-I`); kein Wine-Lauf. Adressen sind RVAs (Image-Basis
0x180000000). „belegt“ = im Code gelesen, „vermutet“ = Schluss ohne vollständigen Beleg. Die Arbeit wurde auf Wunsch
des Koordinators vorzeitig beendet; offene Punkte stehen in Abschnitt 5.

## 1. glLogicOp (ursprüngliche Frage, erledigt)

- **belegt:** `render.dll` 0x57520 ruft über die GL-Tabelle (Zeiger in render 0x180247680, Slots +0x4b0 und +0x200)
  `glLogicOp(GL_COPY 0x1503)` und `glDisable(GL_COLOR_LOGIC_OP 0x0BF2)` auf, danach `SetROP2(hdc, R2_COPYPEN 0xD)`
  (0x575c6–0x575f6). Das ist ein Zurücksetzen des Zeichenmodus. Danach kommt **kein** `glGetError` und keine
  Fähigkeitsprüfung.
- `Visual.dll` und `igl.dll` importieren `glLogicOp` zusätzlich direkt. Diese Aufrufstellen wurden nicht einzeln
  durchgesehen, weil der Koordinator die Frage als beantwortet abgeschlossen hat (Abschnitt 6.3 (c) in
  `rechteck-und-wuerfel.md`).
- **Bewertung:** Mit dem HUD hängt das nicht zusammen. Das passt dazu, dass Würfel und Rechteck auch mit llvmpipe
  fehlen, obwohl llvmpipe logicOp kann.
- Hinweis zum Werkzeug: Die Namen in `re/gltab.pkl` stimmen für spätere Slots nicht. Beispiel: Slot 0x1808997a0 ist
  laut Auflöser `wglSwapIntervalARB/EXT` (Visual 0x23b02d) und nicht „wglSaveBufferRegionARB“. Bei GL-Namen aus
  `dis.py` deshalb den Auflöser-String prüfen.

## 2. HUDController-Anlage: kein Wine-Risiko im Code

Die Messung (4× `PostMessageW(…, HUD_REFRESH)`) zeigt, dass der Controller existiert. Der statische Befund passt dazu.

| Schritt | Adresse | Befund |
|---|---|---|
| Aufruf | corevw 0x1a364 → Thunk 0x86f8e → `JUtil!JCoCreateInstance` | belegt; Argumente: CLSID = IID = `{19600806-ABCD-1234-8000-…}`, `CLSCTX_INPROC_SERVER`, Ziel JCoreView+0x4408 |
| Nur ein Versuch | corevw 0xd479 setzt +0x4400 = 0x80070490; 0x1a328/0x1a334 versucht es nur bei diesem Wert und setzt vorher 0x8007139f | belegt: Ein Fehlschlag wäre pro JCoreView **dauerhaft** und still (nur HRESULT bei +0x4400) |
| JCoCreateInstance | JUtil 0x23d70 → `JCoGetClassObject` 0x23e80 → 0x21950 → `IClassFactory::CreateInstance` | belegt. Sonderfall: CLSID `{12ede2bc-ea3b-4c60-abcf-c3f962cba192}` liefert S_OK mit NULL (betrifft HUD nicht) |
| Klassen-Cache | 0x21d10: lädt einmalig `seclasses.dll!JRegisterSolidEdgeClasses`, dann `hashlookup`; Modul per GetModuleHandle bzw. LoadLibrary (0x21fe0) und `DllGetClassObject`; nur ohne Cache-Eintrag Registry bzw. `CoGetClassObject` (0x21b5c) | belegt |
| Fabrik | `Visual!DllGetClassObject` 0x506550, Zweig 0x506f59 → `ClassFactory<HUDController>` (vtable 0x6b91a8) | belegt; derselbe Weg wie für `CLSID_JRenderer` (0x506651), der nachweislich funktioniert |
| OnCreateInstance | Visual 0x4feed0: `new(0x90)` → Konstruktor 0x1f73a0 → Init 0x1f9620 (nur Refcount −1, gibt S_OK zurück) → QI | belegt; scheitert nur bei E_OUTOFMEMORY bzw. QI |
| Konstruktor | Visual 0x1f73a0: vtables, zwei Listen, `typeid`-Name samt Tracker 0x86880, legt den Transient-Container `{…-8006}` über XIHUDController+0x58 an (0x1f7850 → `new(0x98)`, 0x1fa530) | belegt: **kein** GL, keine Ressourcen, Texturen, Shader, D2D, WIC, Fonts oder Registry |

**Folgerung (belegt):** Im Anlegeweg des HUDControllers gibt es keine Stelle, die unter Wine oder Mesa anders
ausfällt. Shader-Kompilierung, Texturen und Fonts kommen dort nicht vor.

## 3. Würfel-Knoten: Bedingungen in view.dvx 0x19740 (JView, r12 = this)

### 3.1 Funktionseingang (vor dem Controller-Holen)

| Adresse | Bedingung | Bei Verletzung |
|---|---|---|
| 0x1982c | `0xb7e0(this, 0)` ≠ 0 | Sprung 0x1be62 (Funktion endet) |
| 0x1983a | JView+0x4d8 == 0 (Wiedereintrittssperre, siehe 3.4) | 0x1be62 |
| 0x19849 | JView+0x1d8 == 0 | 0x1be62 |
| 0x19956–0x19960 | **Ansichtsart:** JView+0x90 → vtable+0x48(&art) in −0x38(rbp). Nur bei art == 4 folgt eine QI auf `{3bb4e210-c6bb-11ce-a434-0800367e0502}` | — |
| 0x198f8–0x19915 | **Merkmale JView+0x48c:** Ist der Wert 0, wird er über JView+0xe8 → vtable+0x30(&+0x48c) geholt | — |

### 3.2 Controller holen (0x19b0b–0x19c3d)

Alle Bedingungen müssen gelten, sonst geht es weiter bei 0x19d43 und es wird **kein** Würfel-Widget angelegt:

1. JView+0x90 ≠ 0
2. JView+0x4d8 == 0
3. Ansichtsart ≠ 4
4. JView+0x498 == 0x80070490 (noch nicht versucht; wird bei Erfolg in 0x19c0a auf 0 gesetzt)
5. JView+0x4d0 == 0 (noch kein Controller)
6. JView+0x48c ≠ 0
7. **`JGetBufferRegionLevel(JView+0x48c) > 1`** (0x19b64/0x19b6d)

Danach folgt die QI über JView+0x78 auf `{…-8000}`; das Ergebnis landet in JView+0x4d0.

**`Visual!JGetBufferRegionLevel`** (0x44c680, belegt):
- Rückgabe 2, wenn `DEBUG\ENABLE_BUFFER_REGIONS` (Standard 1) gesetzt ist **und** Merkmal-Bit 16 (0x10000) gesetzt ist;
- sonst 1 + Bit 10, wenn `(f & 0x10010000) == 0x10000`;
- sonst 0.

Ohne Bit 16 ist das Ergebnis also ≤ 1, und der Würfel wird nicht angelegt.

### 3.3 Herkunft der Merkmale JView+0x48c (belegt bis auf den letzten Schreiber)

- JView+0xe8 ist das Interface `{9064842a-66e4-11ce-9a5e-0800369b4102}` (IJRenderSetup), per QI geholt bei view 0x16c4f.
  Geliefert wird es von JCoreView+0x90 (`IJRenderSetupImp`, corevw QI 0x1a959–0x1a982; vtable 0x97fd8).
- Slot +0x30 ist **`JCoreView::IJRenderSetupImp::GetSupportedFeatures`** (corevw 0x5b100): QI auf `{85261840-…}`,
  dann +0x18(1) für den Render-Manager, dann QI auf `{9064842a-…}`, dann dessen +0x30.
- Im Render-Manager (render.dll, Konstruktor 0x179f00) hängt bei +0x58 `CIJRenderSetupImp` (vtable 0x1f3740). Der
  Proxy-Zweig 0x185488 greift nur bei `ENABLE_PROXY_WRAPPER`.
  - Slot +0x30 = render **0x192da0**: `*pdw = *(this+0xce8)`, also **JRenderManager+0xd40**.
- **Startwert:** render 0x17b163 im Konstruktor setzt JRenderManager+0xd40 = **0x0100D007**. Darin ist **Bit 16 nicht
  gesetzt** (gesetzt sind die Bits 0, 1, 2, 12, 14, 15 und 24).
  - Später schreibt render 0x194269 (`mov %eax,0xd40(%rsi)`) einen berechneten Wert. Diese Berechnung ist **nicht
    analysiert**.
  - Nur wenn dort Bit 16 (`JRENDER_HARDWARE_BACKINGSTORE`) dazukommt, wird der Würfel angelegt.
- **Backing-Store-Art in Visual** (0x20e350, aufgerufen von 0x20b034 und 0x221a22; belegt):

  | Rückgabe | Bedeutung |
  |---|---|
  | 1 | `glNewBufferRegion` (GL_KTX_buffer_region) |
  | 2 | `wglCreateBufferRegionARB` |
  | 3 | FBO (`glBlitFramebuffer`) |
  | 5 | nichts |
  | 0 | kein Kontext |

  Der Hersteller-Test 0x20e470 liest `glGetString(GL_VENDOR)`.
  - Unter Wine/Mesa fehlen die KTX-Funktionen. Belegt durch `logs/run-gl.log`: „glNewBufferRegion not found“ usw.
    sowie `wglGetProcAddress(glDrawBufferRegion) → NULL` in `logs/mesa-zink-mesa-22.0.3-p.log`.
  - `glBlitFramebuffer` ist vorhanden, also Art 3 (FBO). Das Ladeprotokoll zeigt passend dazu Blit BACK→FBO.
  - **Nicht belegt:** ob Art 3 zu Bit 16 in JRenderManager+0xd40 führt.

### 3.4 Widget anlegen und Attach (0x19c43–0x19e58)

- 0x19c43–0x19d31: IHUDController+0x68 (FindWidget `{…-8002}`). Fehlt das Widget und ist Show (JView+0x630) ≠ 0,
  folgt +0x58 (CreateWidget).
  - In Visual ist das 0x1f7850: `new(0x5b8)` und der HUDCamera-Konstruktor 0x1db2a0 (nicht analysiert).
  - Danach: QI `{…-8002}` und JView-vtable+0x60 (Einstellungen anwenden).
- 0x19d43–0x19d5d: erneut JView+0x90 ≠ 0 und JView+0x1d8 == 0, sonst Ende.
- 0x19d63: Bei **Ansichtsart == 4** geht es in einen eigenen Zweig (0x19d6d–0x19e04: `0xb4a0`, QI IUnknown,
  vtable+0x80). Dieser Zweig enthält **kein** Attach.
- **Attach** (0x19e09–0x19e58, belegt):
  - verlangt JView+0x4d0 ≠ 0 (Controller vorhanden);
  - ist JView+0x490 == 0 und JView+0x4d8 == 0: dann JView+0x4d8 = 1 (Sperre), Aufruf **Controller-vtable+0x38**
    (0x19e3f), Ergebnis nach JView+0x490, danach JView+0x4d8 = 0 (0x19e58);
  - ist das Ergebnis 0, kommt Sprung 0x1bdfb.
  - Der Attach-Rumpf in Visual (laut Agent 1 bei 0x1f9740) wurde hier **nicht** analysiert.
- Folge: Das Attach (und damit die Overlay-Liste, siehe Agent 1) hängt **nur** an JView+0x4d0. JView+0x4d0 wird nur in
  3.2 gesetzt und hängt dort an `JGetBufferRegionLevel > 1`.
- Das Rechteck legt den Controller über corevw an (JCoreView+0x4408). Das ist ein **anderes** Feld als JView+0x4d0. Ein
  existierender Controller (HUD_REFRESH wird gepostet) bedeutet also **nicht**, dass JView ihn geholt und angehängt hat.

## 4. Bewertung und Vorschlag

- **Hauptkandidat (vermutet, gut begründet):** Bedingung 7 in 3.2. Die Merkmale aus JRenderManager+0xd40 haben unter
  Wine/Mesa kein Bit 16.
  - Dann bleibt JView+0x4d0 = 0, es gibt kein Attach, die Overlay-Liste bleibt leer und HUDCamera wird nie aufgerufen.
  - Das passt zu llvmpipe, denn auch dort fehlt GL_KTX_buffer_region bzw. WGL_ARB_buffer_region.
  - Auf NVIDIA/AMD gibt es `WGL_ARB_buffer_region` bzw. `GL_KTX_buffer_region` traditionell. Das ist ein
    plausibler Unterschied zu Mesa.
  - Die Messung des Koordinators („frühere Abfrage vor 0x19b6a greift“) widerspricht dem nicht, sofern damit
    JView+0x48c bzw. der Rückgabewert gemeint ist.
- **Ein Messlauf, der entscheidet:** Haltepunkt (winedbg) auf view.dvx **0x19b64**; dort `ecx` lesen, das ist
  JView+0x48c. Ergänzend `rax` nach Rückkehr 0x19b6a sowie die Felder JView+0x90/+0x4d8/+0x498/+0x4d0 und −0x38(rbp).

  | Befund | Folgerung und Änderung |
  |---|---|
  | Bit 16 fehlt | Statisch render 0x194269 verfolgen: welche Bedingung Bit 16 setzt. Dann gezielt die opengl32-Vorschaltung `wglCreateBufferRegionARB`/`wglSaveBufferRegionARB`/`wglRestoreBufferRegionARB` (oder KTX `glNewBufferRegion`…) per FBO melden und emulieren lassen. Nur Testweg, ohne Garantie: `DEBUG\ENABLE_BUFFER_REGIONS` hilft **nicht**, weil Bit 16 trotzdem nötig ist |
  | Bit 16 vorhanden, eine andere Bedingung greift | Die Ursache liegt in den Feldern aus 3.1/3.2 (Ansichtsart = 4? +0x498 schon verbraucht?) und nicht in GL-Fähigkeiten |

## 5. Offen (bewusst nicht weiter verfolgt)

- render 0x194269: Berechnung der Merkmale (Bit 16);
- Visual 0x1f9740 (Attach), HUDCamera-Konstruktor 0x1db2a0;
- Shader-Pfad des HUD: In HUDCamera::Display gibt es nur Sofortmodus-GL (siehe Hauptdokument), also keinen eigenen
  Shader. Shader-Kompilierfehler sind deshalb kein Kandidat für den Würfel (vermutet, nicht vollständig geprüft).

Arbeitsdateien: `<scratch>/re-c/view_19740.txt`
(annotiertes Listing von view.dvx 0x19740).
