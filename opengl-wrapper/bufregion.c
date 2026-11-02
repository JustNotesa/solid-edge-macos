/*
 * GL_KTX_buffer_region und WGL_ARB_buffer_region für Mesa (wird von memdc_gl.c eingebunden).
 *
 * Solid Edge speichert damit den gerenderten Hintergrund („Backing Store“) und stellt ihn bei jeder
 * Mausbewegung wieder her, bevor es Hervorhebungen und den Ansichtswürfel darüber zeichnet. Ohne die
 * Erweiterung (unter Windows liefern sie NVIDIA/AMD/Intel) fehlen diese Einblendungen.
 *
 * Umsetzung auf der GPU: jede Region ist ein FBO in Größe des Zeichenbereichs (RGBA8 und/oder
 * DEPTH24_STENCIL8). Speichern/Wiederherstellen per glBlitFramebuffer zwischen Standard-Framebuffer und FBO,
 * in Fensterkoordinaten (Ursprung unten links), die Region deckt also den ganzen Zeichenbereich ab:
 *   KTX: glReadBufferRegion(id, x, y, w, h)                → Rechteck speichern
 *        glDrawBufferRegion(id, x, y, w, h, xDest, yDest) → gespeichertes Rechteck (x,y,w,h) nach (xDest,yDest)
 *   WGL: wglSaveBufferRegionARB(h, x, y, w, h)             → Rechteck speichern
 *        wglRestoreBufferRegionARB(h, x, y, w, h, xs, ys) → gespeichertes Rechteck ab (xs,ys) nach (x,y)
 */

#define GL_FRAMEBUFFER_ 0x8D40
#define GL_DRAW_FRAMEBUFFER_ 0x8CA9
#define GL_DRAW_FRAMEBUFFER_BINDING_ 0x8CA6
#define GL_RENDERBUFFER_ 0x8D41
#define GL_RENDERBUFFER_BINDING_ 0x8CA7
#define GL_COLOR_ATTACHMENT0_ 0x8CE0
#define GL_DEPTH_STENCIL_ATTACHMENT_ 0x821A
#define GL_RGBA8_ 0x8058
#define GL_DEPTH24_STENCIL8_ 0x88F0
#define GL_FRAMEBUFFER_COMPLETE_ 0x8CD5
#define GL_RASTERIZER_DISCARD_ 0x8C89
#define GL_FRAMEBUFFER_SRGB_ 0x8DB9
#define GL_STENCIL_BACK_WRITEMASK_ 0x8CA5

/* KTX-Regionstypen und WGL-Bits */
#define KTX_FRONT 0
#define KTX_BACK 1
#define KTX_Z 2
#define KTX_STENCIL 3
#define WGL_FRONT_COLOR_BUFFER_BIT_ARB 0x1
#define WGL_BACK_COLOR_BUFFER_BIT_ARB 0x2
#define WGL_DEPTH_BUFFER_BIT_ARB 0x4
#define WGL_STENCIL_BUFFER_BIT_ARB 0x8

#define MAX_REGIONEN 256

struct region
{
    BOOL belegt;
    unsigned bits;      /* WGL_*_BIT_ARB */
    GLuint fbo, farbe, tiefe;
    int w, h;
};

static struct region regionen[MAX_REGIONEN];
static unsigned br_saves, br_restores;

/* GL-Funktionen (über wglGetProcAddress bzw. Exporte geladen) */
static void (APIENTRY *p_GenFramebuffers)(GLsizei, GLuint *);
static void (APIENTRY *p_DeleteFramebuffers)(GLsizei, const GLuint *);
static void (APIENTRY *p_BindFramebuffer)(GLenum, GLuint);
static void (APIENTRY *p_GenRenderbuffers)(GLsizei, GLuint *);
static void (APIENTRY *p_DeleteRenderbuffers)(GLsizei, const GLuint *);
static void (APIENTRY *p_BindRenderbuffer)(GLenum, GLuint);
static void (APIENTRY *p_RenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
static void (APIENTRY *p_FramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
static GLenum (APIENTRY *p_CheckFramebufferStatus)(GLenum);
static void (APIENTRY *p_BlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
static void (APIENTRY *p_DrawBuffer)(GLenum);
static void (APIENTRY *p_Enable)(GLenum);
static void (APIENTRY *p_Disable)(GLenum);
static GLboolean (APIENTRY *p_IsEnabled)(GLenum);
static void (APIENTRY *p_GetBooleanv)(GLenum, GLboolean *);
static void (APIENTRY *p_ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
static void (APIENTRY *p_DepthMask)(GLboolean);
static void (APIENTRY *p_StencilMask)(GLuint);
static void (APIENTRY *p_StencilMaskSeparate)(GLenum, GLuint);

static BOOL br_bereit(void)
{
    static int status; /* 0 = ungeprüft, 1 = ok, -1 = nicht verfügbar */
    if (status) return status > 0;
#define GLX(var, name) (var = (void *)r_GetProcAddress(name))
#define EXP(var, name) (var = (void *)GetProcAddress(real, name))
    if (GLX(p_GenFramebuffers, "glGenFramebuffers") && GLX(p_DeleteFramebuffers, "glDeleteFramebuffers") &&
        GLX(p_BindFramebuffer, "glBindFramebuffer") && GLX(p_GenRenderbuffers, "glGenRenderbuffers") &&
        GLX(p_DeleteRenderbuffers, "glDeleteRenderbuffers") && GLX(p_BindRenderbuffer, "glBindRenderbuffer") &&
        GLX(p_RenderbufferStorage, "glRenderbufferStorage") &&
        GLX(p_FramebufferRenderbuffer, "glFramebufferRenderbuffer") &&
        GLX(p_CheckFramebufferStatus, "glCheckFramebufferStatus") && GLX(p_BlitFramebuffer, "glBlitFramebuffer") &&
        GLX(p_StencilMaskSeparate, "glStencilMaskSeparate") &&
        EXP(p_DrawBuffer, "glDrawBuffer") && EXP(p_Enable, "glEnable") && EXP(p_Disable, "glDisable") &&
        EXP(p_IsEnabled, "glIsEnabled") && EXP(p_GetBooleanv, "glGetBooleanv") &&
        EXP(p_ColorMask, "glColorMask") && EXP(p_DepthMask, "glDepthMask") && EXP(p_StencilMask, "glStencilMask"))
        status = 1;
    else
        status = -1;
#undef GLX
#undef EXP
    TRACE("Buffer Regions %s\n", status > 0 ? "verfügbar (FBO + glBlitFramebuffer)" : "NICHT verfügbar");
    return status > 0;
}

/* Größe des aktuellen Zeichenbereichs (Fenster bzw. Hilfsfenster eines Speicher-DCs). */
static void zeichenbereich(int *w, int *h)
{
    RECT rc;
    HWND hwnd = WindowFromDC(r_GetCurrentDC());
    *w = *h = 0;
    if (hwnd && GetClientRect(hwnd, &rc)) { *w = rc.right - rc.left; *h = rc.bottom - rc.top; }
}

static struct region *region_holen(unsigned id)
{
    if (!id || id > MAX_REGIONEN || !regionen[id - 1].belegt) return NULL;
    return &regionen[id - 1];
}

static unsigned region_neu(unsigned bits)
{
    unsigned i;
    EnterCriticalSection(&lock);
    for (i = 0; i < MAX_REGIONEN; i++)
        if (!regionen[i].belegt)
        {
            memset(&regionen[i], 0, sizeof(regionen[i]));
            regionen[i].belegt = TRUE;
            regionen[i].bits = bits;
            break;
        }
    LeaveCriticalSection(&lock);
    return i < MAX_REGIONEN ? i + 1 : 0;
}

static void region_freigeben(unsigned id)
{
    struct region *r = region_holen(id);
    if (!r) return;
    if (r->fbo && r_GetCurrentContext()) p_DeleteFramebuffers(1, &r->fbo);
    if (r->farbe && r_GetCurrentContext()) p_DeleteRenderbuffers(1, &r->farbe);
    if (r->tiefe && r_GetCurrentContext()) p_DeleteRenderbuffers(1, &r->tiefe);
    r->belegt = FALSE;
}

/* Speicher der Region anlegen bzw. auf mindestens (w,h) vergrößern (Inhalt geht dabei verloren). */
static BOOL region_speicher(struct region *r, int w, int h)
{
    GLint alt_rb = 0;
    GLenum status;
    if (r->fbo && r->w >= w && r->h >= h) return TRUE;
    if (w < r->w) w = r->w;
    if (h < r->h) h = r->h;
    if (r->fbo) p_DeleteFramebuffers(1, &r->fbo);
    if (r->farbe) p_DeleteRenderbuffers(1, &r->farbe);
    if (r->tiefe) p_DeleteRenderbuffers(1, &r->tiefe);
    r->fbo = r->farbe = r->tiefe = 0;

    r_glGetIntegerv(GL_RENDERBUFFER_BINDING_, &alt_rb);
    p_GenFramebuffers(1, &r->fbo);
    p_BindFramebuffer(GL_FRAMEBUFFER_, r->fbo);
    if (r->bits & (WGL_FRONT_COLOR_BUFFER_BIT_ARB | WGL_BACK_COLOR_BUFFER_BIT_ARB))
    {
        p_GenRenderbuffers(1, &r->farbe);
        p_BindRenderbuffer(GL_RENDERBUFFER_, r->farbe);
        p_RenderbufferStorage(GL_RENDERBUFFER_, GL_RGBA8_, w, h);
        p_FramebufferRenderbuffer(GL_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_RENDERBUFFER_, r->farbe);
    }
    if (r->bits & (WGL_DEPTH_BUFFER_BIT_ARB | WGL_STENCIL_BUFFER_BIT_ARB))
    {
        p_GenRenderbuffers(1, &r->tiefe);
        p_BindRenderbuffer(GL_RENDERBUFFER_, r->tiefe);
        p_RenderbufferStorage(GL_RENDERBUFFER_, GL_DEPTH24_STENCIL8_, w, h);
        p_FramebufferRenderbuffer(GL_FRAMEBUFFER_, GL_DEPTH_STENCIL_ATTACHMENT_, GL_RENDERBUFFER_, r->tiefe);
    }
    if (!r->farbe) { p_DrawBuffer(GL_NONE); r_glReadBuffer(GL_NONE); }
    status = p_CheckFramebufferStatus(GL_FRAMEBUFFER_);
    p_BindRenderbuffer(GL_RENDERBUFFER_, alt_rb);
    r->w = w;
    r->h = h;
    TRACE("Region-Speicher %ux%u (Bits %#x) → %s\n", w, h, r->bits, status == GL_FRAMEBUFFER_COMPLETE_ ? "OK" : "UNVOLLSTÄNDIG");
    return status == GL_FRAMEBUFFER_COMPLETE_;
}

/* Puffer des Standard-Framebuffers zu den Farb-Bits (einfach gepuffert: immer GL_FRONT). */
static GLenum standard_puffer(unsigned bits)
{
    GLboolean doppelt = GL_FALSE;
    p_GetBooleanv(GL_DOUBLEBUFFER, &doppelt);
    if (!doppelt) return GL_FRONT;
    return (bits & WGL_BACK_COLOR_BUFFER_BIT_ARB) ? GL_BACK : GL_FRONT;
}

/* Kopiert ein Rechteck zwischen Standard-Framebuffer und Region. speichern = TRUE: Fenster → Region. */
static BOOL region_blit(struct region *r, BOOL speichern, int sx, int sy, int w, int h, int dx, int dy)
{
    GLint read_fb = 0, draw_fb = 0, read_buf = GL_BACK, draw_buf = GL_BACK;
    GLboolean scissor, discard, srgb, farbmaske[4], tiefenmaske;
    GLint stencilmaske = ~0, stencilmaske_hinten = ~0;
    GLbitfield maske = 0;
    GLenum puffer, fehler;
    int bw, bh;

    if (w <= 0 || h <= 0) return TRUE;
    zeichenbereich(&bw, &bh);
    if (bw < sx + w) bw = sx + w;
    if (bh < sy + h) bh = sy + h;
    if (bw < dx + w) bw = dx + w;
    if (bh < dy + h) bh = dy + h;

    r_glGetError();
    r_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_, &read_fb);
    r_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_, &draw_fb);
    p_BindFramebuffer(GL_FRAMEBUFFER_, 0);
    r_glGetIntegerv(GL_READ_BUFFER, &read_buf);
    r_glGetIntegerv(GL_DRAW_BUFFER, &draw_buf);
    scissor = p_IsEnabled(GL_SCISSOR_TEST);
    discard = p_IsEnabled(GL_RASTERIZER_DISCARD_);
    srgb = p_IsEnabled(GL_FRAMEBUFFER_SRGB_);
    p_GetBooleanv(GL_COLOR_WRITEMASK, farbmaske);
    p_GetBooleanv(GL_DEPTH_WRITEMASK, &tiefenmaske);
    r_glGetIntegerv(GL_STENCIL_WRITEMASK, &stencilmaske);
    r_glGetIntegerv(GL_STENCIL_BACK_WRITEMASK_, &stencilmaske_hinten);
    r_glGetError();

    if (!region_speicher(r, bw, bh)) { p_BindFramebuffer(GL_READ_FRAMEBUFFER_, read_fb); p_BindFramebuffer(GL_DRAW_FRAMEBUFFER_, draw_fb); return FALSE; }

    if (scissor) p_Disable(GL_SCISSOR_TEST);
    if (discard) p_Disable(GL_RASTERIZER_DISCARD_);
    if (srgb) p_Disable(GL_FRAMEBUFFER_SRGB_);
    p_ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    p_DepthMask(GL_TRUE);
    p_StencilMask(~0u);

    if (r->bits & (WGL_FRONT_COLOR_BUFFER_BIT_ARB | WGL_BACK_COLOR_BUFFER_BIT_ARB)) maske |= GL_COLOR_BUFFER_BIT;
    if (r->bits & WGL_DEPTH_BUFFER_BIT_ARB) maske |= GL_DEPTH_BUFFER_BIT;
    if (r->bits & WGL_STENCIL_BUFFER_BIT_ARB) maske |= GL_STENCIL_BUFFER_BIT;
    puffer = standard_puffer(r->bits);

    if (speichern)
    {
        p_BindFramebuffer(GL_READ_FRAMEBUFFER_, 0);
        if (maske & GL_COLOR_BUFFER_BIT) r_glReadBuffer(puffer);
        p_BindFramebuffer(GL_DRAW_FRAMEBUFFER_, r->fbo);
    }
    else
    {
        p_BindFramebuffer(GL_READ_FRAMEBUFFER_, r->fbo);
        p_BindFramebuffer(GL_DRAW_FRAMEBUFFER_, 0);
        if (maske & GL_COLOR_BUFFER_BIT) p_DrawBuffer(puffer);
    }
    p_BlitFramebuffer(sx, sy, sx + w, sy + h, dx, dy, dx + w, dy + h, maske, GL_NEAREST);
    fehler = r_glGetError();

    /* Zustand wiederherstellen: erst Lese-/Zeichenpuffer des Standard-Framebuffers, dann Bindungen. */
    p_BindFramebuffer(GL_FRAMEBUFFER_, 0);
    r_glReadBuffer(read_buf);
    p_DrawBuffer(draw_buf);
    p_BindFramebuffer(GL_READ_FRAMEBUFFER_, read_fb);
    p_BindFramebuffer(GL_DRAW_FRAMEBUFFER_, draw_fb);
    p_ColorMask(farbmaske[0], farbmaske[1], farbmaske[2], farbmaske[3]);
    p_DepthMask(tiefenmaske);
    p_StencilMaskSeparate(GL_FRONT, stencilmaske);
    p_StencilMaskSeparate(GL_BACK, stencilmaske_hinten);
    if (scissor) p_Enable(GL_SCISSOR_TEST);
    if (discard) p_Enable(GL_RASTERIZER_DISCARD_);
    if (srgb) p_Enable(GL_FRAMEBUFFER_SRGB_);

    if (speichern ? OFT_PROTOKOLLIEREN(br_saves) : OFT_PROTOKOLLIEREN(br_restores))
        TRACE("Region %s #%u: (%d,%d %dx%d) → (%d,%d), Bits %#x, Puffer %#x, GL-Fehler %#x\n",
              speichern ? "speichern" : "wiederherstellen", speichern ? br_saves : br_restores,
              sx, sy, w, h, dx, dy, r->bits, puffer, fehler);
    return fehler == GL_NO_ERROR;
}

/* ---- GL_KTX_buffer_region ---- */
static GLuint APIENTRY ktx_BufferRegionEnabled(void)
{
    return br_bereit() ? 1 : 0;
}

static GLuint APIENTRY ktx_NewBufferRegion(GLenum typ)
{
    static const unsigned bits[] = { WGL_FRONT_COLOR_BUFFER_BIT_ARB, WGL_BACK_COLOR_BUFFER_BIT_ARB,
                                     WGL_DEPTH_BUFFER_BIT_ARB, WGL_STENCIL_BUFFER_BIT_ARB };
    unsigned id;
    if (!br_bereit() || typ > KTX_STENCIL) return 0;
    id = region_neu(bits[typ]);
    TRACE("glNewBufferRegion(Typ %u) → %u\n", typ, id);
    return id;
}

static void APIENTRY ktx_DeleteBufferRegion(GLuint id)
{
    region_freigeben(id);
}

static void APIENTRY ktx_ReadBufferRegion(GLuint id, GLint x, GLint y, GLsizei w, GLsizei h)
{
    struct region *r = region_holen(id);
    if (r) region_blit(r, TRUE, x, y, w, h, x, y);
}

static void APIENTRY ktx_DrawBufferRegion(GLuint id, GLint x, GLint y, GLsizei w, GLsizei h, GLint xd, GLint yd)
{
    struct region *r = region_holen(id);
    if (r) region_blit(r, FALSE, x, y, w, h, xd, yd);
}

/* ---- WGL_ARB_buffer_region ---- */
static HANDLE WINAPI arb_CreateBufferRegion(HDC hdc, int ebene, UINT typ)
{
    unsigned id;
    if (ebene != 0 || !typ || (typ & ~0xfu) || !br_bereit()) return NULL;
    id = region_neu(typ);
    TRACE("wglCreateBufferRegionARB(%p, Typ %#x) → %u\n", hdc, typ, id);
    return id ? (HANDLE)(UINT_PTR)id : NULL;
}

static VOID WINAPI arb_DeleteBufferRegion(HANDLE h)
{
    region_freigeben((unsigned)(UINT_PTR)h);
}

static BOOL WINAPI arb_SaveBufferRegion(HANDLE h, int x, int y, int w, int hh)
{
    struct region *r = region_holen((unsigned)(UINT_PTR)h);
    return r ? region_blit(r, TRUE, x, y, w, hh, x, y) : FALSE;
}

static BOOL WINAPI arb_RestoreBufferRegion(HANDLE h, int x, int y, int w, int hh, int xs, int ys)
{
    struct region *r = region_holen((unsigned)(UINT_PTR)h);
    return r ? region_blit(r, FALSE, xs, ys, w, hh, x, y) : FALSE;
}

/* ---- Erweiterungs-Strings ---- */
static const char *(WINAPI *r_GetExtensionsStringARB)(HDC);
static const char *(WINAPI *r_GetExtensionsStringEXT)(void);

static const char *mit_zusatz(const char *basis, const char *zusatz, const char **cache_basis, char **cache)
{
    if (!basis) return basis;
    if (strstr(basis, zusatz)) return basis;
    EnterCriticalSection(&lock);
    if (*cache_basis != basis || !*cache)
    {
        size_t n = strlen(basis) + strlen(zusatz) + 2;
        char *neu = malloc(n);
        if (neu)
        {
            snprintf(neu, n, "%s%s%s", basis, (*basis && basis[strlen(basis) - 1] != ' ') ? " " : "", zusatz);
            free(*cache);
            *cache = neu;
            *cache_basis = basis;
        }
    }
    LeaveCriticalSection(&lock);
    return *cache ? *cache : basis;
}

static const char *WINAPI my_GetExtensionsStringARB(HDC hdc)
{
    static const char *b; static char *c;
    return mit_zusatz(r_GetExtensionsStringARB ? r_GetExtensionsStringARB(hdc) : "", "WGL_ARB_buffer_region", &b, &c);
}

static const char *WINAPI my_GetExtensionsStringEXT(void)
{
    static const char *b; static char *c;
    return mit_zusatz(r_GetExtensionsStringEXT ? r_GetExtensionsStringEXT() : "", "WGL_ARB_buffer_region", &b, &c);
}

static const GLubyte *gl_extensions_mit_ktx(const GLubyte *s)
{
    static const char *b; static char *c;
    return (const GLubyte *)mit_zusatz((const char *)s, "GL_KTX_buffer_region", &b, &c);
}

/* Liefert eigene Funktionen für alle Buffer-Region-Namen (mit/ohne ARB/EXT-Suffix), sonst NULL. */
static PROC bufregion_proc(const char *name, PROC mesa)
{
    static const struct { const char *name; PROC f; } tab[] = {
        { "glBufferRegionEnabled", (PROC)ktx_BufferRegionEnabled },
        { "glNewBufferRegion", (PROC)ktx_NewBufferRegion },
        { "glDeleteBufferRegion", (PROC)ktx_DeleteBufferRegion },
        { "glReadBufferRegion", (PROC)ktx_ReadBufferRegion },
        { "glDrawBufferRegion", (PROC)ktx_DrawBufferRegion },
        { "wglCreateBufferRegion", (PROC)arb_CreateBufferRegion },
        { "wglDeleteBufferRegion", (PROC)arb_DeleteBufferRegion },
        { "wglSaveBufferRegion", (PROC)arb_SaveBufferRegion },
        { "wglRestoreBufferRegion", (PROC)arb_RestoreBufferRegion },
    };
    size_t i, n;
    if (!strcmp(name, "wglGetExtensionsStringARB"))
    {
        if (mesa && !r_GetExtensionsStringARB) r_GetExtensionsStringARB = (void *)mesa;
        return (PROC)my_GetExtensionsStringARB;
    }
    if (!strcmp(name, "wglGetExtensionsStringEXT"))
    {
        if (mesa && !r_GetExtensionsStringEXT) r_GetExtensionsStringEXT = (void *)mesa;
        return (PROC)my_GetExtensionsStringEXT;
    }
    for (i = 0; i < sizeof(tab) / sizeof(tab[0]); i++)
    {
        n = strlen(tab[i].name);
        if (!strncmp(name, tab[i].name, n) &&
            (!name[n] || !strcmp(name + n, "ARB") || !strcmp(name + n, "EXT")))
            return br_bereit() ? tab[i].f : NULL;
    }
    return NULL;
}
