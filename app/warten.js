// Wartefenster für den Start von Solid Edge:
//   osascript -l JavaScript warten.js <pid> <titel> <status-datei> <log-datei>
// Zeigt die Schritte des Starters (Zeilen aus der Status-Datei) und die letzten Zeilen der laufenden Ausgabe
// (start.log), damit man sieht, dass noch etwas passiert. Schließt sich, sobald der Prozess <pid> (nach dem exec des
// Starters: Edge.exe) ein großes Fenster zeigt (also Solid Edges eigenen Startbildschirm), der Prozess endet oder nach
// 15 Minuten.
ObjC.import('Cocoa');
ObjC.import('CoreGraphics');

const SA = Application.currentApplication();
SA.includeStandardAdditions = true;

function run(argv) {
  const pid = parseInt(argv[0], 10);
  const titel = argv[1] || 'Solid Edge startet …';
  const statusDatei = argv[2] || '';
  const logDatei = argv[3] || '';

  const app = $.NSApplication.sharedApplication;
  app.setActivationPolicy($.NSApplicationActivationPolicyAccessory);   // kein eigenes Dock-Symbol

  const B = 560, H = 360;
  const win = $.NSWindow.alloc.initWithContentRectStyleMaskBackingDefer(
    $.NSMakeRect(0, 0, B, H), $.NSWindowStyleMaskTitled, $.NSBackingStoreBuffered, false);
  win.title = 'Solid Edge 2026';
  win.level = $.NSFloatingWindowLevel;
  win.center;
  const v = win.contentView;

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

  // Schritte (aus der Status-Datei: eine Zeile je Schritt, „✓ …“ = erledigt, „… “ = läuft)
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

  win.orderFrontRegardless;

  const start = Date.now();
  const ende = start + 15 * 60 * 1000;
  let letzteAusgabe = '';
  while (Date.now() < ende) {
    $.NSRunLoop.currentRunLoop.runUntilDate($.NSDate.dateWithTimeIntervalSinceNow(0.5));
    const s = Math.round((Date.now() - start) / 1000);
    zeit.stringValue = 'läuft seit ' + (s >= 60 ? Math.floor(s / 60) + ' min ' : '') + (s % 60) + ' s';
    schritte.stringValue = lesen(statusDatei, 1);
    const ausgabe = lesen(logDatei, 12);
    if (ausgabe !== letzteAusgabe) {
      tv.string = ausgabe;
      tv.scrollToEndOfDocument(null);
      letzteAusgabe = ausgabe;
    }
    if (!prozessLebt(pid)) break;
    if (hatFenster(pid)) break;
  }
  win.close;
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
