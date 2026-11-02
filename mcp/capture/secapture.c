/* Fensterbild von Solid Edge aus dem eigenen Prozess heraus (unter Wine liegt die Fensteroberfläche pro Prozess,
 * ein fremder Prozess bekommt per BitBlt/PrintWindow nur leere Bilder).
 *
 * Wird per secapture-inject.exe (CreateRemoteThread + LoadLibraryW) in Edge.exe geladen. In jedem anderen Prozess
 * tut die DLL nichts. In Edge.exe pollt ein Hintergrund-Thread alle 100 ms die Anforderungsdatei
 * C:\secapture\anforderung-v<PROTOKOLL> (UTF-8, eine Angabe pro Zeile) und löscht sie als Quittung:
 *
 *   bild                     Hauptfenster samt aller sichtbaren Top-Level-Fenster des Prozesses (Popups, Menüs,
 *   bitblt | print           Dialoge, Tooltips) als 32-Bit-BMP schreiben (erst .tmp, dann umbenannt);
 *   <Zielpfad.bmp>           bei Fehlern entsteht <Zielpfad>.err.
 *
 *   eingabe                  Maus-/Tastatureingaben ausführen; Koordinaten relativ zur linken oberen Ecke des
 *   sendinput | post         Hauptfensters (wie im Fensterbild). Protokoll und „ok“/„fehler: …“ in <Ergebnisdatei>.
 *   <Ergebnisdatei>          Aktionen: move x y | click x y [left|right|middle] | dblclick x y [taste] |
 *   <Aktion> …               down x y [taste] | up x y [taste] | drag x1 y1 x2 y2 [taste] | key strg+z |
 *                            text <Zeichen> | wait <ms>
 *
 * Die Protokollversion steckt im DLL- und im Dateinamen: eine geladene DLL lässt sich nicht ersetzen, eine neue
 * Version wird daneben geladen und nutzt ihre eigene Anforderungsdatei.
 *
 * Bauen: x86_64-w64-mingw32-gcc -shared -O2 -s -o secapture.dll secapture.c -lgdi32 -luser32
 */
#include <windows.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define PROTOKOLL L"2"
#define VERZ L"C:\\secapture"
#define ANFORDERUNG VERZ L"\\anforderung-v" PROTOKOLL
#define MAX_FENSTER 256

static HMODULE modul;

static BOOL ist_edge(void)
{
    wchar_t p[MAX_PATH], *b;
    GetModuleFileNameW(NULL, p, MAX_PATH);
    b = wcsrchr(p, L'\\');
    return _wcsicmp(b ? b + 1 : p, L"Edge.exe") == 0;
}

struct fenster_liste {
    DWORD pid;
    HWND haupt;
    LONGLONG haupt_flaeche;
    BOOL haupt_titel;
    HWND f[MAX_FENSTER];   /* in Z-Reihenfolge, oberstes zuerst (wie EnumWindows) */
    int n;
};

static BOOL CALLBACK sammeln(HWND h, LPARAM lp)
{
    struct fenster_liste *l = (struct fenster_liste *)lp;
    DWORD pid;
    RECT r;
    wchar_t titel[64];
    LONGLONG flaeche;
    BOOL se;

    GetWindowThreadProcessId(h, &pid);
    if (pid != l->pid || !IsWindowVisible(h) || IsIconic(h) || !GetWindowRect(h, &r)) return TRUE;
    if (r.right <= r.left || r.bottom <= r.top) return TRUE;
    if (l->n < MAX_FENSTER) l->f[l->n++] = h;

    /* Hauptfenster: Titel beginnt mit „Solid Edge“, sonst das größte Fenster */
    flaeche = (LONGLONG)(r.right - r.left) * (r.bottom - r.top);
    titel[0] = 0;
    GetWindowTextW(h, titel, 64);
    se = wcsncmp(titel, L"Solid Edge", 10) == 0;
    if ((se && !l->haupt_titel) || (se == l->haupt_titel && flaeche > l->haupt_flaeche)) {
        l->haupt = h;
        l->haupt_flaeche = flaeche;
        l->haupt_titel = se;
    }
    return TRUE;
}

static void fenster_suchen(struct fenster_liste *l)
{
    ZeroMemory(l, sizeof(*l));
    l->pid = GetCurrentProcessId();
    EnumWindows(sammeln, (LPARAM)l);
}

static void fehler_schreiben(const wchar_t *ziel, const char *text)
{
    wchar_t p[MAX_PATH + 8];
    HANDLE f;
    DWORD w;
    wsprintfW(p, L"%s.err", ziel);
    f = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    WriteFile(f, text, lstrlenA(text), &w, NULL);
    CloseHandle(f);
}

/* Ein Fenster in den Speicher-DC an seine Position relativ zum Bildursprung kopieren. */
static void fenster_kopieren(HDC ziel_dc, HWND h, int ox, int oy, BOOL print)
{
    RECT r;
    int w, hh;
    GetWindowRect(h, &r);
    w = r.right - r.left;
    hh = r.bottom - r.top;
    if (print) {
        /* WM_PRINT-Weg: das Fenster zeichnet sich selbst in einen eigenen DC */
        HDC dc = CreateCompatibleDC(ziel_dc);
        HBITMAP bm = CreateCompatibleBitmap(ziel_dc, w, hh);
        HGDIOBJ alt = SelectObject(dc, bm);
        if (PrintWindow(h, dc, 2 /* PW_RENDERFULLCONTENT */) || PrintWindow(h, dc, 0))
            BitBlt(ziel_dc, r.left - ox, r.top - oy, w, hh, dc, 0, 0, SRCCOPY);
        SelectObject(dc, alt);
        DeleteObject(bm);
        DeleteDC(dc);
    } else {
        HDC dc = GetWindowDC(h);
        if (dc) {
            BitBlt(ziel_dc, r.left - ox, r.top - oy, w, hh, dc, 0, 0, SRCCOPY | CAPTUREBLT);
            ReleaseDC(h, dc);
        }
    }
}

static void aufnehmen(const wchar_t *ziel, BOOL print)
{
    struct fenster_liste l;
    RECT rc, r;
    int w, h, i, y;
    BITMAPINFO bi;
    void *bits = NULL;
    HDC scr, mem;
    HBITMAP dib;
    HGDIOBJ alt;
    BITMAPFILEHEADER fh;
    wchar_t tmp[MAX_PATH + 8];
    HANDLE f;
    DWORD geschrieben;

    fenster_suchen(&l);
    if (!l.haupt) { fehler_schreiben(ziel, "kein sichtbares Fenster in Edge.exe"); return; }
    GetWindowRect(l.haupt, &rc);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;

    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = h;   /* positiv = Zeilen von unten nach oben, wie es jede BMP-Software erwartet */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    scr = GetDC(NULL);
    mem = CreateCompatibleDC(scr);
    dib = CreateDIBSection(scr, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, scr);
    if (!dib || !bits) { DeleteDC(mem); fehler_schreiben(ziel, "CreateDIBSection fehlgeschlagen"); return; }
    alt = SelectObject(mem, dib);
    PatBlt(mem, 0, 0, w, h, BLACKNESS);

    /* von unten nach oben zeichnen; das Hauptfenster zuerst, damit Popups/Dialoge darüber liegen */
    fenster_kopieren(mem, l.haupt, rc.left, rc.top, print);
    for (i = l.n - 1; i >= 0; i--) {
        if (l.f[i] == l.haupt) continue;
        GetWindowRect(l.f[i], &r);
        if (r.right <= rc.left || r.left >= rc.right || r.bottom <= rc.top || r.top >= rc.bottom) continue;
        fenster_kopieren(mem, l.f[i], rc.left, rc.top, print);
    }
    GdiFlush();

    /* Alpha auf deckend setzen (GDI lässt es oft 0) */
    for (y = 0; y < w * h; y++) ((BYTE *)bits)[y * 4 + 3] = 255;

    ZeroMemory(&fh, sizeof(fh));
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fh.bfSize = fh.bfOffBits + (DWORD)w * h * 4;
    wsprintfW(tmp, L"%s.tmp", ziel);
    f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        fehler_schreiben(ziel, "Zieldatei nicht beschreibbar");
    } else {
        WriteFile(f, &fh, sizeof(fh), &geschrieben, NULL);
        WriteFile(f, &bi.bmiHeader, sizeof(BITMAPINFOHEADER), &geschrieben, NULL);
        WriteFile(f, bits, (DWORD)w * h * 4, &geschrieben, NULL);
        CloseHandle(f);
        MoveFileExW(tmp, ziel, MOVEFILE_REPLACE_EXISTING);
    }
    SelectObject(mem, alt);
    DeleteObject(dib);
    DeleteDC(mem);
}

/* ---------------------------------------------------------------- Eingabe */

struct protokoll { char text[8192]; int n; };

static void notiz(struct protokoll *p, const char *fmt, ...)
{
    va_list ap;
    if (p->n >= (int)sizeof(p->text) - 256) return;
    va_start(ap, fmt);
    p->n += wvsprintfA(p->text + p->n, fmt, ap);
    va_end(ap);
    p->text[p->n++] = '\n';
    p->text[p->n] = 0;
}

static WORD taste_vk(const char *t)
{
    static const struct { const char *n; WORD vk; } namen[] = {
        {"esc", VK_ESCAPE}, {"escape", VK_ESCAPE}, {"enter", VK_RETURN}, {"return", VK_RETURN}, {"eingabe", VK_RETURN},
        {"tab", VK_TAB}, {"space", VK_SPACE}, {"leer", VK_SPACE}, {"backspace", VK_BACK}, {"rueck", VK_BACK},
        {"delete", VK_DELETE}, {"del", VK_DELETE}, {"entf", VK_DELETE}, {"insert", VK_INSERT}, {"einfg", VK_INSERT},
        {"up", VK_UP}, {"hoch", VK_UP}, {"down", VK_DOWN}, {"runter", VK_DOWN}, {"left", VK_LEFT}, {"links", VK_LEFT},
        {"right", VK_RIGHT}, {"rechts", VK_RIGHT}, {"home", VK_HOME}, {"pos1", VK_HOME}, {"end", VK_END}, {"ende", VK_END},
        {"pgup", VK_PRIOR}, {"bildauf", VK_PRIOR}, {"pgdn", VK_NEXT}, {"bildab", VK_NEXT},
        {"ctrl", VK_CONTROL}, {"strg", VK_CONTROL}, {"control", VK_CONTROL}, {"shift", VK_SHIFT}, {"umschalt", VK_SHIFT},
        {"alt", VK_MENU}, {"win", VK_LWIN}, {"apps", VK_APPS}, {"menu", VK_APPS}, {"kontextmenue", VK_APPS},
    };
    unsigned i;
    if ((t[0] == 'f' || t[0] == 'F') && t[1] >= '1' && t[1] <= '9') {
        int n = atoi(t + 1);
        if (n >= 1 && n <= 24) return (WORD)(VK_F1 + n - 1);
    }
    for (i = 0; i < sizeof(namen) / sizeof(namen[0]); i++)
        if (_stricmp(t, namen[i].n) == 0) return namen[i].vk;
    if (t[0] && !t[1]) {
        SHORT v = VkKeyScanA(t[0]);
        if (v != -1) return (WORD)(v & 0xff);
    }
    return 0;
}

struct ziel_info { HWND haupt; RECT rc; DWORD gui_thread; };

/* Fenster unter einem Bildschirmpunkt (tiefstes sichtbares Kind) */
static HWND fenster_unter(POINT pt)
{
    HWND h = WindowFromPoint(pt), k;
    while (h) {
        POINT c = pt;
        ScreenToClient(h, &c);
        k = ChildWindowFromPointEx(h, c, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
        if (!k || k == h) break;
        h = k;
    }
    return h;
}

static void maus_post(struct protokoll *p, POINT pt, UINT msg, WPARAM wp)
{
    HWND h = fenster_unter(pt);
    POINT c = pt;
    char klasse[64] = "";
    if (!h) { notiz(p, "  post: kein Fenster bei %d,%d", pt.x, pt.y); return; }
    ScreenToClient(h, &c);
    GetClassNameA(h, klasse, sizeof(klasse));
    PostMessageW(h, msg, wp, MAKELPARAM(c.x, c.y));
    notiz(p, "  post 0x%04x an %p (%s) client %d,%d", msg, (void *)h, klasse, c.x, c.y);
}

static void maus_sendinput(struct protokoll *p, POINT pt, DWORD flags)
{
    INPUT in;
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    UINT n;
    ZeroMemory(&in, sizeof(in));
    in.type = INPUT_MOUSE;
    in.mi.dx = MulDiv(pt.x - vx, 65535, vw > 1 ? vw - 1 : 1);
    in.mi.dy = MulDiv(pt.y - vy, 65535, vh > 1 ? vh - 1 : 1);
    in.mi.dwFlags = flags | MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    n = SendInput(1, &in, sizeof(INPUT));
    if (n != 1) notiz(p, "  SendInput fehlgeschlagen (%lu)", GetLastError());
}

static void maus(struct protokoll *p, BOOL post, POINT pt, const char *was, const char *taste)
{
    int t = !taste || !*taste || !_stricmp(taste, "left") || !_stricmp(taste, "links") ? 0
          : (!_stricmp(taste, "right") || !_stricmp(taste, "rechts")) ? 1 : 2;
    static const DWORD si_down[] = { MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_MIDDLEDOWN };
    static const DWORD si_up[] = { MOUSEEVENTF_LEFTUP, MOUSEEVENTF_RIGHTUP, MOUSEEVENTF_MIDDLEUP };
    static const UINT wm_down[] = { WM_LBUTTONDOWN, WM_RBUTTONDOWN, WM_MBUTTONDOWN };
    static const UINT wm_up[] = { WM_LBUTTONUP, WM_RBUTTONUP, WM_MBUTTONUP };
    static const UINT wm_dbl[] = { WM_LBUTTONDBLCLK, WM_RBUTTONDBLCLK, WM_MBUTTONDBLCLK };
    static const WPARAM mk[] = { MK_LBUTTON, MK_RBUTTON, MK_MBUTTON };
    if (!strcmp(was, "move")) {
        if (post) maus_post(p, pt, WM_MOUSEMOVE, 0); else { SetCursorPos(pt.x, pt.y); maus_sendinput(p, pt, 0); }
    } else if (!strcmp(was, "down")) {
        if (post) maus_post(p, pt, wm_down[t], mk[t]); else maus_sendinput(p, pt, si_down[t]);
    } else if (!strcmp(was, "up")) {
        if (post) maus_post(p, pt, wm_up[t], 0); else maus_sendinput(p, pt, si_up[t]);
    } else if (!strcmp(was, "click")) {
        maus(p, post, pt, "move", taste); Sleep(30);
        maus(p, post, pt, "down", taste); Sleep(60);
        maus(p, post, pt, "up", taste);
        if (post && t == 1) {   /* DefWindowProc erzeugt WM_CONTEXTMENU nur bei echter Eingabe sicher */
            HWND h = fenster_unter(pt);
            if (h) { PostMessageW(h, WM_CONTEXTMENU, (WPARAM)h, MAKELPARAM(pt.x, pt.y)); notiz(p, "  post WM_CONTEXTMENU an %p", (void *)h); }
        }
    } else if (!strcmp(was, "dblclick")) {
        if (post) {
            maus_post(p, pt, wm_down[t], mk[t]); maus_post(p, pt, wm_up[t], 0);
            maus_post(p, pt, wm_dbl[t], mk[t]); maus_post(p, pt, wm_up[t], 0);
        } else {
            maus(p, post, pt, "click", taste); Sleep(40); maus(p, post, pt, "click", taste);
        }
    }
}

static void tasten(struct protokoll *p, BOOL post, struct ziel_info *z, char *kombi)
{
    WORD vks[8];
    int n = 0, i;
    char *teil = strtok(kombi, "+");
    while (teil && n < 8) {
        WORD vk = taste_vk(teil);
        if (!vk) { notiz(p, "fehler: unbekannte Taste %s", teil); return; }
        vks[n++] = vk;
        teil = strtok(NULL, "+");
    }
    if (post) {
        GUITHREADINFO gi;
        HWND h;
        ZeroMemory(&gi, sizeof(gi));
        gi.cbSize = sizeof(gi);
        GetGUIThreadInfo(z->gui_thread, &gi);
        h = gi.hwndFocus ? gi.hwndFocus : (gi.hwndActive ? gi.hwndActive : z->haupt);
        for (i = 0; i < n; i++)
            PostMessageW(h, vks[i] == VK_MENU ? WM_SYSKEYDOWN : WM_KEYDOWN, vks[i], 1 | (MapVirtualKeyW(vks[i], 0) << 16));
        for (i = n - 1; i >= 0; i--)
            PostMessageW(h, vks[i] == VK_MENU ? WM_SYSKEYUP : WM_KEYUP, vks[i], 1 | (MapVirtualKeyW(vks[i], 0) << 16) | 0xC0000000);
        notiz(p, "  post %d Taste(n) an Fokusfenster %p", n, (void *)h);
    } else {
        INPUT in[16];
        ZeroMemory(in, sizeof(in));
        for (i = 0; i < n; i++) {
            in[i].type = INPUT_KEYBOARD; in[i].ki.wVk = vks[i]; in[i].ki.wScan = (WORD)MapVirtualKeyW(vks[i], 0);
            in[2 * n - 1 - i].type = INPUT_KEYBOARD; in[2 * n - 1 - i].ki.wVk = vks[i];
            in[2 * n - 1 - i].ki.wScan = in[i].ki.wScan; in[2 * n - 1 - i].ki.dwFlags = KEYEVENTF_KEYUP;
        }
        if (SendInput(2 * n, in, sizeof(INPUT)) != (UINT)(2 * n)) notiz(p, "  SendInput fehlgeschlagen (%lu)", GetLastError());
    }
}

static void text_tippen(struct protokoll *p, BOOL post, struct ziel_info *z, const char *utf8)
{
    wchar_t w[512];
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, 512) - 1, i;
    if (post) {
        GUITHREADINFO gi;
        HWND h;
        ZeroMemory(&gi, sizeof(gi));
        gi.cbSize = sizeof(gi);
        GetGUIThreadInfo(z->gui_thread, &gi);
        h = gi.hwndFocus ? gi.hwndFocus : z->haupt;
        for (i = 0; i < n; i++) PostMessageW(h, WM_CHAR, w[i], 1);
        notiz(p, "  post %d Zeichen an %p", n, (void *)h);
    } else {
        for (i = 0; i < n; i++) {
            INPUT in[2];
            ZeroMemory(in, sizeof(in));
            in[0].type = in[1].type = INPUT_KEYBOARD;
            in[0].ki.wScan = in[1].ki.wScan = w[i];
            in[0].ki.dwFlags = KEYEVENTF_UNICODE;
            in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
            SendInput(2, in, sizeof(INPUT));
            Sleep(10);
        }
    }
}

static void eingabe(char *zeilen, const wchar_t *ergebnis)
{
    struct protokoll *p = (struct protokoll *)calloc(1, sizeof(struct protokoll));
    struct fenster_liste l;
    struct ziel_info z;
    BOOL post;
    char *zeile, *naechste;
    HANDLE f;
    DWORD w;

    fenster_suchen(&l);
    ZeroMemory(&z, sizeof(z));
    z.haupt = l.haupt;
    if (!l.haupt) { notiz(p, "fehler: kein sichtbares Fenster in Edge.exe"); goto ende; }
    GetWindowRect(l.haupt, &z.rc);
    z.gui_thread = GetWindowThreadProcessId(l.haupt, NULL);

    naechste = strchr(zeilen, '\n');
    if (naechste) *naechste++ = 0;
    zeilen[strcspn(zeilen, "\r")] = 0;
    post = !strcmp(zeilen, "post");
    notiz(p, "methode %s, Hauptfenster %p bei %ld,%ld", post ? "post" : "sendinput", (void *)l.haupt, z.rc.left, z.rc.top);
    for (zeile = naechste; zeile && *zeile; zeile = naechste) {
        char befehl[16] = "", a1[256] = "";
        int x1 = 0, y1 = 0, x2 = 0, y2 = 0, n;
        naechste = strchr(zeile, '\n');
        if (naechste) *naechste++ = 0;
        zeile[strcspn(zeile, "\r")] = 0;
        if (!*zeile) continue;
        notiz(p, "> %s", zeile);
        if (!strncmp(zeile, "text ", 5)) { text_tippen(p, post, &z, zeile + 5); continue; }
        n = sscanf(zeile, "%15s", befehl);
        if (n < 1) continue;
        if (!strcmp(befehl, "wait")) { Sleep(atoi(zeile + 5)); continue; }
        if (!strcmp(befehl, "key")) { char k[128]; if (sscanf(zeile + 4, "%127s", k) == 1) tasten(p, post, &z, k); continue; }
        if (!strcmp(befehl, "drag")) {
            POINT a, b;
            int i;
            n = sscanf(zeile + 5, "%d %d %d %d %255s", &x1, &y1, &x2, &y2, a1);
            if (n < 4) { notiz(p, "fehler: drag x1 y1 x2 y2 [taste]"); continue; }
            a.x = z.rc.left + x1; a.y = z.rc.top + y1; b.x = z.rc.left + x2; b.y = z.rc.top + y2;
            maus(p, post, a, "move", a1); Sleep(30);
            maus(p, post, a, "down", a1); Sleep(60);
            for (i = 1; i <= 15; i++) {
                POINT m;
                m.x = a.x + MulDiv(b.x - a.x, i, 15); m.y = a.y + MulDiv(b.y - a.y, i, 15);
                if (post) maus_post(p, m, WM_MOUSEMOVE, n >= 5 && (!_stricmp(a1, "right") || !_stricmp(a1, "rechts")) ? MK_RBUTTON : MK_LBUTTON);
                else maus_sendinput(p, m, 0);
                Sleep(20);
            }
            maus(p, post, b, "up", a1);
            continue;
        }
        n = sscanf(zeile + strlen(befehl), "%d %d %255s", &x1, &y1, a1);
        if (n < 2) { notiz(p, "fehler: %s braucht x y", befehl); continue; }
        {
            POINT pt;
            pt.x = z.rc.left + x1; pt.y = z.rc.top + y1;
            if (!strcmp(befehl, "move") || !strcmp(befehl, "click") || !strcmp(befehl, "dblclick") ||
                !strcmp(befehl, "down") || !strcmp(befehl, "up")) {
                HWND h = fenster_unter(pt);
                char klasse[64] = "";
                if (h) GetClassNameA(h, klasse, sizeof(klasse));
                notiz(p, "  Bildschirm %ld,%ld, Fenster darunter %p (%s)", pt.x, pt.y, (void *)h, klasse);
                maus(p, post, pt, befehl, n >= 3 ? a1 : NULL);
            } else notiz(p, "fehler: unbekannte Aktion %s", befehl);
        }
    }
    notiz(p, "ok");
ende:
    {
        wchar_t tmp[MAX_PATH + 8];
        wsprintfW(tmp, L"%s.tmp", ergebnis);
        f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            WriteFile(f, p->text, p->n, &w, NULL);
            CloseHandle(f);
            MoveFileExW(tmp, ergebnis, MOVEFILE_REPLACE_EXISTING);
        }
    }
    free(p);
}

/* ---------------------------------------------------------------- Anforderungen */

static DWORD WINAPI waechter(LPVOID unbenutzt)
{
    static char puffer[16384];
    wchar_t ziel[MAX_PATH];
    (void)unbenutzt;
    CreateDirectoryW(VERZ, NULL);
    for (;;) {
        HANDLE f;
        DWORD n = 0;
        char *art, *zeile2, *zeile3;
        Sleep(100);
        if (GetFileAttributesW(ANFORDERUNG) == INVALID_FILE_ATTRIBUTES) continue;
        f = CreateFileW(ANFORDERUNG, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
        if (f == INVALID_HANDLE_VALUE) continue;
        ReadFile(f, puffer, sizeof(puffer) - 1, &n, NULL);
        CloseHandle(f);
        DeleteFileW(ANFORDERUNG);   /* Quittung: die Anforderung ist angekommen */
        puffer[n] = 0;
        /* Zeile 1: Art, Zeile 2: Modus/Methode, Zeile 3: Ziel-/Ergebnisdatei, danach Aktionen */
        art = puffer;
        if (!(zeile2 = strchr(art, '\n'))) continue;
        *zeile2++ = 0;
        art[strcspn(art, "\r")] = 0;
        if (!(zeile3 = strchr(zeile2, '\n'))) continue;
        *zeile3++ = 0;
        {
            char *rest = strchr(zeile3, '\n');
            if (rest) *rest++ = 0; else rest = zeile3 + strlen(zeile3);
            zeile3[strcspn(zeile3, "\r")] = 0;
            if (!MultiByteToWideChar(CP_UTF8, 0, zeile3, -1, ziel, MAX_PATH)) continue;
            if (!strcmp(art, "bild")) {
                aufnehmen(ziel, strncmp(zeile2, "print", 5) == 0);
            } else if (!strcmp(art, "eingabe")) {
                /* eingabe() erwartet „Methode\nAktion\n…“ */
                size_t lm = strcspn(zeile2, "\r");
                memmove(rest - lm - 1, zeile2, lm);
                rest[-1] = '\n';
                eingabe(rest - lm - 1, ziel);
            }
        }
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD grund, LPVOID res)
{
    (void)res;
    if (grund == DLL_PROCESS_ATTACH) {
        modul = inst;
        if (!ist_edge()) return TRUE;   /* fremde Prozesse: nichts tun */
        DisableThreadLibraryCalls(inst);
        /* Keine Arbeit unter dem Loader-Lock: nur den Thread anlegen, er läuft erst danach los.
         * Referenz halten, damit die DLL nie entladen wird, solange der Thread läuft. */
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)DllMain, &modul);
        CloseHandle(CreateThread(NULL, 0, waechter, NULL, 0, NULL));
    }
    return TRUE;
}
