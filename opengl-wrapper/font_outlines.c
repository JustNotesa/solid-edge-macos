/*
 * wglUseFontOutlinesA/W für die Vorschaltung. Mesas opengl32 liefert hier nur einen Platzhalter (`xor eax,eax; ret`),
 * Solid Edge legt aber die Beschriftung des Ansichtswürfels damit an (Visual.dll 0x1f590d: je Zeichen ein Aufruf mit
 * count 1, Format WGL_FONT_POLYGONS, Ergebnis-Metriken in GLYPHMETRICSFLOAT).
 *
 * Umriss-Auswertung, Metriken und Listenaufbau übernommen aus Wine, dlls/opengl32/wgl.c (wglUseFontOutlines_common,
 * fixed_to_double, bezier_fits_deviation, bezier_approximate):
 *   Copyright (c) 1999 Lionel Ulmer
 *   Copyright (c) 2005 Raphael Junqueira
 *   GNU Lesser General Public License 2.1 oder später.
 *
 * Eigene Abweichung: Die Flächen (WGL_FONT_POLYGONS) zerlegt nicht der GLU-Tessellator (Wines glu32 zerlegt konkave
 * Konturen wie E, F, W falsch), sondern eine Trapez-Zerlegung nach der Nonzero-Regel (TrueType-Füllregel): Waagerechte
 * Streifen zwischen allen Eckpunkt- und Schnittpunkt-Höhen, darin die geschnittenen Kanten nach x sortiert und nach
 * Umlaufzahl gepaart, je Spanne zwei Dreiecke (GL_TRIANGLES, gegen den Uhrzeigersinn). Das behandelt Löcher (O, B, R, A),
 * konkave Formen und überlappende Konturen ohne Sonderfälle. Wie bei Wine wird `extrusion` nicht ausgewertet und nur
 * TT_PRIM_LINE/TT_PRIM_QSPLINE verarbeitet.
 *
 * Wird von memdc_gl.c eingebunden (nutzt `real`, TRACE).
 */
#include <math.h>

#ifndef WGL_FONT_LINES
#define WGL_FONT_LINES    0
#define WGL_FONT_POLYGONS 1
#endif

static struct
{
    int geladen;
    void (APIENTRY *NewList)(GLuint, GLenum);
    void (APIENTRY *EndList)(void);
    void (APIENTRY *FrontFace)(GLenum);
    void (APIENTRY *Normal3d)(GLdouble, GLdouble, GLdouble);
    void (APIENTRY *Translated)(GLdouble, GLdouble, GLdouble);
    void (APIENTRY *Begin)(GLenum);
    void (APIENTRY *End)(void);
    void (APIENTRY *Vertex3d)(GLdouble, GLdouble, GLdouble);
} fo;

static int fo_laden(void)
{
    if (!fo.geladen)
    {
#define FO_GL(var, name) if (!(fo.var = (void *)GetProcAddress(real, name))) return 0
        FO_GL(NewList, "glNewList"); FO_GL(EndList, "glEndList"); FO_GL(FrontFace, "glFrontFace");
        FO_GL(Normal3d, "glNormal3d"); FO_GL(Translated, "glTranslated"); FO_GL(Begin, "glBegin");
        FO_GL(End, "glEnd"); FO_GL(Vertex3d, "glVertex3d");
#undef FO_GL
        fo.geladen = 1;
    }
    return 1;
}

/* ---- Diagnose (nur MEMDC_GL_DEBUG): wo und mit welchem Zustand werden die Schriftlisten aufgerufen? ---- */
#define FO_MAX_LISTEN 4096
static struct { GLuint id; HGLRC ctx; } fo_listen[FO_MAX_LISTEN];
static int fo_nlisten;
static HGLRC (WINAPI *fo_GetCurrentContext)(void);
static GLboolean (APIENTRY *fo_IsList)(GLuint);
static GLboolean (APIENTRY *fo_IsEnabled)(GLenum);
static void (APIENTRY *fo_GetIntegerv)(GLenum, GLint *);
static void (APIENTRY *fo_GetFloatv)(GLenum, GLfloat *);
static void (APIENTRY *fo_CallList)(GLuint);
static void (APIENTRY *fo_CallLists)(GLsizei, GLenum, const GLvoid *);

static int fo_diag_laden(void)
{
    if (fo_CallList) return 1;
    fo_GetCurrentContext = (void *)GetProcAddress(real, "wglGetCurrentContext");
    fo_IsList = (void *)GetProcAddress(real, "glIsList");
    fo_IsEnabled = (void *)GetProcAddress(real, "glIsEnabled");
    fo_GetIntegerv = (void *)GetProcAddress(real, "glGetIntegerv");
    fo_GetFloatv = (void *)GetProcAddress(real, "glGetFloatv");
    fo_CallLists = (void *)GetProcAddress(real, "glCallLists");
    fo_CallList = (void *)GetProcAddress(real, "glCallList");
    return fo_CallList && fo_CallLists && fo_GetCurrentContext && fo_IsList && fo_IsEnabled && fo_GetIntegerv && fo_GetFloatv;
}

static void fo_liste_merken(GLuint id)
{
    if (fo_nlisten < FO_MAX_LISTEN && fo_GetCurrentContext)
    {
        fo_listen[fo_nlisten].id = id;
        fo_listen[fo_nlisten].ctx = fo_GetCurrentContext();
        fo_nlisten++;
    }
}

static HGLRC fo_schriftliste(GLuint id)
{
    int i;
    for (i = fo_nlisten - 1; i >= 0; i--)
        if (fo_listen[i].id == id) return fo_listen[i].ctx ? fo_listen[i].ctx : (HGLRC)1;
    return NULL;
}

static void fo_zustand_protokoll(GLuint id, const char *wie)
{
    static char zuletzt[64][256];
    static int nzuletzt, gesamt;
    char z[256];
    GLint cull, cullmode = 0, front = 0, depth, dfunc = 0, blend, light, pmode[2] = { 0, 0 }, db = 0, dmask = 0, cmask[4] = { 0 };
    GLfloat col[4] = { 0 }, mv[16], pr[16], det;
    HGLRC ctx = fo_GetCurrentContext(), von = fo_schriftliste(id);
    int i, j;
    if (!von) return;
    cull = fo_IsEnabled(GL_CULL_FACE);
    fo_GetIntegerv(GL_CULL_FACE_MODE, &cullmode);
    fo_GetIntegerv(GL_FRONT_FACE, &front);
    depth = fo_IsEnabled(GL_DEPTH_TEST);
    fo_GetIntegerv(GL_DEPTH_FUNC, &dfunc);
    fo_GetIntegerv(GL_DEPTH_WRITEMASK, &dmask);
    blend = fo_IsEnabled(GL_BLEND);
    light = fo_IsEnabled(GL_LIGHTING);
    fo_GetIntegerv(GL_POLYGON_MODE, pmode);
    fo_GetIntegerv(GL_DRAW_BUFFER, &db);
    fo_GetIntegerv(GL_COLOR_WRITEMASK, cmask);
    fo_GetFloatv(GL_CURRENT_COLOR, col);
    fo_GetFloatv(GL_MODELVIEW_MATRIX, mv);
    fo_GetFloatv(GL_PROJECTION_MATRIX, pr);
    {
        /* Händigkeit von Projektion*Modell (3x3): negativ = gespiegelt → Umlaufsinn kehrt sich um */
        float m[9];
        for (i = 0; i < 3; i++)
            for (j = 0; j < 3; j++)
                m[i * 3 + j] = pr[0 * 4 + j] * mv[i * 4 + 0] + pr[1 * 4 + j] * mv[i * 4 + 1] + pr[2 * 4 + j] * mv[i * 4 + 2];
        det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
    }
    snprintf(z, sizeof(z),
             "%s Liste %u: Kontext %p (angelegt in %p) IsList=%d | Cull=%d Modus=%#x Front=%#x | Tiefe=%d Func=%#x Maske=%d |"
             " Blend=%d Licht=%d Poly=%#x/%#x | Farbe %.2f/%.2f/%.2f/%.2f Farbmaske %d%d%d%d | Puffer %#x | det %s",
             wie, id, (void *)ctx, (void *)von, fo_IsList(id), cull, cullmode, front, depth, dfunc, dmask, blend, light,
             pmode[0], pmode[1], col[0], col[1], col[2], col[3], cmask[0], cmask[1], cmask[2], cmask[3], db,
             det < 0 ? "<0 (gespiegelt)" : ">=0");
    /* nur neue Zustandskombinationen ausgeben (ohne Listen-Nummer verglichen) */
    {
        const char *ohne = strchr(z, ':');
        for (i = 0; i < nzuletzt; i++)
            if (!strcmp(zuletzt[i], ohne)) return;
        if (nzuletzt < 64) strcpy(zuletzt[nzuletzt++], ohne);
    }
    if (++gesamt <= 200) TRACE("Schriftliste %s\n", z);
}

void APIENTRY glCallList(GLuint id)
{
    if (debug && fo_diag_laden()) fo_zustand_protokoll(id, "glCallList");
    if (!fo_CallList) fo_diag_laden();
    fo_CallList(id);
}

void APIENTRY glCallLists(GLsizei n, GLenum type, const GLvoid *lists)
{
    if (debug && fo_diag_laden() && n > 0 && lists)
    {
        GLint basis = 0;
        GLuint id = 0;
        fo_GetIntegerv(GL_LIST_BASE, &basis);
        switch (type)
        {
        case GL_UNSIGNED_BYTE: id = ((const GLubyte *)lists)[0]; break;
        case GL_BYTE: id = ((const GLbyte *)lists)[0]; break;
        case GL_UNSIGNED_SHORT: id = ((const GLushort *)lists)[0]; break;
        case GL_SHORT: id = ((const GLshort *)lists)[0]; break;
        case GL_UNSIGNED_INT: id = ((const GLuint *)lists)[0]; break;
        case GL_INT: id = ((const GLint *)lists)[0]; break;
        default: break;
        }
        fo_zustand_protokoll(basis + id, "glCallLists");
    }
    if (!fo_CallLists) fo_diag_laden();
    fo_CallLists(n, type, lists);
}

static const MAT2 fo_identity = { { 0, 1 }, { 0, 0 }, { 0, 0 }, { 0, 1 } };

static void fixed_to_double(POINTFX fixed, UINT em_size, GLdouble vertex[2])
{
    vertex[0] = (fixed.x.value + (GLdouble)fixed.x.fract / (1 << 16)) / em_size;
    vertex[1] = (fixed.y.value + (GLdouble)fixed.y.fract / (1 << 16)) / em_size;
}

typedef struct { GLdouble x, y; } bezier_vector;

static BOOL bezier_fits_deviation(const bezier_vector *p, FLOAT max_deviation)
{
    bezier_vector deviation, vertex, base;
    double base_length, dot;

    max_deviation *= max_deviation;
    vertex.x = (p[0].x + p[1].x * 2 + p[2].x) / 4 - p[0].x;
    vertex.y = (p[0].y + p[1].y * 2 + p[2].y) / 4 - p[0].y;
    base.x = p[2].x - p[0].x;
    base.y = p[2].y - p[0].y;
    base_length = base.x * base.x + base.y * base.y;
    if (base_length <= max_deviation)
    {
        base.x = 0.0;
        base.y = 0.0;
    }
    else
    {
        base_length = sqrt(base_length);
        base.x /= base_length;
        base.y /= base_length;
        dot = base.x * vertex.x + base.y * vertex.y;
        dot = dot < 0.0 ? 0.0 : dot > base_length ? base_length : dot;
        base.x *= dot;
        base.y *= dot;
    }
    deviation.x = vertex.x - base.x;
    deviation.y = vertex.y - base.y;
    return deviation.x * deviation.x + deviation.y * deviation.y <= max_deviation;
}

static int bezier_approximate(const bezier_vector *p, bezier_vector *points, FLOAT deviation)
{
    bezier_vector first_curve[3], second_curve[3], vertex;
    int total_vertices;

    if (bezier_fits_deviation(p, deviation))
    {
        if (points) *points = p[2];
        return 1;
    }
    vertex.x = (p[0].x + p[1].x * 2 + p[2].x) / 4;
    vertex.y = (p[0].y + p[1].y * 2 + p[2].y) / 4;
    first_curve[0] = p[0];
    first_curve[1].x = (p[0].x + p[1].x) / 2;
    first_curve[1].y = (p[0].y + p[1].y) / 2;
    first_curve[2] = vertex;
    second_curve[0] = vertex;
    second_curve[1].x = (p[2].x + p[1].x) / 2;
    second_curve[1].y = (p[2].y + p[1].y) / 2;
    second_curve[2] = p[2];
    total_vertices = bezier_approximate(first_curve, points, deviation);
    if (points) points += total_vertices;
    total_vertices += bezier_approximate(second_curve, points, deviation);
    return total_vertices;
}

/* ---- Konturpuffer ---- */

struct fo_konturen
{
    GLdouble *p;   /* x,y-Paare */
    int n, kap;    /* Punkte, Kapazität */
    int *start;    /* Startindex je Kontur, start[nk] = n */
    int nk, kkap;
};

static int fo_punkt(struct fo_konturen *k, GLdouble x, GLdouble y)
{
    int s = k->nk ? k->start[k->nk - 1] : 0;
    if (k->n > s && k->p[2 * (k->n - 1)] == x && k->p[2 * (k->n - 1) + 1] == y) return 1;   /* doppelt */
    if (k->n == k->kap)
    {
        int neu = k->kap ? k->kap * 2 : 256;
        GLdouble *q = realloc(k->p, neu * 2 * sizeof(GLdouble));
        if (!q) return 0;
        k->p = q;
        k->kap = neu;
    }
    k->p[2 * k->n] = x;
    k->p[2 * k->n + 1] = y;
    k->n++;
    return 1;
}

static int fo_kontur_beginnen(struct fo_konturen *k)
{
    if (k->nk + 2 > k->kkap)
    {
        int neu = k->kkap ? k->kkap * 2 : 16;
        int *q = realloc(k->start, neu * sizeof(int));
        if (!q) return 0;
        k->start = q;
        k->kkap = neu;
    }
    k->start[k->nk++] = k->n;
    k->start[k->nk] = k->n;
    return 1;
}

static void fo_kontur_beenden(struct fo_konturen *k)
{
    int s = k->start[k->nk - 1];
    /* Schlusspunkt gleich Startpunkt entfernen */
    if (k->n - s >= 2 && k->p[2 * s] == k->p[2 * (k->n - 1)] && k->p[2 * s + 1] == k->p[2 * (k->n - 1) + 1]) k->n--;
    if (k->n - s < 3) k->n = s;   /* entartet */
    k->start[k->nk] = k->n;
}

/* ---- Trapez-Zerlegung (Nonzero) ---- */

struct fo_kante { double xa, ya, xb, yb; int dir; double xm; };

static int fo_cmp_double(const void *a, const void *b)
{
    double d = *(const double *)a - *(const double *)b;
    return d < 0 ? -1 : d > 0;
}

static int fo_cmp_kante(const void *a, const void *b)
{
    double d = ((const struct fo_kante *)a)->xm - ((const struct fo_kante *)b)->xm;
    return d < 0 ? -1 : d > 0;
}

static double fo_x_bei(const struct fo_kante *e, double y)
{
    return e->xa + (y - e->ya) * (e->xb - e->xa) / (e->yb - e->ya);
}

static void fo_flaechen(const struct fo_konturen *k)
{
    struct fo_kante *kanten = malloc((k->n ? k->n : 1) * sizeof(*kanten)), *aktiv;
    double *ys;
    int ne = 0, ny = 0, yk, c, i, j;

    if (!kanten) return;
    for (c = 0; c < k->nk; c++)
    {
        int s = k->start[c], e = k->start[c + 1];
        for (i = s; i < e; i++)
        {
            int nx = i + 1 < e ? i + 1 : s;
            double x0 = k->p[2 * i], y0 = k->p[2 * i + 1], x1 = k->p[2 * nx], y1 = k->p[2 * nx + 1];
            if (y0 == y1) continue;   /* waagerecht: trägt nichts bei */
            if (y0 < y1) { kanten[ne].xa = x0; kanten[ne].ya = y0; kanten[ne].xb = x1; kanten[ne].yb = y1; kanten[ne].dir = 1; }
            else { kanten[ne].xa = x1; kanten[ne].ya = y1; kanten[ne].xb = x0; kanten[ne].yb = y0; kanten[ne].dir = -1; }
            ne++;
        }
    }
    /* Streifengrenzen: alle Eckpunkt-Höhen und Höhen innerer Schnittpunkte */
    yk = k->n + 16;
    ys = malloc(yk * sizeof(double));
    aktiv = malloc((ne ? ne : 1) * sizeof(*aktiv));
    if (!ys || !aktiv) { free(kanten); free(ys); free(aktiv); return; }
    for (i = 0; i < k->n; i++) ys[ny++] = k->p[2 * i + 1];
    for (i = 0; i < ne; i++)
        for (j = i + 1; j < ne; j++)
        {
            const struct fo_kante *a = &kanten[i], *b = &kanten[j];
            double lo = a->ya > b->ya ? a->ya : b->ya, hi = a->yb < b->yb ? a->yb : b->yb, dl, dh, t;
            if (lo >= hi) continue;
            dl = fo_x_bei(a, lo) - fo_x_bei(b, lo);
            dh = fo_x_bei(a, hi) - fo_x_bei(b, hi);
            if ((dl < 0 && dh > 0) || (dl > 0 && dh < 0))
            {
                t = dl / (dl - dh);
                if (ny == yk)
                {
                    double *q = realloc(ys, (yk *= 2) * sizeof(double));
                    if (!q) continue;
                    ys = q;
                }
                ys[ny++] = lo + t * (hi - lo);
            }
        }
    qsort(ys, ny, sizeof(double), fo_cmp_double);

    fo.Begin(GL_TRIANGLES);
    for (i = 0; i + 1 < ny; i++)
    {
        double y0 = ys[i], y1 = ys[i + 1], ym;
        int na = 0, w = 0, links = 0;
        if (y1 - y0 < 1e-9) continue;
        ym = (y0 + y1) / 2;
        for (j = 0; j < ne; j++)
            if (kanten[j].ya < ym && kanten[j].yb > ym)
            {
                aktiv[na] = kanten[j];
                aktiv[na].xm = fo_x_bei(&kanten[j], ym);
                na++;
            }
        qsort(aktiv, na, sizeof(*aktiv), fo_cmp_kante);
        for (j = 0; j < na; j++)
        {
            int vorher = w;
            w += aktiv[j].dir;
            if (vorher == 0 && w != 0) links = j;
            else if (vorher != 0 && w == 0)
            {
                double l0 = fo_x_bei(&aktiv[links], y0), l1 = fo_x_bei(&aktiv[links], y1);
                double r0 = fo_x_bei(&aktiv[j], y0), r1 = fo_x_bei(&aktiv[j], y1);
                fo.Vertex3d(l0, y0, 0.0); fo.Vertex3d(r0, y0, 0.0); fo.Vertex3d(r1, y1, 0.0);
                fo.Vertex3d(l0, y0, 0.0); fo.Vertex3d(r1, y1, 0.0); fo.Vertex3d(l1, y1, 0.0);
            }
        }
    }
    fo.End();
    free(kanten);
    free(ys);
    free(aktiv);
}

static BOOL use_font_outlines(HDC hdc, DWORD first, DWORD count, DWORD listBase, FLOAT deviation, FLOAT extrusion,
                              int format, LPGLYPHMETRICSFLOAT lpgmf, BOOL unicode)
{
    UINT glyph;
    LOGFONTW lf;
    HFONT old_font, unscaled_font;
    UINT em_size = 1024;
    RECT rc;
    struct fo_konturen k = { 0 };

    TRACE("wglUseFontOutlines%s(%p, %lu, %lu, Liste %lu, Abw. %f, Extr. %f, Format %d, gmf %p)\n", unicode ? "W" : "A",
          hdc, (unsigned long)first, (unsigned long)count, (unsigned long)listBase, deviation, extrusion, format,
          (void *)lpgmf);
    if (debug && fo_diag_laden())
        TRACE("wglUseFontOutlines: Kontext %p, DC %p\n", (void *)fo_GetCurrentContext(), hdc);
    if (!fo_laden())
    {
        TRACE("wglUseFontOutlines: GL-Funktionen fehlen\n");
        return FALSE;
    }
    if (deviation <= 0.0) deviation = 1.0 / em_size;

    GetObjectW(GetCurrentObject(hdc, OBJ_FONT), sizeof(lf), &lf);
    rc.left = rc.right = rc.bottom = 0;
    rc.top = em_size;
    DPtoLP(hdc, (POINT *)&rc, 2);
    lf.lfHeight = -abs(rc.top - rc.bottom);
    lf.lfOrientation = lf.lfEscapement = 0;
    unscaled_font = CreateFontIndirectW(&lf);
    old_font = SelectObject(hdc, unscaled_font);

    for (glyph = first; glyph < first + count; glyph++)
    {
        DWORD needed;
        GLYPHMETRICS gm;
        BYTE *buf;
        TTPOLYGONHEADER *pph;
        TTPOLYCURVE *ppc;
        int c;

        needed = unicode ? GetGlyphOutlineW(hdc, glyph, GGO_NATIVE, &gm, 0, NULL, &fo_identity)
                         : GetGlyphOutlineA(hdc, glyph, GGO_NATIVE, &gm, 0, NULL, &fo_identity);
        if (needed == GDI_ERROR) goto error;
        buf = malloc(needed ? needed : 1);
        if (unicode) GetGlyphOutlineW(hdc, glyph, GGO_NATIVE, &gm, needed, buf, &fo_identity);
        else GetGlyphOutlineA(hdc, glyph, GGO_NATIVE, &gm, needed, buf, &fo_identity);

        if (lpgmf)
        {
            lpgmf->gmfBlackBoxX = (float)gm.gmBlackBoxX / em_size;
            lpgmf->gmfBlackBoxY = (float)gm.gmBlackBoxY / em_size;
            lpgmf->gmfptGlyphOrigin.x = (float)gm.gmptGlyphOrigin.x / em_size;
            lpgmf->gmfptGlyphOrigin.y = (float)gm.gmptGlyphOrigin.y / em_size;
            lpgmf->gmfCellIncX = (float)gm.gmCellIncX / em_size;
            lpgmf->gmfCellIncY = (float)gm.gmCellIncY / em_size;
            lpgmf++;
        }

        /* Umriss in Konturen sammeln (Auswertung wie Wine) */
        k.n = k.nk = 0;
        pph = (TTPOLYGONHEADER *)buf;
        while ((BYTE *)pph < buf + needed)
        {
            GLdouble previous[2];
            fixed_to_double(pph->pfxStart, em_size, previous);
            if (!fo_kontur_beginnen(&k)) break;
            fo_punkt(&k, previous[0], previous[1]);

            ppc = (TTPOLYCURVE *)((char *)pph + sizeof(*pph));
            while ((char *)ppc < (char *)pph + pph->cb)
            {
                int i, j, num;
                switch (ppc->wType)
                {
                case TT_PRIM_LINE:
                    for (i = 0; i < ppc->cpfx; i++)
                    {
                        fixed_to_double(ppc->apfx[i], em_size, previous);
                        fo_punkt(&k, previous[0], previous[1]);
                    }
                    break;

                case TT_PRIM_QSPLINE:
                    for (i = 0; i < ppc->cpfx - 1; i++)
                    {
                        bezier_vector curve[3], *points;
                        GLdouble cv[2];

                        curve[0].x = previous[0];
                        curve[0].y = previous[1];
                        fixed_to_double(ppc->apfx[i], em_size, cv);
                        curve[1].x = cv[0];
                        curve[1].y = cv[1];
                        fixed_to_double(ppc->apfx[i + 1], em_size, cv);
                        curve[2].x = cv[0];
                        curve[2].y = cv[1];
                        if (i < ppc->cpfx - 2)
                        {
                            curve[2].x = (curve[1].x + curve[2].x) / 2;
                            curve[2].y = (curve[1].y + curve[2].y) / 2;
                        }
                        num = bezier_approximate(curve, NULL, deviation);
                        points = malloc(num * sizeof(bezier_vector));
                        if (points)
                        {
                            num = bezier_approximate(curve, points, deviation);
                            for (j = 0; j < num; j++) fo_punkt(&k, points[j].x, points[j].y);
                            free(points);
                        }
                        previous[0] = curve[2].x;
                        previous[1] = curve[2].y;
                    }
                    break;

                default:
                    TRACE("wglUseFontOutlines: Kurventyp %d nicht unterstützt (Zeichen %u)\n", ppc->wType, glyph);
                    break;
                }
                ppc = (TTPOLYCURVE *)((char *)ppc + sizeof(*ppc) + (ppc->cpfx - 1) * sizeof(POINTFX));
            }
            fo_kontur_beenden(&k);
            pph = (TTPOLYGONHEADER *)((char *)pph + pph->cb);
        }

        if (debug && fo_diag_laden()) fo_liste_merken(listBase);
        fo.NewList(listBase++, GL_COMPILE);
        fo.FrontFace(GL_CCW);
        if (format == WGL_FONT_POLYGONS)
        {
            fo.Normal3d(0.0, 0.0, 1.0);
            fo_flaechen(&k);
        }
        else
        {
            for (c = 0; c < k.nk; c++)
            {
                int i;
                if (k.start[c + 1] - k.start[c] < 2) continue;
                fo.Begin(GL_LINE_LOOP);
                for (i = k.start[c]; i < k.start[c + 1]; i++) fo.Vertex3d(k.p[2 * i], k.p[2 * i + 1], 0.0);
                fo.End();
            }
        }
        fo.Translated((GLdouble)gm.gmCellIncX / em_size, (GLdouble)gm.gmCellIncY / em_size, 0.0);
        fo.EndList();
        free(buf);
    }

error:
    DeleteObject(SelectObject(hdc, old_font));
    free(k.p);
    free(k.start);
    return TRUE;
}

BOOL WINAPI wglUseFontOutlinesA(HDC hdc, DWORD first, DWORD count, DWORD listBase, FLOAT deviation, FLOAT extrusion,
                                int format, LPGLYPHMETRICSFLOAT lpgmf)
{
    return use_font_outlines(hdc, first, count, listBase, deviation, extrusion, format, lpgmf, FALSE);
}

BOOL WINAPI wglUseFontOutlinesW(HDC hdc, DWORD first, DWORD count, DWORD listBase, FLOAT deviation, FLOAT extrusion,
                                int format, LPGLYPHMETRICSFLOAT lpgmf)
{
    return use_font_outlines(hdc, first, count, listBase, deviation, extrusion, format, lpgmf, TRUE);
}
