// Dauerhafte Brücke zwischen dem macOS-MCP-Server und Solid Edge (COM-Automatisierung, läuft unter Wine).
// Liest pro Zeile eine JSON-Anfrage von stdin und schreibt pro Zeile eine JSON-Antwort nach stdout:
//   {"id":1,"cmd":"status","args":{}}  ->  {"id":1,"ok":true,"result":{...}}  bzw.  {"id":1,"ok":false,"error":"..."}
// Längen in Millimetern, Winkel in Grad (Solid Edge selbst rechnet in Metern und Bogenmaß).
// Übersetzen: csc.exe /nologo /r:System.Web.Extensions.dll /r:Microsoft.CSharp.dll /out:sebridge.exe sebridge.cs
using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Web.Script.Serialization;
using TYPEATTR = System.Runtime.InteropServices.ComTypes.TYPEATTR;
using TYPEKIND = System.Runtime.InteropServices.ComTypes.TYPEKIND;
using VARDESC = System.Runtime.InteropServices.ComTypes.VARDESC;

class Fehler : Exception { public Fehler(string m) : base(m) { } }

class SeBridge
{
    const double MM = 0.001, GRAD = Math.PI / 180.0;
    const int RPC_E_CALL_REJECTED = unchecked((int)0x80010001);
    const int RPC_E_SERVERCALL_RETRYLATER = unchecked((int)0x8001010A);

    static dynamic app;
    static JavaScriptSerializer json = new JavaScriptSerializer { MaxJsonLength = int.MaxValue, RecursionLimit = 200 };
    static Dictionary<string, List<KeyValuePair<string, int>>> enums;   // aus constant.tlb, bei Bedarf geladen

    [STAThread]
    static int Main(string[] a)
    {
        var stdin = new StreamReader(Console.OpenStandardInput(), new UTF8Encoding(false));
        var stdout = new StreamWriter(Console.OpenStandardOutput(), new UTF8Encoding(false)) { AutoFlush = true, NewLine = "\n" };
        stdout.WriteLine(Ascii(json.Serialize(new Dictionary<string, object> { { "bereit", true } })));
        string zeile;
        while ((zeile = stdin.ReadLine()) != null) {
            zeile = zeile.Trim();
            if (zeile.Length == 0) continue;
            object id = null;
            var antwort = new Dictionary<string, object>();
            try {
                var req = json.Deserialize<Dictionary<string, object>>(zeile);
                req.TryGetValue("id", out id);
                string cmd = req.ContainsKey("cmd") ? (string)req["cmd"] : "";
                var args = req.ContainsKey("args") && req["args"] != null ? (Dictionary<string, object>)req["args"] : new Dictionary<string, object>();
                if (cmd == "beenden") { antwort["id"] = id; antwort["ok"] = true; stdout.WriteLine(Ascii(json.Serialize(antwort))); return 0; }
                antwort["result"] = MitWiederholung(() => Ausfuehren(cmd, args));
                antwort["ok"] = true;
            } catch (Exception e) {
                antwort["ok"] = false;
                antwort["error"] = Meldung(e);
            }
            antwort["id"] = id;
            string s;
            try { s = json.Serialize(antwort); }
            catch (Exception e) { s = json.Serialize(new Dictionary<string, object> { { "id", id }, { "ok", false }, { "error", "Antwort nicht serialisierbar: " + e.Message } }); }
            stdout.WriteLine(Ascii(s));
        }
        return 0;
    }

    // Nicht-ASCII als \uXXXX, damit die Pipe unabhängig von Codepages sauber bleibt.
    static string Ascii(string s)
    {
        var sb = new StringBuilder(s.Length);
        foreach (char c in s) { if (c < 128) sb.Append(c); else sb.Append("\\u").Append(((int)c).ToString("x4")); }
        return sb.ToString();
    }

    static string Meldung(Exception e)
    {
        while (e is TargetInvocationException && e.InnerException != null) e = e.InnerException;
        var ce = e as COMException;
        if (ce != null) return "COM-Fehler 0x" + ce.ErrorCode.ToString("X8") + ": " + ce.Message;
        return e.Message;
    }

    // Solid Edge lehnt Aufrufe ab, solange es beschäftigt ist (RPC_E_CALL_REJECTED) → kurz warten und wiederholen.
    // Ist die Verbindung weg (Solid Edge neu gestartet), einmal neu verbinden.
    static object MitWiederholung(Func<object> f)
    {
        bool neuVerbunden = false;
        for (int versuch = 0; ; versuch++) {
            try { return f(); }
            catch (Exception e) {
                Exception inner = e;
                while (inner is TargetInvocationException && inner.InnerException != null) inner = inner.InnerException;
                int hr = inner is COMException ? ((COMException)inner).ErrorCode : 0;
                if ((hr == RPC_E_CALL_REJECTED || hr == RPC_E_SERVERCALL_RETRYLATER) && versuch < 100) { Thread.Sleep(200); continue; }
                bool getrennt = hr == unchecked((int)0x800706BA) || hr == unchecked((int)0x80010108) || hr == unchecked((int)0x800706BE);
                if (getrennt && !neuVerbunden) { app = null; neuVerbunden = true; continue; }
                throw;
            }
        }
    }

    static dynamic App()
    {
        if (app == null) {
            try { app = Marshal.GetActiveObject("SolidEdge.Application"); }
            catch (Exception e) { throw new Fehler("Solid Edge läuft nicht oder ist nicht erreichbar (" + e.Message + ")"); }
        }
        return app;
    }

    static dynamic AktivesDokument()
    {
        dynamic a = App();
        if ((int)a.Documents.Count == 0) throw new Fehler("Kein Dokument offen.");
        return a.ActiveDocument;
    }

    // ---------- Argument-Helfer ----------
    static string S(Dictionary<string, object> a, string k, string def = null)
    {
        object v; return a.TryGetValue(k, out v) && v != null ? Convert.ToString(v, CultureInfo.InvariantCulture) : def;
    }
    static double D(Dictionary<string, object> a, string k, double? def = null)
    {
        object v;
        if (a.TryGetValue(k, out v) && v != null) return Convert.ToDouble(v, CultureInfo.InvariantCulture);
        if (def.HasValue) return def.Value;
        throw new Fehler("Parameter fehlt: " + k);
    }
    static bool B(Dictionary<string, object> a, string k, bool def)
    {
        object v; return a.TryGetValue(k, out v) && v != null ? Convert.ToBoolean(v) : def;
    }
    static object[] Liste(object v)
    {
        if (v is ArrayList) return ((ArrayList)v).ToArray();
        if (v is object[]) return (object[])v;
        throw new Fehler("Liste erwartet");
    }
    static string Muss(Dictionary<string, object> a, string k)
    {
        string s = S(a, k); if (string.IsNullOrEmpty(s)) throw new Fehler("Parameter fehlt: " + k); return s;
    }

    static object Ausfuehren(string cmd, Dictionary<string, object> a)
    {
        switch (cmd) {
            case "ping": return "pong";
            case "status": return Status();
            case "oeffnen": return Oeffnen(a);
            case "neu": return Neu(a);
            case "speichern": return Speichern(a);
            case "exportieren": return Exportieren(a);
            case "schliessen": return Schliessen(a);
            case "aktivieren": return Aktivieren(a);
            case "befehl": return Befehl(a);
            case "ansicht": return Ansicht(a);
            case "kamera": return Kamera(a);
            case "baum": return Baum(a);
            case "variablen": return Variablen(a);
            case "variable_setzen": return VariableSetzen(a);
            case "extrusion": return Extrusion(a);
            case "konstanten": return Konstanten(a);
            case "com": return Com(a);
            case "bild": return Bild(a);
            default: throw new Fehler("Unbekannter Befehl: " + cmd);
        }
    }

    // ---------- Status / Dokumente ----------
    static readonly string[] DokTypen = { "unbekannt", "Teil", "Zeichnung", "Baugruppe", "Blech", "unbekannt", "Schweißkonstruktion",
        "Schweißbaugruppe", "Teil (synchron)", "Blech (synchron)", "Baugruppe (synchron)", "Baugruppen-Viewer", "Teil-Viewer", "Zeichnungs-Viewer" };

    static Dictionary<string, object> DokInfo(dynamic d)
    {
        var r = new Dictionary<string, object>();
        r["name"] = (string)d.Name;
        try { r["pfad"] = (string)d.FullName; } catch { }
        try { int t = (int)d.Type; r["typ"] = t; r["typName"] = t >= 0 && t < DokTypen.Length ? DokTypen[t] : "?"; } catch { }
        try { r["geaendert"] = (bool)d.Dirty; } catch { }
        try { int m = (int)d.ModelingMode; r["modus"] = m == 1 ? "synchron" : m == 2 ? "geordnet" : m.ToString(); } catch { }
        try { r["schreibgeschuetzt"] = (bool)d.ReadOnly; } catch { }
        return r;
    }

    static object Status()
    {
        dynamic a = App();
        var r = new Dictionary<string, object>();
        r["version"] = (string)a.Version;
        try { r["umgebung"] = (string)a.ActiveEnvironment; } catch { }
        try { r["sichtbar"] = (bool)a.Visible; } catch { }
        var docs = new List<object>();
        int n = (int)a.Documents.Count;
        string aktiv = null;
        if (n > 0) { try { aktiv = (string)a.ActiveDocument.Name; } catch { } }
        for (int i = 1; i <= n; i++) {
            var info = DokInfo(a.Documents.Item(i));
            info["aktiv"] = (string)info["name"] == aktiv;
            docs.Add(info);
        }
        r["dokumente"] = docs;
        r["aktivesDokument"] = aktiv;
        return r;
    }

    static object Oeffnen(Dictionary<string, object> a)
    {
        string pfad = Muss(a, "pfad");
        dynamic d = App().Documents.Open(pfad);
        bool geaendert = false;
        try { geaendert = (bool)d.Dirty; } catch { }
        try { App().ActiveWindow.View.Fit(); } catch { }
        try { if (!geaendert) d.Dirty = false; } catch { }   // Einpassen allein soll keine „Speichern?“-Rückfrage auslösen
        return DokInfo(d);
    }

    static object Neu(Dictionary<string, object> a)
    {
        string typ = (S(a, "typ", "teil") ?? "teil").ToLowerInvariant();
        string progId;
        switch (typ) {
            case "teil": case "part": progId = "SolidEdge.PartDocument"; break;
            case "blech": case "sheetmetal": progId = "SolidEdge.SheetMetalDocument"; break;
            case "baugruppe": case "assembly": progId = "SolidEdge.AssemblyDocument"; break;
            case "zeichnung": case "draft": progId = "SolidEdge.DraftDocument"; break;
            default: throw new Fehler("Unbekannter Dokumenttyp: " + typ + " (teil, blech, baugruppe, zeichnung)");
        }
        string vorlage = S(a, "vorlage");
        dynamic d = vorlage != null ? App().Documents.Add(progId, vorlage) : App().Documents.Add(progId);
        string modus = S(a, "modus");
        if (modus != null && (typ == "teil" || typ == "part" || typ == "blech" || typ == "sheetmetal"))
            d.ModelingMode = modus.StartsWith("sync") ? 1 : 2;
        return DokInfo(d);
    }

    static object Speichern(Dictionary<string, object> a)
    {
        dynamic d = AktivesDokument();
        string pfad = S(a, "pfad");
        if (pfad == null) d.Save(); else d.SaveAs(pfad);
        return DokInfo(d);
    }

    static object Exportieren(Dictionary<string, object> a)
    {
        dynamic d = AktivesDokument();
        string pfad = Muss(a, "pfad");
        string ext = Path.GetExtension(pfad).ToLowerInvariant();
        if (ext == ".jt") d.SaveAsJT(pfad);
        else d.SaveCopyAs(pfad);   // Format ergibt sich aus der Endung (.stp/.step, .x_t, .igs, .stl, .pdf, .dxf …)
        var r = new Dictionary<string, object>();
        r["pfad"] = pfad;
        r["existiert"] = File.Exists(pfad);
        if (File.Exists(pfad)) r["bytes"] = new FileInfo(pfad).Length;
        return r;
    }

    static object Schliessen(Dictionary<string, object> a)
    {
        dynamic d = AktivesDokument();
        string name = (string)d.Name;
        d.Close(B(a, "speichern", false));
        return "geschlossen: " + name;
    }

    static object Aktivieren(Dictionary<string, object> a)
    {
        string name = Muss(a, "name");
        dynamic docs = App().Documents;
        for (int i = 1; i <= (int)docs.Count; i++) {
            dynamic d = docs.Item(i);
            if (string.Equals((string)d.Name, name, StringComparison.OrdinalIgnoreCase) ||
                string.Equals((string)d.FullName, name, StringComparison.OrdinalIgnoreCase)) { d.Activate(); return DokInfo(d); }
        }
        throw new Fehler("Dokument nicht offen: " + name);
    }

    // ---------- Befehle / Ansicht ----------
    static object Befehl(Dictionary<string, object> a)
    {
        string name = S(a, "name");
        int id;
        if (name != null && !int.TryParse(name, out id)) {
            int? v = KonstanteNachName(name);
            if (v == null) throw new Fehler("Befehlsname unbekannt: " + name + " (mit 'konstanten' suchen)");
            id = v.Value;
        } else id = name != null ? int.Parse(name) : (int)D(a, "id");
        App().StartCommand(id);
        return new Dictionary<string, object> { { "gestartet", id } };
    }

    // Befehls-IDs der Standardansichten (gleich für Teil, Blech und Baugruppe, siehe constant.tlb *View*View).
    static readonly Dictionary<string, int> Ansichten = new Dictionary<string, int> {
        { "iso", 32810 }, { "vorne", 32811 }, { "rechts", 32812 }, { "oben", 32813 }, { "links", 33109 },
        { "unten", 33110 }, { "hinten", 45079 }, { "dimetrisch", 45078 }, { "trimetrisch", 45077 } };

    static object Ansicht(Dictionary<string, object> a)
    {
        dynamic view = App().ActiveWindow.View;
        string art = (S(a, "art", "einpassen") ?? "einpassen").ToLowerInvariant();
        if (Ansichten.ContainsKey(art)) {
            App().StartCommand(Ansichten[art]);
            App().DoIdle();
        } else if (art == "drehen") {
            double lx, ly, lz, hx, hy, hz;
            view.GetModelRange(out lx, out ly, out lz, out hx, out hy, out hz);
            string achse = (S(a, "achse", "z") ?? "z").ToLowerInvariant();
            view.RotateCamera(D(a, "winkel", 45),   // RotateCamera erwartet Grad (gemessen)
                (lx + hx) / 2, (ly + hy) / 2, (lz + hz) / 2,
                achse == "x" ? 1.0 : 0.0, achse == "y" ? 1.0 : 0.0, achse == "z" ? 1.0 : 0.0);
        } else if (art == "zoom") {
            view.ZoomCamera(D(a, "faktor", 1.5));
        } else if (art != "einpassen") {
            throw new Fehler("Unbekannte Ansicht: " + art + " (iso, vorne, hinten, oben, unten, links, rechts, dimetrisch, trimetrisch, einpassen, drehen, zoom)");
        }
        if (art != "zoom" && B(a, "einpassen", true)) view.Fit();
        view.Update();
        return KameraLesen(view);
    }

    static Dictionary<string, object> KameraLesen(dynamic view)
    {
        double ex, ey, ez, tx, ty, tz, ux, uy, uz, s; bool p;
        view.GetCamera(out ex, out ey, out ez, out tx, out ty, out tz, out ux, out uy, out uz, out p, out s);
        Func<double, double> mm = v => Math.Round(v / MM, 4);
        return new Dictionary<string, object> {
            { "auge_mm", new[] { mm(ex), mm(ey), mm(ez) } }, { "ziel_mm", new[] { mm(tx), mm(ty), mm(tz) } },
            { "oben", new[] { Math.Round(ux, 5), Math.Round(uy, 5), Math.Round(uz, 5) } }, { "perspektive", p }, { "massstabOderWinkel", s } };
    }

    static object Kamera(Dictionary<string, object> a)
    {
        dynamic view = App().ActiveWindow.View;
        if (a.ContainsKey("auge_mm")) {
            var cur = KameraLesen(view);
            Func<string, double[]> vec = k => {
                object v; if (!a.TryGetValue(k, out v) || v == null) return null;
                var l = Liste(v); return new[] { Convert.ToDouble(l[0]), Convert.ToDouble(l[1]), Convert.ToDouble(l[2]) };
            };
            double[] e = vec("auge_mm"), t = vec("ziel_mm") ?? (double[])cur["ziel_mm"], u = vec("oben") ?? (double[])cur["oben"];
            view.SetCamera(e[0] * MM, e[1] * MM, e[2] * MM, t[0] * MM, t[1] * MM, t[2] * MM, u[0], u[1], u[2],
                B(a, "perspektive", (bool)cur["perspektive"]), (double)cur["massstabOderWinkel"]);
            if (B(a, "einpassen", false)) view.Fit();
            view.Update();
        }
        var r = KameraLesen(view);
        double lx, ly, lz, hx, hy, hz;
        try {
            view.GetModelRange(out lx, out ly, out lz, out hx, out hy, out hz);
            r["modellbereich_mm"] = new[] { lx / MM, ly / MM, lz / MM, hx / MM, hy / MM, hz / MM };
        } catch { }
        return r;
    }

    static object Bild(Dictionary<string, object> a)
    {
        dynamic view = App().ActiveWindow.View;
        string pfad = Muss(a, "pfad");
        // Braucht die opengl32-Vorschaltung neben Edge.exe (MESA_WRAPPER=1), sonst nur Hintergrund.
        if (File.Exists(pfad)) File.Delete(pfad);
        if (a.ContainsKey("breite")) view.SaveAsImage(pfad, (int)D(a, "breite"), (int)D(a, "hoehe"));
        else view.SaveAsImage(pfad);
        var r = new Dictionary<string, object> { { "pfad", pfad }, { "existiert", File.Exists(pfad) } };
        if (File.Exists(pfad)) r["bytes"] = new FileInfo(pfad).Length;
        return r;
    }

    // ---------- Feature-Baum / Variablen ----------
    static object Baum(Dictionary<string, object> a)
    {
        dynamic d = AktivesDokument();
        int typ = (int)d.Type;
        var liste = new List<object>();
        if (typ == 3 || typ == 7 || typ == 10) {          // Baugruppe: Bauteile
            dynamic occ = d.Occurrences;
            for (int i = 1; i <= (int)occ.Count; i++) {
                dynamic o = occ.Item(i);
                var e = new Dictionary<string, object> { { "name", (string)o.Name } };
                try { e["datei"] = (string)o.OccurrenceFileName; } catch { }
                try { e["sichtbar"] = (bool)o.Visible; } catch { }
                liste.Add(e);
            }
            return new Dictionary<string, object> { { "bauteile", liste } };
        }
        dynamic feats;
        try { feats = d.DesignEdgebarFeatures; } catch { throw new Fehler("Dieser Dokumenttyp hat keinen Feature-Baum."); }
        for (int i = 1; i <= (int)feats.Count; i++) {
            dynamic f = feats.Item(i);
            var e = new Dictionary<string, object>();
            try { e["name"] = (string)f.EdgebarName; } catch { try { e["name"] = (string)f.Name; } catch { e["name"] = "?"; } }
            try { e["systemName"] = (string)f.SystemName; } catch { }
            try { e["typ"] = EnumName("FeatureTypeConstants", (int)f.Type); } catch { }
            try { object desc; e["status"] = EnumName("FeatureStatusConstants", (int)f.Status(out desc)); } catch { }
            liste.Add(e);
        }
        return new Dictionary<string, object> { { "features", liste } };
    }

    static object Variablen(Dictionary<string, object> a)
    {
        dynamic d = AktivesDokument();
        dynamic vars = d.Variables;
        string filter = S(a, "filter", "*");
        // NamedBy: 2 = Benutzer- und Systemname; VarType: 3 = Bemaßungen und Variablen
        dynamic list = vars.Query(filter, 2, 3, true);
        var r = new List<object>();
        for (int i = 1; i <= (int)list.Count; i++) {
            dynamic v = list.Item(i);
            var e = new Dictionary<string, object>();
            try { e["name"] = (string)vars.GetDisplayName(v); } catch { }
            try { e["systemName"] = (string)vars.GetSystemName(v); } catch { }
            try { e["wert"] = (double)v.Value; } catch { }
            try { e["formel"] = (string)v.Formula; } catch { }
            try { e["einheitentyp"] = (int)v.UnitsType; } catch { }
            try { e["art"] = (int)v.Type; } catch { }
            try { if (e.ContainsKey("einheitentyp") && (int)e["einheitentyp"] == 1) e["wert_mm"] = Math.Round((double)e["wert"] / MM, 6); } catch { }
            r.Add(e);
        }
        return new Dictionary<string, object> { { "hinweis", "wert in SI-Einheiten (m, rad); einheitentyp 1 = Länge" }, { "variablen", r } };
    }

    static object VariableSetzen(Dictionary<string, object> a)
    {
        dynamic d = AktivesDokument();
        dynamic vars = d.Variables;
        string name = Muss(a, "name");
        dynamic list = vars.Query(name, 2, 3, true);
        if ((int)list.Count == 0) {
            if (B(a, "anlegen", false)) { vars.Add(name, Muss(a, "formel")); }
            else throw new Fehler("Variable/Bemaßung nicht gefunden: " + name);
        } else {
            dynamic v = list.Item(1);
            if (a.ContainsKey("formel")) v.Formula = S(a, "formel");
            else if (a.ContainsKey("wert_mm")) v.Value = D(a, "wert_mm") * MM;
            else v.Value = D(a, "wert");
        }
        try { App().DoIdle(); } catch { }
        return Variablen(new Dictionary<string, object> { { "filter", name } });
    }

    // ---------- Modellieren: Skizze auf Ebene + Extrusion (Aufsatz oder Ausschnitt) ----------
    static dynamic Ebene(dynamic d, string ebene)
    {
        dynamic planes = d.RefPlanes;
        int idx;
        switch ((ebene ?? "xy").ToLowerInvariant()) {
            case "xy": case "oben": case "top": idx = 1; break;
            case "yz": case "rechts": case "right": idx = 2; break;
            case "xz": case "vorne": case "front": idx = 3; break;
            default:
                if (!int.TryParse(ebene, out idx)) {
                    for (int i = 1; i <= (int)planes.Count; i++) {
                        dynamic p = planes.Item(i);
                        try { if (string.Equals((string)p.Name, ebene, StringComparison.OrdinalIgnoreCase)) return p; } catch { }
                    }
                    throw new Fehler("Ebene unbekannt: " + ebene + " (xy, yz, xz, Nummer oder Name)");
                }
                break;
        }
        return planes.Item(idx);
    }

    static void Linienzug(dynamic lines, dynamic rel, List<double[]> pkt)
    {
        var obj = new List<dynamic>();
        for (int i = 0; i < pkt.Count; i++) {
            double[] p = pkt[i], q = pkt[(i + 1) % pkt.Count];
            obj.Add(lines.AddBy2Points(p[0] * MM, p[1] * MM, q[0] * MM, q[1] * MM));
        }
        for (int i = 0; i < obj.Count; i++) rel.AddKeypoint(obj[i], 1, obj[(i + 1) % obj.Count], 0);   // igLineEnd → igLineStart
    }

    static object Extrusion(Dictionary<string, object> a)
    {
        dynamic d = AktivesDokument();
        int typ = (int)d.Type;
        if (typ != 1 && typ != 4 && typ != 8 && typ != 9) throw new Fehler("Extrusion geht nur im Teil- oder Blechdokument.");
        try { if ((int)d.ModelingMode != 2) d.ModelingMode = 2; } catch { }   // API-Features brauchen die geordnete Umgebung

        object roh; if (!a.TryGetValue("elemente", out roh) || roh == null) throw new Fehler("Parameter fehlt: elemente");
        var elemente = Liste(roh);
        dynamic ps = d.ProfileSets.Add();
        dynamic profile = ps.Profiles.Add(Ebene(d, S(a, "ebene", "xy")));
        dynamic lines = profile.Lines2d, circles = profile.Circles2d, rel = profile.Relations2d;
        foreach (object o in elemente) {
            var e = (Dictionary<string, object>)o;
            string t = (S(e, "typ", "") ?? "").ToLowerInvariant();
            if (t == "rechteck") {
                double x = D(e, "x", 0), y = D(e, "y", 0), b = D(e, "breite"), h = D(e, "hoehe");
                if (B(e, "zentriert", false)) { x -= b / 2; y -= h / 2; }
                Linienzug(lines, rel, new List<double[]> { new[] { x, y }, new[] { x + b, y }, new[] { x + b, y + h }, new[] { x, y + h } });
            } else if (t == "kreis") {
                circles.AddByCenterRadius(D(e, "x", 0) * MM, D(e, "y", 0) * MM, (e.ContainsKey("durchmesser") ? D(e, "durchmesser") / 2 : D(e, "radius")) * MM);
            } else if (t == "polygon" || t == "linienzug") {
                var pkt = new List<double[]>();
                foreach (object p in Liste(e["punkte"])) { var l = Liste(p); pkt.Add(new[] { Convert.ToDouble(l[0]), Convert.ToDouble(l[1]) }); }
                if (pkt.Count < 3) throw new Fehler("Polygon braucht mindestens 3 Punkte.");
                Linienzug(lines, rel, pkt);
            } else throw new Fehler("Unbekanntes Skizzenelement: " + t + " (rechteck, kreis, polygon)");
        }
        int status = (int)profile.End(1 | 8192);   // igProfileClosed | igProfileAllowNested
        if (status != 0) { try { profile.Delete(); } catch { } throw new Fehler("Profil ungültig (End-Status " + status + "): geschlossene, überschneidungsfreie Konturen nötig."); }

        string richtung = (S(a, "richtung", "normal") ?? "normal").ToLowerInvariant();
        int seite = richtung == "symmetrisch" ? 3 : richtung == "umgekehrt" ? 1 : 2;   // igSymmetric / igLeft / igRight
        string art = (S(a, "art", "aufsatz") ?? "aufsatz").ToLowerInvariant();
        bool durchAlles = B(a, "durch_alles", false);
        double tiefe = durchAlles ? 0 : D(a, "tiefe_mm") * MM;
        dynamic feature;
        dynamic models = d.Models;
        if (art == "aufsatz") {
            if ((int)models.Count == 0) {
                // Erster Körper. AddFiniteExtrudedProtrusion liefert unter Wine nur E_FAIL; die ausführliche Variante
                // geht, braucht aber das Profil-Array als SAFEARRAY(IDispatch) → Roh-Invoke (siehe DispAufruf).
                object N = new DispatchWrapper(null);
                dynamic model = DispAufruf(models, "AddExtrudedProtrusion", 1, new DispArray(profile), 2, 13 /*igFinite*/, seite, tiefe,
                    N, new RefInt(), N, 44, 0.0, 44, 44, 0.0, 0, 44, 44, 0.0, 0.0,      // Richtung 1: keine Schräge/Bombierung
                    44 /*igNone*/, 44, 0.0, N, new RefInt(), N, 44, 0.0, 44, 44, 0.0, 0, 44, 44, 0.0, 0.0);
                feature = model.ExtrudedProtrusions.Item(1);
            }
            else feature = models.Item(1).ExtrudedProtrusions.AddFinite(profile, 2, seite, tiefe);   // ProfileSide igRight: Material innerhalb der Kontur
        } else if (art == "ausschnitt") {
            if ((int)models.Count == 0) throw new Fehler("Ausschnitt braucht einen vorhandenen Körper.");
            dynamic cut = models.Item(1).ExtrudedCutouts;
            // ProfileSide igLeft (1): innerhalb der geschlossenen Kontur schneiden
            feature = durchAlles ? cut.AddThroughAll(profile, 1, seite) : cut.AddFinite(profile, 1, seite, tiefe);
        } else throw new Fehler("Unbekannte Art: " + art + " (aufsatz, ausschnitt)");
        try { profile.Visible = false; } catch { }
        try { App().ActiveWindow.View.Fit(); } catch { }

        var r = new Dictionary<string, object>();
        try { r["feature"] = (string)feature.Name; } catch { }
        try { object desc; r["status"] = EnumName("FeatureStatusConstants", (int)feature.Status(out desc)); r["statusText"] = Convert.ToString(desc); } catch { }
        try { r["koerper"] = (int)d.Models.Count; } catch { }
        try { r["volumen_mm3"] = Math.Round((double)d.Models.Item(1).Body.Volume * 1e9, 3); } catch { }
        try {
            double lx, ly, lz, hx, hy, hz;
            App().ActiveWindow.View.GetModelRange(out lx, out ly, out lz, out hx, out hy, out hz);
            r["modellbereich_mm"] = new[] { lx / MM, ly / MM, lz / MM, hx / MM, hy / MM, hz / MM };
        } catch { }
        return r;
    }

    // ---------- Roh-IDispatch::Invoke für Parameter, die der dynamic-Binder unter Wine nicht richtig übergibt ----------
    // Der Binder macht aus object[] ein SAFEARRAY(VARIANT); Solid Edge erwartet SAFEARRAY(IDispatch) → Typfehler.
    // Byref übergeben stürzt Wines IDispatch-Proxy ab; by value wandelt Wines ITypeInfo::Invoke im Server korrekt um.
    class DispArray { public object[] Elemente; public DispArray(params object[] e) { Elemente = e; } }
    class RefInt { public int Wert; }

    [ComImport, Guid("00020400-0000-0000-C000-000000000046"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IDispatchRoh
    {
        [PreserveSig] int GetTypeInfoCount(out uint c);
        [PreserveSig] int GetTypeInfo(uint i, uint lcid, out IntPtr ti);
        [PreserveSig] int GetIDsOfNames(ref Guid riid, [MarshalAs(UnmanagedType.LPArray, ArraySubType = UnmanagedType.LPWStr)] string[] namen, uint n, uint lcid, out int dispId);
        [PreserveSig] int Invoke(int dispId, ref Guid riid, uint lcid, ushort flags, IntPtr dispParams, IntPtr ergebnis, IntPtr excepInfo, IntPtr argFehler);
    }

    [DllImport("oleaut32.dll")] static extern IntPtr SafeArrayCreateVector(ushort vt, int lbound, uint n);
    [DllImport("oleaut32.dll")] static extern int SafeArrayPutElement(IntPtr psa, int[] idx, IntPtr pv);
    [DllImport("oleaut32.dll")] static extern int VariantClear(IntPtr v);

    static object DispAufruf(object obj, string name, params object[] args)
    {
        const int VS = 24;   // sizeof(VARIANT) unter x64
        var disp = (IDispatchRoh)obj;
        Guid leer = Guid.Empty; int id;
        int hr = disp.GetIDsOfNames(ref leer, new[] { name }, 1, 0, out id);
        if (hr != 0) throw new COMException("Unbekannte Methode " + name, hr);
        int n = args.Length;
        IntPtr va = Marshal.AllocCoTaskMem(VS * Math.Max(n, 1)), ints = Marshal.AllocCoTaskMem(8 * Math.Max(n, 1));
        IntPtr dp = Marshal.AllocCoTaskMem(24), res = Marshal.AllocCoTaskMem(VS), ex = Marshal.AllocCoTaskMem(64), argErr = Marshal.AllocCoTaskMem(4);
        try {
            for (int b = 0; b < VS; b++) Marshal.WriteByte(res, b, 0);
            for (int b = 0; b < 64; b++) Marshal.WriteByte(ex, b, 0);
            for (int i = 0; i < n; i++) {
                IntPtr v = va + VS * (n - 1 - i);   // DISPPARAMS erwartet die Argumente rückwärts
                for (int b = 0; b < VS; b++) Marshal.WriteByte(v, b, 0);
                object a = args[i];
                if (a is DispArray) {
                    var el = ((DispArray)a).Elemente;
                    IntPtr psa = SafeArrayCreateVector(9 /*VT_DISPATCH*/, 0, (uint)el.Length);
                    for (int k = 0; k < el.Length; k++) {
                        IntPtr pd = Marshal.GetIDispatchForObject(el[k]);
                        SafeArrayPutElement(psa, new[] { k }, pd);   // erhöht den Referenzzähler selbst
                        Marshal.Release(pd);
                    }
                    Marshal.WriteInt16(v, (short)(0x2000 | 9));   // VT_ARRAY | VT_DISPATCH, by value
                    Marshal.WriteIntPtr(v + 8, psa);
                } else if (a is RefInt) {
                    Marshal.WriteInt32(ints + 8 * i, ((RefInt)a).Wert);
                    Marshal.WriteInt16(v, (short)(0x4000 | 3));   // VT_BYREF | VT_I4
                    Marshal.WriteIntPtr(v + 8, ints + 8 * i);
                } else Marshal.GetNativeVariantForObject(a, v);
            }
            Marshal.WriteIntPtr(dp, va); Marshal.WriteIntPtr(dp + 8, IntPtr.Zero); Marshal.WriteInt32(dp + 16, n); Marshal.WriteInt32(dp + 20, 0);
            hr = disp.Invoke(id, ref leer, 0, 1 /*DISPATCH_METHOD*/, dp, res, ex, argErr);
            if (hr == unchecked((int)0x80020009)) {   // DISP_E_EXCEPTION
                IntPtr desc = Marshal.ReadIntPtr(ex, 16);
                int scode = Marshal.ReadInt32(ex, 56);
                string text = desc != IntPtr.Zero ? Marshal.PtrToStringBSTR(desc) : "";
                throw new COMException(name + " fehlgeschlagen" + (text.Length > 0 ? ": " + text : ""), scode != 0 ? scode : hr);
            }
            if (hr != 0) throw new COMException(name + " fehlgeschlagen (Argument " + Marshal.ReadInt32(argErr) + ")", hr);
            object r = Marshal.GetObjectForNativeVariant(res);
            for (int i = 0; i < n; i++) if (args[i] is RefInt) ((RefInt)args[i]).Wert = Marshal.ReadInt32(ints + 8 * i);
            return r;
        } finally {
            VariantClear(res);
            for (int i = 0; i < n; i++) if (!(args[i] is RefInt)) VariantClear(va + VS * (n - 1 - i));
            foreach (IntPtr p in new[] { va, ints, dp, res, ex, argErr }) Marshal.FreeCoTaskMem(p);
        }
    }

    // ---------- Konstanten aus constant.tlb (Befehls-IDs, Enums) ----------
    [DllImport("oleaut32.dll", CharSet = CharSet.Unicode)]
    static extern int LoadTypeLibEx(string file, int regKind, out ITypeLib lib);

    static void EnumsLaden()
    {
        if (enums != null) return;
        enums = new Dictionary<string, List<KeyValuePair<string, int>>>();
        string dir = null;
        try { dir = Path.GetDirectoryName((string)App().Path + "\\x"); } catch { }
        string[] kandidaten = { dir != null ? Path.Combine(dir, "constant.tlb") : null,
            @"C:\Program Files\Siemens\Solid Edge 2026\Program\constant.tlb" };
        foreach (string f in kandidaten) {
            if (f == null || !File.Exists(f)) continue;
            ITypeLib lib;
            if (LoadTypeLibEx(f, 0, out lib) != 0) continue;
            for (int i = 0; i < lib.GetTypeInfoCount(); i++) {
                ITypeInfo ti; lib.GetTypeInfo(i, out ti);
                IntPtr pa; ti.GetTypeAttr(out pa);
                var ta = (TYPEATTR)Marshal.PtrToStructure(pa, typeof(TYPEATTR));
                if (ta.typekind == TYPEKIND.TKIND_ENUM) {
                    string en, doc, hf; int hc; ti.GetDocumentation(-1, out en, out doc, out hc, out hf);
                    var l = new List<KeyValuePair<string, int>>();
                    for (int v = 0; v < ta.cVars; v++) {
                        IntPtr pv; ti.GetVarDesc(v, out pv);
                        var vd = (VARDESC)Marshal.PtrToStructure(pv, typeof(VARDESC));
                        string n; ti.GetDocumentation(vd.memid, out n, out doc, out hc, out hf);
                        try { l.Add(new KeyValuePair<string, int>(n, Convert.ToInt32(Marshal.GetObjectForNativeVariant(vd.desc.lpvarValue)))); } catch { }
                        ti.ReleaseVarDesc(pv);
                    }
                    enums[en] = l;
                }
                ti.ReleaseTypeAttr(pa);
            }
            return;
        }
        throw new Fehler("constant.tlb nicht gefunden.");
    }

    static object EnumName(string aufzaehlung, int wert)
    {
        try {
            EnumsLaden();
            List<KeyValuePair<string, int>> l;
            foreach (string en in new[] { aufzaehlung, "ObjectType" })
                if (enums.TryGetValue(en, out l)) foreach (var p in l) if (p.Value == wert) return p.Key;
        } catch { }
        return wert;
    }

    static int? KonstanteNachName(string name)
    {
        EnumsLaden();
        foreach (var kv in enums)
            foreach (var p in kv.Value)
                if (string.Equals(p.Key, name, StringComparison.OrdinalIgnoreCase)) return p.Value;
        return null;
    }

    static object Konstanten(Dictionary<string, object> a)
    {
        EnumsLaden();
        string filter = S(a, "filter", "");
        string aufzaehlung = S(a, "aufzaehlung");
        int max = (int)D(a, "max", 200);
        var rx = new Regex(filter, RegexOptions.IgnoreCase);
        var treffer = new List<object>();
        foreach (var kv in enums) {
            if (aufzaehlung != null && !Regex.IsMatch(kv.Key, aufzaehlung, RegexOptions.IgnoreCase)) continue;
            foreach (var p in kv.Value) {
                if (!rx.IsMatch(p.Key)) continue;
                treffer.Add(new Dictionary<string, object> { { "aufzaehlung", kv.Key }, { "name", p.Key }, { "wert", p.Value } });
                if (treffer.Count >= max) return new Dictionary<string, object> { { "treffer", treffer }, { "abgeschnitten", true } };
            }
        }
        if (filter == "" && aufzaehlung == null) {
            var namen = new List<string>(enums.Keys); namen.Sort();
            return new Dictionary<string, object> { { "aufzaehlungen", namen } };
        }
        return new Dictionary<string, object> { { "treffer", treffer } };
    }

    // ---------- Generischer COM-Zugriff: Pfad wie ActiveDocument.Models.Item(1).Name ----------
    static object Com(Dictionary<string, object> a)
    {
        string pfad = Muss(a, "pfad");
        object cur = App();
        var teile = PfadZerlegen(pfad);
        object args; a.TryGetValue("argumente", out args);
        object wert; bool setzen = a.TryGetValue("setzen", out wert);
        for (int i = 0; i < teile.Count; i++) {
            string name = teile[i].Key; object[] parm = teile[i].Value;
            bool letztes = i == teile.Count - 1;
            if (cur == null) throw new Fehler("null bei " + name);
            if (letztes && setzen) {
                cur.GetType().InvokeMember(name, BindingFlags.SetProperty, null, cur, new[] { wert });
                return "gesetzt";
            }
            if (letztes && args != null) parm = Liste(args);
            cur = Rufen(cur, name, parm);
        }
        return Darstellen(cur, 0);
    }

    static object Rufen(object o, string name, object[] parm)
    {
        var t = o.GetType();
        try { return t.InvokeMember(name, BindingFlags.GetProperty | BindingFlags.InvokeMethod, null, o, parm); }
        catch (TargetInvocationException e) { throw e.InnerException ?? e; }
    }

    static List<KeyValuePair<string, object[]>> PfadZerlegen(string pfad)
    {
        var r = new List<KeyValuePair<string, object[]>>();
        var rx = new Regex(@"\G\.?([A-Za-z_][A-Za-z0-9_]*)(\(([^()]*)\))?");
        int pos = 0;
        while (pos < pfad.Length) {
            var m = rx.Match(pfad, pos);
            if (!m.Success || m.Length == 0) throw new Fehler("Pfad nicht lesbar ab: " + pfad.Substring(pos));
            object[] parm = new object[0];
            if (m.Groups[2].Success) {
                string inner = m.Groups[3].Value.Trim();
                parm = inner.Length == 0 ? new object[0] : Liste(json.DeserializeObject("[" + inner + "]"));
            }
            r.Add(new KeyValuePair<string, object[]>(m.Groups[1].Value, parm));
            pos = m.Index + m.Length;
        }
        return r;
    }

    static object Darstellen(object v, int tiefe)
    {
        if (v == null || v is string || v is bool || v.GetType().IsPrimitive || v is decimal) return v;
        if (v is DateTime) return ((DateTime)v).ToString("o");
        if (v is Array) {
            var l = new List<object>(); foreach (object x in (Array)v) l.Add(Darstellen(x, tiefe + 1)); return l;
        }
        if (Marshal.IsComObject(v)) {
            var r = new Dictionary<string, object> { { "_com", true } };
            foreach (string p in new[] { "Name", "Count", "Type", "DisplayName", "FullName" }) {
                try { r[p] = Darstellen(v.GetType().InvokeMember(p, BindingFlags.GetProperty, null, v, null), tiefe + 1); } catch { }
            }
            return r;
        }
        return v.ToString();
    }
}
