/*
 * opengl32.dll-Vorschaltung für Mesa (WGL): emuliert PFD_DRAW_TO_BITMAP für Speicher-DCs.
 *
 * Solid Edge rendert Ansichtswürfel/Achsenkreuz, SaveAsImage und Vorschaubilder per OpenGL in eine Bitmap
 * (Speicher-DC, ChoosePixelFormat mit PFD_DRAW_TO_BITMAP | PFD_SUPPORT_OPENGL | PFD_GENERIC_FORMAT).
 * Mesas WGL kann nur in Fenster und Pbuffer zeichnen → ChoosePixelFormat liefert 0, SetPixelFormat FALSE.
 *
 * Diese DLL heißt opengl32.dll und liegt neben Edge.exe; Mesas echte opengl32.dll liegt als mesaogl32.dll
 * daneben. Alle Exporte außer den hier abgefangenen werden per .def-Forwarder an mesaogl32 durchgereicht.
 * Für Fenster-DCs ändert sich nichts. Für Speicher-DCs:
 *   - Choose/Describe/Set/GetPixelFormat liefern bzw. merken ein normales Mesa-Fensterformat,
 *   - wglCreateContext legt ein verstecktes Hilfsfenster in Bitmapgröße an und erzeugt den Kontext dort,
 *   - wglMakeCurrent macht das Hilfsfenster aktuell,
 *   - bei glFlush/glFinish/SwapBuffers, beim Wechsel des aktuellen Kontexts und beim Löschen wird das Bild
 *     per glReadPixels in die Bitmap des Speicher-DCs kopiert.
 * Protokoll: Umgebungsvariable MEMDC_GL_DEBUG=1 → stderr.
 */
#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_BGR
#define GL_BGR 0x80E0
#endif
#define GL_PACK_ROW_LENGTH_ 0x0D02
#define GL_PACK_SKIP_ROWS_ 0x0D03
#define GL_PACK_SKIP_PIXELS_ 0x0D04
#define GL_READ_FRAMEBUFFER_ 0x8CA8
#define GL_READ_FRAMEBUFFER_BINDING_ 0x8CAA
#define GL_PIXEL_PACK_BUFFER_ 0x88EB
#define GL_PIXEL_PACK_BUFFER_BINDING_ 0x88ED

#define BITMAP_FLAGS (PFD_DRAW_TO_BITMAP | PFD_SUPPORT_GDI | PFD_GENERIC_FORMAT)

/* ---- echte Funktionen aus mesaogl32.dll ---- */
static HMODULE real;
static int   (WINAPI *r_ChoosePixelFormat)(HDC, const PIXELFORMATDESCRIPTOR *);
static int   (WINAPI *r_DescribePixelFormat)(HDC, int, UINT, PIXELFORMATDESCRIPTOR *);
static int   (WINAPI *r_GetPixelFormat)(HDC);
static BOOL  (WINAPI *r_SetPixelFormat)(HDC, int, const PIXELFORMATDESCRIPTOR *);
static HGLRC (WINAPI *r_CreateContext)(HDC);
static BOOL  (WINAPI *r_DeleteContext)(HGLRC);
static BOOL  (WINAPI *r_MakeCurrent)(HDC, HGLRC);
static HDC   (WINAPI *r_GetCurrentDC)(void);
static HGLRC (WINAPI *r_GetCurrentContext)(void);
static BOOL  (WINAPI *r_SwapBuffers)(HDC);
static BOOL  (WINAPI *r_ShareLists)(HGLRC, HGLRC);
static PROC  (WINAPI *r_GetProcAddress)(LPCSTR);
static void  (APIENTRY *r_glFlush)(void);
static const GLubyte *(APIENTRY *r_glGetString)(GLenum);
static HGLRC (WINAPI *r_CreateLayerContext)(HDC, int);
static BOOL  (WINAPI *r_DescribeLayerPlane)(HDC, int, int, UINT, LAYERPLANEDESCRIPTOR *);
static BOOL  (WINAPI *r_SwapLayerBuffers)(HDC, UINT);
static BOOL  (WINAPI *r_RealizeLayerPalette)(HDC, int, BOOL);
static int   (WINAPI *r_SetLayerPaletteEntries)(HDC, int, int, int, const COLORREF *);
static int   (WINAPI *r_GetLayerPaletteEntries)(HDC, int, int, int, COLORREF *);
static void  (APIENTRY *r_glFinish)(void);
static void  (APIENTRY *r_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
static void  (APIENTRY *r_glGetIntegerv)(GLenum, GLint *);
static void  (APIENTRY *r_glReadBuffer)(GLenum);
static void  (APIENTRY *r_glPixelStorei)(GLenum, GLint);
static GLenum (APIENTRY *r_glGetError)(void);
static HGLRC (WINAPI *r_CreateContextAttribsARB)(HDC, HGLRC, const int *);

static int debug;
#define TRACE(...) do { if (debug) { fprintf(stderr, "memdc_gl: [%lu] ", (unsigned long)GetTickCount()); fprintf(stderr, __VA_ARGS__); fflush(stderr); } } while (0)

/* Diagnose: liefert TRUE für die ersten 3 Aufrufe und danach jeden 500. (Zähler pro Stelle). */
#define OFT_PROTOKOLLIEREN(zaehler) (debug && (++(zaehler) <= 3 || (zaehler) % 500 == 0))

/* Diagnose: jeden Namen nur einmal protokollieren (kleine Liste, nur mit MEMDC_GL_DEBUG). */
static BOOL erstes_mal(const char *name)
{
    static char *namen[1024];
    static int anzahl;
    int i;
    if (!debug || !name) return FALSE;
    for (i = 0; i < anzahl; i++) if (!strcmp(namen[i], name)) return FALSE;
    if (anzahl < 1024 && (namen[anzahl] = malloc(strlen(name) + 1))) strcpy(namen[anzahl++], name);
    return TRUE;
}

/* ---- Verwaltung: Speicher-DCs und Kontexte ---- */
struct memdc
{
    HDC hdc;            /* Speicher-DC der Anwendung */
    int fmt;            /* gemerktes Mesa-Pixelformat */
    HWND wnd;           /* verstecktes Hilfsfenster */
    HDC wdc;            /* dessen DC (CS_OWNDC) */
    int w, h;           /* aktuelle Größe des Hilfsfensters */
    struct memdc *next;
};

struct ctx
{
    HGLRC rc;
    struct memdc *md;
    struct ctx *next;
};

static CRITICAL_SECTION lock;
static struct memdc *memdcs;
static struct ctx *ctxs;
static DWORD tls_cur = TLS_OUT_OF_INDEXES;   /* aktueller Speicher-DC des Threads */

static BOOL is_memdc(HDC hdc)
{
    return hdc && GetObjectType(hdc) == OBJ_MEMDC;
}

static struct memdc *find_memdc(HDC hdc, BOOL create)
{
    struct memdc *m;
    EnterCriticalSection(&lock);
    for (m = memdcs; m; m = m->next) if (m->hdc == hdc) break;
    if (!m && create && (m = calloc(1, sizeof(*m))))
    {
        m->hdc = hdc;
        m->next = memdcs;
        memdcs = m;
    }
    LeaveCriticalSection(&lock);
    return m;
}

static struct ctx *find_ctx(HGLRC rc)
{
    struct ctx *c;
    EnterCriticalSection(&lock);
    for (c = ctxs; c; c = c->next) if (c->rc == rc) break;
    LeaveCriticalSection(&lock);
    return c;
}

static void add_ctx(HGLRC rc, struct memdc *md)
{
    struct ctx *c = calloc(1, sizeof(*c));
    if (!c) return;
    c->rc = rc;
    c->md = md;
    EnterCriticalSection(&lock);
    c->next = ctxs;
    ctxs = c;
    LeaveCriticalSection(&lock);
}

static void remove_ctx(HGLRC rc)
{
    struct ctx **p, *c;
    EnterCriticalSection(&lock);
    for (p = &ctxs; (c = *p); p = &c->next)
        if (c->rc == rc) { *p = c->next; free(c); break; }
    LeaveCriticalSection(&lock);
}

static struct memdc *current_md(void)
{
    return tls_cur == TLS_OUT_OF_INDEXES ? NULL : TlsGetValue(tls_cur);
}

static void set_current_md(struct memdc *md)
{
    if (tls_cur != TLS_OUT_OF_INDEXES) TlsSetValue(tls_cur, md);
}

/* ---- Prozessende (docs/beenden-haenger.md) ----
 * Solid Edge räumt seine OpenGL-Kontexte in statischen Destruktoren auf, also erst in DLL_PROCESS_DETACH von
 * render.dll (JRC::~JRC → uglDeleteContext → wglMakeCurrent(NULL, NULL)). ExitProcess hat zu dem Zeitpunkt alle
 * anderen Threads schon beendet, auch Mesas Arbeits-Threads (Threaded Context / Zink-Flush-Queue). Der Flush beim
 * Lösen des Kontexts wartet dann in cnd_wait auf einen Thread, den es nicht mehr gibt: Edge.exe hängt für immer.
 * Mesa selbst räumt beim Prozessende aus genau diesem Grund nicht auf (DllMain mit lpReserved != NULL).
 * Abhilfe: Sobald ntdll das Prozessende meldet (RtlDllShutdownInProgress), geht von hier aus kein Aufruf mehr an
 * Mesa. wgl*-Aufrufe melden Erfolg, und die GL-Dispatch-Tabelle des Threads wird auf Mesas Leerlauf-Tabelle gesetzt,
 * damit auch die direkt durchgereichten gl*-Funktionen nichts mehr tun. Ein FLS-Callback erledigt das gleich zu
 * Beginn des Prozessendes (ntdll ruft ihn in LdrShutdownProcess vor allen DllMain-Aufrufen). Speicher, Grafik-
 * ressourcen und Fenster gibt das Betriebssystem mit dem Prozess frei. Abschalten: MEMDC_GL_EXIT=0. */
static BOOL exit_fix = TRUE;
static BOOLEAN (WINAPI *p_RtlDllShutdownInProgress)(void);
static void (*p_glapi_set_dispatch)(void *);
static void (*p_glapi_set_context)(void *);
static DWORD fls_ende = FLS_OUT_OF_INDEXES;

static BOOL prozess_endet(void)
{
    return exit_fix && p_RtlDllShutdownInProgress && p_RtlDllShutdownInProgress();
}

/* GL dieses Threads von Mesa lösen, ohne Mesa aufzurufen (nur Mesas Thread-Zeiger in libglapi). */
static void gl_abkoppeln(const char *wo)
{
    static LONG gemeldet;
    if (p_glapi_set_dispatch) p_glapi_set_dispatch(NULL);   /* NULL = Leerlauf-Tabelle */
    if (p_glapi_set_context) p_glapi_set_context(NULL);
    set_current_md(NULL);
    if (!InterlockedExchange(&gemeldet, 1)) TRACE("Prozessende (%s): GL von Mesa abgekoppelt, keine Aufräumaufrufe mehr\n", wo);
}

/* TRUE: Prozess endet, Aufruf nicht an Mesa weitergeben (GL ist dann schon abgekoppelt). */
static BOOL beim_prozessende(const char *wo)
{
    if (!prozess_endet()) return FALSE;
    gl_abkoppeln(wo);
    return TRUE;
}

static void WINAPI fls_ende_cb(void *data)
{
    if (data && prozess_endet()) gl_abkoppeln("FLS");
}

static void prozessende_vorbereiten(void)
{
    const char *e = getenv("MEMDC_GL_EXIT");
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"), glapi = GetModuleHandleW(L"libglapi.dll");
    exit_fix = !(e && *e == '0');
    if (!exit_fix) return;
    p_RtlDllShutdownInProgress = (void *)GetProcAddress(ntdll, "RtlDllShutdownInProgress");
    if (!glapi) glapi = GetModuleHandleW(L"libgallium_wgl.dll");   /* neuere Mesa-Builds ohne libglapi.dll */
    if (glapi)
    {
        p_glapi_set_dispatch = (void *)GetProcAddress(glapi, "_glapi_set_dispatch");
        p_glapi_set_context = (void *)GetProcAddress(glapi, "_glapi_set_context");
    }
    fls_ende = FlsAlloc(fls_ende_cb);
    TRACE("Prozessende-Schutz: RtlDllShutdownInProgress %p, _glapi_set_dispatch %p, FLS %lu\n",
          p_RtlDllShutdownInProgress, p_glapi_set_dispatch, (unsigned long)fls_ende);
}

/* Merkt sich den Thread für das FLS-Callback (nur Threads, die je einen Kontext aktuell gemacht haben). */
static void prozessende_thread_merken(void)
{
    if (fls_ende != FLS_OUT_OF_INDEXES && !FlsGetValue(fls_ende)) FlsSetValue(fls_ende, (void *)1);
}

/* Größe der im Speicher-DC ausgewählten Bitmap (1x1, wenn keine gewählt ist). */
static void bitmap_size(HDC hdc, int *w, int *h)
{
    BITMAP bm;
    HGDIOBJ obj = GetCurrentObject(hdc, OBJ_BITMAP);
    *w = *h = 1;
    if (obj && GetObjectW(obj, sizeof(bm), &bm))
    {
        *w = bm.bmWidth > 0 ? bm.bmWidth : 1;
        *h = bm.bmHeight > 0 ? bm.bmHeight : 1;
    }
}

/* Für Speicher-DCs: Wunschformat auf ein Fensterformat umschreiben (Mesa kennt keine Bitmap-Formate). */
static void to_window_pfd(PIXELFORMATDESCRIPTOR *pfd)
{
    pfd->dwFlags &= ~(BITMAP_FLAGS | PFD_GENERIC_ACCELERATED | PFD_DOUBLEBUFFER_DONTCARE | PFD_STEREO);
    pfd->dwFlags |= PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
    pfd->cAuxBuffers = 0;
}

static int default_format(HDC hdc)
{
    PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd), 1 };
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    return r_ChoosePixelFormat(hdc, &pfd);
}

static LRESULT CALLBACK helper_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT) { ValidateRect(hwnd, NULL); return 0; }
    if (msg == WM_ERASEBKGND) return 1;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static ATOM helper_class(void)
{
    static ATOM atom;
    if (!atom)
    {
        WNDCLASSW wc = { 0 };
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = helper_proc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = L"MemDcGLHelper";
        atom = RegisterClassW(&wc);
    }
    return atom;
}

/* Hilfsfenster anlegen bzw. an die aktuelle Bitmapgröße anpassen. */
static BOOL ensure_helper(struct memdc *md)
{
    PIXELFORMATDESCRIPTOR pfd;
    int w, h;

    bitmap_size(md->hdc, &w, &h);
    if (md->wnd)
    {
        if (w != md->w || h != md->h)
        {
            SetWindowPos(md->wnd, NULL, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            md->w = w;
            md->h = h;
            TRACE("Hilfsfenster %p → %dx%d\n", md->wnd, w, h);
        }
        return TRUE;
    }
    if (!helper_class()) return FALSE;
    md->wnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"MemDcGLHelper", L"", WS_POPUP,
                              -32000, -32000, w, h, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!md->wnd) return FALSE;
    md->wdc = GetDC(md->wnd);
    md->w = w;
    md->h = h;
    if (md->fmt <= 0) md->fmt = default_format(md->wdc);
    memset(&pfd, 0, sizeof(pfd));
    r_DescribePixelFormat(md->wdc, md->fmt, sizeof(pfd), &pfd);
    if (!r_SetPixelFormat(md->wdc, md->fmt, &pfd))
    {
        TRACE("SetPixelFormat(%d) am Hilfsfenster fehlgeschlagen\n", md->fmt);
        return FALSE;
    }
    TRACE("Hilfsfenster %p (%dx%d, Format %d) für Speicher-DC %p\n", md->wnd, w, h, md->fmt, md->hdc);
    return TRUE;
}

/* Gerendertes Bild des aktuellen Kontexts in die Bitmap des Speicher-DCs kopieren. */
static void copy_back(struct memdc *md)
{
    GLint draw_buf = GL_BACK, read_buf = GL_BACK, read_fb = 0, pack_buf = 0;
    GLint align = 4, row_len = 0, skip_rows = 0, skip_px = 0;
    void (APIENTRY *bind_fb)(GLenum, GLuint) = NULL;
    void (APIENTRY *bind_buf)(GLenum, GLuint) = NULL;
    HGDIOBJ obj;
    DIBSECTION ds;
    int w, h;

    if (!md || !md->wnd) return;
    obj = GetCurrentObject(md->hdc, OBJ_BITMAP);
    if (!obj) return;
    bitmap_size(md->hdc, &w, &h);
    if (w > md->w) w = md->w;
    if (h > md->h) h = md->h;

    r_glGetIntegerv(GL_DRAW_BUFFER, &draw_buf);
    r_glGetIntegerv(GL_READ_BUFFER, &read_buf);
    r_glGetIntegerv(GL_PACK_ALIGNMENT, &align);
    r_glGetIntegerv(GL_PACK_ROW_LENGTH_, &row_len);
    r_glGetIntegerv(GL_PACK_SKIP_ROWS_, &skip_rows);
    r_glGetIntegerv(GL_PACK_SKIP_PIXELS_, &skip_px);
    r_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_, &read_fb);
    r_glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING_, &pack_buf);
    r_glGetError(); /* Fehler älterer Kontexte ohne diese Abfragen verwerfen */
    if (read_fb && (bind_fb = (void *)r_GetProcAddress("glBindFramebuffer"))) bind_fb(GL_READ_FRAMEBUFFER_, 0);
    if (pack_buf && (bind_buf = (void *)r_GetProcAddress("glBindBuffer"))) bind_buf(GL_PIXEL_PACK_BUFFER_, 0);
    r_glReadBuffer(draw_buf);
    r_glPixelStorei(GL_PACK_ROW_LENGTH_, 0);
    r_glPixelStorei(GL_PACK_SKIP_ROWS_, 0);
    r_glPixelStorei(GL_PACK_SKIP_PIXELS_, 0);

    GdiFlush();
    if (GetObjectW(obj, sizeof(ds), &ds) == sizeof(ds) && ds.dsBm.bmBits &&
        (ds.dsBm.bmBitsPixel == 32 || ds.dsBm.bmBitsPixel == 24) && ds.dsBmih.biHeight > 0)
    {
        /* DIB-Section unten-nach-oben wie GL: direkt hineinlesen (Zeilen auf 4 Byte ausgerichtet). */
        r_glPixelStorei(GL_PACK_ALIGNMENT, 4);
        r_glReadPixels(0, 0, w, h, ds.dsBm.bmBitsPixel == 32 ? GL_BGRA : GL_BGR, GL_UNSIGNED_BYTE,
                       (BYTE *)ds.dsBm.bmBits + (size_t)(ds.dsBm.bmHeight - h) * ds.dsBm.bmWidthBytes);
    }
    else
    {
        /* Alles andere (DDB, oben-nach-unten, 16/8 Bit): über einen 32-Bit-Puffer und SetDIBitsToDevice. */
        BITMAPINFO bmi = { { sizeof(BITMAPINFOHEADER), w, h, 1, 32, BI_RGB } };
        BYTE *buf = malloc((size_t)w * h * 4);
        if (buf)
        {
            r_glPixelStorei(GL_PACK_ALIGNMENT, 4);
            r_glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, buf);
            SetDIBitsToDevice(md->hdc, 0, 0, w, h, 0, 0, 0, h, buf, &bmi, DIB_RGB_COLORS);
            free(buf);
        }
    }

    r_glPixelStorei(GL_PACK_ALIGNMENT, align);
    r_glPixelStorei(GL_PACK_ROW_LENGTH_, row_len);
    r_glPixelStorei(GL_PACK_SKIP_ROWS_, skip_rows);
    r_glPixelStorei(GL_PACK_SKIP_PIXELS_, skip_px);
    r_glReadBuffer(read_buf);
    if (bind_fb) bind_fb(GL_READ_FRAMEBUFFER_, read_fb);
    if (bind_buf) bind_buf(GL_PIXEL_PACK_BUFFER_, pack_buf);
    TRACE("%dx%d nach Speicher-DC %p kopiert\n", w, h, md->hdc);
}

/* Formatwahl für Speicher-DCs: nur einfach gepufferte RGBA-Fensterformate (die wglDescribePixelFormat als
 * bitmaptauglich meldet), Bewertung wie Mesa (Farbe/Tiefe/Stencil/Alpha) plus Accum – so trifft die Wahl
 * dasselbe Format wie Solid Edges eigene Suche. */
static int choose_memdc_format(HDC hdc, const PIXELFORMATDESCRIPTOR *want)
{
    PIXELFORMATDESCRIPTOR p;
    unsigned best_delta = ~0u;
    int i, n, best = 0;

    n = r_DescribePixelFormat(hdc, 1, sizeof(p), &p);
    for (i = 1; i <= n; i++)
    {
        unsigned d = 0;
        if (!r_DescribePixelFormat(hdc, i, sizeof(p), &p)) continue;
        if (!(p.dwFlags & PFD_DRAW_TO_WINDOW) || !(p.dwFlags & PFD_SUPPORT_OPENGL) ||
            (p.dwFlags & PFD_DOUBLEBUFFER) || p.iPixelType != PFD_TYPE_RGBA)
            continue;
#define BEWERTE(feld, fehlt, zuwenig) \
        if (want->feld && !p.feld) d += fehlt; \
        else if (want->feld > p.feld) d += zuwenig; \
        else if (want->feld < p.feld) d += 1;
        BEWERTE(cColorBits, 10000, 100)
        BEWERTE(cDepthBits, 10000, 200)
        BEWERTE(cStencilBits, 10000, 400)
        BEWERTE(cAlphaBits, 10000, 100)
        BEWERTE(cAccumBits, 1000, 10)
#undef BEWERTE
        if (d < best_delta) { best_delta = d; best = i; }
    }
    return best;
}

#include "bufregion.c"
#include "font_outlines.c"

/* ---- Diagnose: Wege der Solid-Edge-„Regionen“ (Verfahren 3 = glBlitFramebuffer) und Zeichenpuffer ---- */
static void (APIENTRY *r_BlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
static void (APIENTRY *r_glDrawBuffer)(GLenum);
static void (APIENTRY *r_glCopyPixels)(GLint, GLint, GLsizei, GLsizei, GLenum);
static void (APIENTRY *r_glDrawPixels)(GLsizei, GLsizei, GLenum, GLenum, const void *);

/* Vorderpuffer-Zeichnen: Solid Edge zeichnet Einblendungen (Hervorhebung beim Überfahren) direkt in GL_FRONT und
 * ruft kein SwapBuffers. Mesa gibt den Vorderpuffer nur in glFlush/glFinish aus (st_manager_flush_frontbuffer →
 * stw_st_framebuffer_flush_front → zink flush_frontbuffer → GDI). Mit MEMDC_GL_FRONT_FLUSH=1 löst die Vorschaltung
 * diesen Weg selbst aus, sobald in FRONT kopiert/gezeichnet wurde bzw. FRONT wieder verlassen wird. */
static GLenum zeichenpuffer_aktuell = 0x0405;   /* zuletzt per glDrawBuffer gesetzt (GL_BACK als Annahme) */
static BOOL front_schmutzig;
static int front_flush = -1;

static BOOL front_flush_an(void)
{
    if (front_flush < 0)
    {
        const char *e = getenv("MEMDC_GL_FRONT_FLUSH");
        front_flush = e && *e && *e != '0';
        TRACE("Vorderpuffer-Ausgabe nach FRONT-Zeichnen: %s\n", front_flush ? "AN" : "aus");
    }
    return front_flush;
}

static BOOL zeichnet_in_front(void)
{
    return zeichenpuffer_aktuell == 0x0404 || zeichenpuffer_aktuell == 0x0408;
}

/* Nach Zeichnen/Kopieren in FRONT: Vorderpuffer über Mesa ausgeben (Option) bzw. als „schmutzig“ merken. */
/* Diagnose-Zeitreihe: einmal pro Sekunde (nur wenn etwas passiert ist) eine Zeile mit allen GL-Zählern. */
enum { Z_FLUSH, Z_FINISH, Z_SWAP, Z_BLIT, Z_BLIT_STANDARD, Z_DRAWBUF_FRONT, Z_DRAWBUF_BACK, Z_LOGICOP, Z_LOGIC_AN,
       Z_COPYPIX, Z_DRAWPIX, Z_BEGIN_LINIEN, Z_BEGIN_FLAECHEN, Z_BEGIN_PUNKTE, Z_FRONT_OPS, Z_HUD_WUERFEL,
       Z_HUD_WUERFEL_FRONT, Z_ANZAHL };
static const char *const zaehler_name[Z_ANZAHL] = {
    "flush", "finish", "swap", "blit", "blit→Standard-FB", "drawbuf FRONT", "drawbuf BACK", "logicop", "logicop an",
    "copypixels", "drawpixels", "glBegin Linien", "glBegin Flächen", "glBegin Punkte", "Ops in FRONT",
    "glBegin aus HUDCamera", "davon in FRONT" };
static unsigned zaehler[Z_ANZAHL];

static void zusammenfassung(void)
{
    static DWORD zuletzt;
    DWORD jetzt = GetTickCount();
    char zeile[512];
    int i, n = 0, belegt = 0;
    if (!debug || jetzt - zuletzt < 1000) return;
    zuletzt = jetzt;
    for (i = 0; i < Z_ANZAHL; i++)
    {
        if (!zaehler[i]) continue;
        belegt = 1;
        n += snprintf(zeile + n, sizeof(zeile) - n, " %s=%u", zaehler_name[i], zaehler[i]);
        if (n >= (int)sizeof(zeile) - 1) break;
        zaehler[i] = 0;
    }
    if (belegt) TRACE("Aktivität/s:%s\n", zeile);
}

static void nach_front_operation(const char *was)
{
    static unsigned n;
    if (!zeichnet_in_front()) return;
    zaehler[Z_FRONT_OPS]++;
    front_schmutzig = TRUE;
    if (debug && (++n <= 30 || n % 200 == 0)) TRACE("%s in FRONT #%u\n", was, n);
    if (front_flush_an()) { r_glFlush(); front_schmutzig = FALSE; }
}

static void aufrufer(const char *was, void *ret);

static void APIENTRY diag_BlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0,
                                          GLint dx1, GLint dy1, GLbitfield mask, GLenum filter)
{
    static unsigned n;
    zaehler[Z_BLIT]++;
    aufrufer("glBlitFramebuffer", __builtin_return_address(0));
    zusammenfassung();
    if (debug && (++n <= 40 || n % 200 == 0))
    {
        GLint rfb = -1, dfb = -1, rb = 0, db = 0;
        r_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_, &rfb);
        r_glGetIntegerv(0x8CA6 /* GL_DRAW_FRAMEBUFFER_BINDING */, &dfb);
        r_glGetIntegerv(GL_READ_BUFFER, &rb);
        r_glGetIntegerv(GL_DRAW_BUFFER, &db);
        TRACE("glBlitFramebuffer #%u: (%d,%d)-(%d,%d) → (%d,%d)-(%d,%d) Maske %#x, Lese-FB %d/%#x → Zeichen-FB %d/%#x\n",
              n, sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, rfb, rb, dfb, db);
    }
    r_BlitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter);
    {
        GLint dfb = 0;
        r_glGetIntegerv(0x8CA6 /* GL_DRAW_FRAMEBUFFER_BINDING */, &dfb);
        if (!dfb) { zaehler[Z_BLIT_STANDARD]++; nach_front_operation("glBlitFramebuffer"); }
    }
}

static const char *puffer_name(GLenum b);

/* Diagnose: XOR-Zeichnen (glLogicOp / GL_COLOR_LOGIC_OP), z. B. für Auswahlrechteck und Ghost-Linien. */
static void (APIENTRY *r_glLogicOp)(GLenum);
static void (APIENTRY *r_glEnable)(GLenum);
static void (APIENTRY *r_glDisable)(GLenum);

void APIENTRY glLogicOp(GLenum op)
{
    static unsigned n;
    zaehler[Z_LOGICOP]++;
    if (debug && (++n <= 30 || n % 200 == 0))
        TRACE("glLogicOp #%u: %#x (Zeichenpuffer %s)\n", n, op, puffer_name(zeichenpuffer_aktuell));
    r_glLogicOp(op);
}

static void logic_op_protokoll(const char *was, GLenum cap)
{
    static unsigned n;
    if (cap != 0x0BF2 /* GL_COLOR_LOGIC_OP */ && cap != 0x0BF1 /* GL_INDEX_LOGIC_OP */) return;
    if (!strcmp(was, "glEnable")) zaehler[Z_LOGIC_AN]++;
    if (debug && (++n <= 30 || n % 200 == 0))
        TRACE("%s(%s) #%u (Zeichenpuffer %s)\n", was, cap == 0x0BF2 ? "GL_COLOR_LOGIC_OP" : "GL_INDEX_LOGIC_OP", n,
              puffer_name(zeichenpuffer_aktuell));
}

void APIENTRY glEnable(GLenum cap)
{
    logic_op_protokoll("glEnable", cap);
    r_glEnable(cap);
}

void APIENTRY glDisable(GLenum cap)
{
    logic_op_protokoll("glDisable", cap);
    r_glDisable(cap);
}

static void (APIENTRY *r_glBegin)(GLenum);

/* Aufrufer-Zuordnung: jede neue Aufrufstelle (Modul+RVA) einmal protokollieren, mit aktuellem Zeichenpuffer.
 * Visual.dll RVA 0x1e05e0–0x1e6300 ist HUDCamera::Display (Ansichtswürfel), siehe docs/rechteck-und-wuerfel.md. */
static void aufrufer(const char *was, void *ret)
{
    static struct { HMODULE mod; DWORD rva; } bekannt[512];
    static unsigned anzahl;
    HMODULE mod = NULL;
    char pfad[MAX_PATH], *name;
    DWORD rva;
    unsigned i;
    if (!debug) return;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)ret, &mod) || !mod)
        return;
    rva = (DWORD)((char *)ret - (char *)mod);
    for (i = 0; i < anzahl; i++)
        if (bekannt[i].mod == mod && bekannt[i].rva == rva) return;
    if (anzahl >= sizeof(bekannt) / sizeof(bekannt[0])) return;
    bekannt[anzahl].mod = mod;
    bekannt[anzahl].rva = rva;
    anzahl++;
    if (!GetModuleFileNameA(mod, pfad, sizeof(pfad))) strcpy(pfad, "?");
    name = strrchr(pfad, '\\');
    TRACE("Aufrufer %s: %s+%#lx (Zeichenpuffer %s)\n", was, name ? name + 1 : pfad, (unsigned long)rva,
          puffer_name(zeichenpuffer_aktuell));
}

static int ist_hudcamera(void *ret)
{
    static char *visual;
    if (!visual) visual = (char *)GetModuleHandleA("Visual.dll");
    return visual && (char *)ret >= visual + 0x1e05e0 && (char *)ret < visual + 0x1e6300;
}

void APIENTRY glBegin(GLenum mode)
{
    if (mode == GL_POINTS) zaehler[Z_BEGIN_PUNKTE]++;
    else if (mode == GL_LINES || mode == GL_LINE_LOOP || mode == GL_LINE_STRIP) zaehler[Z_BEGIN_LINIEN]++;
    else zaehler[Z_BEGIN_FLAECHEN]++;
    if (debug && ist_hudcamera(__builtin_return_address(0)))
    {
        zaehler[Z_HUD_WUERFEL]++;
        if (zeichnet_in_front()) zaehler[Z_HUD_WUERFEL_FRONT]++;
    }
    aufrufer("glBegin", __builtin_return_address(0));
    zusammenfassung();
    r_glBegin(mode);
}

void APIENTRY glCopyPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum type)
{
    zaehler[Z_COPYPIX]++;
    aufrufer("glCopyPixels", __builtin_return_address(0));
    r_glCopyPixels(x, y, w, h, type);
    nach_front_operation("glCopyPixels");
}

void APIENTRY glDrawPixels(GLsizei w, GLsizei h, GLenum format, GLenum type, const void *pixels)
{
    zaehler[Z_DRAWPIX]++;
    r_glDrawPixels(w, h, format, type, pixels);
    nach_front_operation("glDrawPixels");
}

static const char *puffer_name(GLenum b)
{
    switch (b)
    {
    case 0x0404: return "FRONT";
    case 0x0405: return "BACK";
    case 0x0408: return "FRONT_AND_BACK";
    case 0: return "NONE";
    default: return b >= 0x8CE0 && b < 0x8D00 ? "COLOR_ATTACHMENT" : "andere";
    }
}

void APIENTRY glDrawBuffer(GLenum b)
{
    if (b == 0x0404 || b == 0x0408) zaehler[Z_DRAWBUF_FRONT]++;
    else if (b == 0x0405) zaehler[Z_DRAWBUF_BACK]++;
    aufrufer(b == 0x0404 || b == 0x0408 ? "glDrawBuffer(FRONT)" : "glDrawBuffer", __builtin_return_address(0));
    zusammenfassung();
    static unsigned n, front;
    if (b == 0x0404 || b == 0x0408) front++;
    if (debug && (++n <= 20 || n % 500 == 0 || ((b == 0x0404 || b == 0x0408) && front <= 20)))
        TRACE("glDrawBuffer #%u: %s (%#x), davon FRONT bisher %u\n", n, puffer_name(b), b, front);
    if (zeichnet_in_front() && b != 0x0404 && b != 0x0408 && front_flush_an())
    {
        /* FRONT wird verlassen: alles bisher in FRONT Gezeichnete ausgeben (Dreiecke, Linien usw. sieht die
         * Vorschaltung nicht einzeln). */
        r_glFlush();
        front_schmutzig = FALSE;
    }
    r_glDrawBuffer(b);
    zeichenpuffer_aktuell = b;
}

void APIENTRY glReadBuffer(GLenum b)
{
    static unsigned n;
    if (debug && (++n <= 20 || n % 500 == 0)) TRACE("glReadBuffer #%u: %s (%#x)\n", n, puffer_name(b), b);
    r_glReadBuffer(b);
}

/* ---- abgefangene Exporte ---- */
int WINAPI wglChoosePixelFormat(HDC hdc, const PIXELFORMATDESCRIPTOR *ppfd)
{
    PIXELFORMATDESCRIPTOR pfd;
    int fmt;

    /* Fenster-DCs unverändert an Mesa (Solid Edge sucht dort selbst über DescribePixelFormat). */
    if (!ppfd || !is_memdc(hdc)) return r_ChoosePixelFormat(hdc, ppfd);

    /* Mesa verwirft Anfragen mit nVersion != 1 (Solid Edge schickt 0) und wertet Accum nicht aus; daher
     * eigene Auswahl unter den als bitmaptauglich gemeldeten (einfach gepufferten) Formaten. */
    pfd = *ppfd;
    fmt = choose_memdc_format(hdc, &pfd);
    if (fmt <= 0)
    {
        to_window_pfd(&pfd);
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.iPixelType = PFD_TYPE_RGBA;
        fmt = r_ChoosePixelFormat(hdc, &pfd);
    }
    if (fmt <= 0) fmt = default_format(hdc);   /* bei Speicher-DCs nie 0: nächstbestes Format */
    TRACE("ChoosePixelFormat(Speicher-DC %p, Flags %#lx, Farbe %u, Tiefe %u, Stencil %u) → %d\n", hdc,
          (unsigned long)ppfd->dwFlags, ppfd->cColorBits, ppfd->cDepthBits, ppfd->cStencilBits, fmt);
    return fmt;
}

int WINAPI wglDescribePixelFormat(HDC hdc, int fmt, UINT size, PIXELFORMATDESCRIPTOR *pfd)
{
    int ret = r_DescribePixelFormat(hdc, fmt, size, pfd);
    /* Für Speicher-DCs: einfach gepufferte Fensterformate als Bitmap-tauglich ausweisen, damit die eigene
     * Formatsuche der Anwendung (sie liest DescribePixelFormat selbst aus) fündig wird. */
    if (ret && pfd && size >= sizeof(*pfd) && is_memdc(hdc) &&
        (pfd->dwFlags & PFD_DRAW_TO_WINDOW) && !(pfd->dwFlags & PFD_DOUBLEBUFFER))
        pfd->dwFlags |= BITMAP_FLAGS;
    return ret;
}

int WINAPI wglGetPixelFormat(HDC hdc)
{
    struct memdc *md;
    if (is_memdc(hdc) && (md = find_memdc(hdc, FALSE)) && md->fmt > 0) return md->fmt;
    return r_GetPixelFormat(hdc);
}

BOOL WINAPI wglSetPixelFormat(HDC hdc, int fmt, const PIXELFORMATDESCRIPTOR *pfd)
{
    struct memdc *md;

    if (!is_memdc(hdc)) return r_SetPixelFormat(hdc, fmt, pfd);
    if (fmt <= 0) return FALSE;
    if (!(md = find_memdc(hdc, TRUE))) return FALSE;
    if (md->fmt > 0 && md->fmt != fmt) return FALSE;   /* wie unter Windows: nur einmal */
    md->fmt = fmt;
    TRACE("SetPixelFormat(Speicher-DC %p, %d)\n", hdc, fmt);
    return TRUE;
}

static HGLRC create_on_memdc(HDC hdc, HGLRC share, const int *attribs)
{
    struct memdc *md = find_memdc(hdc, TRUE);
    HGLRC rc;

    if (!md || !ensure_helper(md)) return NULL;
    if (attribs && r_CreateContextAttribsARB)
    {
        rc = r_CreateContextAttribsARB(md->wdc, share, attribs);
        if (share) TRACE("CreateContextAttribsARB(Speicher-DC %p, Share %p) → %p\n", hdc, share, rc);
    }
    else
        rc = r_CreateContext(md->wdc);
    if (rc) add_ctx(rc, md);
    TRACE("Kontext %p auf Speicher-DC %p (Hilfsfenster %p)\n", rc, hdc, md->wnd);
    return rc;
}

HGLRC WINAPI wglCreateContext(HDC hdc)
{
    if (!is_memdc(hdc))
    {
        HGLRC rc = r_CreateContext(hdc);
        if (!rc) TRACE("wglCreateContext(Fenster-DC %p, Format %d) fehlgeschlagen\n", hdc, r_GetPixelFormat(hdc));
        return rc;
    }
    return create_on_memdc(hdc, NULL, NULL);
}

static HGLRC WINAPI my_CreateContextAttribsARB(HDC hdc, HGLRC share, const int *attribs)
{
    if (!is_memdc(hdc))
    {
        HGLRC rc = r_CreateContextAttribsARB(hdc, share, attribs);
        if (!rc || share) TRACE("CreateContextAttribsARB(Fenster-DC %p, Share %p) → %p\n", hdc, share, rc);
        return rc;
    }
    return create_on_memdc(hdc, share, attribs);
}

/* Spur (a), HUD-Bedingung: view.dvx 0x19b64 holt den HUD-Controller nur bei Visual!JGetBufferRegionLevel(Merkmale) > 1.
 * MEMDC_GL_HUD_BRL=log  ersetzt die Funktion durch eine gleichwertige Nachbildung, die Argument, Ergebnis und
 *                       Aufrufer protokolliert (Visual 0x44c680: Stufe 2 bei Bit 16 und ENABLE_BUFFER_REGIONS,
 *                       sonst 1+Bit 10 bei (f & 0x10010000) == 0x10000, sonst 0).
 * MEMDC_GL_HUD_BRL=force wie log, liefert aber für den Aufruf aus view.dvx (Rücksprung RVA 0x19b6a) mindestens 2. */
static int hud_brl_modus;   /* 0 aus, 1 log, 2 force */
static volatile DWORD brl_renderer_merkmale;   /* zuletzt von render.dll an JGetBufferRegionLevel übergeben (mit Bit 16) */
static DWORD (*r_JGetDebugBOOL)(const char *, int);

static int WINAPI ersatz_JGetBufferRegionLevel(DWORD f)
{
    void *ret = __builtin_return_address(0);
    HMODULE mod = NULL;
    char pfad[MAX_PATH] = "?", *name;
    DWORD rva = 0;
    int stufe, aus_view = 0;
    static unsigned n;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)ret, &mod) && mod)
    {
        rva = (DWORD)((char *)ret - (char *)mod);
        GetModuleFileNameA(mod, pfad, sizeof(pfad));
    }
    name = strrchr(pfad, '\\');
    name = name ? name + 1 : pfad;
    if (r_JGetDebugBOOL && r_JGetDebugBOOL("ENABLE_BUFFER_REGIONS", 1) && (f & 0x10000)) stufe = 2;
    else if ((f & 0x10010000) == 0x10000) stufe = 1 + ((f >> 10) & 1);
    else stufe = 0;
    aus_view = !_stricmp(name, "view.dvx") && rva == 0x19b6a;
    if (!_stricmp(name, "render.dll") && (f & 0x10000)) brl_renderer_merkmale = f;
    if (aus_view || ++n <= 40 || n % 500 == 0)
        if (debug) fprintf(stderr, "memdc_gl: [%lu] JGetBufferRegionLevel(%#lx) = %d, Aufrufer %s+%#lx%s\n",
                (unsigned long)GetTickCount(), (unsigned long)f, stufe, name, (unsigned long)rva,
                aus_view ? (hud_brl_modus == 2 && stufe < 2 ? " → HUD-Bedingung, erzwungen 2" : " → HUD-Bedingung") : "");
    if (aus_view && hud_brl_modus == 2 && stufe < 2) stufe = 2;
    return stufe;
}

static void hud_hooks_einrichten(void)
{
    static int erledigt;
    HMODULE visual;
    unsigned char *ziel, code[12];
    DWORD alt;
    const char *e;
    if (erledigt) return;
    e = getenv("MEMDC_GL_HUD_BRL");
    if (!e || !*e || *e == '0') { erledigt = 1; return; }
    visual = GetModuleHandleA("Visual.dll");
    if (!visual) return;   /* später erneut versuchen */
    erledigt = 1;
    hud_brl_modus = !strcmp(e, "force") ? 2 : 1;
    r_JGetDebugBOOL = (void *)GetProcAddress(visual, "?JGetDebugBOOL@@YAKPEBDH@Z");
    ziel = (unsigned char *)GetProcAddress(visual, "?JGetBufferRegionLevel@@YAHK@Z");
    if (!ziel) { if (debug) fprintf(stderr, "memdc_gl: JGetBufferRegionLevel nicht gefunden\n"); return; }
    code[0] = 0x48; code[1] = 0xB8;                     /* mov rax, imm64 */
    *(void **)(code + 2) = (void *)ersatz_JGetBufferRegionLevel;
    code[10] = 0xFF; code[11] = 0xE0;                   /* jmp rax */
    if (!VirtualProtect(ziel, sizeof(code), PAGE_EXECUTE_READWRITE, &alt)) return;
    memcpy(ziel, code, sizeof(code));
    VirtualProtect(ziel, sizeof(code), alt, &alt);
    FlushInstructionCache(GetCurrentProcess(), ziel, sizeof(code));
    if (debug) fprintf(stderr, "memdc_gl: HUD-Bedingung: Visual!JGetBufferRegionLevel %p ersetzt (Modus %s)\n", ziel,
            hud_brl_modus == 2 ? "force" : "log");
}


/* Spur (a), Fall 3: JView-Felder protokollieren. view.dvx JView-vtable (RVA 0x5ee18) Slot 16 (+0x80) = 0x19740 ist die
 * Funktion mit der HUD-Abfrage (0x19b0b). MEMDC_GL_HUD_VIEW=1 ersetzt den vtable-Eintrag durch einen Stub, der vor dem
 * Original die Felder liest: +0x90 (Ansicht), +0x1d8, +0x4d8, +0x498 (HRESULT), +0x4d0 (Controller), +0x48c
 * (Merkmale), +0x490 (Attach-Cookie), +0x630 (Show) sowie die Ansichtsart aus (+0x90)->vtable+0x48. */
void *jview_orig_80;
void jview_80_stub(void);
__asm__(".globl jview_80_stub\n"
        "jview_80_stub:\n"
        "  push %rcx\n  push %rdx\n  push %r8\n  push %r9\n"
        "  sub $0x28, %rsp\n"
        "  call jview_felder_protokoll\n"
        "  add $0x28, %rsp\n"
        "  pop %r9\n  pop %r8\n  pop %rdx\n  pop %rcx\n"
        "  jmp *jview_orig_80(%rip)\n");

static int jview_fix;
static char *view_basis;   /* view.dvx */
typedef void *(*fn_b7e0)(void *, int);   /* view.dvx 0xb7e0: Ansichten-Tabelle; 0 = suchen, 1 = eintragen, -1 = austragen */

void jview_felder_protokoll(char *jv)
{
    static char zuletzt[160];
    static unsigned n, fix_meldungen;
    char zeile[160];
    void *eingetragen = ((fn_b7e0)(view_basis + 0xb7e0))(jv, 0);   /* gleicher Aufruf wie view 0x1982c */
    if (!eingetragen && jview_fix)
    {
        /* Eintragung wie im JView-Konstruktor (view 0xfb5b: 0xb7e0(this, 1)); ging dort wegen der Wine-LockCount-
         * Abfrage (siehe jview_hook_einrichten) ins Leere. */
        void *r = ((fn_b7e0)(view_basis + 0xb7e0))(jv, 1);
        void *neu = ((fn_b7e0)(view_basis + 0xb7e0))(jv, 0);
        if (debug) fprintf(stderr, "memdc_gl: [%lu] fix: JView %p nachgetragen (eintragen → %p, suchen → %p)\n",
                (unsigned long)GetTickCount(), jv, r, neu);
        eingetragen = neu;
    }
    if (jview_fix && *(void **)(jv + 0x90) && !*(unsigned *)(jv + 0x48c))
    {
        /* wie view 0x198f8–0x19915: (+0xe8)->vtable+0x30(&+0x48c) = JCoreView::IJRenderSetupImp::GetSupportedFeatures */
        void **setup = *(void ***)(jv + 0xe8);
        long hr = -1;
        unsigned wert = 0;
        if (setup)
        {
            typedef long (*fn30)(void *, unsigned *);
            hr = ((fn30)((void **)*setup)[0x30 / 8])(setup, &wert);
        }
        if (++fix_meldungen <= 10)
            if (debug) fprintf(stderr, "memdc_gl: [%lu] fix: GetSupportedFeatures(+e8=%p) → hr %#lx, Merkmale %#x; render.dll meldet %#lx\n",
                    (unsigned long)GetTickCount(), (void *)setup, (unsigned long)hr, wert,
                    (unsigned long)brl_renderer_merkmale);
        if (hr >= 0 && wert) *(unsigned *)(jv + 0x48c) = wert;
        else if (brl_renderer_merkmale)
        {
            *(unsigned *)(jv + 0x48c) = brl_renderer_merkmale;
            if (fix_meldungen <= 10)
                if (debug) fprintf(stderr, "memdc_gl: fix: JView %p +0x48c ← %#lx (Merkmale des Renderers)\n", jv,
                        (unsigned long)brl_renderer_merkmale);
        }
    }
    void **ansicht = *(void ***)(jv + 0x90);
    long art = -1;
    if (!debug) return;
    if (ansicht)
    {
        /* wie view 0x19929–0x19956: (+0x90)->vtable+0x48(&art, &unk) */
        void *unk = NULL;
        typedef long (*fn48)(void *, long *, void **);
        long hr = ((fn48)((void **)*ansicht)[0x48 / 8])(ansicht, &art, &unk);
        if (unk) ((unsigned long (*)(void *))((void **)*(void **)unk)[2])(unk);
        if (hr < 0) art = -2;
    }
    snprintf(zeile, sizeof(zeile), "b7e0=%p +90=%p art=%ld +1d8=%u +4d8=%u +498=%#x +4d0=%p +48c=%#x +490=%p show=%u",
             eingetragen, (void *)ansicht, art, *(unsigned *)(jv + 0x1d8), *(unsigned *)(jv + 0x4d8), *(unsigned *)(jv + 0x498),
             *(void **)(jv + 0x4d0), *(unsigned *)(jv + 0x48c), *(void **)(jv + 0x490), *(unsigned *)(jv + 0x630));
    n++;
    if (strcmp(zeile, zuletzt) || n % 200 == 0)
    {
        strcpy(zuletzt, zeile);
        if (debug) fprintf(stderr, "memdc_gl: [%lu] JView %p vfunc+0x80 #%u: %s\n", (unsigned long)GetTickCount(), jv, n, zeile);
    }
}

static void jview_hook_einrichten(void)
{
    static int erledigt;
    const char *e;
    HMODULE view;
    void **slot;
    DWORD alt;
    if (erledigt) return;
    e = getenv("MEMDC_GL_HUD_VIEW");
    if (!e || !*e || *e == '0') { erledigt = 1; return; }
    view = GetModuleHandleA("view.dvx");
    if (!view) return;   /* view.dvx wird erst mit dem Dokument geladen; beim nächsten wglMakeCurrent erneut */
    erledigt = 1;
    view_basis = (char *)view;
    jview_fix = !strcmp(e, "fix");
    if (jview_fix)
    {
        /* Ursache: view.dvx prüft nach EnterCriticalSection(&lock+8) das Feld lock+0x10 = CRITICAL_SECTION.LockCount
         * auf ≠ 0 (0xb50c, 0xb84c). Windows ab Vista: gehaltene Sperre → LockCount = -2 (≠ 0). Wine (NT4-Semantik):
         * gehaltene Sperre → LockCount = 0 → 0xb7e0 liefert immer NULL, JView wird nie eingetragen/gefunden.
         * Änderung: den bedingten Sprung (je, 6 Byte) an beiden Stellen durch NOPs ersetzen = Windows-Verhalten. */
        static const unsigned stellen[2] = { 0xb510, 0xb850 };
        static const unsigned char erwartet[2][6] = { { 0x0f, 0x84, 0x3b, 0x01, 0x00, 0x00 },
                                                      { 0x0f, 0x84, 0x3b, 0x01, 0x00, 0x00 } };
        int k;
        for (k = 0; k < 2; k++)
        {
            unsigned char *ziel = (unsigned char *)view + stellen[k];
            DWORD alt2;
            if (memcmp(ziel - 4, "\x83\x78\x10\x00", 4) || memcmp(ziel, erwartet[k], 6))
            {
                if (debug) fprintf(stderr, "memdc_gl: fix: view.dvx+%#x passt nicht, nicht geändert\n", stellen[k]);
                continue;
            }
            if (!VirtualProtect(ziel, 6, PAGE_EXECUTE_READWRITE, &alt2)) continue;
            memcpy(ziel, "\x66\x0f\x1f\x44\x00\x00", 6);   /* 6-Byte-NOP */
            VirtualProtect(ziel, 6, alt2, &alt2);
            FlushInstructionCache(GetCurrentProcess(), ziel, 6);
            if (debug) fprintf(stderr, "memdc_gl: fix: view.dvx+%#x LockCount-Abfrage entfernt\n", stellen[k]);
        }
    }
    slot = (void **)((char *)view + 0x5ee18 + 0x80);
    if ((char *)*slot != (char *)view + 0x19740)
    {
        if (debug) fprintf(stderr, "memdc_gl: JView-vtable passt nicht (%p), kein Eingriff\n", *slot);
        return;
    }
    jview_orig_80 = *slot;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &alt)) return;
    *slot = (void *)jview_80_stub;
    VirtualProtect(slot, sizeof(void *), alt, &alt);
    if (debug) fprintf(stderr, "memdc_gl: JView vfunc+0x80 (view.dvx+0x19740) wird protokolliert%s\n",
            jview_fix ? ", +0x48c wird bei 0 nachgeladen (fix)" : "");
}

/* „Skizze schließen“ (MEMDC_GL_OLEMENU=fix, siehe docs/skizze-schliessen.md): Die Skizzen-/Profilumgebung ist ein
 * in-place aktives OLE-Objekt. Beim Verlassen ruft jengine IOleInPlaceFrame::SetMenu(NULL, …) zweimal (erst
 * JInPlaceObject::UIDeactivate selbst, dann über OLESITE IOleInPlaceSite::OnUIDeactivate); beide enden in
 * ole32!OleSetMenuDescriptor(NULL, hwndFrame, …). Wines Version liefert E_FAIL, wenn für den Thread kein Menü-Hook
 * (mehr) installiert ist (OLEMenu_UnInstallHooks), der zweite Aufruf scheitert also. Windows meldet hier keinen
 * Fehler. OnUIDeactivate gibt das E_FAIL weiter, HProfiles::_EndSketch bricht ab, die Umgebung bleibt aktiv.
 * Fix: jengine.dlls Importeintrag auf eine Hülle umbiegen, die beim Entfernen (hOleMenu == NULL) E_FAIL zu S_OK macht.
 * jengine ist das einzige Solid-Edge-Modul, das OleSetMenuDescriptor importiert. */
typedef HRESULT (WINAPI *osmd_fn)(HANDLE, HWND, HWND, void *, void *);
static osmd_fn osmd_orig;

static HRESULT WINAPI osmd_stub(HANDLE olemenu, HWND frame, HWND aktiv, void *ipframe, void *ipobj)
{
    HRESULT hr = osmd_orig(olemenu, frame, aktiv, ipframe, ipobj);
    if (!olemenu && hr == E_FAIL)
    {
        if (debug) fprintf(stderr, "memdc_gl: OleSetMenuDescriptor(NULL, %p): E_FAIL → S_OK (kein Menü-Hook mehr)\n", frame);
        hr = S_OK;
    }
    return hr;
}

static void olemenu_fix_einrichten(void)
{
    static int erledigt;
    const char *e;
    BYTE *b;
    IMAGE_NT_HEADERS *nt;
    IMAGE_DATA_DIRECTORY *dd;
    IMAGE_IMPORT_DESCRIPTOR *imp;
    if (erledigt) return;
    e = getenv("MEMDC_GL_OLEMENU");
    if (!e || !*e || *e == '0') { erledigt = 1; return; }
    b = (BYTE *)GetModuleHandleA("jengine.dll");
    if (!b) return;   /* noch nicht geladen: beim nächsten wglMakeCurrent erneut */
    erledigt = 1;
    nt = (IMAGE_NT_HEADERS *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew);
    dd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)(b + dd->VirtualAddress); dd->VirtualAddress && imp->Name; imp++)
    {
        IMAGE_THUNK_DATA *name, *iat;
        if (_stricmp((char *)(b + imp->Name), "ole32.dll")) continue;
        name = (IMAGE_THUNK_DATA *)(b + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        iat = (IMAGE_THUNK_DATA *)(b + imp->FirstThunk);
        for (; name->u1.AddressOfData; name++, iat++)
        {
            DWORD alt;
            if (IMAGE_SNAP_BY_ORDINAL(name->u1.Ordinal)) continue;
            if (strcmp((char *)((IMAGE_IMPORT_BY_NAME *)(b + name->u1.AddressOfData))->Name, "OleSetMenuDescriptor")) continue;
            if (!VirtualProtect(&iat->u1.Function, sizeof(void *), PAGE_READWRITE, &alt)) return;
            osmd_orig = (osmd_fn)iat->u1.Function;
            iat->u1.Function = (ULONG_PTR)osmd_stub;
            VirtualProtect(&iat->u1.Function, sizeof(void *), alt, &alt);
            if (debug) fprintf(stderr, "memdc_gl: OleSetMenuDescriptor-Import von jengine.dll umgebogen (fix)\n");
            return;
        }
    }
    if (debug) fprintf(stderr, "memdc_gl: jengine.dll importiert OleSetMenuDescriptor nicht, kein Eingriff\n");
}

BOOL WINAPI wglMakeCurrent(HDC hdc, HGLRC rc)
{
    if (beim_prozessende("wglMakeCurrent")) return TRUE;
    if (rc) prozessende_thread_merken();
    hud_hooks_einrichten();
    jview_hook_einrichten();
    olemenu_fix_einrichten();
    struct memdc *old = current_md(), *md = NULL;
    struct ctx *c;
    BOOL ok;

    if (old) copy_back(old);

    if (rc && is_memdc(hdc) && (c = find_ctx(rc)))
    {
        md = find_memdc(hdc, TRUE);
        if (md && md->fmt <= 0) md->fmt = c->md ? c->md->fmt : 0;
        if (!md || !ensure_helper(md)) { set_current_md(NULL); return FALSE; }
        ok = r_MakeCurrent(md->wdc, rc);
    }
    else
    {
        ok = r_MakeCurrent(hdc, rc);
        if (!ok && rc) TRACE("wglMakeCurrent(%p, %p) fehlgeschlagen\n", hdc, rc);
    }

    set_current_md(ok ? md : NULL);
    return ok;
}

BOOL WINAPI wglDeleteContext(HGLRC rc)
{
    if (beim_prozessende("wglDeleteContext")) return TRUE;
    struct memdc *md = current_md();
    if (md && r_GetCurrentContext() == rc) { copy_back(md); set_current_md(NULL); }
    remove_ctx(rc);
    return r_DeleteContext(rc);
}

BOOL WINAPI wglShareLists(HGLRC a, HGLRC b)
{
    if (beim_prozessende("wglShareLists")) return TRUE;
    struct ctx *ca = find_ctx(a), *cb = find_ctx(b);
    BOOL ok = r_ShareLists(a, b);
    TRACE("wglShareLists(%p%s, %p%s) → %s\n", a, ca ? " Speicher-DC" : "", b, cb ? " Speicher-DC" : "",
          ok ? "OK" : "FEHLGESCHLAGEN");
    return ok;
}

HDC WINAPI wglGetCurrentDC(void)
{
    struct memdc *md = current_md();
    return md ? md->hdc : r_GetCurrentDC();
}

/* Diagnose (nur mit MEMDC_GL_DUMP_RENDERERS=<Sekunden>): ruft render.dll!g_DumpRenderers() höchstens alle N
 * Sekunden auf. Die Funktion listet alle aktiven Solid-Edge-Renderer mit ihren Merkmalen (Features=[...]) per
 * OutputDebugStringA – im Wine-Log sichtbar mit WINEDEBUG=+debugstr. Ändert kein Verhalten. */
static void renderer_dump(void)
{
    static DWORD intervall_ms = (DWORD)-1, zuletzt;
    static void (*dump)(void);
    static BOOL gesucht;
    DWORD jetzt = GetTickCount();
    if (intervall_ms == (DWORD)-1)
    {
        const char *e = getenv("MEMDC_GL_DUMP_RENDERERS");
        intervall_ms = (e && atoi(e) > 0) ? (DWORD)atoi(e) * 1000 : 0;
        zuletzt = jetzt;
    }
    if (!intervall_ms || jetzt - zuletzt < intervall_ms) return;
    zuletzt = jetzt;
    if (!gesucht)
    {
        HMODULE r = GetModuleHandleW(L"render.dll");
        gesucht = TRUE;
        if (r) dump = (void *)GetProcAddress(r, "?g_DumpRenderers@@YAXXZ");
        TRACE("render.dll %p, g_DumpRenderers %p\n", r, dump);
    }
    if (dump) { TRACE("g_DumpRenderers() …\n"); dump(); }
}

BOOL WINAPI wglSwapBuffers(HDC hdc)
{
    if (beim_prozessende("wglSwapBuffers")) return TRUE;
    zaehler[Z_SWAP]++;
    zusammenfassung();
    renderer_dump();
    static unsigned n;
    struct memdc *md;
    if (OFT_PROTOKOLLIEREN(n)) TRACE("SwapBuffers #%u (%s-DC %p)\n", n, is_memdc(hdc) ? "Speicher" : "Fenster", hdc);
    if (is_memdc(hdc))
    {
        if ((md = current_md()) && md->hdc == hdc) { r_glFinish(); copy_back(md); }
        return TRUE;
    }
    return r_SwapBuffers(hdc);
}

void APIENTRY glFlush(void)
{
    if (beim_prozessende("glFlush")) return;
    zaehler[Z_FLUSH]++;
    zusammenfassung();
    static unsigned n;
    renderer_dump();
    struct memdc *md = current_md();
    if (OFT_PROTOKOLLIEREN(n) || (debug && zeichnet_in_front()))
        TRACE("glFlush #%u (%s, Zeichenpuffer %s%s)\n", n, md ? "Speicher-DC" : "Fenster",
              puffer_name(zeichenpuffer_aktuell), front_schmutzig ? ", FRONT geändert" : "");
    front_schmutzig = FALSE;
    r_glFlush();
    if (md) copy_back(md);
}

void APIENTRY glFinish(void)
{
    if (beim_prozessende("glFinish")) return;
    zaehler[Z_FINISH]++;
    zusammenfassung();
    renderer_dump();
    static unsigned n;
    struct memdc *md = current_md();
    if (OFT_PROTOKOLLIEREN(n) || (debug && zeichnet_in_front()))
        TRACE("glFinish #%u (%s, Zeichenpuffer %s%s)\n", n, md ? "Speicher-DC" : "Fenster",
              puffer_name(zeichenpuffer_aktuell), front_schmutzig ? ", FRONT geändert" : "");
    front_schmutzig = FALSE;
    r_glFinish();
    if (md) copy_back(md);
}

/* Optional: Herstellertexte vortäuschen. Solid Edge (Visual.dll) ordnet den Treiber über den Anfang von
 * GL_VENDOR zu (NVIDIA/ATI/AMD/INTEL/VMWARE/MICROSOFT) und schaltet davon abhängig Darstellungswege
 * (Backing Store/Buffer Regions, Hervorhebung, Ansichtswürfel) frei. Mesa meldet „Collabora Ltd“ bzw.
 * „Mesa/X.org“ → unbekannt. MEMDC_GL_VENDOR / MEMDC_GL_RENDERER ersetzen die Texte (Standard: aus). */
static const char *vorgetaeuscht(GLenum what)
{
    static const char *vendor, *renderer;
    static int geladen;
    if (!geladen)
    {
        vendor = getenv("MEMDC_GL_VENDOR");
        renderer = getenv("MEMDC_GL_RENDERER");
        if (vendor && !*vendor) vendor = NULL;
        if (renderer && !*renderer) renderer = NULL;
        geladen = 1;
    }
    if (what == GL_VENDOR) return vendor;
    if (what == GL_RENDERER) return renderer;
    return NULL;
}

const GLubyte *APIENTRY glGetString(GLenum what)
{
    const GLubyte *s = r_glGetString(what);
    const char *ersatz = vorgetaeuscht(what);
    if (ersatz && s)
    {
        char key[48];
        snprintf(key, sizeof(key), "vorgetaeuscht:%#x", what);
        if (erstes_mal(key)) TRACE("glGetString(%#x): „%s“ → vorgetäuscht „%s“\n", what, (const char *)s, ersatz);
        return (const GLubyte *)ersatz;
    }
    char key[32];
    snprintf(key, sizeof(key), "glGetString:%#x", what);
    if (erstes_mal(key))
    {
        if (what == GL_EXTENSIONS) TRACE("glGetString(GL_EXTENSIONS) → %u Zeichen: %s\n", s ? (unsigned)strlen((const char *)s) : 0, s ? (const char *)s : "(NULL)");
        else TRACE("glGetString(%#x) → %s\n", what, s ? (const char *)s : "(NULL)");
    }
    if (what == GL_EXTENSIONS && s && br_bereit()) return gl_extensions_mit_ktx(s);
    return s;
}

HGLRC WINAPI wglCreateLayerContext(HDC hdc, int layer)
{
    HGLRC rc = (layer == 0 && is_memdc(hdc)) ? wglCreateContext(hdc) : r_CreateLayerContext(hdc, layer);
    TRACE("wglCreateLayerContext(%p, Ebene %d) → %p\n", hdc, layer, rc);
    return rc;
}

BOOL WINAPI wglDescribeLayerPlane(HDC hdc, int fmt, int layer, UINT size, LAYERPLANEDESCRIPTOR *lpd)
{
    BOOL ok = r_DescribeLayerPlane(hdc, fmt, layer, size, lpd);
    static unsigned n;
    if (OFT_PROTOKOLLIEREN(n)) TRACE("wglDescribeLayerPlane(%p, Format %d, Ebene %d) → %d\n", hdc, fmt, layer, ok);
    return ok;
}

BOOL WINAPI wglSwapLayerBuffers(HDC hdc, UINT planes)
{
    static unsigned n;
    BOOL ok;
    if (beim_prozessende("wglSwapLayerBuffers")) return TRUE;
    if (is_memdc(hdc) && (planes & 1)) { wglSwapBuffers(hdc); ok = TRUE; }
    else ok = r_SwapLayerBuffers(hdc, planes);
    if (OFT_PROTOKOLLIEREN(n)) TRACE("wglSwapLayerBuffers #%u (%p, Ebenen %#x) → %d\n", n, hdc, planes, ok);
    return ok;
}

BOOL WINAPI wglRealizeLayerPalette(HDC hdc, int layer, BOOL realize)
{
    BOOL ok = r_RealizeLayerPalette(hdc, layer, realize);
    TRACE("wglRealizeLayerPalette(%p, Ebene %d) → %d\n", hdc, layer, ok);
    return ok;
}

int WINAPI wglSetLayerPaletteEntries(HDC hdc, int layer, int start, int count, const COLORREF *cr)
{
    int r = r_SetLayerPaletteEntries(hdc, layer, start, count, cr);
    TRACE("wglSetLayerPaletteEntries(%p, Ebene %d, %d Einträge) → %d\n", hdc, layer, count, r);
    return r;
}

int WINAPI wglGetLayerPaletteEntries(HDC hdc, int layer, int start, int count, COLORREF *cr)
{
    int r = r_GetLayerPaletteEntries(hdc, layer, start, count, cr);
    TRACE("wglGetLayerPaletteEntries(%p, Ebene %d) → %d\n", hdc, layer, r);
    return r;
}

PROC WINAPI wglGetProcAddress(LPCSTR name)
{
    PROC p = r_GetProcAddress(name);
    if (!name) return p;
    if (erstes_mal(name)) TRACE("wglGetProcAddress(%s) → %s\n", name, p ? "vorhanden" : "NULL");
    {
        PROC eigen = bufregion_proc(name, p);
        if (eigen) return eigen;
    }
    if (!strcmp(name, "wglCreateContextAttribsARB"))
    {
        if (p && !r_CreateContextAttribsARB) r_CreateContextAttribsARB = (void *)p;
        return p ? (PROC)my_CreateContextAttribsARB : NULL;
    }
    if (!strcmp(name, "glFlush")) return (PROC)glFlush;
    if (p && (!strcmp(name, "glBlitFramebuffer") || !strcmp(name, "glBlitFramebufferEXT")))
    {
        if (!r_BlitFramebuffer) r_BlitFramebuffer = (void *)p;
        return debug ? (PROC)diag_BlitFramebuffer : p;
    }
    if (!strcmp(name, "glFinish")) return (PROC)glFinish;
    return p;
}

/* ---- Laden ---- */
static BOOL load_real(HMODULE self)
{
    WCHAR path[MAX_PATH], *slash;
    DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
    if (!n || n >= MAX_PATH) return FALSE;
    if ((slash = wcsrchr(path, L'\\'))) slash[1] = 0;
    if (wcslen(path) + 14 >= MAX_PATH) return FALSE;
    wcscat(path, L"mesaogl32.dll");
    if (!(real = LoadLibraryW(path))) return FALSE;

#define GET(var, name) if (!(var = (void *)GetProcAddress(real, name))) return FALSE
    GET(r_ChoosePixelFormat, "wglChoosePixelFormat");
    GET(r_DescribePixelFormat, "wglDescribePixelFormat");
    GET(r_GetPixelFormat, "wglGetPixelFormat");
    GET(r_SetPixelFormat, "wglSetPixelFormat");
    GET(r_CreateContext, "wglCreateContext");
    GET(r_DeleteContext, "wglDeleteContext");
    GET(r_MakeCurrent, "wglMakeCurrent");
    GET(r_GetCurrentDC, "wglGetCurrentDC");
    GET(r_GetCurrentContext, "wglGetCurrentContext");
    GET(r_SwapBuffers, "wglSwapBuffers");
    GET(r_ShareLists, "wglShareLists");
    GET(r_GetProcAddress, "wglGetProcAddress");
    GET(r_glFlush, "glFlush");
    GET(r_glDrawBuffer, "glDrawBuffer");
    GET(r_glCopyPixels, "glCopyPixels");
    GET(r_glLogicOp, "glLogicOp");
    GET(r_glBegin, "glBegin");
    GET(r_glEnable, "glEnable");
    GET(r_glDisable, "glDisable");
    GET(r_glDrawPixels, "glDrawPixels");
    GET(r_glGetString, "glGetString");
    GET(r_CreateLayerContext, "wglCreateLayerContext");
    GET(r_DescribeLayerPlane, "wglDescribeLayerPlane");
    GET(r_SwapLayerBuffers, "wglSwapLayerBuffers");
    GET(r_RealizeLayerPalette, "wglRealizeLayerPalette");
    GET(r_SetLayerPaletteEntries, "wglSetLayerPaletteEntries");
    GET(r_GetLayerPaletteEntries, "wglGetLayerPaletteEntries");
    GET(r_glFinish, "glFinish");
    GET(r_glReadPixels, "glReadPixels");
    GET(r_glGetIntegerv, "glGetIntegerv");
    GET(r_glReadBuffer, "glReadBuffer");
    GET(r_glPixelStorei, "glPixelStorei");
    GET(r_glGetError, "glGetError");
#undef GET
    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        const char *d = getenv("MEMDC_GL_DEBUG");
        debug = d && *d && *d != '0';
        InitializeCriticalSection(&lock);
        tls_cur = TlsAlloc();
        DisableThreadLibraryCalls(inst);
        if (!load_real(inst))
        {
            fprintf(stderr, "memdc_gl: mesaogl32.dll fehlt oder ist unvollständig\n");
            return FALSE;
        }
        prozessende_vorbereiten();
        TRACE("geladen, Mesa-opengl32 als mesaogl32.dll\n");
    }
    return TRUE;
}
