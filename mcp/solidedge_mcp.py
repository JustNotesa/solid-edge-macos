#!/usr/bin/env python3
"""MCP-Server für Solid Edge 2026 unter Wine (macOS).

Spricht MCP über stdio (JSON-RPC, eine Nachricht pro Zeile) und reicht die Aufrufe an die
Brücke bridge/sebridge.exe weiter. Die Brücke läuft dauerhaft in Wine, hängt sich per COM an das
laufende Solid Edge und beantwortet pro Zeile eine JSON-Anfrage.

Nur Standardbibliothek, läuft ab Python 3.8 (auch mit /usr/bin/python3 von macOS).
"""
import base64
import json
import os
import queue
import re
import subprocess
import sys
import tempfile
import threading
import time

HIER = os.path.dirname(os.path.abspath(__file__))
SE_ROOT = os.environ.get("SE_ROOT") or os.path.dirname(HIER)
BRIDGE_CS = os.path.join(HIER, "bridge", "sebridge.cs")
BRIDGE_EXE = os.path.join(HIER, "bridge", "sebridge.exe")
LOG = os.path.join(HIER, "bridge", "sebridge.log")
CSC = r"C:\windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
VERSION = "0.1.0"


def log(*teile):
    print("[solidedge-mcp]", *teile, file=sys.stderr, flush=True)


# ---------------------------------------------------------------- Umgebung / Pfade

_env = None


def wine_env():
    """Umgebung aus env.sh übernehmen (WINE, WINEPREFIX, WINEMSYNC …) – muss zum laufenden wineserver passen."""
    global _env
    if _env is None:
        skript = 'source "$1/env.sh" >/dev/null 2>&1; env -0'
        roh = subprocess.run(["bash", "-c", skript, "bash", SE_ROOT], capture_output=True, check=True).stdout
        _env = dict(z.split("=", 1) for z in roh.decode("utf-8", "replace").split("\0") if "=" in z)
        _env["WINEDEBUG"] = "-all"
    return _env


def prefix():
    return wine_env().get("WINEPREFIX", os.path.join(SE_ROOT, "prefix"))


def win_pfad(p):
    """macOS-Pfad → Windows-Pfad (C:\\… im Prefix, sonst Z:\\…). Windows-Pfade bleiben unverändert."""
    if not p:
        return p
    if re.match(r"^[A-Za-z]:[\\/]", p) or "\\" in p:
        return p
    p = os.path.abspath(os.path.expanduser(p))
    drive_c = os.path.realpath(os.path.join(prefix(), "drive_c"))
    real = os.path.realpath(p) if os.path.exists(p) else p
    if real == drive_c or real.startswith(drive_c + os.sep):
        return "C:" + real[len(drive_c):].replace("/", "\\") or "C:\\"
    return "Z:" + p.replace("/", "\\")


def mac_pfad(p):
    """Windows-Pfad → macOS-Pfad (für Rückgaben)."""
    if not isinstance(p, str):
        return p
    m = re.match(r"^([A-Za-z]):\\(.*)$", p)
    if not m:
        return p
    laufwerk, rest = m.group(1).lower(), m.group(2).replace("\\", "/")
    if laufwerk == "z":
        return "/" + rest
    if laufwerk == "c":
        return os.path.join(prefix(), "drive_c", rest)
    return p


def mac_pfade_ergaenzen(obj):
    """Ergänzt zu jedem Feld „pfad“ mit Windows-Pfad ein Feld „pfad_mac“."""
    if isinstance(obj, dict):
        neu = {}
        for k, v in obj.items():
            neu[k] = mac_pfade_ergaenzen(v)
            if k == "pfad" and isinstance(v, str) and re.match(r"^[A-Za-z]:\\", v):
                neu["pfad_mac"] = mac_pfad(v)
        return neu
    if isinstance(obj, list):
        return [mac_pfade_ergaenzen(x) for x in obj]
    return obj


def solid_edge_laeuft():
    return subprocess.run(["pgrep", "-qf", r"Program/Edge\.exe"]).returncode == 0


# ---------------------------------------------------------------- Brücke

class BrueckenFehler(Exception):
    pass


class Bruecke:
    def __init__(self):
        self.proc = None
        self.antworten = queue.Queue()
        self.naechste_id = 0
        self.sperre = threading.Lock()

    def uebersetzen(self):
        if os.path.exists(BRIDGE_EXE) and os.path.getmtime(BRIDGE_EXE) >= os.path.getmtime(BRIDGE_CS):
            return
        log("übersetze Brücke …")
        env = wine_env()
        aufruf = [env["WINE"], CSC, "/nologo", "/r:System.Web.Extensions.dll", "/r:Microsoft.CSharp.dll",
                  "/out:" + win_pfad(BRIDGE_EXE), win_pfad(BRIDGE_CS)]
        r = subprocess.run(aufruf, env=env, stdin=subprocess.DEVNULL, capture_output=True, timeout=300)
        ausgabe = r.stdout.decode("utf-8", "replace")
        fehler = [z for z in ausgabe.splitlines() if "error" in z]
        if fehler or not os.path.exists(BRIDGE_EXE):
            raise BrueckenFehler("Übersetzen der Brücke fehlgeschlagen:\n" + ausgabe)

    def _leser(self, proc):
        for zeile in proc.stdout:
            zeile = zeile.decode("utf-8", "replace").strip()
            if not zeile.startswith("{"):
                continue
            try:
                self.antworten.put(json.loads(zeile))
            except ValueError:
                log("unlesbare Zeile:", zeile[:200])
        self.antworten.put(None)   # Prozess beendet

    def laeuft(self):
        return self.proc is not None and self.proc.poll() is None

    def starten(self):
        if self.laeuft():
            return
        if not solid_edge_laeuft():
            raise BrueckenFehler("Solid Edge läuft nicht. Erst mit dem Tool „starten“ (oder ./start.sh) öffnen.")
        self.uebersetzen()
        env = wine_env()
        self.antworten = queue.Queue()
        with open(LOG, "ab") as logdatei:
            self.proc = subprocess.Popen([env["WINE"], win_pfad(BRIDGE_EXE)], env=env, cwd=SE_ROOT,
                                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=logdatei,
                                         start_new_session=True)
        threading.Thread(target=self._leser, args=(self.proc,), daemon=True).start()
        try:
            erste = self.antworten.get(timeout=60)
        except queue.Empty:
            erste = None
        if not erste or not erste.get("bereit"):
            self.stoppen()
            raise BrueckenFehler("Brücke startet nicht (siehe %s)." % LOG)

    def stoppen(self):
        if self.proc is None:
            return
        try:
            if self.proc.poll() is None:
                try:
                    self.proc.stdin.write(b'{"cmd":"beenden"}\n')
                    self.proc.stdin.flush()
                    self.proc.wait(timeout=5)
                except Exception:
                    pass
            if self.proc.poll() is None:
                self.proc.kill()
        finally:
            self.proc = None

    def aufruf(self, cmd, args=None, timeout=120):
        with self.sperre:
            for versuch in range(2):
                self.starten()
                self.naechste_id += 1
                rid = self.naechste_id
                try:
                    self.proc.stdin.write((json.dumps({"id": rid, "cmd": cmd, "args": args or {}}) + "\n").encode())
                    self.proc.stdin.flush()
                except (BrokenPipeError, OSError):
                    self.stoppen()
                    continue
                ende = time.time() + timeout
                while True:
                    try:
                        antwort = self.antworten.get(timeout=max(0.1, ende - time.time()))
                    except queue.Empty:
                        # Solid Edge hängt (z. B. offener Dialog) – Brücke verwerfen, nächster Aufruf startet neu.
                        self.stoppen()
                        raise BrueckenFehler("Keine Antwort von Solid Edge nach %d s (offener Dialog?)." % timeout)
                    if antwort is None:
                        self.stoppen()
                        break   # Prozess weg → einmal neu starten
                    if antwort.get("id") == rid:
                        if antwort.get("ok"):
                            return antwort.get("result")
                        raise BrueckenFehler(antwort.get("error") or "unbekannter Fehler")
            raise BrueckenFehler("Brücke beendet sich unerwartet (siehe %s)." % LOG)


bruecke = Bruecke()


# ---------------------------------------------------------------- Tools

def _vec3():
    return {"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3}


ELEMENT_SCHEMA = {
    "type": "object",
    "properties": {
        "typ": {"type": "string", "enum": ["rechteck", "kreis", "polygon"]},
        "x": {"type": "number", "description": "rechteck: linke untere Ecke (bzw. Mitte bei zentriert), kreis: Mittelpunkt; mm"},
        "y": {"type": "number", "description": "wie x; mm"},
        "breite": {"type": "number", "description": "rechteck: Breite in mm"},
        "hoehe": {"type": "number", "description": "rechteck: Höhe in mm"},
        "zentriert": {"type": "boolean", "description": "rechteck: x/y ist die Mitte"},
        "radius": {"type": "number", "description": "kreis: Radius in mm"},
        "durchmesser": {"type": "number", "description": "kreis: Durchmesser in mm (statt radius)"},
        "punkte": {"type": "array", "items": {"type": "array", "items": {"type": "number"}, "minItems": 2, "maxItems": 2},
                   "description": "polygon: Eckpunkte [[x,y],…] in mm, wird automatisch geschlossen"},
    },
    "required": ["typ"],
}


def t_status(a):
    return bruecke.aufruf("status")


def t_oeffnen(a):
    return bruecke.aufruf("oeffnen", {"pfad": win_pfad(a["pfad"])}, timeout=300)


def t_neu(a):
    args = {"typ": a.get("typ", "teil")}
    if a.get("vorlage"):
        args["vorlage"] = win_pfad(a["vorlage"])
    if a.get("modus"):
        args["modus"] = a["modus"]
    return bruecke.aufruf("neu", args, timeout=180)


def t_speichern(a):
    args = {"pfad": win_pfad(a["pfad"])} if a.get("pfad") else {}
    return bruecke.aufruf("speichern", args, timeout=300)


def t_exportieren(a):
    pfad = os.path.abspath(os.path.expanduser(a["pfad"])) if not re.match(r"^[A-Za-z]:", a["pfad"]) else a["pfad"]
    if pfad.startswith("/"):
        os.makedirs(os.path.dirname(pfad), exist_ok=True)
    return bruecke.aufruf("exportieren", {"pfad": win_pfad(pfad)}, timeout=600)


def t_schliessen(a):
    return bruecke.aufruf("schliessen", {"speichern": bool(a.get("speichern", False))}, timeout=120)


def t_aktivieren(a):
    return bruecke.aufruf("aktivieren", {"name": a["name"]})


def t_befehl(a):
    if "id" not in a and "name" not in a:
        raise BrueckenFehler("id oder name angeben")
    return bruecke.aufruf("befehl", {"name": str(a.get("name") or a.get("id"))})


def t_ansicht(a):
    return bruecke.aufruf("ansicht", a)


def t_kamera(a):
    return bruecke.aufruf("kamera", a)


def t_baum(a):
    return bruecke.aufruf("baum")


def t_variablen(a):
    return bruecke.aufruf("variablen", {"filter": a.get("filter", "*")})


def t_variable_setzen(a):
    return bruecke.aufruf("variable_setzen", a, timeout=180)


def t_extrusion(a):
    return bruecke.aufruf("extrusion", a, timeout=300)


def t_konstanten(a):
    return bruecke.aufruf("konstanten", a)


def t_com(a):
    return bruecke.aufruf("com", a, timeout=300)


BILDTYPEN = {".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg", ".bmp": "image/bmp"}


def t_bild(a):
    """Speichert die aktuelle 3D-Ansicht als Bild (View.SaveAsImage) und gibt es an den Client zurück."""
    if a.get("ansicht"):
        bruecke.aufruf("ansicht", {"art": a["ansicht"]})
    ziel = a.get("pfad")
    if ziel:
        ziel = os.path.abspath(os.path.expanduser(ziel))
        os.makedirs(os.path.dirname(ziel), exist_ok=True)
    else:
        ordner = os.path.join(tempfile.gettempdir(), "solidedge-mcp")
        os.makedirs(ordner, exist_ok=True)
        ziel = os.path.join(ordner, "ansicht-%d.png" % int(time.time() * 1000))
    endung = os.path.splitext(ziel)[1].lower()
    if endung not in BILDTYPEN:
        raise BrueckenFehler("Bildformat über die Endung wählen: .png, .jpg oder .bmp")
    breite, hoehe = int(a.get("breite", 1024)), int(a.get("hoehe", 768))
    info = bruecke.aufruf("bild", {"pfad": win_pfad(ziel), "breite": breite, "hoehe": hoehe}, timeout=180)
    if not os.path.exists(ziel) or os.path.getsize(ziel) == 0:
        raise BrueckenFehler("Solid Edge hat kein Bild geschrieben (%s)." % json.dumps(info, ensure_ascii=False))
    ergebnis = {"pfad_mac": ziel, "breite": breite, "hoehe": hoehe, "bytes": os.path.getsize(ziel)}
    if a.get("zurueckgeben", True):
        with open(ziel, "rb") as f:
            ergebnis["_bilder"] = [{"type": "image", "mimeType": BILDTYPEN[endung],
                                    "data": base64.b64encode(f.read()).decode("ascii")}]
        if not a.get("pfad"):
            os.remove(ziel)   # nur zurückgegeben, nicht gespeichert
            ergebnis["pfad_mac"] = None
    return ergebnis


CAPTURE_QUELLE = os.path.join(HIER, "capture")
CAPTURE_PROTOKOLL = 2   # wie PROTOKOLL in capture/secapture.c
CAPTURE_DLL = "secapture-v%d.dll" % CAPTURE_PROTOKOLL
letzte_skalierung = 1.0   # Verkleinerung des letzten Fensterbilds (für eingabe mit bildkoordinaten)


def capture_installieren():
    """DLL + Injektor nach C:\\secapture im Prefix kopieren (wie capture/install.sh)."""
    ziel = os.path.join(prefix(), "drive_c", "secapture")
    os.makedirs(ziel, exist_ok=True)
    for name in (CAPTURE_DLL, "secapture-inject.exe"):
        q, z = os.path.join(CAPTURE_QUELLE, name), os.path.join(ziel, name)
        if not os.path.exists(q):
            raise BrueckenFehler("%s fehlt – erst mcp/capture/build.sh ausführen." % q)
        if not os.path.exists(z) or os.path.getmtime(z) < os.path.getmtime(q):
            try:
                with open(q, "rb") as fq, open(z, "wb") as fz:
                    fz.write(fq.read())
            except OSError:
                pass   # DLL ist in Edge.exe geladen und gesperrt – die vorhandene weiterverwenden
    return ziel


def _warten(bedingung, sekunden):
    ende = time.time() + sekunden
    while time.time() < ende:
        if bedingung():
            return True
        time.sleep(0.05)
    return bedingung()


def capture_anfordern(zeilen, ergebnis, timeout=30):
    """Schickt eine Anforderung an die DLL in Edge.exe (lädt sie bei Bedarf) und wartet auf die Ergebnisdatei.
    zeilen: Art, Modus/Methode, danach Aktionen; ergebnis: macOS-Pfad im Ordner C:\\secapture."""
    if not solid_edge_laeuft():
        raise BrueckenFehler("Solid Edge läuft nicht.")
    ordner = capture_installieren()
    anforderung = os.path.join(ordner, "anforderung-v%d" % CAPTURE_PROTOKOLL)
    win_ergebnis = "C:\\secapture\\" + os.path.basename(ergebnis)
    inhalt = "\n".join([zeilen[0], zeilen[1], win_ergebnis] + list(zeilen[2:])) + "\n"
    with open(anforderung + ".neu", "w", encoding="utf-8") as f:
        f.write(inhalt)
    os.replace(anforderung + ".neu", anforderung)
    if not _warten(lambda: not os.path.exists(anforderung), 1.0):
        # Noch niemand quittiert: DLL in Edge.exe laden (einmal pro Solid-Edge-Sitzung nötig).
        env = wine_env()
        r = subprocess.run([env["WINE"], "C:\\secapture\\secapture-inject.exe", "C:\\secapture\\" + CAPTURE_DLL],
                           env=env, cwd=SE_ROOT, stdin=subprocess.DEVNULL, capture_output=True, timeout=120)
        if not _warten(lambda: not os.path.exists(anforderung), 5):
            try:
                os.remove(anforderung)
            except OSError:
                pass
            raise BrueckenFehler("DLL in Edge.exe antwortet nicht. Injektor: %s" %
                                 r.stdout.decode("utf-8", "replace").strip())
    if not _warten(lambda: os.path.exists(ergebnis) or os.path.exists(ergebnis + ".err"), timeout):
        raise BrueckenFehler("Keine Antwort aus Edge.exe nach %d s." % timeout)
    if os.path.exists(ergebnis + ".err"):
        with open(ergebnis + ".err", encoding="utf-8", errors="replace") as f:
            meldung = f.read()
        os.remove(ergebnis + ".err")
        raise BrueckenFehler("Fehler in Edge.exe: " + meldung)


def t_fenster_bild(a):
    """Bild des ganzen Solid-Edge-Fensters, aufgenommen in Edge.exe selbst (capture/secapture.c)."""
    global letzte_skalierung
    stempel = int(time.time() * 1000)
    bmp = os.path.join(prefix(), "drive_c", "secapture", "fenster-%d.bmp" % stempel)
    modus = "print" if a.get("modus") == "print" else "bitblt"
    capture_anfordern(["bild", modus], bmp)

    max_breite = int(a.get("max_breite", 1600))
    ziel = os.path.abspath(os.path.expanduser(a["pfad"])) if a.get("pfad") else os.path.join(
        tempfile.gettempdir(), "solidedge-mcp", "fenster-%d.png" % stempel)
    os.makedirs(os.path.dirname(ziel), exist_ok=True)
    endung = os.path.splitext(ziel)[1].lower()
    fmt = {".png": "png", ".jpg": "jpeg", ".jpeg": "jpeg", ".bmp": "bmp"}.get(endung)
    if not fmt:
        os.remove(bmp)
        raise BrueckenFehler("Bildformat über die Endung wählen: .png, .jpg oder .bmp")
    breite = hoehe = None
    try:
        info = subprocess.run(["sips", "-g", "pixelWidth", "-g", "pixelHeight", bmp], capture_output=True, text=True).stdout
        breite = int(re.search(r"pixelWidth: (\d+)", info).group(1))
        hoehe = int(re.search(r"pixelHeight: (\d+)", info).group(1))
        aufruf = ["sips", "-s", "format", fmt]
        if breite > max_breite:
            aufruf += ["--resampleWidth", str(max_breite)]
        subprocess.run(aufruf + [bmp, "--out", ziel], capture_output=True, check=True)
    finally:
        os.remove(bmp)
    letzte_skalierung = min(1.0, float(max_breite) / breite) if breite else 1.0
    ergebnis = {"pfad_mac": ziel, "fenster_px": [breite, hoehe], "skalierung": round(letzte_skalierung, 4),
                "modus": modus, "bytes": os.path.getsize(ziel)}
    if a.get("zurueckgeben", True):
        with open(ziel, "rb") as f:
            ergebnis["_bilder"] = [{"type": "image", "mimeType": BILDTYPEN[endung],
                                    "data": base64.b64encode(f.read()).decode("ascii")}]
        if not a.get("pfad"):
            os.remove(ziel)
            ergebnis["pfad_mac"] = None
    return ergebnis


def _aktion_zeile(akt, faktor):
    """Eine Aktion (dict) in eine Zeile des DLL-Protokolls übersetzen."""
    art = str(akt.get("aktion", "")).lower()
    taste = str(akt.get("taste", "") or "")

    def xy(kx="x", ky="y"):
        if kx not in akt or ky not in akt:
            raise BrueckenFehler("Aktion %s braucht %s und %s" % (art, kx, ky))
        return "%d %d" % (round(float(akt[kx]) / faktor), round(float(akt[ky]) / faktor))

    if art in ("move", "bewegen"):
        return "move " + xy()
    if art in ("click", "klick", "klicken"):
        return ("click %s %s" % (xy(), taste)).strip()
    if art in ("rechtsklick", "rightclick"):
        return "click %s right" % xy()
    if art in ("dblclick", "doppelklick"):
        return ("dblclick %s %s" % (xy(), taste)).strip()
    if art in ("down", "druecken"):
        return ("down %s %s" % (xy(), taste)).strip()
    if art in ("up", "loslassen"):
        return ("up %s %s" % (xy(), taste)).strip()
    if art in ("drag", "ziehen"):
        return ("drag %s %s %s" % (xy(), xy("x2", "y2"), taste)).strip()
    if art in ("key", "taste", "tasten"):
        kombi = str(akt.get("tasten") or akt.get("taste") or "").replace(" ", "")
        if not kombi:
            raise BrueckenFehler("Aktion taste braucht „tasten“, z. B. esc oder strg+z")
        return "key " + kombi.lower()
    if art == "text":
        return "text " + str(akt.get("text", "")).replace("\n", " ")
    if art in ("wait", "warten"):
        return "wait %d" % int(akt.get("ms", 200))
    raise BrueckenFehler("Unbekannte Aktion: %s" % art)


def t_eingabe(a):
    """Maus-/Tastatureingaben in Edge.exe ausführen (SendInput oder gepostete Fensternachrichten)."""
    aktionen = a.get("aktionen") or []
    if isinstance(aktionen, dict):
        aktionen = [aktionen]
    if not aktionen:
        raise BrueckenFehler("aktionen fehlt")
    faktor = letzte_skalierung if a.get("bildkoordinaten") else 1.0
    zeilen = [_aktion_zeile(x, faktor) for x in aktionen]
    methode = "post" if a.get("methode") == "post" else "sendinput"
    ergebnis = os.path.join(prefix(), "drive_c", "secapture", "eingabe-%d.txt" % int(time.time() * 1000))
    dauer = sum(int(x.get("ms", 0)) for x in aktionen if str(x.get("aktion")).lower() in ("wait", "warten")) / 1000.0
    capture_anfordern(["eingabe", methode] + zeilen, ergebnis, timeout=30 + int(dauer))
    with open(ergebnis, encoding="utf-8", errors="replace") as f:
        protokoll = f.read().strip()
    os.remove(ergebnis)
    antwort = {"methode": methode, "protokoll": protokoll.splitlines()}
    if a.get("danach_bild", False):
        time.sleep(int(a.get("warten_ms", 500)) / 1000.0)
        bild = t_fenster_bild({"max_breite": a.get("max_breite", 1600)})
        antwort["_bilder"] = bild.pop("_bilder", [])
        antwort["bild"] = bild
    return antwort


def t_starten(a):
    if solid_edge_laeuft():
        return {"laeuft": True, "hinweis": "Solid Edge lief bereits."}
    aufruf = ["bash", os.path.join(SE_ROOT, "start.sh")]
    if a.get("datei"):
        aufruf.append(win_pfad(a["datei"]))
    subprocess.Popen(aufruf, cwd=SE_ROOT, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                     stderr=subprocess.DEVNULL, start_new_session=True)
    warte = int(a.get("warten_s", 180))
    ende = time.time() + warte
    letzter = None
    while time.time() < ende:
        time.sleep(5)
        if not solid_edge_laeuft():
            continue
        try:
            return {"laeuft": True, "status": bruecke.aufruf("status", timeout=30)}
        except BrueckenFehler as e:
            letzter = str(e)
            bruecke.stoppen()
    return {"laeuft": solid_edge_laeuft(), "hinweis": "Solid Edge startet noch (%s). Später „status“ aufrufen." % (letzter or "keine Antwort")}


TOOLS = [
    ("status", t_status,
     "Zustand von Solid Edge: Version, Umgebung, offene Dokumente (Name, Pfad, Typ, geändert, Modellierungsmodus) und aktives Dokument.",
     {}, []),
    ("starten", t_starten,
     "Startet Solid Edge (falls es nicht läuft) und wartet, bis die Automatisierung antwortet. Dauert 1–3 Minuten.",
     {"datei": {"type": "string", "description": "optional: Dokument, das gleich geöffnet wird (macOS- oder Windows-Pfad)"},
      "warten_s": {"type": "integer", "description": "maximale Wartezeit in Sekunden (Standard 180)"}}, []),
    ("oeffnen", t_oeffnen,
     "Öffnet ein Dokument (.par, .psm, .asm, .dft, auch STEP usw.) und passt die Ansicht ein. Beispiel: C:\\Program Files\\Siemens\\Solid Edge 2026\\Training\\block.par",
     {"pfad": {"type": "string", "description": "macOS-Pfad (/Users/…) oder Windows-Pfad (C:\\…)"}}, ["pfad"]),
    ("neu", t_neu,
     "Legt ein neues Dokument an und macht es aktiv.",
     {"typ": {"type": "string", "enum": ["teil", "blech", "baugruppe", "zeichnung"], "description": "Standard: teil"},
      "vorlage": {"type": "string", "description": "optional: Vorlagendatei"},
      "modus": {"type": "string", "enum": ["geordnet", "synchron"], "description": "optional für Teil/Blech; Extrusionen über die API brauchen „geordnet“"}}, []),
    ("speichern", t_speichern,
     "Speichert das aktive Dokument; mit pfad als neue Datei (Speichern unter).",
     {"pfad": {"type": "string", "description": "optional: Zielpfad mit passender Endung (.par, .asm …)"}}, []),
    ("exportieren", t_exportieren,
     "Exportiert eine Kopie des aktiven Dokuments; Format über die Endung: .stp/.step, .x_t, .igs, .stl, .jt, bei Zeichnungen .pdf/.dxf/.dwg.",
     {"pfad": {"type": "string", "description": "Zielpfad, z. B. ~/Desktop/teil.stp"}}, ["pfad"]),
    ("schliessen", t_schliessen,
     "Schließt das aktive Dokument.",
     {"speichern": {"type": "boolean", "description": "vorher speichern (Standard false = Änderungen verwerfen)"}}, []),
    ("aktivieren", t_aktivieren,
     "Macht ein offenes Dokument aktiv (Name wie in „status“, z. B. block.par).",
     {"name": {"type": "string"}}, ["name"]),
    ("befehl", t_befehl,
     "Startet einen Solid-Edge-Befehl wie per Menü (Application.StartCommand). Interaktive Befehle warten danach auf Eingaben in der Oberfläche. IDs/Namen mit „konstanten_suchen“ finden (Aufzählungen *CommandConstants).",
     {"id": {"type": "integer", "description": "Befehls-ID, z. B. 32810 (ISO-Ansicht)"},
      "name": {"type": "string", "description": "oder Konstantenname, z. B. PartViewISOView"}}, []),
    ("ansicht", t_ansicht,
     "Ändert die 3D-Ansicht: Standardansicht, Einpassen, Drehen oder Zoomen. Gibt die neue Kamera zurück.",
     {"art": {"type": "string", "enum": ["einpassen", "iso", "vorne", "hinten", "oben", "unten", "links", "rechts",
                                          "dimetrisch", "trimetrisch", "drehen", "zoom"]},
      "winkel": {"type": "number", "description": "drehen: Winkel in Grad (Standard 45)"},
      "achse": {"type": "string", "enum": ["x", "y", "z"], "description": "drehen: Modellachse durch die Modellmitte (Standard z)"},
      "faktor": {"type": "number", "description": "zoom: >1 vergrößert, <1 verkleinert"},
      "einpassen": {"type": "boolean", "description": "nach Standardansicht/Drehen einpassen (Standard true)"}}, ["art"]),
    ("kamera", t_kamera,
     "Liest die Kamera (Auge, Ziel, Oben-Vektor in mm) und den Modellbereich; mit auge_mm setzt es die Kamera.",
     {"auge_mm": _vec3(), "ziel_mm": _vec3(), "oben": _vec3(), "perspektive": {"type": "boolean"},
      "einpassen": {"type": "boolean"}}, []),
    ("feature_baum", t_baum,
     "Feature-Baum des aktiven Teils (Name, Systemname, Typ, Status) bzw. Bauteile einer Baugruppe.",
     {}, []),
    ("variablen", t_variablen,
     "Variablen und Bemaßungen des aktiven Dokuments (Variablentabelle): Name, Wert (SI), wert_mm bei Längen, Formel.",
     {"filter": {"type": "string", "description": "Namensmuster mit * (Standard *)"}}, []),
    ("variable_setzen", t_variable_setzen,
     "Ändert eine Variable oder Bemaßung (Modell wird neu berechnet). Genau eins von wert_mm, wert (SI) oder formel angeben.",
     {"name": {"type": "string"}, "wert_mm": {"type": "number"}, "wert": {"type": "number", "description": "SI-Einheiten (m, rad)"},
      "formel": {"type": "string", "description": "z. B. \"V263 / 2\" oder \"40 mm\""},
      "anlegen": {"type": "boolean", "description": "neue Benutzervariable mit formel anlegen, falls nicht vorhanden"}}, ["name"]),
    ("extrusion", t_extrusion,
     "Skizziert geschlossene Konturen auf einer Bezugsebene des aktiven Teils und extrudiert sie: als Aufsatz (Material, der erste Aufsatz erzeugt den Körper) oder als Ausschnitt. "
     "Ebenen: xy (Oben), xz (Vorne), yz (Rechts) oder Nummer/Name. Skizzenkoordinaten in mm; auf xz entspricht y der Modell-Z-Achse. "
     "Innere Konturen (z. B. Kreis im Rechteck) werden zu Löchern. Schaltet das Teil bei Bedarf in den geordneten Modus.",
     {"ebene": {"type": "string", "description": "xy | xz | yz | Nummer | Name (Standard xy)"},
      "elemente": {"type": "array", "items": ELEMENT_SCHEMA, "minItems": 1},
      "tiefe_mm": {"type": "number", "description": "Extrusionstiefe in mm"},
      "art": {"type": "string", "enum": ["aufsatz", "ausschnitt"], "description": "Standard aufsatz"},
      "richtung": {"type": "string", "enum": ["normal", "umgekehrt", "symmetrisch"], "description": "normal = in Richtung der Ebenennormale"},
      "durch_alles": {"type": "boolean", "description": "nur Ausschnitt: durch das ganze Teil (tiefe_mm entfällt)"}}, ["elemente"]),
    ("konstanten_suchen", t_konstanten,
     "Durchsucht die Konstanten der Solid-Edge-API (constant.tlb), z. B. Befehls-IDs (filter \"^PartView\") oder Enum-Werte. Ohne filter/aufzaehlung: Liste aller Aufzählungen.",
     {"filter": {"type": "string", "description": "regulärer Ausdruck auf den Konstantennamen"},
      "aufzaehlung": {"type": "string", "description": "regulärer Ausdruck auf den Aufzählungsnamen, z. B. PartCommandConstants"},
      "max": {"type": "integer", "description": "Trefferzahl (Standard 200)"}}, []),
    ("bild", t_bild,
     "Bild der aktuellen 3D-Ansicht (schattiert, mit Kanten) über View.SaveAsImage – zum Prüfen von Modellierungsschritten. "
     "Gibt das Bild direkt zurück; mit pfad wird es zusätzlich gespeichert (.png, .jpg, .bmp). Optional vorher eine Standardansicht wählen.",
     {"ansicht": {"type": "string", "enum": ["einpassen", "iso", "vorne", "hinten", "oben", "unten", "links", "rechts", "dimetrisch", "trimetrisch"],
                  "description": "optional: vorher diese Ansicht einstellen (und einpassen)"},
      "breite": {"type": "integer", "description": "Pixel (Standard 1024)"},
      "hoehe": {"type": "integer", "description": "Pixel (Standard 768)"},
      "pfad": {"type": "string", "description": "optional: Datei, in der das Bild bleiben soll"},
      "zurueckgeben": {"type": "boolean", "description": "Bild im Ergebnis mitschicken (Standard true)"}}, []),
    ("fenster_bild", t_fenster_bild,
     "Bildschirmfoto des ganzen Solid-Edge-Fensters wie es der Benutzer sieht: Menüband, Ansichtswürfel, Hervorhebungen, "
     "offene Dialoge, Menüs und Tooltips. Wird in Edge.exe selbst aufgenommen (beim ersten Aufruf pro Sitzung wird dazu eine kleine DLL geladen). "
     "Für reine Modellbilder ist „bild“ schärfer.",
     {"modus": {"type": "string", "enum": ["bitblt", "print"], "description": "bitblt (Standard): Fensterinhalt kopieren; print: Fenster per WM_PRINT selbst zeichnen lassen"},
      "max_breite": {"type": "integer", "description": "größere Bilder werden auf diese Breite verkleinert (Standard 1600)"},
      "pfad": {"type": "string", "description": "optional: Datei, in der das Bild bleiben soll (.png, .jpg, .bmp)"},
      "zurueckgeben": {"type": "boolean", "description": "Bild im Ergebnis mitschicken (Standard true)"}}, []),
    ("eingabe", t_eingabe,
     "Maus- und Tastatureingaben in Solid Edge wie ein Benutzer (zum Nachstellen von Bedienproblemen). Koordinaten in Pixeln relativ "
     "zur linken oberen Ecke des Fensters, so wie „fenster_bild“ es zeigt (bei verkleinertem Bild bildkoordinaten=true setzen). "
     "Läuft in Edge.exe; Standard SendInput (echte Eingabe inkl. Hover/Capture/Kontextmenü, bewegt den Mauszeiger), Rückfall "
     "methode=post (Nachrichten direkt an das Fenster unter dem Punkt). Mit danach_bild kommt gleich ein Fensterbild zurück.",
     {"aktionen": {"type": "array", "minItems": 1, "items": {"type": "object", "properties": {
         "aktion": {"type": "string", "enum": ["bewegen", "klick", "rechtsklick", "doppelklick", "druecken", "loslassen",
                                               "ziehen", "taste", "text", "warten"]},
         "x": {"type": "number"}, "y": {"type": "number"},
         "x2": {"type": "number", "description": "ziehen: Endpunkt"}, "y2": {"type": "number"},
         "taste": {"type": "string", "enum": ["links", "rechts", "mitte"], "description": "Maustaste (Standard links)"},
         "tasten": {"type": "string", "description": "taste: z. B. esc, enter, entf, f3, strg+z, umschalt+tab, alt+f4"},
         "text": {"type": "string", "description": "text: Zeichen tippen"},
         "ms": {"type": "integer", "description": "warten: Millisekunden"}}, "required": ["aktion"]}},
      "methode": {"type": "string", "enum": ["sendinput", "post"], "description": "Standard sendinput"},
      "bildkoordinaten": {"type": "boolean", "description": "x/y stammen aus dem verkleinerten letzten Fensterbild"},
      "danach_bild": {"type": "boolean", "description": "nach der Eingabe ein Fensterbild zurückgeben"},
      "warten_ms": {"type": "integer", "description": "Wartezeit vor dem Bild (Standard 500)"}}, ["aktionen"]),
    ("com_zugriff", t_com,
     "Expertenwerkzeug: liest oder setzt beliebige Eigenschaften bzw. ruft Methoden der Solid-Edge-API über einen Pfad ab Application auf, "
     "z. B. \"ActiveDocument.Models.Item(1).Body.Volume\" (SI-Einheiten). Argumente in Klammern als JSON-Literale. COM-Objekte werden mit Name/Count/Type zusammengefasst.",
     {"pfad": {"type": "string"},
      "argumente": {"type": "array", "description": "optional: Argumente für das letzte Pfadglied (statt Klammern)"},
      "setzen": {"description": "optional: Wert, der dem letzten Pfadglied (Eigenschaft) zugewiesen wird"}}, ["pfad"]),
]
TOOL_INDEX = {name: fn for name, fn, _, _, _ in TOOLS}

ANLEITUNG = (
    "Steuert Solid Edge 2026 (läuft unter Wine auf diesem Mac) über die COM-Automatisierung. "
    "Längen in mm, Winkel in Grad, sofern nicht anders angegeben. Erst „status“ aufrufen; läuft Solid Edge nicht, „starten“. "
    "Mit „bild“ lässt sich das Ergebnis ansehen; Maße zusätzlich über „kamera“ (Modellbereich), „feature_baum“, „variablen“ "
    "oder „com_zugriff“ prüfen, z. B. ActiveDocument.Models.Item(1).Body.Volume."
)


def tool_liste():
    return [{"name": name, "description": beschr,
             "inputSchema": {"type": "object", "properties": props, "required": pflicht}}
            for name, _, beschr, props, pflicht in TOOLS]


# ---------------------------------------------------------------- MCP / JSON-RPC

PROTOKOLLE = ["2025-06-18", "2025-03-26", "2024-11-05"]


def senden(nachricht):
    sys.stdout.write(json.dumps(nachricht, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def behandeln(msg):
    methode = msg.get("method")
    mid = msg.get("id")
    params = msg.get("params") or {}
    if methode == "initialize":
        gewuenscht = params.get("protocolVersion")
        return {"protocolVersion": gewuenscht if gewuenscht in PROTOKOLLE else PROTOKOLLE[0],
                "capabilities": {"tools": {"listChanged": False}},
                "serverInfo": {"name": "solidedge", "version": VERSION},
                "instructions": ANLEITUNG}
    if methode == "ping":
        return {}
    if methode == "tools/list":
        return {"tools": tool_liste()}
    if methode == "tools/call":
        name = params.get("name")
        fn = TOOL_INDEX.get(name)
        if fn is None:
            return {"content": [{"type": "text", "text": "Unbekanntes Tool: %s" % name}], "isError": True}
        try:
            ergebnis = mac_pfade_ergaenzen(fn(params.get("arguments") or {}))
            bilder = ergebnis.pop("_bilder", []) if isinstance(ergebnis, dict) else []
            text = ergebnis if isinstance(ergebnis, str) else json.dumps(ergebnis, ensure_ascii=False, indent=1)
            return {"content": bilder + [{"type": "text", "text": text}], "isError": False}
        except (BrueckenFehler, KeyError, ValueError, subprocess.SubprocessError, OSError) as e:
            text = "Fehlender Parameter: %s" % e if isinstance(e, KeyError) else str(e)
            return {"content": [{"type": "text", "text": text}], "isError": True}
    if mid is not None:
        raise LookupError(methode)
    return None   # Benachrichtigung (notifications/initialized usw.)


def main():
    if "--test" in sys.argv:   # Schnelltest ohne MCP-Client: python3 solidedge_mcp.py --test status
        i = sys.argv.index("--test")
        name = sys.argv[i + 1]
        args = json.loads(sys.argv[i + 2]) if len(sys.argv) > i + 2 else {}
        try:
            ergebnis = mac_pfade_ergaenzen(TOOL_INDEX[name](args))
            if isinstance(ergebnis, dict) and "_bilder" in ergebnis:
                ergebnis["_bilder"] = ["%s, %d Zeichen Base64" % (b["mimeType"], len(b["data"])) for b in ergebnis["_bilder"]]
            print(json.dumps(ergebnis, ensure_ascii=False, indent=1))
        except BrueckenFehler as e:
            print("Fehler:", e)
            sys.exit(1)
        finally:
            bruecke.stoppen()
        return
    try:
        for zeile in sys.stdin:
            zeile = zeile.strip()
            if not zeile:
                continue
            try:
                msg = json.loads(zeile)
            except ValueError:
                senden({"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": "Parse error"}})
                continue
            if not isinstance(msg, dict) or "method" not in msg:
                continue   # Antworten des Clients auf eigene Anfragen gibt es hier nicht
            try:
                ergebnis = behandeln(msg)
                if msg.get("id") is not None:
                    senden({"jsonrpc": "2.0", "id": msg["id"], "result": ergebnis})
            except LookupError as e:
                senden({"jsonrpc": "2.0", "id": msg.get("id"), "error": {"code": -32601, "message": "Method not found: %s" % e}})
            except Exception as e:   # nie den Server sterben lassen
                log("Fehler:", repr(e))
                senden({"jsonrpc": "2.0", "id": msg.get("id"), "error": {"code": -32603, "message": str(e)}})
    finally:
        bruecke.stoppen()


if __name__ == "__main__":
    main()
