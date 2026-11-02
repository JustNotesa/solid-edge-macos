# Solid-Edge-MCP-Server

Damit können AI-Assistenten wie Claude Code oder Claude Desktop das unter Wine laufende Solid Edge 2026 bedienen. Der Server nutzt dafür die COM-Automatisierung von Solid Edge.

```
Claude ──MCP (stdio)──▶ solidedge_mcp.py ──JSON-Zeilen (stdin/stdout)──▶ wine sebridge.exe ──COM──▶ Edge.exe
         macOS, Python 3, nur Standardbibliothek        dauerhafter .NET-4.8-Prozess im Prefix
```

- **`solidedge_mcp.py`** ist der MCP-Server auf der macOS-Seite. Er liest die Umgebung aus `../env.sh` (WINE, WINEPREFIX, MSYNC …) und startet die Brücke beim ersten Tool-Aufruf. Pfade rechnet er in beide Richtungen um (`/Users/…` ↔ `Z:\Users\…`, Prefix ↔ `C:\…`).
- **`bridge/sebridge.cs`** ist die Brücke auf der Wine-Seite. Sie hängt sich per `Marshal.GetActiveObject("SolidEdge.Application")` an das laufende Solid Edge und führt die Befehle aus. Der Server übersetzt sie selbst mit dem `csc.exe` im Prefix, sobald `sebridge.exe` fehlt oder älter als die Quelle ist.

## Voraussetzungen

- Solid Edge ist installiert (`./install.sh`) und **läuft** (`./start.sh`, oder das Tool `starten`).
- Für `bild` muss die opengl32-Vorschaltung der Haupt-Session neben Edge.exe liegen (`MESA_WRAPPER=1`, `opengl-wrapper/`). Ohne sie liefert `View.SaveAsImage` nur den Hintergrund.
- Python ab 3.8; `/usr/bin/python3` von macOS reicht.

## Registrieren

### Claude Code

```bash
claude mcp add solidedge --scope user -- /usr/bin/python3 ~/solidedge/mcp/solidedge_mcp.py
```

Ohne `--scope user` gilt der Server nur im aktuellen Projekt. Prüfen mit `claude mcp list`. In einer Sitzung zeigt `/mcp` den Server, die Tools heißen dann `mcp__solidedge__status` usw.

### Claude Desktop

In `~/Library/Application Support/Claude/claude_desktop_config.json` eintragen:

```json
{
  "mcpServers": {
    "solidedge": {
      "command": "/usr/bin/python3",
      "args": ["~/solidedge/mcp/solidedge_mcp.py"]
    }
  }
}
```

Liegt das Projekt woanders, beide Pfade anpassen. Mit der Umgebungsvariable `SE_ROOT` lässt sich der Projektordner auch ausdrücklich setzen.

## Tools

| Tool | Zweck |
|---|---|
| `status` | Version, Umgebung, offene Dokumente (Pfad, Typ, geändert, Modus), aktives Dokument |
| `starten` | Solid Edge über `start.sh` starten und warten, bis COM antwortet (optional mit Datei) |
| `oeffnen` / `neu` / `speichern` / `schliessen` / `aktivieren` | Dokumente verwalten (Teil, Blech, Baugruppe, Zeichnung) |
| `exportieren` | Kopie exportieren, Format nach Endung: `.stp`, `.x_t`, `.igs`, `.stl`, `.jt`, Zeichnung `.pdf`/`.dxf` |
| `befehl` | `Application.StartCommand` per ID oder Konstantenname (z. B. `PartViewISOView`) |
| `ansicht` | `iso`, `vorne`, `hinten`, `oben`, `unten`, `links`, `rechts`, `dimetrisch`, `trimetrisch`, `einpassen`, `drehen` (Achse/Grad), `zoom` |
| `kamera` | Kamera lesen/setzen (mm) plus Modellbereich (Bounding Box) |
| `bild` | Bild der 3D-Ansicht (`View.SaveAsImage`, schattiert mit Kanten), optional vorher eine Standardansicht; kommt als Bild direkt zum Assistenten, mit `pfad` auch als `.png`/`.jpg`/`.bmp` gespeichert |
| `fenster_bild` | Bildschirmfoto des ganzen Solid-Edge-Fensters, so wie es der Benutzer sieht: Menüband, PathFinder, 3D-Ansicht, Befehlsdialoge, Popups und Hervorhebungen. Aufgenommen wird in Edge.exe selbst, siehe „Fensteraufnahme“ |
| `eingabe` | Maus und Tastatur wie ein Benutzer: bewegen, klicken, Rechtsklick, Doppelklick, ziehen, Tasten wie `esc` oder `strg+z`, Text. Koordinaten sind Pixel im Fensterbild. Mit `danach_bild` kommt gleich ein Fensterbild zurück, siehe „Eingabe“ |
| `feature_baum` | Features eines Teils mit Typ und Status bzw. Bauteile einer Baugruppe |
| `variablen` / `variable_setzen` | Variablentabelle samt Bemaßungen lesen und ändern (Wert in mm, SI oder als Formel) |
| `extrusion` | Skizze auf Ebene `xy`/`xz`/`yz` mit Rechteck, Kreis oder Polygon (mm) als Aufsatz oder Ausschnitt extrudieren (endlich, symmetrisch, durch alles); innere Konturen werden zu Löchern |
| `konstanten_suchen` | Konstanten aus `constant.tlb` durchsuchen (Befehls-IDs, Enum-Werte) |
| `com_zugriff` | Expertenwerkzeug für einen beliebigen API-Pfad, z. B. `ActiveDocument.Models.Item(1).Body.Volume` |

Längen gibt man in mm an, Winkel in Grad. Wo SI-Werte zurückkommen (`variablen.wert`, `com_zugriff`), steht das dabei. Auf der Ebene `xz` entspricht die Skizzen-y-Achse der Modell-Z-Achse.

## Schnelltest ohne MCP-Client

```bash
python3 mcp/solidedge_mcp.py --test status
```

```bash
python3 mcp/solidedge_mcp.py --test neu '{"typ":"teil"}'
```

```bash
python3 mcp/solidedge_mcp.py --test extrusion '{"elemente":[{"typ":"rechteck","breite":80,"hoehe":50,"zentriert":true},{"typ":"kreis","durchmesser":12}],"tiefe_mm":20}'
```

Die Brücke selbst lässt sich auch direkt ansprechen, eine JSON-Zeile pro Befehl:
`printf '%s\n' '{"id":1,"cmd":"status"}' | wine 'Z:\…\mcp\bridge\sebridge.exe'`.
Stderr der Brücke landet in `bridge/sebridge.log`.

## Nachgewiesen (01.11.2026, Solid Edge 226.00.01.04, Wine Staging 11.18, Zink)

Getestet gegen das laufende Solid Edge mit `Training\block.par`, als MCP-Protokoll per Hand und mit `claude -p --mcp-config`:

- **`status`:** Version, Dokumentliste, aktives Dokument.
- **`oeffnen`:** öffnet block.par.
- **`befehl`:** `PartViewTopView` (32813) ändert die Kamera auf Draufsicht.
- **`ansicht`:** `einpassen`, `iso`, `oben`, `drehen` 30° um z.
- **`extrusion`** in einem neuen Teil, alle Volumina rechnerisch geprüft:
  - Block 80×50×20 mm
  - Bohrung Ø12 „durch alles“
  - symmetrische Nut auf `xz`; Ergebnis 76 488 mm³ wie berechnet
  - Rechteck mit Loch in einem Profil: 22 233 mm³
  - Polygon symmetrisch: 4 500 mm³
- **`variable_setzen`:** Tiefe auf 12 mm, das Volumen folgt.
- **`exportieren`:** STEP, STL und `speichern` als .par.
- **`feature_baum`, `variablen`, `kamera`, `konstanten_suchen`, `com_zugriff`.**
- **`bild`** (mit opengl32-Vorschaltung): ISO- und Draufsicht von block.par stimmen. Über `claude -p` hat der Client ein neues Teil extrudiert, das Bild abgerufen und richtig als Block mit durchgehendem Loch beschrieben.
- **`fenster_bild`:** DLL ins laufende Edge.exe geladen, ohne Absturz; ein Bild dauert etwa 3 s. Das Bild (1520×901) zeigt Titelleiste, Menüband, Aufforderungszeile, PathFinder, die 3D-Ansicht mit block.par und die Statusleiste. Bei laufendem Extrusionsbefehl sind auch das Befehlsfenster und das hervorgehobene Symbol zu sehen. Es fehlt nur der Ansichtswürfel, siehe unten.

Nicht getestet: **`starten`**, weil Solid Edge schon lief und nur eine Sitzung es starten und beenden darf. Auch Baugruppen und Zeichnungen sind nicht getestet, nur über `status`, `feature_baum` und `exportieren` vorgesehen.

## Fensteraufnahme (`fenster_bild`)

Ein fremder Prozess bekommt unter Wine per `PrintWindow`, `GetWindowDC`+`BitBlt` oder `CopyFromScreen` nur leere Bilder, weil die Fensteroberfläche pro Prozess liegt. Die macOS-Bildschirmaufnahme ist nicht freigegeben. Deshalb läuft der Aufnahmecode in Edge.exe:

- **`capture/secapture.c`** wird zu `secapture-v2.dll`. In jedem anderen Prozess tut die DLL nichts. In Edge.exe startet sie einen Thread, der alle 100 ms `C:\secapture\anforderung-v2` prüft. Das Format steht im Kopf von `secapture.c`: Zeile 1 `bild` oder `eingabe`, Zeile 2 Modus bzw. Methode, Zeile 3 Ergebnisdatei, danach die Aktionen.
  - Die Protokollversion steckt im DLL- und im Dateinamen, weil sich eine geladene DLL nicht ersetzen lässt. Eine neue Version wird neben der alten geladen und nutzt ihre eigene Anforderungsdatei. Bei Änderungen am Protokoll `PROTOKOLL` in `secapture.c`, `DLL` in `build.sh`/`install.sh`, den Standardnamen in `secapture-inject.c` und `CAPTURE_PROTOKOLL` im Server gemeinsam hochzählen.
  - Der Thread löscht die Anforderung als Quittung.
  - Er kopiert das Hauptfenster (Titel beginnt mit „Solid Edge“) per `GetWindowDC`+`BitBlt` (`SRCCOPY|CAPTUREBLT`) in eine 32-Bit-DIB-Section.
  - Alle weiteren sichtbaren Top-Level-Fenster des Prozesses legt er in Z-Reihenfolge darüber: schwebender PathFinder, Dialoge, Menüs, Tooltips.
  - Das Ergebnis schreibt er als BMP, bei Fehlern entsteht `<Ziel>.err`.
- **`capture/secapture-inject.c`** wird zu `secapture-inject.exe` und lädt die DLL per `CreateRemoteThread`+`LoadLibraryW` in jedes laufende Edge.exe. Mehrfaches Laden ist harmlos.
- **Laden der DLL.** Wine Staging 11.18 wertet **`AppInit_DLLs` nicht aus**: Kernel32 hat nur einen Stub `LoadAppInitDlls`, user32 liest den Registry-Wert nie. Die Injektion braucht dafür keinen Neustart: Der Server ruft den Injektor selbst auf, wenn eine Anforderung nach 1 s noch nicht quittiert ist, also einmal pro Solid-Edge-Sitzung.
- **`capture/build.sh`** baut beides mit mingw-w64. **`capture/install.sh`** kopiert beides nach `C:\secapture` im Prefix, mit `--inject` lädt es die DLL auch gleich. Das MCP-Tool kopiert bei Bedarf selbst.
- **Auf der MCP-Seite** wandelt `sips` die BMP in PNG um, bei Bedarf auf `max_breite` verkleinert (Standard 1600 px).

Grenzen:
- **Modus `print` (WM_PRINT)** liefert keine 3D-Ansicht. Standard ist deshalb `bitblt`.
- **Ansichtswürfel.** Am 01.11. fehlte er im Bild. Am 02.11., nach einem Neustart mit geänderter Vorschaltung, war er im Skizzenmodus unten rechts zu sehen. Er ist kein eigenes Fenster: Unter der Ansicht liegt nur das GL-Kindfenster „Jupiter 3D View“, der Würfel wird also in den OpenGL-Inhalt gezeichnet. Die 3D-Ansicht selbst erscheint, weil die opengl32-Vorschaltung das GL-Bild in die GDI-Fensteroberfläche zurückkopiert. Was nur in die Bildschirmausgabe gelangt (Vulkan/Metal-Layer, ggf. Overlays), fehlt im GDI-Bild. Zwei Wege, das zu ergänzen:
  - Die Vorschaltung kopiert auch die Overlays in die Oberfläche.
  - Die DLL hängt sich an `SwapBuffers`/`wglSwapLayerBuffers` von Edge.exe und liest dort per `glReadPixels` das fertige Bild der 3D-Ansicht. Das legt sie dann an die Position von „Jupiter 3D View“.
- **Teile außerhalb des Bildschirms.** Liegt das Fenster teilweise außerhalb (z. B. bei x = −4), erscheinen dort schwarze Ränder.

## Eingabe (`eingabe`)

Die Eingaben führt derselbe Thread in Edge.exe aus. Koordinaten sind Pixel relativ zur linken oberen Ecke des Hauptfensters, wie im Fensterbild. Wurde das Bild verkleinert (`skalierung` < 1), rechnet `bildkoordinaten: true` sie zurück.

- **`methode: sendinput`** (Standard) läuft wie echte Eingabe durch Wine: Hover, Maus-Capture, `WM_CONTEXTMENU` und Fokus entstehen wie beim Benutzer. Achtung: Dabei **bewegt sich der echte Mac-Mauszeiger**. Benutzt jemand gleichzeitig die Maus, kommen sich die Eingaben in die Quere.
- **`methode: post`** ist der Rückfall. Mauseingaben gehen als `WM_*BUTTON*`/`WM_MOUSEMOVE` an das tiefste sichtbare Fenster unter dem Punkt (`WindowFromPoint` und `ChildWindowFromPointEx`). Beim Rechtsklick kommt zusätzlich `WM_CONTEXTMENU` dazu. Tasten gehen als `WM_KEY*` bzw. `WM_CHAR` an das Fokusfenster des GUI-Threads (`GetGUIThreadInfo`). Der Mac-Zeiger bleibt dabei unberührt. **Dieser Weg ist noch ungetestet.**
- **Getestet (02.11.2026):** nur ein einzelner Rechtsklick per SendInput in die 3D-Fläche. Die Eingabe landete laut Protokoll im Fenster „Jupiter 3D View“, ein Kontextmenü war nicht zu sehen. Allerdings arbeitete der User gleichzeitig in einer Skizze, das Ergebnis ist also nicht aussagekräftig. Tasten, Ziehen und `methode: post` sind noch offen.
- **Vor jeder Eingabe klären, dass niemand gerade an Solid Edge arbeitet:** Sonst stören sich die Eingaben gegenseitig, und SendInput nimmt dem User den Mauszeiger weg.
- **Rückmeldung.** Das Tool gibt ein Protokoll zurück: Methode, Bildschirmpunkt, Fenster(klasse) unter dem Punkt, Fehler.

Beispiele für die Kommandozeile:

```bash
python3 mcp/solidedge_mcp.py --test eingabe '{"aktionen":[{"aktion":"rechtsklick","x":1250,"y":500}]}'
```

```bash
python3 mcp/solidedge_mcp.py --test eingabe '{"aktionen":[{"aktion":"taste","tasten":"esc"}]}'
```

```bash
python3 mcp/solidedge_mcp.py --test eingabe '{"aktionen":[{"aktion":"ziehen","x":600,"y":400,"x2":800,"y2":500}],"methode":"post"}'
```

```bash
python3 mcp/solidedge_mcp.py --test fenster_bild '{"pfad":"/tmp/se.png","zurueckgeben":false}'
```

Mit `--test` wird von Bildern nur die Größe ausgegeben. Zum Ansehen deshalb `fenster_bild` mit `pfad` aufrufen.

## Wine-Fallen, die die Brücke umgeht

- **Profil-Arrays.** Viele Feature-Methoden erwarten `SAFEARRAY(IDispatch)*`. Der C#-`dynamic`-Binder schickt aber `SAFEARRAY(VARIANT)`, das endet in `DISP_E_TYPEMISMATCH`. Byref mit dem richtigen Typ bringt Wines IDispatch-Proxy zum Absturz. Deshalb ruft `DispAufruf()` `IDispatch::Invoke` roh auf und übergibt das Array **by value** als `VT_ARRAY|VT_DISPATCH`. Wines `ITypeInfo::Invoke` im Solid-Edge-Prozess macht daraus korrekt einen Zeiger.
- **Erster Körper.** `Models.AddFiniteExtrudedProtrusion` liefert unter Wine immer `E_FAIL`. Die ausführliche Variante `Models.AddExtrudedProtrusion` mit allen 35 Parametern funktioniert. Weitere Aufsätze und Ausschnitte laufen über `ExtrudedProtrusions/ExtrudedCutouts.AddFinite/AddThroughAll`, die kein Array brauchen.
- **Drehen.** `View.RotateCamera` erwartet Grad, nicht Bogenmaß, wie die Messung zeigt.
- **Bilder.** `View.SaveAsImage` zeichnet in eine Bitmap. Mit Mesa allein bleibt die leer, bis auf den Hintergrund. Erst die opengl32-Vorschaltung der Haupt-Session macht daraus echte Bilder.
- **Hängende Aufrufe.** Bleibt eine Antwort aus (z. B. wegen eines offenen Dialogs in Solid Edge), verwirft der Server die Brücke nach dem Timeout und startet sie beim nächsten Aufruf neu.
- **Typbibliotheken.** Die API-Signaturen stammen aus den Typbibliotheken der Installation, ausgelesen mit `tools/tlbdump.exe` (`tools/tlbdump.cs`), z. B.:
  `wine tools/tlbdump.exe "C:\Program Files\Siemens\Solid Edge 2026\Program\Part.tlb" > Part.txt`
