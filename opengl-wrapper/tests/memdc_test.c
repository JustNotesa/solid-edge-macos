/*
 * Prüft die PFD_DRAW_TO_BITMAP-Emulation wie Solid Edge sie braucht: Speicher-DC mit 64x64-DIB-Section,
 * ChoosePixelFormat(PFD_DRAW_TO_BITMAP|PFD_SUPPORT_OPENGL|PFD_GENERIC_FORMAT), SetPixelFormat, wglCreateContext,
 * wglMakeCurrent, Zeichnen, glFlush – danach müssen die Pixel in der Bitmap stehen.
 * Ausgabe: je Schritt OK/FEHLER, Rückgabewert 0 bei Erfolg.
 */
#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <string.h>

static int fehler;
#define PRUEFE(cond, ...) do { int ok_ = !!(cond); printf("%s: ", ok_ ? "OK    " : "FEHLER"); printf(__VA_ARGS__); printf("\n"); if (!ok_) fehler++; } while (0)

int main(void)
{
    BITMAPINFO bmi = { { sizeof(BITMAPINFOHEADER), 64, 64, 1, 32, BI_RGB } };
    PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd), 1 }, got;
    unsigned char *bits = NULL;
    HDC hdc = CreateCompatibleDC(NULL);
    HBITMAP bmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    HGLRC rc;
    int fmt;

    PRUEFE(hdc && bmp && bits, "Speicher-DC %p mit DIB-Section %p", (void *)hdc, (void *)bmp);
    SelectObject(hdc, bmp);
    memset(bits, 0x11, 64 * 64 * 4);

    pfd.dwFlags = PFD_DRAW_TO_BITMAP | PFD_SUPPORT_OPENGL | PFD_GENERIC_FORMAT;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    /* wie Solid Edge: nVersion 0, Accum 64, 2 Aux-Puffer */
    pfd.nVersion = 0;
    pfd.cAccumBits = 64;
    pfd.cAuxBuffers = 2;
    fmt = ChoosePixelFormat(hdc, &pfd);
    PRUEFE(fmt > 0, "ChoosePixelFormat(PFD_DRAW_TO_BITMAP, nVersion 0, Accum 64, Aux 2) = %d", fmt);
    memset(&got, 0, sizeof(got));
    DescribePixelFormat(hdc, fmt, sizeof(got), &got);
    PRUEFE(got.dwFlags & PFD_DRAW_TO_BITMAP, "DescribePixelFormat meldet DRAW_TO_BITMAP (Flags %#lx)", (unsigned long)got.dwFlags);
    PRUEFE(SetPixelFormat(hdc, fmt, &pfd), "SetPixelFormat");
    PRUEFE(GetPixelFormat(hdc) == fmt, "GetPixelFormat = %d", GetPixelFormat(hdc));
    PRUEFE(SetPixelFormat(hdc, fmt, &pfd), "zweites SetPixelFormat mit gleichem Format");

    rc = wglCreateContext(hdc);
    PRUEFE(rc != NULL, "wglCreateContext = %p", (void *)rc);
    PRUEFE(wglMakeCurrent(hdc, rc), "wglMakeCurrent");
    PRUEFE(wglGetCurrentDC() == hdc, "wglGetCurrentDC liefert den Speicher-DC");
    printf("        GL_RENDERER = %s\n        GL_VERSION  = %s\n",
           (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));

    /* links Rot, rechts oben ein grünes Rechteck (GL: Ursprung unten links) */
    glViewport(0, 0, 64, 64);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 64, 0, 64, -1, 1);
    glColor3f(0.0f, 1.0f, 0.0f);
    glBegin(GL_QUADS);
    glVertex2f(32, 32); glVertex2f(64, 32); glVertex2f(64, 64); glVertex2f(32, 64);
    glEnd();
    glFlush();

    /* DIB ist unten-nach-oben: Zeile 0 = unten. Pixel (8,8) rot, (48,48) grün. BGRA. */
    {
        unsigned char *u = bits + (8 * 64 + 8) * 4, *o = bits + (48 * 64 + 48) * 4;
        PRUEFE(u[2] > 200 && u[1] < 50 && u[0] < 50, "Pixel unten links rot (B=%d G=%d R=%d)", u[0], u[1], u[2]);
        PRUEFE(o[1] > 200 && o[2] < 50 && o[0] < 50, "Pixel oben rechts grün (B=%d G=%d R=%d)", o[0], o[1], o[2]);
    }

    /* Bitmap per BitBlt in einen zweiten Speicher-DC – so blendet Solid Edge das Ergebnis ein */
    {
        HDC ziel = CreateCompatibleDC(NULL);
        HBITMAP zb = CreateCompatibleBitmap(GetDC(NULL), 64, 64);
        COLORREF c;
        SelectObject(ziel, zb);
        BitBlt(ziel, 0, 0, 64, 64, hdc, 0, 0, SRCCOPY);
        c = GetPixel(ziel, 48, 15);   /* GDI: Ursprung oben links → (48,15) liegt im grünen Feld */
        PRUEFE(GetGValue(c) > 200 && GetRValue(c) < 50, "BitBlt-Ergebnis grün (R=%d G=%d B=%d)", GetRValue(c), GetGValue(c), GetBValue(c));
        DeleteDC(ziel);
        DeleteObject(zb);
    }

    /* ---- Buffer Regions (GL_KTX_buffer_region und WGL_ARB_buffer_region) ---- */
    {
        typedef GLuint (APIENTRY *NEW)(GLenum);
        typedef void (APIENTRY *READ)(GLuint, GLint, GLint, GLsizei, GLsizei);
        typedef void (APIENTRY *DRAW)(GLuint, GLint, GLint, GLsizei, GLsizei, GLint, GLint);
        typedef void (APIENTRY *DEL)(GLuint);
        typedef HANDLE (WINAPI *WCREATE)(HDC, int, UINT);
        typedef BOOL (WINAPI *WSAVE)(HANDLE, int, int, int, int);
        typedef BOOL (WINAPI *WRESTORE)(HANDLE, int, int, int, int, int, int);
        typedef VOID (WINAPI *WDEL)(HANDLE);
        NEW pNew = (NEW)wglGetProcAddress("glNewBufferRegion");
        READ pRead = (READ)wglGetProcAddress("glReadBufferRegion");
        DRAW pDraw = (DRAW)wglGetProcAddress("glDrawBufferRegionEXT");
        DEL pDel = (DEL)wglGetProcAddress("glDeleteBufferRegion");
        WCREATE wCreate = (WCREATE)wglGetProcAddress("wglCreateBufferRegionARB");
        WSAVE wSave = (WSAVE)wglGetProcAddress("wglSaveBufferRegionARB");
        WRESTORE wRestore = (WRESTORE)wglGetProcAddress("wglRestoreBufferRegionARB");
        WDEL wDel = (WDEL)wglGetProcAddress("wglDeleteBufferRegionARB");
        const char *ext = (const char *)glGetString(GL_EXTENSIONS);
        PRUEFE(pNew && pRead && pDraw && pDel, "glNew/Read/Draw/DeleteBufferRegion vorhanden");
        PRUEFE(wCreate && wSave && wRestore && wDel, "wglCreate/Save/Restore/DeleteBufferRegionARB vorhanden");
        PRUEFE(ext && strstr(ext, "GL_KTX_buffer_region"), "GL_EXTENSIONS enthält GL_KTX_buffer_region");

        if (pNew && pRead && pDraw && pDel)
        {
            GLuint id = pNew(1 /* KTX_BACK_REGION */);
            unsigned char *u = bits + (8 * 64 + 8) * 4, *o = bits + (48 * 64 + 48) * 4;
            PRUEFE(id != 0, "glNewBufferRegion(KTX_BACK) = %u", id);
            pRead(id, 0, 0, 64, 64);                     /* rot/grün sichern */
            glClearColor(0.0f, 0.0f, 1.0f, 1.0f);        /* alles blau */
            glClear(GL_COLOR_BUFFER_BIT);
            pDraw(id, 0, 0, 32, 32, 0, 0);               /* nur unten links zurück */
            glFlush();
            PRUEFE(u[2] > 200 && u[0] < 50, "KTX: unten links wieder rot (B=%d G=%d R=%d)", u[0], u[1], u[2]);
            PRUEFE(o[0] > 200 && o[1] < 50, "KTX: oben rechts blau, nicht wiederhergestellt (B=%d G=%d R=%d)", o[0], o[1], o[2]);
            pDraw(id, 32, 32, 32, 32, 32, 32);          /* oben rechts zurück */
            glFlush();
            PRUEFE(o[1] > 200 && o[2] < 50, "KTX: oben rechts wieder grün (B=%d G=%d R=%d)", o[0], o[1], o[2]);
            pDraw(id, 32, 32, 16, 16, 0, 0);            /* verschoben: grünes Stück nach unten links */
            glFlush();
            PRUEFE(u[1] > 200 && u[2] < 50, "KTX: verschobenes Rechteck grün (B=%d G=%d R=%d)", u[0], u[1], u[2]);
            pDel(id);
        }
        if (wCreate && wSave && wRestore && wDel)
        {
            HANDLE h = wCreate(hdc, 0, 0x2 /* WGL_BACK_COLOR_BUFFER_BIT_ARB */);
            unsigned char *o = bits + (48 * 64 + 48) * 4;
            PRUEFE(h != NULL, "wglCreateBufferRegionARB = %p", h);
            PRUEFE(wSave(h, 0, 0, 64, 64), "wglSaveBufferRegionARB");
            glClearColor(1.0f, 1.0f, 0.0f, 1.0f);       /* gelb */
            glClear(GL_COLOR_BUFFER_BIT);
            PRUEFE(wRestore(h, 32, 32, 32, 32, 32, 32), "wglRestoreBufferRegionARB");
            glFlush();
            PRUEFE(o[1] > 200 && o[2] < 50, "WGL: oben rechts wieder grün (B=%d G=%d R=%d)", o[0], o[1], o[2]);
            wDel(h);
        }
    }

    /* wglUseFontOutlinesW wie für die Würfelbeschriftung (Visual 0x1f590d: je Zeichen, WGL_FONT_POLYGONS) */
    {
        HFONT f = CreateFontW(-48, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, 0,
                              ANTIALIASED_QUALITY, 0, L"Tahoma");
        HFONT alt = SelectObject(hdc, f);
        GLYPHMETRICSFLOAT gmf[2];
        GLuint basis = glGenLists(2);
        int k, flaeche = 0, umriss = 0;

        memset(gmf, 0, sizeof(gmf));
        PRUEFE(wglUseFontOutlinesW(hdc, L'W', 1, basis, 0.0f, 0.1f, WGL_FONT_POLYGONS, &gmf[0]),
               "wglUseFontOutlinesW('W', WGL_FONT_POLYGONS)");
        PRUEFE(gmf[0].gmfCellIncX > 0.3f && gmf[0].gmfBlackBoxY > 0.3f,
               "GLYPHMETRICSFLOAT: CellIncX %.3f, BlackBox %.3f x %.3f", gmf[0].gmfCellIncX, gmf[0].gmfBlackBoxX,
               gmf[0].gmfBlackBoxY);
        PRUEFE(wglUseFontOutlinesW(hdc, L'W', 1, basis + 1, 0.0f, 0.0f, WGL_FONT_LINES, &gmf[1]),
               "wglUseFontOutlinesW('W', WGL_FONT_LINES)");

        for (k = 0; k < 2; k++)
        {
            int i, n = 0;
            glViewport(0, 0, 64, 64);
            glDisable(GL_DEPTH_TEST);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glOrtho(-0.05, 1.05, -0.2, 0.9, -1, 1);
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            glColor3f(1.0f, 1.0f, 1.0f);
            glCallList(basis + k);
            glFlush();
            for (i = 0; i < 64 * 64; i++)
                if (bits[i * 4] > 128 || bits[i * 4 + 1] > 128 || bits[i * 4 + 2] > 128) n++;
            if (k == 0) flaeche = n; else umriss = n;
        }
        PRUEFE(flaeche > 150, "'W' als Fläche gezeichnet: %d helle Pixel von 4096", flaeche);
        PRUEFE(umriss > 40 && umriss < flaeche, "'W' als Umriss gezeichnet: %d helle Pixel", umriss);
        /* 'E' (und 'W') gegen GDI: gleiche Größe (1 em = 64 Pixel), Grundlinie 6 Pixel über dem unteren Rand.
         * Überdeckung (Schnitt/Vereinigung) muss hoch sein; ein falsch tesselliertes Zeichen (Dreieck) fällt durch. */
        {
            /* Solid Edge (Visual 0x1da2d6): CreateFontW(-12, …, FW_NORMAL, …, OUT_TT_PRECIS, ANTIALIASED_QUALITY, <Name>) */
            static const WCHAR zeichen[] = { L'E', L'W', L'F', L'H', L'B', L'O', L'R', L'A' };
            static const WCHAR *const namen[] = { L"Arial", L"Tahoma" };
            static const int gewichte[] = { FW_NORMAL, FW_BOLD };
            static unsigned char maske[64 * 64];
            int z, nf, ng;
            for (nf = 0; nf < 2; nf++)
            for (ng = 0; ng < 2; ng++)
            {
            WCHAR gewaehlt[64] = L"?";
            HFONT g = CreateFontW(-64, 0, 0, 0, gewichte[ng], 0, 0, 0, DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, 0,
                                  NONANTIALIASED_QUALITY, 0, namen[nf]);
            HFONT v0 = SelectObject(hdc, g);
            GetTextFaceW(hdc, 64, gewaehlt);
            SelectObject(hdc, v0);
            printf("        Schrift %ls %d → %ls\n", namen[nf], gewichte[ng], gewaehlt);
            for (z = 0; z < (int)(sizeof(zeichen) / sizeof(zeichen[0])); z++)
            {
                GLuint liste = glGenLists(1);
                GLYPHMETRICSFLOAT m;
                int i, schnitt = 0, verein = 0;
                HFONT vorher = SelectObject(hdc, g);
                PRUEFE(wglUseFontOutlinesW(hdc, zeichen[z], 1, liste, 0.0f, 0.0f, WGL_FONT_POLYGONS, &m),
                       "wglUseFontOutlinesW('%c')", (char)zeichen[z]);
                glViewport(0, 0, 64, 64);
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                glMatrixMode(GL_PROJECTION);
                glLoadIdentity();
                glOrtho(0, 1, 0, 1, -1, 1);
                glMatrixMode(GL_MODELVIEW);
                glLoadIdentity();
                glTranslated(0.0, 6.0 / 64, 0.0);
                glColor3f(1.0f, 1.0f, 1.0f);
                glCallList(liste);
                glFlush();
                for (i = 0; i < 64 * 64; i++) maske[i] = bits[i * 4 + 1] > 128;
                /* GDI-Referenz in dieselbe DIB (unten-oben: GL-Zeile 0 = Speicherzeile 0 = GDI y 63) */
                memset(bits, 0, 64 * 64 * 4);
                GdiFlush();
                SetTextColor(hdc, RGB(255, 255, 255));
                SetBkMode(hdc, TRANSPARENT);
                SetTextAlign(hdc, TA_BASELINE | TA_LEFT);
                TextOutW(hdc, 0, 64 - 6, &zeichen[z], 1);
                GdiFlush();
                {
                    /* Übereinstimmung mit 1 Pixel Toleranz (GDI rastert gehintet, GL ungehintet): ein gesetztes Pixel
                     * zählt als getroffen, wenn im 3x3-Umfeld der anderen Maske eines gesetzt ist. */
                    static unsigned char ref[64 * 64];
                    int x, y, dx, dy;
                    for (i = 0; i < 64 * 64; i++) ref[i] = bits[i * 4 + 1] > 128;
                    for (y = 0; y < 64; y++)
                        for (x = 0; x < 64; x++)
                        {
                            int a = maske[y * 64 + x], b = ref[y * 64 + x], na = 0, nb = 0;
                            if (!a && !b) continue;
                            for (dy = -1; dy <= 1; dy++)
                                for (dx = -1; dx <= 1; dx++)
                                {
                                    int xx = x + dx, yy = y + dy;
                                    if (xx < 0 || yy < 0 || xx > 63 || yy > 63) continue;
                                    na |= maske[yy * 64 + xx];
                                    nb |= ref[yy * 64 + xx];
                                }
                            verein++;
                            if ((a ? nb : 1) && (b ? na : 1)) schnitt++;
                        }
                }
                PRUEFE(verein > 0 && schnitt * 100 >= verein * 90,
                       "%ls %d '%c' deckt sich mit GDI (±1 Pixel): %d von %d Pixeln", namen[nf], gewichte[ng], (char)zeichen[z], schnitt, verein);
                SelectObject(hdc, vorher);
                glDeleteLists(liste, 1);
            }
            DeleteObject(g);
            }
        }
        glDeleteLists(basis, 2);
        DeleteObject(SelectObject(hdc, alt));
    }

    wglMakeCurrent(NULL, NULL);
    PRUEFE(wglDeleteContext(rc), "wglDeleteContext");
    DeleteObject(bmp);
    DeleteDC(hdc);

    printf("%s (%d Fehler)\n", fehler ? "ERGEBNIS: FEHLER" : "ERGEBNIS: OK", fehler);
    return fehler ? 1 : 0;
}
