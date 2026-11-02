// Steuert das laufende Solid Edge über seine COM-Automatisierung (SolidEdge.Application).
// Aufruf: seauto.exe info | bild <ausgabe.png> [breite hoehe] | befehl <id>
using System;
using System.Runtime.InteropServices;

class SeAuto
{
    static int Main(string[] a)
    {
        dynamic app;
        try { app = Marshal.GetActiveObject("SolidEdge.Application"); }
        catch (Exception e) { Console.Error.WriteLine("Solid Edge nicht erreichbar: " + e.Message); return 1; }
        string cmd = a.Length > 0 ? a[0] : "info";
        if (cmd == "info") {
            Console.WriteLine("Version: " + app.Version);
            Console.WriteLine("Dokumente: " + app.Documents.Count);
            if (app.Documents.Count > 0) Console.WriteLine("Aktiv: " + app.ActiveDocument.Name);
        } else if (cmd == "bild") {
            dynamic view = app.ActiveWindow.View;
            if (a.Length > 3) view.SaveAsImage(a[1], int.Parse(a[2]), int.Parse(a[3]));
            else view.SaveAsImage(a[1]);
            Console.WriteLine("gespeichert: " + a[1]);
        } else if (cmd == "befehl") {
            app.StartCommand(int.Parse(a[1]));
            Console.WriteLine("gestartet: " + a[1]);
        }
        return 0;
    }
}
