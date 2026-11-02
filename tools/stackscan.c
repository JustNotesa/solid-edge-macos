/* stackscan.exe <pid> [tid…]  – Register + heuristische Aufrufkette (Rücksprungadressen auf dem Stack) fremder Threads.
 * Ohne tid: alle Threads des Prozesses. Rücksprungadressen werden als Modul+RVA (+ nächster Export) ausgegeben. */
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <winternl.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { BYTE *base; DWORD size; WCHAR name[64]; DWORD *fn; DWORD *names; WORD *ords; DWORD nnames; char *namebuf; BYTE *img; } MOD;
static MOD mods[2048]; static int nmods; static HANDLE proc;

static BOOL rd(const void *a, void *b, SIZE_T n) { SIZE_T g = 0; return ReadProcessMemory(proc, a, b, n, &g) && g == n; }

static void load_mods(void)
{
    HMODULE hm[2048]; DWORD need; int i;
    if (!EnumProcessModulesEx(proc, hm, sizeof(hm), &need, LIST_MODULES_ALL)) { printf("EnumProcessModules %lu\n", GetLastError()); return; }
    nmods = need / sizeof(HMODULE);
    for (i = 0; i < nmods; i++) {
        MODULEINFO mi; mods[i].base = (BYTE *)hm[i];
        GetModuleInformation(proc, hm[i], &mi, sizeof(mi)); mods[i].size = mi.SizeOfImage;
        GetModuleBaseNameW(proc, hm[i], mods[i].name, 64);
    }
}

static void load_exports(MOD *m)
{
    IMAGE_DOS_HEADER dos; IMAGE_NT_HEADERS64 nt; IMAGE_EXPORT_DIRECTORY ed; DWORD rva, sz;
    if (m->img) return;
    m->img = (BYTE *)1;
    if (!rd(m->base, &dos, sizeof(dos)) || !rd(m->base + dos.e_lfanew, &nt, sizeof(nt))) return;
    rva = nt.OptionalHeader.DataDirectory[0].VirtualAddress; sz = nt.OptionalHeader.DataDirectory[0].Size;
    if (!rva || !rd(m->base + rva, &ed, sizeof(ed))) return;
    m->fn = malloc(ed.NumberOfFunctions * 4); m->names = malloc(ed.NumberOfNames * 4); m->ords = malloc(ed.NumberOfNames * 2);
    if (!rd(m->base + ed.AddressOfFunctions, m->fn, ed.NumberOfFunctions * 4) || !rd(m->base + ed.AddressOfNames, m->names, ed.NumberOfNames * 4)
        || !rd(m->base + ed.AddressOfNameOrdinals, m->ords, ed.NumberOfNames * 2)) { m->nnames = 0; return; }
    m->nnames = ed.NumberOfNames;
    (void)sz;
}

static void sym(ULONG64 a, char *out, size_t n)
{
    int i; DWORD k;
    for (i = 0; i < nmods; i++) if (a >= (ULONG64)mods[i].base && a < (ULONG64)mods[i].base + mods[i].size) break;
    if (i == nmods) { snprintf(out, n, "%llx", a); return; }
    MOD *m = &mods[i]; DWORD r = (DWORD)(a - (ULONG64)m->base), best = 0; int bi = -1;
    load_exports(m);
    for (k = 0; k < m->nnames; k++) { DWORD f = m->fn[m->ords[k]]; if (f <= r && f > best) { best = f; bi = k; } }
    if (bi >= 0 && r - best < 0x20000) {
        char nm[128] = ""; rd(m->base + m->names[bi], nm, 127); nm[127] = 0;
        snprintf(out, n, "%ls+0x%lx (%s+0x%lx)", m->name, r, nm, r - best);
    } else snprintf(out, n, "%ls+0x%lx", m->name, r);
}

static BOOL is_code(ULONG64 a)
{
    int i; for (i = 0; i < nmods; i++) if (a >= (ULONG64)mods[i].base + 0x1000 && a < (ULONG64)mods[i].base + mods[i].size) {
        BYTE b[8]; if (!rd((void *)(a - 7), b, 7)) return FALSE;
        if (b[2] == 0xE8) return TRUE;                       /* call rel32 */
        if (b[5] == 0xFF && (b[6] & 0x38) == 0x10) return TRUE;   /* call r/m, 2 Byte */
        if (b[4] == 0xFF && (b[5] & 0x38) == 0x10) return TRUE;   /* 3 Byte */
        if (b[1] == 0xFF && (b[2] & 0x38) == 0x10) return TRUE;   /* 6 Byte (rip-rel) */
        if (b[0] == 0xFF && (b[1] & 0x38) == 0x10) return TRUE;   /* 7 Byte */
        if (b[3] == 0xFF && (b[4] & 0x38) == 0x10) return TRUE;   /* 4 Byte (sib+disp8) */
        return FALSE;
    }
    return FALSE;
}

typedef NTSTATUS (WINAPI *NQIT)(HANDLE, int, PVOID, ULONG, PULONG);
typedef struct { NTSTATUS st; PVOID teb; CLIENT_ID cid; ULONG_PTR aff; LONG pri, bpri; } TBI;

static void dump(DWORD tid)
{
    HANDLE t = OpenThread(THREAD_ALL_ACCESS, FALSE, tid); CONTEXT c; TBI tbi; NT_TIB tib; char s[300]; ULONG64 sp; int n = 0;
    static NQIT nq; if (!nq) nq = (NQIT)GetProcAddress(GetModuleHandleA("ntdll"), "NtQueryInformationThread");
    printf("=== Thread %04lx\n", tid);
    if (!t) { printf("OpenThread %lu\n", GetLastError()); return; }
    SuspendThread(t);
    c.ContextFlags = CONTEXT_FULL;
    if (!GetThreadContext(t, &c)) printf("GetThreadContext %lu\n", GetLastError());
    sym(c.Rip, s, sizeof(s));
    printf("rip %s\nrsp %llx rbp %llx rax %llx rbx %llx rcx %llx rdx %llx\nrsi %llx rdi %llx r8 %llx r9 %llx r12 %llx r13 %llx r14 %llx r15 %llx\n",
           s, c.Rsp, c.Rbp, c.Rax, c.Rbx, c.Rcx, c.Rdx, c.Rsi, c.Rdi, c.R8, c.R9, c.R12, c.R13, c.R14, c.R15);
    if (nq(t, 0, &tbi, sizeof(tbi), NULL) == 0 && rd(tbi.teb, &tib, sizeof(tib))) {
        printf("Stack %p–%p\n", tib.StackLimit, tib.StackBase);
        if (c.Rsp < (ULONG64)tib.StackLimit || c.Rsp >= (ULONG64)tib.StackBase) sp = (ULONG64)tib.StackLimit; else sp = c.Rsp;
        for (; sp < (ULONG64)tib.StackBase && n < 120; sp += 8) {
            ULONG64 v; if (!rd((void *)sp, &v, 8)) { sp = (sp | 0xfff); continue; }
            if (is_code(v)) { sym(v, s, sizeof(s)); printf("  [%llx] %s\n", sp, s); n++; }
        }
    }
    ResumeThread(t); CloseHandle(t);
}

int main(int argc, char **argv)
{
    DWORD pid = strtoul(argv[1], 0, 0); int i;
    proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!proc) { printf("OpenProcess %lu\n", GetLastError()); return 1; }
    load_mods(); printf("%d Module\n", nmods);
    if (argc > 2) for (i = 2; i < argc; i++) dump(strtoul(argv[i], 0, 16));
    else {
        HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0); THREADENTRY32 te = { sizeof(te) };
        for (BOOL ok = Thread32First(s, &te); ok; ok = Thread32Next(s, &te)) if (te.th32OwnerProcessID == pid) dump(te.th32ThreadID);
    }
    return 0;
}
