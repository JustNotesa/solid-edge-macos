// Wartefenster für den Start von Solid Edge:
//   osascript -l JavaScript warten.js <pid> <titel> <status-datei> <log-datei> [<wahl-datei>]
// Zeigt die Schritte des Starters (Zeilen aus der Status-Datei) und die letzten Zeilen der laufenden Ausgabe
// (start.log), damit man sieht, dass noch etwas passiert. Schließt sich, sobald der Prozess <pid> (nach dem exec des
// Starters: Edge.exe) ein großes Fenster zeigt (also Solid Edges eigenen Startbildschirm), der Prozess endet oder nach
// 15 Minuten.
// Mit <wahl-datei> (erster Start, Modus noch offen) zeigt das Fenster zuerst die Wahl „Installiert“/„Portabel“ und
// schreibt die Antwort (installiert, portabel oder abbrechen) in diese Datei; der Starter wartet darauf. Die Dateien
// liegen dann in einem Zwischenordner, den das Fenster am Ende wegräumt.
ObjC.import('Cocoa');
ObjC.import('CoreGraphics');

const SA = Application.currentApplication();
SA.includeStandardAdditions = true;

const B = 560, H = 360;
let gewaehlt = '';   // Antwort aus der Wahl


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
    app.finishLaunching;   // nur für die Wahl nötig (Tastatur, Standardknopf) – kostet sonst Zeit
    f.fortschritt.hidden = true;
    const w = wahlBauen(f.win.contentView);
    f.win.defaultButtonCell = w.weiter.cell;
    f.win.makeKeyAndOrderFront(null);
    app.activateIgnoringOtherApps(true);
    const fm = $.NSFileManager.defaultManager;
    let antwort = '';
    while (!gewaehlt) {
      ereignisse(app, 0.25);
      if (!prozessLebt(pid)) break;
      // Antwort von außen (Tests): Datei schon geschrieben → übernehmen
      if (fm.fileExistsAtPath(wahlDatei)) { antwort = lesen(wahlDatei, 1).trim(); break; }
    }
    if (!antwort) {
      antwort = 'abbrechen';
      if (gewaehlt === 'weiter') antwort = w.portabel.state === $.NSControlStateValueOn ? 'portabel' : 'installiert';
      $(antwort).writeToFileAtomicallyEncodingError(wahlDatei, true, $.NSUTF8StringEncoding, null);
    }
    w.ansicht.removeFromSuperview;
    f.fortschritt.hidden = false;
    if (antwort !== 'installiert' && antwort !== 'portabel') { f.win.close; aufraeumen(statusDatei, logDatei, wahlDatei); return; }
  } else {
    f.win.orderFrontRegardless;
  }

  const start = Date.now();
  const ende = start + 15 * 60 * 1000;
  let letzteAusgabe = '';
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
    if (hatFenster(pid)) break;
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

// Wahl „Installiert“/„Portabel“ als eigene Ebene (der Fortschritt ist solange versteckt).
function wahlBauen(v) {
  // Ziel für die Knöpfe (Tag 1 = Weiter, 2 = Abbrechen). Erst hier registrieren: unter Rosetta (x86_64-osascript)
  // hängt registerSubclass endlos – der Starter startet das Fenster deshalb nativ.
  if (!$.SEWahlZiel) ObjC.registerSubclass({
    name: 'SEWahlZiel',
    methods: {
      'knopf:': {
        types: ['void', ['id']],
        implementation: function (sender) { gewaehlt = Number(sender.tag) === 1 ? 'weiter' : 'abbrechen'; }
      },
      'auswahl:': { types: ['void', ['id']], implementation: function (sender) {} }
    }
  });
  const ansicht = $.NSView.alloc.initWithFrame($.NSMakeRect(0, 0, B, H));
  const ziel = $.SEWahlZiel.alloc.init;
  let y = H - 44;

  const kopf = $.NSTextField.labelWithString('Wie soll Solid Edge eingerichtet werden?');
  kopf.frame = $.NSMakeRect(24, y, B - 48, 22);
  kopf.font = $.NSFont.boldSystemFontOfSize(15);
  ansicht.addSubview(kopf);

  function option(titelText, erklaerung, oben, hoehe) {
    const r = $.NSButton.radioButtonWithTitleTargetAction(titelText, ziel, 'auswahl:');
    r.frame = $.NSMakeRect(24, oben - 20, B - 48, 20);
    r.font = $.NSFont.boldSystemFontOfSize(13);
    ansicht.addSubview(r);
    const t = $.NSTextField.wrappingLabelWithString(erklaerung);
    t.frame = $.NSMakeRect(44, oben - 24 - hoehe, B - 68, hoehe);
    t.font = $.NSFont.systemFontOfSize(11.5);
    t.textColor = $.NSColor.secondaryLabelColor;
    ansicht.addSubview(t);
    return r;
  }
  y -= 18;
  const installiert = option('Installiert (empfohlen)',
    'Wine kommt in die App, die Windows-Umgebung mit deinen Solid-Edge-Einstellungen nach ' +
    '~/Library/Application Support/SolidEdge2026. Ein Update der App behält die Einstellungen. ' +
    'Zum Entfernen legt Solid Edge „Solid Edge deinstallieren“ neben die App.', y, 46);
  y -= 24 + 46 + 14;
  const portabel = option('Portabel',
    'Alles kommt in die App, außerhalb bleibt nichts – App löschen = alles weg. ' +
    'Ein Update der App setzt die Solid-Edge-Einstellungen zurück. Die Zeichnungen in den Mac-Ordnern bleiben. ' +
    'Nach dem Kopieren auf einen anderen Mac dauert der erste Start lange (macOS prüft dann die ganzen 13 GB).', y, 62);
  installiert.state = $.NSControlStateValueOn;

  const weiter = $.NSButton.buttonWithTitleTargetAction('Weiter', ziel, 'knopf:');
  weiter.tag = 1;
  weiter.keyEquivalent = '\r';
  weiter.frame = $.NSMakeRect(B - 24 - 110, 18, 110, 32);
  ansicht.addSubview(weiter);
  const abbrechen = $.NSButton.buttonWithTitleTargetAction('Abbrechen', ziel, 'knopf:');
  abbrechen.tag = 2;
  abbrechen.keyEquivalent = '\x1b';
  abbrechen.frame = $.NSMakeRect(B - 24 - 110 - 8 - 110, 18, 110, 32);
  ansicht.addSubview(abbrechen);

  v.addSubview(ansicht);
  return { ansicht: ansicht, installiert: installiert, portabel: portabel, weiter: weiter, abbrechen: abbrechen };
}

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

function hatFenster(pid) {
  const ref = $.CGWindowListCopyWindowInfo($.kCGWindowListOptionOnScreenOnly | $.kCGWindowListExcludeDesktopElements, $.kCGNullWindowID);
  const liste = ObjC.deepUnwrap(ObjC.castRefToObject(ref)) || [];
  for (const w of liste) {
    if (w.kCGWindowOwnerPID !== pid || w.kCGWindowLayer !== 0) continue;
    const b = w.kCGWindowBounds || {};
    if ((b.Width || 0) >= 300 && (b.Height || 0) >= 150) return true;
  }
  return false;
}
