# „Skizze schließen“ wirkt nicht – Ursache und Korrektur

Stand 2026-11-02. Solid Edge 2026 (226.00.01.04), Wine Staging 11.18, Mesa 22.0.3 (zink) mit opengl32-Vorschaltung.

## 1. Ergebnis in Kürze

- **Ursache ist Wines `ole32!OleSetMenuDescriptor`.** Die Skizzen- bzw. Profilumgebung ist in Solid Edge ein
  in-place aktives OLE-Objekt. Beim Verlassen ruft jengine `IOleInPlaceFrame::SetMenu(NULL, NULL, NULL)` **zweimal**
  hintereinander auf. Beide Aufrufe enden in `OleSetMenuDescriptor(NULL, hwndFrame, NULL, NULL, NULL)`.
  Der erste entfernt Wines Menü-Hook und liefert `S_OK`. Der zweite findet keinen Hook mehr, und Wine gibt dann
  `E_FAIL` zurück. Windows meldet in diesem Fall keinen Fehler, sonst ginge „Skizze schließen“ dort auch nicht.
- Das `E_FAIL` wandert nach oben: `IOleInPlaceSite::OnUIDeactivate` (OLESITE.dll) → `JInPlaceObject::UIDeactivate`
  (jengine) → `HProfiles::IHProfilesImp::_EndSketch` (PROFDAX) → `CProfValidateDlg::ExitProfile` (profdcx). Dort
  bricht das Verlassen still ab. Die Befehlsleiste ist dann schon abgebaut, deshalb ist die Aufforderungszeile leer,
  aber die Umgebung bleibt `LayoutInPart`.
- **Es liegt nicht am Menüband.** Derselbe Fehler tritt auf bei `Application.StartCommand(10231)` per COM, beim
  grünen Haken in der 3D-Ansicht und bei einer leeren Skizze.
- **Korrektur:** In der opengl32-Vorschaltung wird jengines Importeintrag für `OleSetMenuDescriptor` auf eine Hülle
  umgebogen. Sie macht beim Entfernen (`hOleMenu == NULL`) aus `E_FAIL` ein `S_OK`. Schalter `MEMDC_GL_OLEMENU=fix`,
  in `env.sh` standardmäßig an.
- **Bestätigt:** Mit der Korrektur führt ein Klick auf „Skizze schließen“ zurück in „Sequentielles Teil“ (Umgebung
  `Part`). Danach zeigt die Skizzen-Befehlsleiste „Fertigstellen“, und Skizze 2 bzw. Skizze 3 erscheinen im
  PathFinder. Getestet wurden zwei Durchläufe nacheinander in derselben Sitzung.

## 2. Nachstellen

1. `MESA_WRAPPER=1 MESA_DIR=runtime/mesa-22.0.3-p SE_TESTDATEI='C:\skizze-test.par' ./mesa-test.sh zink 55`
2. Menüband „Skizze“ (413,95), Maus auf die Fläche (480,650), dort klicken: Skizzenmodus, Befehl Linie.
3. Linie: Klick (500,820), Klick (800,840), Esc.
4. „Skizze schließen“ (1412,95).

Ohne Korrektur wird der Knopf hervorgehoben, die gezeichnete Linie verschwindet aus der Ansicht, die Aufforderungszeile
bleibt leer und der grüne Haken in der Ansicht verschwindet. `ActiveEnvironment` bleibt `LayoutInPart`. Es gibt weder
CPU-Last noch Wine-Fehler noch Ausnahmen: Mit `+seh` sind beim Klick 0 neue Zeilen im Log.

## 3. Abgrenzung: Menüband oder Befehl?

| Weg | Ergebnis ohne Korrektur |
|---|---|
| Menüband „Skizze schließen“ (XTPToolBar) | bleibt in `LayoutInPart` |
| `befehl {"id":10231}` (`Application.StartCommand`, `PartEnvironmentsExit`) | bleibt in `LayoutInPart` |
| grüner Haken links oben in der 3D-Ansicht | bleibt in `LayoutInPart` |
| leere Skizze (ohne Linie) schließen | bleibt in `LayoutInPart` |
| Kontextmenü (Rechtsklick in die Ansicht) | Bei den Tests erschien gar kein Menü, siehe 8 |

Weitere Befehle laufen im Skizzenmodus ganz normal, z. B. `befehl 25011` (Kreis): Befehlsleiste und Aufforderung erscheinen.
Ein zweiter Schließversuch in derselben Sitzung tut nichts mehr. Er erreicht `_EndSketch` nicht, weil die
UI-Deaktivierung schon halb gelaufen ist. Deshalb braucht jeder Messlauf einen Neustart.

Befehls-IDs: Das Menüband sendet `WM_COMMAND 0x27f7` (10231) an das Hauptfenster (`EngineFrame`), und zwar innerhalb
der `WM_LBUTTONDOWN`-Verfolgung der XTPToolBar. Laut `control.dll` (RVA 0x2e4ab9/0x2e579b) werden auch 0x29b5 und
0x2caf auf 10231 abgebildet. `SECmdMap.xml` kennt 10231 nur als „3D-Skizze Schließen“. Eine eigene Konstante für
„Skizze schließen“ gibt es in `constant.tlb` nicht. `PartEnvironmentsExit` = 10231 ist der passende Befehl.

## 4. Was ausgeschlossen wurde

- **Verdeckter oder modaler Dialog:** Es gibt keinen. Alle Top-Level-Fenster wurden aufgelistet (eigenes `enumwin.exe`
  im selben wineserver). Das Hauptfenster ist aktiviert und es gibt kein sichtbares oder deaktivierendes Popup. Das
  einzige versteckte `#32770` (210×210 bei −105,−105) existiert schon vor dem Klick.
- **Verschluckte Ausnahmen:** Mit `WINEDEBUG=+seh` gibt es während des Klicks keine einzige Ausnahme.
- **Relay-Spur** (`user32` PostMessage/SetTimer/ShowWindow/SetWindowPos/CreateWindowEx/Dialog-APIs/MessageBox,
  `ole32.CoCreateInstance`):
  - kein MessageBox, kein Dialog, kein CoCreateInstance;
  - der Befehl baut die Befehlsleiste der Linie ab (`DestroyWindow`), lädt `ResDLLs\0007\PROFCMD.drx`, setzt das
    Menü (`WM_MDISETMENU`) und den MDI-Titel und kehrt dann zurück.
  - Die 16 Threadpool-Threads (`ntdll+0x662b0`), die danach entstehen, sind normale Wine-Threadpool-Worker: Sie
    entstehen und enden über die ganze Sitzung, 161 Stück. Mit dem Fehler haben sie nichts zu tun.

## 5. Befehlsweg (gemessen)

Gemessen mit einem kleinen In-Prozess-Tracer (`tools/setrace.c`, siehe 7). Er setzt int3-Haltepunkte und gibt
Aufrufketten aus. Der Schlüssel war ein Haltepunkt auf `JUtil.dll!HRerrEx`: Das ist die Fehlerprotokoll-Funktion
von Solid Edge, sie bekommt HRESULT, Quelldatei, Zeile und Funktionsname.

```
HRerrEx 80004005  z:\ENGINE\SRC\APP\DEV\Jipobj.cpp  Zeile 879   JInPlaceObject::UIDeactivate
HRerrEx 80004005  P:\PROFILE\SRC\DAX\DEV\HPROFILS.CPP           HProfiles::IHProfilesImp::_EndSketch
HRerrEx 80004005  P:\PROFILE\SRC\DCX\DEV\PFVALDLG.CPP  Zeile 3006 CProfValidateDlg::ExitProfile
```

Aufrufkette (Modul+RVA, innerster Rahmen oben):

```
ole32!OleSetMenuDescriptor(NULL, hwndFrame=0x1009A, NULL, NULL, NULL)  → E_FAIL
jengine+0x1477c0   IOleInPlaceFrame::SetMenu (Rückgabe = Ergebnis von OleSetMenuDescriptor, Tail bei +0x147938)
OLESITE+0x2dc90    IOleInPlaceSite::OnUIDeactivate (Aufruf SetMenu bei +0x2dd16, Ergebnis nach ebx bei +0x2dd1e,
                   wegen Flag 0x10000 in [site+0x98] direkt zurückgegeben)
jengine+0x385220   JInPlaceObject::UIDeactivate (ruft site->OnUIDeactivate(FALSE) bei +0x385281, HRerrEx Zeile 0x36f)
jengine+0x3839ba
PROFDAX+0x3bf89    HProfiles::IHProfilesImp::_EndSketch
profdcx+0x2bfe2 / +0x2bec2 / +0x3910e   CProfValidateDlg::ExitProfile …
profdcx+0x1a20c    Befehlsausführung (Funktion +0x1a100)
cmdspt+0x28ced → jengine+0x50d71 … ACASM.DCX+0x18dedc … jengine WM_COMMAND-Behandlung (+0x2723f1)
```

Die beiden `SetMenu(NULL)`-Aufrufe beim Verlassen, gemessen mit der Test-Hülle:

```
OleSetMenuDescriptor(0xBA00D8, 0x1009C, 0x500F4) = 0             ← beim Betreten der Skizze (Hook wird installiert)
OleSetMenuDescriptor(NULL,     0x1009C, NULL)    = 0             ← jengine+0x383ca4 (JInPlaceObject, Teil von UIDeactivate)
OleSetMenuDescriptor(NULL,     0x1009C, NULL)    = 0x80004005    ← über OLESITE OnUIDeactivate → SetMenu
```

## 6. Ursache in Wine

`dlls/ole32/ole2.c`, `OleSetMenuDescriptor`: Im Zweig `hOleMenu == NULL` steht
`if (!OLEMenu_UnInstallHooks(tid)) return E_FAIL;`. `OLEMenu_UnInstallHooks` liefert FALSE, wenn für den Thread
kein Hook-Eintrag existiert. Ein zweites Entfernen ist unter Wine also ein Fehler. Unter Windows ist es das nicht:
Solid Edge ruft genau diese Folge auf und wertet das Ergebnis des zweiten Aufrufs als fatal aus. Unter Windows
funktioniert „Skizze schließen“ aber.

**Vorschlag für Wine upstream:** Im Entfernen-Zweig `S_OK` zurückgeben, wenn kein Hook installiert ist, und nur noch
`RemovePropW` ausführen.

## 7. Korrektur (umgesetzt)

### 7.1 Änderungen

- **`opengl-wrapper/memdc_gl.c`**
  - Neue Funktionen `osmd_stub` und `olemenu_fix_einrichten`, direkt vor `wglMakeCurrent`.
  - Aufruf von `olemenu_fix_einrichten()` am Anfang von `wglMakeCurrent`, neben `hud_hooks_einrichten()` und
    `jview_hook_einrichten()`.
  - Ablauf:
    - nur wenn `MEMDC_GL_OLEMENU` gesetzt und nicht `0` ist;
    - wartet, bis `jengine.dll` geladen ist, und läuft danach einmal;
    - sucht in jengines Importtabelle `ole32.dll!OleSetMenuDescriptor` (per Name, nicht per fester Adresse) und
      ersetzt den IAT-Eintrag durch `osmd_stub`.
  - `osmd_stub` ruft das Original auf und gibt bei `hOleMenu == NULL && hr == E_FAIL` stattdessen `S_OK` zurück.
  - Mit `MEMDC_GL_DEBUG=1` erscheinen im Log:
    - `memdc_gl: OleSetMenuDescriptor-Import von jengine.dll umgebogen (fix)`
    - `memdc_gl: OleSetMenuDescriptor(NULL, …): E_FAIL → S_OK (kein Menü-Hook mehr)`
  - Neu gebaut mit `opengl-wrapper/build.sh`. Es gibt nur die bekannten Warnungen; `out/opengl32.dll` ist aktualisiert.
- **`env.sh`:** `export MEMDC_GL_OLEMENU="${MEMDC_GL_OLEMENU:-fix}"` (mit Kommentar) nach `MEMDC_GL_HUD_VIEW`.
- **`README.md`:** neue Zeile `MEMDC_GL_OLEMENU=fix` in der Schaltertabelle der Vorschaltung.
- **`tools/setrace.c`:** neu, der In-Prozess-Tracer, siehe 7.3.

Warum über die Vorschaltung: Sie ist der einzige eigene Code, der ohnehin in Edge.exe läuft. Dort liegen schon die
view.dvx-Korrekturen. jengine ist das **einzige** Solid-Edge-Modul, das `OleSetMenuDescriptor` importiert (geprüft
über alle `.dll/.dcx/.dex/.dax/.dvx/.exe` in `Program`). Der IAT-Hook hängt nicht von Wine-Bytefolgen ab und ändert
keine Datei der Installation.

Eingesetzt wird die neue `opengl32.dll` von `mesa-test.sh` mit `MESA_WRAPPER=1` und von `install-opengl.sh`.
Im Prefix liegt sie seit dem Testlauf bereits neben Edge.exe.

### 7.2 Erwogene Alternativen

- **2-Byte-Patch in OLESITE.dll:** RVA 0x2dd1e `89 c3` (`mov ebx,eax`) → `31 db` (`xor ebx,ebx`), also das Ergebnis
  von SetMenu ignorieren. Das wirkt nur an dieser einen Stelle und ändert eine Solid-Edge-Datei. Verworfen.
- **Gepatchtes ole32.dll als native Override:** Das trifft alle Programme im Prefix und ist bei einer Kern-DLL
  riskant. Verworfen.

### 7.3 Werkzeug `tools/setrace.c`

int3-Haltepunkte mit Vectored Exception Handler. Die DLL wird per `mcp/capture/secapture-inject.exe` in Edge.exe
geladen und über `C:\setrace\cmd.txt` gesteuert.

- **Optionen:** Bedingungen auf Register, Aufrufkette über `RtlVirtualUnwind`, Stack-Argumente und Zeichenketten,
  Ziel einer virtuellen Methode (`vt=`).
- **Läuft unter Wine/Rosetta:** Das Trap-Flag zum Wiedereinsetzen funktioniert.
- **Gefahrlos:** Im Gegensatz zu Relay auf KERNEL32/ntdll stürzt Edge.exe dabei nicht ab.

Beispiel, mit dem die Ursache gefunden wurde:

```
bp JUtil.dll HRerrEx max=10 bt=14 stk=7 str=1
bp OLESITE.dll 0x2dd0b vt=50 label=frame_SetMenu
bp ole32.dll OleSetMenuDescriptor max=20 bt=8
```

## 8. Bestätigung und offene Punkte

- **Fensterbilder mit Korrektur** (ohne Injektion, nur die neue Vorschaltung):
  - nach dem Klick: Titel „Solid Edge 2026 - Sequentielles Teil - [skizze-test.par:1]“, Menüband des Teils, Befehlsleiste
    „Skizze“ mit „Fertigstellen“ und Name „Skizze 2“, die gezeichnete Linie ist sichtbar;
  - nach „Fertigstellen“: Skizze 2 steht im PathFinder, der Befehl fragt wie gewohnt nach der nächsten Ebene;
  - nach Esc: Auswahlwerkzeug.
- **Zweiter Durchlauf** in derselben Sitzung: Skizze 3 wird angelegt, die Umgebung ist wieder `Part`. Im Log steht der
  `E_FAIL → S_OK`-Eintrag zweimal, einmal je Schließen.
- **Nicht getestet:**
  - Profilumgebung eines Formelements (z. B. Extrusion interaktiv → Profil zeichnen → Schließen). Sie läuft über
    dasselbe `HProfiles::_EndSketch`/`UIDeactivate` und sollte deshalb ebenso behoben sein.
  - Synchrones Teil, Blech, Baugruppe.
  - Normaler Start über `start.sh` mit Mesa 26.2.4. Der Fix sitzt dort ebenfalls in der Vorschaltung, sobald
    `install-opengl.sh` die neue Version kopiert.
- **Offen: Kontextmenü.** Ein Rechtsklick (per SendInput) in die 3D-Ansicht der Skizze brachte in diesem Lauf kein
  Kontextmenü (Fenster unter dem Punkt: „Jupiter 3D View“). Das ist nicht weiter untersucht und unabhängig von
  diesem Fehler.

## 9. Nachtrag: Warnung bei offener Kontur – übergeht die Korrektur die Profilprüfung?

Anlass: Laut User zeigt Solid Edge unter Windows beim Schließen einer Skizze mit offener Kontur eine Warnung.
Mit `MEMDC_GL_OLEMENU=fix` wird die offene Skizze bei uns ohne Warnung übernommen.

### 9.1 Ergebnis in Kürze

- **Die Korrektur übergeht die Prüfung nicht.**
  - Die Profilprüfung läuft in profdcx **vor** dem Verlassen der Umgebung.
  - `OleSetMenuDescriptor` (und damit die Korrektur) wird erst danach aufgerufen, und nur, wenn die Prüfung bestanden ist.
  - Schon **ohne** Korrektur meldete die Prüfung „gültig“: Das ursprüngliche `E_FAIL` kam aus `profdcx+0x2bf40`, und
    diese Funktion wird nur im gültigen Zweig erreicht (`HRerrEx` Zeile 0xbbe, Abschnitt 5).
- **Die Warnmechanik funktioniert unter Wine samt Korrektur.** Bei einem ungültigen Formelementprofil (selbstschneidender
  Linienzug in einer Extrusion) erscheint der **Profilfehlerassistent** sichtbar im Fensterbild. Er zeigt die Meldung
  „Das Profil ist selbstschneidend. Die markierten Elemente überschneiden …“ sowie die Schaltflächen „Fehlgeschlagenes
  Formelement“ und „Skizze“. Solid Edge bleibt dabei korrekt in der Profilumgebung.
- **Bei einer normalen Skizze (Befehl „Skizze“) liefert die Profilprüfung keinen einzigen Fehler**, ganz gleich,
  wie die Kontur aussieht:
  - offen,
  - selbstschneidend,
  - geschlossenes Rechteck (wird als geschlossener Bereich schattiert).

  Alle drei nehmen in profdcx denselben Weg „keine Fehler → gültig → Umgebung verlassen“. Es entsteht weder ein Dialog
  noch eine MessageBox. Neu erzeugt werden nur die normalen Befehlsleisten von partDcx/i2d (`CreateDialogIndirectParamW`
  aus cmdspt).
- Einen Mechanismus, der bei offener Kontur einer normalen Skizze warnt, habe ich in profdcx/PROFCMD nicht gefunden.
  Unter Wine wird er weder ausgelöst noch unterdrückt. Ob Windows hier wirklich warnt, lässt sich ohne
  Windows-Vergleich nicht belegen. Am ehesten hat der User den Profilfehlerassistenten bzw. die Profilprüfung bei
  **Formelementprofilen** gesehen. Die funktioniert, siehe oben.
- **Deshalb gibt es keine Änderung an der Korrektur.** Um weiterzukommen, brauchen wir Text oder Bildschirmfoto der
  Windows-Warnung. Mit dem Text lässt sich die Ressource und damit der erzeugende Code direkt finden.

### 9.2 Wo die Warnung erzeugt würde (profdcx, statisch und gemessen)

- **Verlassen:** `profdcx+0x1a100` ist die Befehlsausführung beim Verlassen.
  - Ist `[cmd+0x468] == 0` (bei Skizze und Extrusion gemessen), geht es nach `0x38f50` und dann nach
    `CProfValidateDlg::ExitProfile` (`0x2bea0`).
  - Sonst geht es nach `0x39210`. Dort wird ein `CProfValidateDlg` (0xfa0 Byte) angelegt und bei Fehlern per
    `CDialog::Create(0x41c)` angezeigt.
- **`ExitProfile` (`0x2bea0`):** Zuerst `0x31d20` = Prüfung.
  - gültig → `0x2bf40` (`_EndSketch` → `UIDeactivate` → `SetMenu` → `OleSetMenuDescriptor`);
  - ungültig → `0x2be70`/`0x331a0`/`0x333a0` (Fehler anzeigen, Umgebung bleibt).
- **`0x31d20`:** gültig = `!(Fehlerzahl > 0) || 0x31440()`. Die Fehlerzahl steht in `[dlg+0x960]` (Zähler einer
  MFC-Liste ab `+0x950`), Abfrage `0x33e00`. Die Liste füllt die Live-Prüfung während des Zeichnens anhand der
  Kriterien des aufrufenden Befehls.
- **Dialoge** in `ResDLLs\0007\PROFCMD.drx`:
  - 1052 (0x41c) „Profilfehlerassistent“ mit Überprüfen, Ansicht anpassen, Löschen, In Konstruktion umwandeln und
    Fehlgeschlagenes Formelement;
  - 1053 „Profilüberprüfung“;
  - Meldetexte u. a. 9 „Das Profil ist nicht geschlossen. Die markierten Elemente sind nicht verbunden.“, 18
    „selbstschneidend“, 109 „Mehrere offene Profile …“.
- Es ist ein **eigener MFC-Dialog** (`CreateDialogIndirectParamW` aus mfc140u, aufgerufen von `profdcx+0x39101`). Er ist
  weder WebView2- noch D2D-basiert und auch keine MessageBox.

### 9.3 Messungen (Tracer `tools/setrace.c`, Haltepunkte in profdcx und user32)

| Fall | Fehlerzahl `[dlg+0x960]` | ExitProfile | Dialog/MessageBox | Ergebnis |
|---|---|---|---|---|
| normale Skizze, offener Linienzug aus 3 Linien | 0 (`0x31d4a`) | gültig (`0x2bebd`) | keine | Umgebung `Part`, Befehlsleiste „Fertigstellen“ |
| normale Skizze, selbstschneidender Linienzug | 0 | gültig | keine | `Part` |
| normale Skizze, geschlossenes Rechteck (schattiert) | 0 | gültig | keine | `Part`, Skizze 5 angelegt |
| Extrusion, offener Linienzug (einzelnes offenes Profil ist zulässig) | 0 | gültig | keine | weiter zum Schritt „Seite“ |
| Extrusion, selbstschneidender Linienzug | > 0 (`0x31d39`, `0x31440` = 0) | **ungültig** (`0x2becf`) | `CreateDialogIndirectParamW` aus `profdcx+0x39101`, Profilfehlerassistent **sichtbar** | bleibt im Profil, kein `OleSetMenuDescriptor` |

Für Wine typische Ursachen sind damit ausgeschlossen:
- Ein Dialog, der zwar erzeugt, aber nicht sichtbar wird: Bei der normalen Skizze wird keiner erzeugt. Der
  Profilfehlerassistent erscheint, wenn er gebraucht wird, sichtbar und an der richtigen Stelle (über dem PathFinder).
- Eine still gescheiterte Erzeugung: Es gibt kein `HRerrEx` und keinen Dialogaufruf.
- Eine veränderte Reihenfolge: Die Prüfung liegt im Code vor `_EndSketch`, unabhängig von der Korrektur.

Mit `enumwin.exe` sind nach dem Schließen nur die bekannten versteckten Fenster zu sehen, darunter
`BalloonHelpClassDS`, Tooltips und `#32770`, aber keine neuen Popups.

## 10. Nachtrag 2: „Das Profil ist nicht geschlossen“ (Profilfehlerassistent wie unter Windows)

Anlass: Laut Bildschirmfoto des Users zeigt Windows den Profilfehlerassistenten mit Meldung 9 „Das Profil ist nicht
geschlossen. Die markierten Elemente sind nicht verbunden.“, dazu „Speichern unter: [Fehlgeschlagenes Formelement]
(grau) [Skizze] [Hilfe]“.

Ab diesem Abschnitt liefen alle Eingaben über `methode: post` oder COM (`StartCommand`, `Documents.Add`). Der
Mauszeiger des Users wurde nicht mehr bewegt.

### 10.1 Ergebnis in Kürze

- **Unter Wine erscheint dieselbe Meldung, sobald die Regel „geschlossen“ gilt.**
  - Testfall: neues Teil (Part2, noch ohne Körper), Extrusion, Koinzidente Ebene „Oben (xy)“, eine einzelne offene
    Linie im leeren Raum, „Skizze schließen“.
  - Ergebnis: Profilfehlerassistent mit „Das Profil ist nicht geschlossen. Die markierten Elemente sind nicht ver…“,
    „Fehlgeschlagenes Formelement“ grau, dazu „Skizze“ und „Hilfe“. Das entspricht dem Windows-Bild.
  - Ein Klick auf „Skizze“ legt Skizze 1 an und führt zurück in den Extrusionsbefehl, wie erwartet.
- **Die Prüfung selbst rechnet unter Wine richtig.** `Profile.End(igProfileClosed)` per COM in einem neuen Teil
  liefert für ein offenes Profil −103 und für ein geschlossenes Rechteck 0. −103 ist der Code, den profdcx auf
  Meldung 9 abbildet.
  - Ein Gleitkomma-, FPU- bzw. MXCSR- oder Parasolid-Unterschied ist damit ausgeschlossen.
  - Es gibt kein `HRerrEx` aus der Prüfkette. Die einzigen `HRerrEx` in diesem Zeitraum stammen aus
    `EXPRDVX!JVarListener::ComputeImp::EnumValues` (0x80040225) und hängen nicht mit der Prüfung zusammen.
- **In `skizze-test.par` verlangt Solid Edge die Geschlossenheit gar nicht.** Das liegt am Befehlskontext und
  nicht an Wine:
  - Der Extrusionsbefehl (partDcx) gibt die Prüfkriterien vor.
  - Für das bestehende Teil mit Körper gibt er `0x22008` vor, also *kein* `igProfileClosed`, aber
    `igProfileNoSelfIntersect` (8), `igProfileAllowNested` (0x2000) und Bit 17 (0x20000).
  - Für ein Teil ohne Körper gibt er `0x2009` vor, mit `igProfileClosed` (1).
  - Deshalb meldete der Test „Extrusion, offenes Profil“ aus Nachtrag 1 (in skizze-test.par) 0 Fehler. Das gilt auch
    im leeren Raum, ohne Berührung von Kanten: Ein einzelnes offenes Profil ist dort laut Kriterien zulässig.
- **Bei einer eigenständigen Skizze** (Befehl „Skizze“) gibt es kein „geschlossen“. Gemessen wurden die Kriterien
  `0x100000` mit Formelementtyp 0x12, ohne Bit 0. Eine Warnung bei offener Kontur ist dort also nicht vorgesehen.
  Das Windows-Bild mit „Fehlgeschlagenes Formelement/Skizze“ gehört zu einem Formelementprofil.
- **Die Korrektur `MEMDC_GL_OLEMENU` übergeht nichts.** Die Prüfung läuft vor dem Verlassen der Umgebung. Ist das
  Profil ungültig, wird `OleSetMenuDescriptor` gar nicht erst aufgerufen. Eine Änderung ist nicht nötig.

### 10.2 Gemessene Kette (Tracer `tools/setrace.c`)

| Fall | Kriterien beim Eintritt in profhlp (`r9` an `profhlp+0xe770`) | an profDex `0x9df00` (`rdx`) | `profDex+0x9ef09` (−103 setzen) | Ergebnis |
|---|---|---|---|---|
| neues Teil ohne Körper, Extrusion, offene Linie | `0x2009` | `0x2009` | **ja** | Fehler vorhanden, Profilfehlerassistent „nicht geschlossen“ |
| skizze-test.par mit Körper, Extrusion, offene Linie im leeren Raum (Ebene „Oben“) | `0x22008` | `0x22008` | nein | keine Fehler, weiter zu „Seite“ |
| skizze-test.par, Extrusion, selbstschneidend (Nachtrag 1) | – | `0x22008` | – | Fehler „selbstschneidend“, Assistent sichtbar |
| skizze-test.par, Befehl „Skizze“, offene Linie | `0x100000` (Typ 0x12) | `0x100000` | nein | keine Fehler, Umgebung `Part` |
| COM `Profile.End(1)`, neues Teil | – | `1` | ja | −103; geschlossenes Rechteck: 0 |

### 10.3 Wo die Regel steckt

- **partDcx** wählt die Kriterien nach dem Zustand des Befehls `[cmd+0x1848]`:
  - bei 2 oder 4: `0x22008`, offen zulässig (`partDcx+0xbfb9b`/`0xbfba7`);
  - bei 1: `0x2009`, geschlossen (`0xbfbb6`);
  - bei 0x10: `0x22038`.
  - Ebenso `0xc2ce7`/`0xc2d3d`.
- **profhlp:**
  - Die Funktion bei `profhlp+0xe770` erhält die Kriterien (r9) und den Formelementtyp (r8) und ruft damit die
    Prüfung `profDex+0x9df00` auf (Rücksprung `profhlp+0xeb60`).
  - Ausnahme für alte Dokumente: Ist `partSvc!JMGetFeatureComputeVersion` kleiner als 5 und
    `JMGetActiveMPartOwnerCount` = 1, wird `0x2009` zu `0x22008` gelockert (`profhlp+0xe860`).
  - In beiden Tests war die Compute-Version ≥ 5; die Lockerung wurde nicht durchlaufen.
- **profDex `0x9df00`:**
  - Kriterien in `edx`. Ist Bit 0 (`igProfileClosed`) gesetzt und die Kette offen (`profDex+0x109810` liefert
    „nicht geschlossen“), wird Fehler −103 eingetragen (`0x9ef09`).
  - Bit 17 (0x20000) prüft stattdessen „mehrere offene Profile“ (−127).
- **profdcx:**
  - Die Fehlerliste `[dlg+0x950]`/Zähler `+0x960` wird beim Schließen in `0x37770` → `0x35b80` → `0x36212` gefüllt.
  - Fehlercode → Meldung über die Sprungtabelle in `0x31d70`: −103 → 9, −104 → 8, −109 → 18.

### 10.4 Nebenbefund

`Edge.exe` hat schon direkt nach dem Start rund 2 000 Threads. Im Laufe der Sitzung wurden es über 3 100, auch ohne
geladenen Tracer. Zeitweise lag die CPU-Last bei mehreren Hundert Prozent. Zweimal reagierte Solid Edge über eine
Minute lang weder auf COM noch auf die Fensteraufnahme; danach lief es von selbst weiter bzw. wurde neu gestartet.
Das hängt nicht mit „Skizze schließen“ zusammen und wurde nicht weiter untersucht. Es wäre aber einen eigenen Blick
wert (Wine-Threadpool, Mesa/zink-Threads?).
