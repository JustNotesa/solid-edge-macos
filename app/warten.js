// Wartefenster für den Start von Solid Edge:
//   osascript -l JavaScript warten.js <pid> <titel> <status-datei> <log-datei> [<wahl-datei>]
// Zeigt die Schritte des Starters (Zeilen aus der Status-Datei) und die letzten Zeilen der laufenden Ausgabe
// (start.log), damit man sieht, dass noch etwas passiert. Sobald der Prozess <pid> (nach dem exec des Starters:
// Edge.exe) seinen Startbildschirm zeigt, legt es sich dahinter; es schließt sich, wenn das Hauptfenster erscheint,
// der Prozess endet oder nach 15 Minuten.
// Mit <wahl-datei> (erster Start, Modus noch offen) fragt das Fenster zuerst per Dialog „Installiert“/„Portabel“ und
// schreibt die Antwort (installiert, portabel oder abbrechen) in diese Datei; der Starter wartet darauf. Die Dateien
// liegen dann in einem Zwischenordner, den das Fenster am Ende wegräumt.
ObjC.import('Cocoa');
ObjC.import('CoreGraphics');

const SA = Application.currentApplication();
SA.includeStandardAdditions = true;

const B = 560, H = 360;


function run(argv) {
  const pid = parseInt(argv[0], 10);
  const titel = argv[1] || 'Solid Edge startet …';
  const statusDatei = argv[2] || '';
  const logDatei = argv[3] || '';
  const wahlDatei = argv[4] || '';

  const app = $.NSApplication.sharedApplication;
  app.setActivationPolicy($.NSApplicationActivationPolicyAccessory);   // kein eigenes Dock-Symbol

  const f = fensterBauen(titel);
  if (wahlDatei) {
    // Wahl als normaler macOS-Dialog (StandardAdditions): zuverlässig anklickbar. Kein Standardknopf – keine der beiden
    // Varianten wird hervorgehoben; Esc = Abbrechen.
    // Das Wartefenster steht dahinter und zeigt solange „Warte auf deine Auswahl“.
    f.schritte.stringValue = lesen(statusDatei, 1) + '\n⏳  Warte auf deine Auswahl: Installiert oder Portabel';
    f.win.orderFrontRegardless;
    let antwort = 'abbrechen';
    try {
      SA.activate();
      const r = SA.displayDialog(WAHL_TEXT, {
        withTitle: 'Solid Edge 2026 – Einrichtung',
        buttons: ['Abbrechen', 'Portabel', 'Installiert'],
        cancelButton: 'Abbrechen', withIcon: 'note'
      });
      antwort = r.buttonReturned === 'Portabel' ? 'portabel' : 'installiert';
    } catch (e) { antwort = 'abbrechen'; }   // Abbrechen/Esc wirft einen Fehler
    $(antwort).writeToFileAtomicallyEncodingError(wahlDatei, true, $.NSUTF8StringEncoding, null);
    if (antwort === 'abbrechen') { f.win.close; aufraeumen(statusDatei, logDatei, wahlDatei); return; }
  } else {
    f.win.orderFrontRegardless;
  }

  const start = Date.now();
  const ende = start + 15 * 60 * 1000;
  let letzteAusgabe = '';
  let startbild = 0;   // Fensternummer von Solid Edges Startbildschirm
  while (Date.now() < ende) {
    ereignisse(app, 0.5);
    const s = Math.round((Date.now() - start) / 1000);
    f.zeit.stringValue = 'läuft seit ' + (s >= 60 ? Math.floor(s / 60) + ' min ' : '') + (s % 60) + ' s';
    f.schritte.stringValue = lesen(statusDatei, 1);
    const ausgabe = lesen(logDatei, 12);
    if (ausgabe !== letzteAusgabe) {
      f.tv.string = ausgabe;
      f.tv.scrollToEndOfDocument(null);
      letzteAusgabe = ausgabe;
    }
    if (!prozessLebt(pid)) break;
    // Erstes großes Fenster = Startbildschirm. Wine zeigt es erst einige Sekunden leer (unsichtbar) an, bevor es
    // gezeichnet ist – das Wartefenster schließt deshalb noch nicht, sondern legt sich direkt dahinter: Solange der
    // Startbildschirm leer ist, sieht man das Wartefenster, danach deckt er es zu. Erst wenn ein weiteres Fenster
    // erscheint (Solid Edges Hauptfenster) oder der Startbildschirm wieder verschwindet, schließt es sich.
    const fenster = grosseFenster(pid);
    const nummern = fenster.map(w => w.nummer);
    if (!startbild && fenster.length) {
      startbild = fenster[0].nummer;
      // mittig hinter den Startbildschirm (CG-Koordinaten: Ursprung oben links, Cocoa: unten links)
      const b = fenster[0].rahmen, hoehe = $.NSScreen.screens.objectAtIndex(0).frame.size.height;
      if (b.Width >= B && b.Height >= H)
        f.win.setFrameOrigin($.NSMakePoint(b.X + (b.Width - B) / 2, hoehe - (b.Y + (b.Height - H) / 2) - H));
      f.win.level = $.NSNormalWindowLevel;
      f.win.orderWindowRelativeTo($.NSWindowBelow, startbild);
    } else if (startbild && (nummern.some(n => n !== startbild) || nummern.indexOf(startbild) < 0)) {
      break;
    }
  }
  f.win.close;
  if (wahlDatei) aufraeumen(statusDatei, logDatei, wahlDatei);
}

// Fenster mit Kopfzeile, Schritten und laufender Ausgabe.
function fensterBauen(titel) {
  const win = $.NSWindow.alloc.initWithContentRectStyleMaskBackingDefer(
    $.NSMakeRect(0, 0, B, H), $.NSWindowStyleMaskTitled, $.NSBackingStoreBuffered, false);
  win.title = 'Solid Edge 2026';
  win.level = $.NSFloatingWindowLevel;
  win.center;
  // Fortschritt in eigener Ebene, damit die Wahl sie vorher verdecken kann.
  const v = $.NSView.alloc.initWithFrame($.NSMakeRect(0, 0, B, H));
  win.contentView.addSubview(v);

  const spinner = $.NSProgressIndicator.alloc.initWithFrame($.NSMakeRect(20, H - 52, 28, 28));
  spinner.style = $.NSProgressIndicatorStyleSpinning;
  spinner.startAnimation(null);
  v.addSubview(spinner);

  const kopf = $.NSTextField.labelWithString(titel);
  kopf.frame = $.NSMakeRect(60, H - 48, B - 80, 22);
  kopf.font = $.NSFont.boldSystemFontOfSize(15);
  v.addSubview(kopf);

  const zeit = $.NSTextField.labelWithString('');
  zeit.frame = $.NSMakeRect(60, H - 68, B - 80, 18);
  zeit.font = $.NSFont.systemFontOfSize(11);
  zeit.textColor = $.NSColor.secondaryLabelColor;
  v.addSubview(zeit);

  // Schritte (aus der Status-Datei: eine Zeile je Schritt, „✓ …“ = erledigt, „⏳ …“ = läuft)
  const schritte = $.NSTextField.wrappingLabelWithString('');
  schritte.frame = $.NSMakeRect(24, 168, B - 48, 104);
  schritte.font = $.NSFont.systemFontOfSize(12.5);
  v.addSubview(schritte);

  // Laufende Ausgabe (die letzten Zeilen aus start.log), wie im Terminal
  const scroll = $.NSScrollView.alloc.initWithFrame($.NSMakeRect(20, 20, B - 40, 138));
  scroll.hasVerticalScroller = true;
  scroll.borderType = $.NSBezelBorder;
  const tv = $.NSTextView.alloc.initWithFrame($.NSMakeRect(0, 0, B - 44, 138));
  tv.editable = false;
  tv.font = $.NSFont.monospacedSystemFontOfSizeWeight(10, 0);
  tv.textColor = $.NSColor.colorWithCalibratedWhiteAlpha(0.85, 1);
  tv.backgroundColor = $.NSColor.colorWithCalibratedWhiteAlpha(0.12, 1);
  scroll.documentView = tv;
  v.addSubview(scroll);

  return { win: win, fortschritt: v, zeit: zeit, schritte: schritte, tv: tv };
}

const WAHL_TEXT =
  'Wie soll Solid Edge eingerichtet werden?\n\n' +
  'INSTALLIERT\nWine kommt in die App, die Windows-Umgebung mit deinen Solid-Edge-Einstellungen nach ' +
  '~/Library/Application Support/SolidEdge2026. Ein Update der App behält die Einstellungen. ' +
  'Zum Entfernen liegt „Solid Edge deinstallieren“ neben der App.\n\n' +
  'PORTABEL\nAlles kommt in die App, außerhalb bleibt nichts – App löschen = alles weg. ' +
  'Ein Update der App setzt die Solid-Edge-Einstellungen zurück. Die Zeichnungen in den Mac-Ordnern bleiben. ' +
  'Nach dem Kopieren auf einen anderen Mac dauert der erste Start lange (macOS prüft dann die ganzen 13 GB).';

// Ereignisse (Klicks, Tasten) abarbeiten, bis zu <sek> Sekunden warten.
function ereignisse(app, sek) {
  const bis = $.NSDate.dateWithTimeIntervalSinceNow(sek);
  for (;;) {
    const ev = app.nextEventMatchingMaskUntilDateInModeDequeue($.NSEventMaskAny, bis, $.NSDefaultRunLoopMode, true);
    if (!ev || ev.isNil()) break;
    app.sendEvent(ev);
  }
}

// Zwischenordner der Wahl wegräumen (nur die drei Dateien bzw. Verweise, dann den leeren Ordner).
function aufraeumen(status, log, wahl) {
  try {
    SA.doShellScript('rm -f ' + [status, log, wahl].map(quoted).join(' ') + '; rmdir ' + quoted(wahl.replace(/\/[^/]*$/, '')));
  } catch (e) {}
}

// Letzte n Zeilen einer Datei (status: alle Zeilen); Steuerzeichen und leere Zeilen weg.
function lesen(pfad, n) {
  if (!pfad) return '';
  try {
    const cmd = n === 1 ? 'cat ' : 'tail -c 20000 ';
    const t = SA.doShellScript(cmd + quoted(pfad) + " | tr -d '\\r' | grep -v '^[[:space:]]*$'" + (n === 1 ? '' : ' | tail -n ' + n));
    return t.replace(/\x1b\[[0-9;]*m/g, '');
  } catch (e) { return ''; }
}
function quoted(p) { return "'" + p.replace(/'/g, "'\\''") + "'"; }

function prozessLebt(pid) {
  try { SA.doShellScript('kill -0 ' + pid); return true; } catch (e) { return false; }
}

// Sichtbare, große Fenster (≥ 300×150, normale Ebene) des Prozesses <pid>: [{nummer, rahmen}].
function grosseFenster(pid) {
  const ref = $.CGWindowListCopyWindowInfo($.kCGWindowListOptionOnScreenOnly | $.kCGWindowListExcludeDesktopElements, $.kCGNullWindowID);
  const liste = ObjC.deepUnwrap(ObjC.castRefToObject(ref)) || [];
  const nummern = [];
  for (const w of liste) {
    if (w.kCGWindowOwnerPID !== pid || w.kCGWindowLayer !== 0) continue;
    const b = w.kCGWindowBounds || {};
    if ((b.Width || 0) >= 300 && (b.Height || 0) >= 150) nummern.push({ nummer: w.kCGWindowNumber, rahmen: b });
  }
  return nummern;
}
