# Solid Edge 2026 Community Edition auf dem Mac – Anleitung

Diese Anleitung zeigt, wie man **Siemens Solid Edge 2026 Community Edition** auf einem Mac mit Apple Silicon
(M1–M4) als normale **„Solid Edge 2026.app“** zum Laufen bringt – mit **einem Terminal-Befehl**, ohne virtuelle
Maschine, mit **GPU-Beschleunigung** für Oberfläche und 3D-Ansicht. Optional entsteht eine DMG für weitere eigene Macs.

Grundlage ist [cryinkfly/Solid-Edge-on-Linux](https://codeberg.org/cryinkfly/Solid-Edge-on-Linux) (Wine-Prefix,
`winetricks win11`, Original-Installer) und die Linux-Fixes aus
[JustNotesa/solid-edge-wine-fixes](https://github.com/JustNotesa/solid-edge-wine-fixes). Auf dem Mac kommen
einige Probleme hinzu, die es unter Linux nicht gibt – vor allem bei der Grafik. Die Lösungen dafür sind hier
Schritt für Schritt beschrieben, die zugehörigen Skripte und Patches liegen in diesem Projekt.

> **Getestet:** MacBook Pro M3 Pro, 18 GB RAM, macOS 26, Solid Edge 2026 Community (Build 2510, Version
> 226.00.01.04). Teile, Skizzen, Extrusion, Bemaßung, Baugruppe und Blech wurden geöffnet bzw. angelegt;
> Langzeit-Praxis fehlt noch. Rückmeldungen willkommen.

---

> **Nur schnell eine laufende Version?** Im Terminal:
> `curl -fsSL https://raw.githubusercontent.com/JustNotesa/solid-edge-macos/main/install-online.sh | bash`
> – das Skript öffnet die Siemens-Seite, wartet auf den Download und baut dann alles allein. Diese Anleitung erklärt, was dabei passiert.

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

- **„Solid Edge 2026“ im Ordner Programme**, startbar per Doppelklick wie jedes Mac-Programm (kein „Rechtsklick →
  Öffnen“ nötig, weil die App auf deinem Mac gebaut wird).
- Die App wird **klein ausgeliefert** (Starter, Wartefenster, Symbol, Deinstallations-App). Grund: macOS prüft beim
  allerersten Öffnen jedes Byte im App-Bundle, ohne irgendetwas anzuzeigen – leer 0,5 s, mit Wine 5 s, mit 13 GB
  77 s. Was nach dem ersten Start ins Bundle kommt, wird nicht erneut geprüft (auch nicht nach dem Verschieben);
  eine Kopie der vollen App auf einem anderen Mac dagegen schon (über 4 Minuten).
- Beim ersten Start erscheint sofort das Wartefenster mit Fortschritt und laufender Ausgabe, bis Solid Edges eigener
  Startbildschirm kommt. Darin wählst du einmalig, wie Solid Edge eingerichtet wird („Weiter“/„Abbrechen“):

  | | **Installiert** (empfohlen, vorausgewählt) | **Portabel** |
  |---|---|---|
  | Wine | in der App (`Contents/Resources/wine`) | in der App (`Contents/Resources/wine`) |
  | „Windows“-Ordner (Prefix, mit den Solid-Edge-Einstellungen) | `~/Library/Application Support/SolidEdge2026/prefix` | in der App (`Contents/Resources/inhalt/prefix`) |
  | `start.log`, `status.txt` | `~/Library/Application Support/SolidEdge2026/` | `Contents/Resources/inhalt/` |
  | Update der App | Einstellungen bleiben, nur Wine und Grafik-DLLs werden erneuert | setzt die Solid-Edge-Einstellungen zurück |
  | Entfernen | „Solid Edge deinstallieren“ (siehe unten) | App löschen – außerhalb bleibt nichts |

  In beiden Fällen sind Dokumente/Schreibtisch/Downloads im Prefix die Mac-Ordner und Laufwerk `Z:` ist die
  Mac-Festplatte. Der Windows-Benutzer heißt einfach `user` (ältere Installationen behalten ihren bisherigen Namen;
  der Starter gibt ihn Wine beim Start über `USER`/`WINEUSERNAME` mit) – dein Mac-Name steht nicht in der
  Windows-Umgebung. Zu Portabel steht im Fenster: „Ein Update der App setzt die
  Solid-Edge-Einstellungen zurück. Die Zeichnungen in den Mac-Ordnern bleiben.“ und „Nach dem Kopieren auf einen
  anderen Mac dauert der erste Start lange (macOS prüft dann die ganzen 13 GB).“ Der gewählte Modus steht in
  `Contents/Resources/modus.txt`; `SE_MODUS=installiert|portabel` überspringt die Frage.
- Die App muss in einem **beschreibbaren Ordner** liegen (z. B. Programme). Direkt aus der DMG oder per
  App-Translocation (aus „Downloads“ gestartet, ohne sie zu verschieben) startet sie nicht und meldet, sie erst in
  den Ordner Programme zu ziehen.
- **Deinstallieren (Installiert):** Der Starter legt neben die App (meist in Programme) **„Solid Edge
  deinstallieren“**. Sie zeigt, was entfernt wird (mit Größe), bittet ggf., Solid Edge zu beenden, und legt App,
  Arbeitsordner und sich selbst per Finder in den Papierkorb (sonst `/usr/bin/trash`) – nichts wird endgültig
  gelöscht, Zeichnungen in Dokumente/Schreibtisch/Downloads bleiben. Beim ersten Mal fragt macOS evtl., ob sie den
  Finder steuern darf.
- Ältere Installationen (Wine und Prefix in `~/Library/Application Support/SolidEdge2026`) gelten als Installiert:
  Beim nächsten Update wandert Wine in die App, `…/SolidEdge2026/wine` verschwindet, Prefix und Einstellungen
  bleiben, die Deinstallations-App kommt dazu.
- Leistung auf dem Test-Mac: Start in ~30 s, ~25 Threads, ~1,7 GB Speicher, Drehen/Zoomen flüssig.
- Optional: `SolidEdge2026-macOS.dmg` (~5,7 GB) für weitere **eigene** Macs, ohne persönliche Daten.

---

## 3. Voraussetzungen

| Was | Hinweis |
|---|---|
| Mac mit Apple Silicon, macOS 13 oder neuer | getestet mit macOS 26 |
| ~40 GB freier Speicher während der Installation | danach ~15 GB (egal ob installiert oder portabel), die Bau-Dateien werden gelöscht |
| Internet, ~1 GB Downloads + 5 GB Siemens-Installer | Wine, DXVK, Mesa, WebView2, .NET |
| Siemens-Konto (kostenlos) | für den Download der Community Edition |
| ~30 Minuten | das meiste ist die .NET-Installation |

Alles Weitere – Rosetta 2, [Homebrew](https://brew.sh) mit `winetricks` und `p7zip` – installiert der Befehl
bei Bedarf selbst (dabei fragt macOS ggf. einmal nach deinem Passwort).

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

### 5.1 Der schnelle Weg: ein Befehl

Terminal öffnen (Programme → Dienstprogramme → Terminal) und einfügen:

```bash
curl -fsSL https://raw.githubusercontent.com/JustNotesa/solid-edge-macos/main/install-online.sh | bash
```

1. Das Skript prüft den Mac, installiert bei Bedarf Homebrew und Rosetta 2 und lädt dieses Projekt nach
   `~/solid-edge-macos`. Dann fragt es, ob Solid Edge **installiert** (Enter, empfohlen) oder **portabel**
   eingerichtet wird (siehe Abschnitt 2).
2. Es öffnet die **Siemens-Seite**: dort registrieren und „Solid Edge Community Edition“ herunterladen
   (`Solid_Edge_Community_2026_….zip`, ~5 GB). Die Datei **in „Downloads“ lassen** – das Terminal wartet, bis der
   Download fertig ist (die Größe bleibt stehen, kein `.crdownload`/`.download` mehr), und macht dann allein weiter.
3. Nach ~30 Minuten liegt **„Solid Edge 2026“ in Programme**, die Bau-Dateien sind gelöscht und die App startet.

Den Befehl kannst du jederzeit erneut ausführen: Er aktualisiert die Skripte und überspringt Erledigtes.
Den Siemens-Download in „Downloads“ kannst du danach löschen.

### 5.2 Was dabei im Einzelnen passiert

`install-online.sh` ruft `setup.sh --app --aufraeumen --starten` auf. Mit `--app` fragt `setup.sh` zuerst im
Terminal nach dem Modus (Enter = Installiert); `--installiert` bzw. `--portabel` legen ihn ohne Rückfrage fest, und
liegt im Arbeitsordner schon eine Installation (`prefix/`), gilt Installiert ohne Frage. Dann macht `setup.sh`:

1. **Mac prüfen:** Apple Silicon, Rosetta 2 (sonst `softwareupdate --install-rosetta`), freier Speicher.
2. **Werkzeuge:** `brew install winetricks p7zip` (nur was fehlt).
3. **Installer:** `Solid_Edge_Community*.zip` im Projektordner oder in `~/Downloads` suchen bzw. darauf warten und
   nach `installer/` entpacken.
4. **Laufzeiten laden:** Wine Staging 11.18 (Gcenx) nach `runtime/Wine Staging.app` (Quarantäne-Attribut wird
   entfernt), DXVK-macOS 1.10.3 nach `runtime/dxvk/`.
5. **Die zwei eigenen DLLs** (`d2d1.dll`, `opengl32.dll`) aus dem
   [GitHub-Release](https://github.com/JustNotesa/solid-edge-macos/releases) laden und die SHA-256-Prüfsummen
   kontrollieren – oder mit `--selbst-bauen` aus dem Quelltext bauen (`brew install mingw-w64`, dann
   `d2d1-build/build.sh` lädt die Wine-11.21-Quellen und wendet die Patches `0001` SVG/nanosvg und `0002`
   geteiltes D3D-Gerät an; die opengl32-Vorschaltung baut `install-opengl.sh`).
6. **`install.sh`:**
   1. Prefix anlegen (`wineboot`, nur beim ersten Mal).
   2. **Wine Mono entfernen** und die .NET-4-Registrierung beider Registry-Ansichten leeren – sonst meldet der
      .NET-Installer „schon installiert“ und tut nichts.
   3. **.NET Framework 4.8** über winetricks; ein Wächter beendet hängende NGen-Prozesse (`mscorsvw.exe` hängt
      unter Wine endlos bei 100 % CPU), danach wird der NGen-Dienst abgeschaltet.
   4. **Windows 11** als Windows-Version.
   5. **Mac-Tastatur:** beide Cmd = Strg (Cmd+C/V/Z wie am Mac), rechte Option = Alt, linke Option bleibt Option
      (für @ € { } auf der deutschen Tastatur).
   6. **Menüs nicht über andere Programme legen:** `WindowsFloatWhenInactive=none`.
   7. **Grafik:** DXVK (d3d10core/d3d11/dxgi) und die eigene `d2d1.dll` nach `system32`, Overrides auf `native`.
   8. **WebView2 Runtime** still installieren; Browser-Optionen über die **WebView2-Policy** (Solid Edge ignoriert
      die Umgebungsvariable); Edge-Update-Dienste aus.
   9. **VC++ 2022**, dann **Solid Edge** per `msiexec /qn` mit der mitgelieferten Community-Lizenz. Der
      Installer-Ordner wird dafür kurz als Laufwerk `S:` eingehängt – mit langen Unix-Pfaden stürzt Wines msiexec ab.
   10. **OpenGL** (`install-opengl.sh`): Mesa 22.0.3 laden, `libgallium_wgl.dll` mit
       `patches/patch-mesa-sampler-name.py` patchen, Vorschaltung + Mesa neben `Edge.exe`, Override nur für Edge.exe.
   11. **Solid-Edge-Einstellungen:** kein IE-Startfenster, kein „Ordered/Synchronous“-Popup, DIN-Vorlagen.
7. **App bauen** (`build-dmg.sh --app`): `app/launcher.sh` als Starter, `app/warten.js` als Wartefenster und
   `app/deinstallieren.sh` als Deinstallations-App ins Bundle; Wine und Prefix per APFS-Klon als „Inhalt“ nach
   `…/SolidEdge2026/neu`, persönliche Daten entfernen (`app/prefix-anonymisieren.py`), App nach /Programme. App und
   Inhalt tragen dieselbe Bau-Kennung (`stand.txt`).
   **Voreinrichtung:** `setup.sh` startet die App einmal mit
   `open -n -W --env SE_NUR_EINRICHTEN=1 --env SE_MODUS=…` – so prüft macOS sie, solange sie klein ist – und zeigt
   das `start.log` im Terminal mit. Der Starter übernimmt dabei den Inhalt: Wine in die App, den Prefix je nach Modus
   in den Arbeitsordner oder in die App (neu eingerichtet: ganzer Prefix; Update bei Installiert: nur Wine und
   Grafik-DLLs, Einstellungen bleiben) und legt bei Installiert „Solid Edge deinstallieren“ neben die App.
8. **Aufräumen** (`--aufraeumen`): Installer-Kopie, Laufzeiten, Projekt-Prefix und Bau-Reste löschen – Solid Edge
   steckt danach nur noch in der App (und bei Installiert im Arbeitsordner). **Starten** (`--starten`).

Ohne `--aufraeumen` (z. B. `./setup.sh --app` nach eigenem `git clone`) bleibt der Arbeits-Prefix im
Projektordner erhalten und Solid Edge lässt sich zusätzlich mit `./start.sh` starten.

### 5.3 Testen

Prüfliste: Menüband mit Symbolen · Teil öffnen (z. B. `C:\Program Files\Siemens\Solid Edge 2026\Training\block.par`)
· 3D-Ansicht schattiert mit Kanten · Ansichtswürfel unten rechts mit Beschriftung beim Überfahren ·
Auswahlrechteck beim Ziehen · Rechtsklick-Kontextmenü · Skizze anlegen und „Skizze schließen“ · offenes Profil in
einer Extrusion (neues Teil) → Profilfehlerassistent.

### 5.4 DMG für weitere eigene Macs

```bash
git clone https://github.com/JustNotesa/solid-edge-macos.git && cd solid-edge-macos && ./setup.sh --dmg
```

- Ergebnis: `dist/SolidEdge2026-macOS.dmg` (LZFSE; `DMG_FORMAT=ULMO` für LZMA, kleiner, deutlich langsamer).
- Persönliche Daten werden entfernt: Benutzername → `user` (bleibt auch auf dem anderen Mac so, siehe Abschnitt 2),
  Zuletzt-geöffnet-Listen, Registry-Einträge mit Pfaden ins eigene Home (z. B. eigene
  Schriften), Installationsquelle, WebView2-Browserprofil, Nutzungsstatistik, Protokolle, Rechnername und Domain
  (Wine setzt sie beim Start aus dem jeweiligen Mac neu), Gecko-Profil (`wine_gecko`). Danach durchsucht das Skript
  den ganzen Prefix (Text und binär, auch UTF-16) nach Benutzername, vollem Namen, Rechnername, Domain und
  8.3-Kurzname (z. B. `ABCD~…`) – bei einem Treffer bricht `build-dmg.sh` ab und nennt die Dateien.
- Der Inhalt (Wine + Prefix) liegt in der DMG im versteckten Ordner `.solidedge-inhalt` neben der App.
- Auf dem anderen Mac: DMG öffnen, App nach /Programme ziehen, beim ersten Start **Rechtsklick → Öffnen**
  (nicht signiert), Rosetta 2 muss installiert sein. **Die DMG beim ersten Start geöffnet lassen** und im
  Wartefenster den Modus wählen – der Starter kopiert den Inhalt von dort (~13 GB, einige Minuten, mit
  Fortschrittsanzeige; Wine immer in die App, den Prefix je nach Modus); danach kann sie weg. Direkt aus der DMG
  startet die App nicht.
- Vor dem Weitergeben einmal mit leerem Datenordner testen – Fehler beim allerersten Start sieht man sonst nicht.
  Dazu die App aus der geöffneten DMG in einen beschreibbaren Ordner ziehen (z. B. `~/se-test`, nicht über die
  eigene App in Programme) und starten:
  ```bash
  open -n --env SE_DATA_DIR=/tmp/se-test --env SE_MODUS=installiert "$HOME/se-test/Solid Edge 2026.app"
  ```
- **Nur für eigene Macs** – siehe Abschnitt 1.

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

- **Protokoll:** `~/solid-edge-macos/logs/` (Installation); App: `~/Library/Application Support/SolidEdge2026/start.log`
  (Installiert) bzw. `Solid Edge 2026.app/Contents/Resources/inhalt/start.log` (Portabel; Rechtsklick auf die App →
  „Paketinhalt zeigen“). Mehr Wine-Meldungen: `WINEDEBUG=err+all` vor den Start setzen.
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
- Portabel: Der allererste Start von Solid Edge dauert ~70 s statt ~20 s – macOS (`tccd`) prüft dabei jeden
  Schreibzugriff in das App-Bundle; ab dem zweiten Start ~7 s.
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
