/* Mini-Tracer für Edge.exe: int3-Haltepunkte + Vectored Exception Handler, Protokoll in eine Datei.
 * Steuerdatei C:\setrace\cmd.txt (wird nach dem Lesen gelöscht), eine Anweisung pro Zeile:
 *   bp <modul> <0xRVA|export> [max=N] [bt=N] [stk=N] [str=1] [vt=OFF] [rcx=X] [rdx=X] [r8=X] [r9=X] [label=TEXT]
 *        max = nach N Treffern abschalten, bt = Aufrufkette mit N Rahmen, stk = Stack-Argumente 5..N ausgeben,
 *        str = Register/Argumente als Zeichenkette zeigen, wenn lesbar, vt = Ziel von (*(rcx))[OFF] (virtuelle Methode),
 *        rcx/rdx/r8/r9 = nur bei diesem Registerwert protokollieren (Hex). Mehrere bp auf derselben Adresse sind erlaubt.
 *   clear                    alle Haltepunkte entfernen
 *   mark <text>              Markierung ins Protokoll
 * Protokoll: C:\setrace\log.txt (Zeit, Thread, Treffer, rax/rcx/rdx/r8/r9, Rücksprung als Modul+RVA).
 * Bauen:  x86_64-w64-mingw32-gcc -shared -O2 -o setrace.dll setrace.c   (ohne libgcc, nur KERNEL32 + UCRT)
 * Laden:  DLL nach prefix/drive_c/setrace/ kopieren, dann
 *         wine mcp/capture/secapture-inject.exe 'C:\setrace\setrace.dll'
 * Nach einer Änderung neuen Dateinamen wählen (eine geladene DLL lässt sich nicht ersetzen).
 * Haltepunkte: int3 + Einzelschritt (Trap-Flag) zum Wiedereinsetzen, funktioniert unter Wine/Rosetta.
 * Siehe docs/skizze-schliessen.md.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXBP 256
typedef struct {
    BYTE *addr; BYTE orig; int aktiv; int treffer; int max; int bt; int stk; int str; int vt;
    int hatbed[4]; ULONG64 bed[4];
    char label[300];
} BP;
static BP bps[MAXBP];
static int nbp;
static CRITICAL_SECTION cs;
static FILE *logf;
static DWORD tlsi;
#define PENDING_GET() ((BP *)TlsGetValue(tlsi))
#define PENDING_SET(x) TlsSetValue(tlsi,(x))

static void schreib(const char *fmt, ...)
{
    va_list a; va_start(a, fmt);
    EnterCriticalSection(&cs);
    if (logf) { vfprintf(logf, fmt, a); fflush(logf); }
    LeaveCriticalSection(&cs);
    va_end(a);
}

static void modname(ULONG64 a, char *buf, size_t n)
{
    HMODULE m = NULL; char p[MAX_PATH], *b;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)a, &m) && m) {
        GetModuleFileNameA(m, p, MAX_PATH); b = strrchr(p, '\\');
        snprintf(buf, n, "%s+0x%llx", b ? b + 1 : p, (unsigned long long)(a - (ULONG64)m));
    } else snprintf(buf, n, "%llx", (unsigned long long)a);
}

static void setz(BP *b, int an)
{
    DWORD alt;
    VirtualProtect(b->addr, 1, PAGE_EXECUTE_READWRITE, &alt);
    *b->addr = an ? 0xCC : b->orig;
    VirtualProtect(b->addr, 1, alt, &alt);
    FlushInstructionCache(GetCurrentProcess(), b->addr, 1);
}


static int lesbar(ULONG64 p)
{
    MEMORY_BASIC_INFORMATION mi;
    if (p < 0x10000 || !VirtualQuery((void *)p, &mi, sizeof mi)) return 0;
    if (mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
    return (ULONG64)mi.BaseAddress + mi.RegionSize - p >= 64;
}
static void zeig(const char *was, ULONG64 p)
{
    char out[200]; int i, n = 0;
    if (!lesbar(p)) return;
    const unsigned char *a = (const unsigned char *)p;
    if (a[0] >= 32 && a[0] < 127 && a[1] == 0 && a[2] >= 32 && a[2] < 127 && a[3] == 0) {
        const wchar_t *w = (const wchar_t *)p;
        for (i = 0; i < 120 && w[i]; i++) out[n++] = (w[i] >= 32 && w[i] < 127) ? (char)w[i] : '?';
    } else if (a[0] >= 32 && a[0] < 127) {
        for (i = 0; i < 120 && a[i]; i++) out[n++] = (a[i] >= 32 && a[i] < 127) ? (char)a[i] : '?';
    } else return;
    out[n] = 0;
    schreib("      %s -> \"%s\"\n", was, out);
}

static void backtrace(CONTEXT *c0, int tiefe)
{
    CONTEXT c = *c0; int i;
    for (i = 0; i < tiefe; i++) {
        ULONG64 base = 0; PVOID hd; ULONG64 ef; char nm[160];
        PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(c.Rip, &base, NULL);
        if (!f) { c.Rip = *(ULONG64 *)c.Rsp; c.Rsp += 8; }
        else RtlVirtualUnwind(0, base, c.Rip, f, &c, &hd, &ef, NULL);
        if (!c.Rip) break;
        modname(c.Rip, nm, sizeof nm);
        schreib("    #%d %s\n", i + 1, nm);
    }
}

static LONG CALLBACK veh(EXCEPTION_POINTERS *e)
{
    DWORD code = e->ExceptionRecord->ExceptionCode;
    CONTEXT *c = e->ContextRecord;
    int i;
    if (code == EXCEPTION_BREAKPOINT) {
        BYTE *a = (BYTE *)e->ExceptionRecord->ExceptionAddress, *hit = NULL;
        BP *erster = NULL; int noch = 0;
        for (i = 0; i < nbp; i++) {
            BP *b = &bps[i];
            if (!b->aktiv) continue;
            if (!hit) { if (b->addr == a || b->addr == (BYTE *)c->Rip - 1 || b->addr == (BYTE *)c->Rip) hit = b->addr; else continue; }
            if (b->addr != hit) continue;
            if (!erster) erster = b;
            ULONG64 r[4] = { c->Rcx, c->Rdx, c->R8, c->R9 };
            int ok = 1, k;
            for (k = 0; k < 4; k++) if (b->hatbed[k] && r[k] != b->bed[k]) ok = 0;
            if (ok) {
                char ret[160]; LARGE_INTEGER t, fq;
                QueryPerformanceCounter(&t); QueryPerformanceFrequency(&fq);
                modname(*(ULONG64 *)c->Rsp, ret, sizeof ret);
                b->treffer++;
                schreib("%.3f t%04lx %s #%d rax=%llx rcx=%llx rdx=%llx r8=%llx r9=%llx ret=%s\n",
                        (double)t.QuadPart / fq.QuadPart, GetCurrentThreadId(), b->label, b->treffer,
                        c->Rax, c->Rcx, c->Rdx, c->R8, c->R9, ret);
                if (b->str) { zeig("rcx", c->Rcx); zeig("rdx", c->Rdx); zeig("r8", c->R8); zeig("r9", c->R9); }
                if (b->stk) { int q; char nm[16]; ULONG64 *sp = (ULONG64 *)c->Rsp;
                    for (q = 5; q <= b->stk; q++) { schreib("      arg%d=%llx\n", q, sp[q]); snprintf(nm, sizeof nm, "arg%d", q); if (b->str) zeig(nm, sp[q]); } }
                if (b->vt && lesbar(c->Rcx) && lesbar(*(ULONG64 *)c->Rcx)) {
                    char nm[160], nv[160]; ULONG64 vtab = *(ULONG64 *)c->Rcx;
                    modname(vtab, nv, sizeof nv); modname(*(ULONG64 *)(vtab + b->vt - 1), nm, sizeof nm);
                    schreib("      vtbl=%s  [+0x%x]=%s\n", nv, b->vt - 1, nm);
                }
                if (b->bt) backtrace(c, b->bt);
                if (b->max && b->treffer >= b->max) { b->aktiv = 0; schreib("  (%s deaktiviert)\n", b->label); }
            }
            if (b->aktiv) noch = 1;
        }
        if (erster) {
            c->Rip = (ULONG64)hit;
            setz(erster, 0);
            if (noch) { PENDING_SET(erster); c->EFlags |= 0x100; }
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    } else if (code == EXCEPTION_SINGLE_STEP && PENDING_GET()) {
        BP *b = PENDING_GET(); PENDING_SET(NULL);
        for (i = 0; i < nbp; i++) if (bps[i].aktiv && bps[i].addr == b->addr) { setz(b, 1); break; }
        c->EFlags &= ~0x100;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void anweisung(char *z)
{
    char mod[128], wo[128]; int n = 0;
    if (!strncmp(z, "clear", 5)) {
        int i; for (i = 0; i < nbp; i++) if (bps[i].aktiv) { bps[i].aktiv = 0; setz(&bps[i], 0); }
        nbp = 0; schreib("== clear\n"); return;
    }
    if (!strncmp(z, "mark", 4)) { schreib("== MARK %s", z + 4); return; }
    if (sscanf(z, "bp %127s %127s%n", mod, wo, &n) == 2 && nbp < MAXBP) {
        HMODULE m = GetModuleHandleA(mod); BYTE *a = NULL; BP *b = &bps[nbp];
        char *rest = z + n, *tok;
        if (!m) m = LoadLibraryExA(mod, NULL, 0);
        if (!m) { schreib("!! Modul %s fehlt\n", mod); return; }
        if (wo[0] == '0' && wo[1] == 'x') a = (BYTE *)m + strtoull(wo, NULL, 16);
        else a = (BYTE *)GetProcAddress(m, wo);
        if (!a) { schreib("!! %s!%s fehlt\n", mod, wo); return; }
        memset(b, 0, sizeof *b);
        b->addr = a; b->orig = *a;
        snprintf(b->label, sizeof b->label, "%s!%s", mod, wo);
        for (tok = strtok(rest, " \t\r\n"); tok; tok = strtok(NULL, " \t\r\n")) {
            if (!strncmp(tok, "max=", 4)) b->max = atoi(tok + 4);
            else if (!strncmp(tok, "bt=", 3)) b->bt = atoi(tok + 3);
            else if (!strncmp(tok, "stk=", 4)) b->stk = atoi(tok + 4);
            else if (!strncmp(tok, "str=", 4)) b->str = atoi(tok + 4);
            else if (!strncmp(tok, "vt=", 3)) b->vt = (int)strtol(tok + 3, NULL, 16) + 1;
            else if (!strncmp(tok, "rcx=", 4)) { b->hatbed[0] = 1; b->bed[0] = strtoull(tok + 4, NULL, 16); }
            else if (!strncmp(tok, "rdx=", 4)) { b->hatbed[1] = 1; b->bed[1] = strtoull(tok + 4, NULL, 16); }
            else if (!strncmp(tok, "r8=", 3)) { b->hatbed[2] = 1; b->bed[2] = strtoull(tok + 3, NULL, 16); }
            else if (!strncmp(tok, "r9=", 3)) { b->hatbed[3] = 1; b->bed[3] = strtoull(tok + 3, NULL, 16); }
            else if (!strncmp(tok, "label=", 6)) snprintf(b->label, sizeof b->label, "%s", tok + 6);
        }
        { int j; for (j = 0; j < nbp; j++) if (bps[j].aktiv && bps[j].addr == a) { b->orig = bps[j].orig; break; } }
        if (b->orig == 0xCC) { schreib("!! %s: schon int3\n", b->label); return; }
        b->aktiv = 1; nbp++;
        setz(b, 1);
        schreib("== bp %s @%p orig=%02x\n", b->label, a, b->orig);
    }
}

static DWORD WINAPI waechter(LPVOID p)
{
    (void)p;
    for (;;) {
        FILE *f = fopen("C:\\setrace\\cmd.txt", "r");
        if (f) {
            char z[512];
            while (fgets(z, sizeof z, f)) anweisung(z);
            fclose(f);
            DeleteFileA("C:\\setrace\\cmd.txt");
        }
        Sleep(100);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD grund, LPVOID res)
{
    (void)res;
    if (grund == DLL_PROCESS_ATTACH) {
        char p[MAX_PATH], *b; HMODULE self;
        GetModuleFileNameA(NULL, p, MAX_PATH); b = strrchr(p, '\\');
        if (_stricmp(b ? b + 1 : p, "Edge.exe")) return TRUE;
        DisableThreadLibraryCalls(inst);
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)DllMain, &self);
        InitializeCriticalSection(&cs); tlsi = TlsAlloc();
        CreateDirectoryA("C:\\setrace", NULL);
        logf = fopen("C:\\setrace\\log.txt", "a");
        schreib("== setrace geladen pid %lu\n", GetCurrentProcessId());
        AddVectoredExceptionHandler(1, veh);
        CloseHandle(CreateThread(NULL, 0, waechter, NULL, 0, NULL));
    }
    return TRUE;
}
