/* Lädt secapture-vN.dll in jedes laufende Edge.exe (CreateRemoteThread + LoadLibraryW).
 * Mehrfaches Laden ist harmlos: LoadLibrary erhöht dann nur den Referenzzähler, DllMain läuft nicht erneut.
 *
 * Aufruf: secapture-inject.exe [Pfad\zu\secapture-vN.dll]   (Standard: neben dieser .exe)
 * Rückgabe: 0 = in mindestens ein Edge.exe geladen, 1 = kein Edge.exe, 2 = Fehler
 * Bauen: x86_64-w64-mingw32-gcc -O2 -s -municode -o secapture-inject.exe secapture-inject.c
 */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <wchar.h>

int wmain(int argc, wchar_t **argv)
{
    wchar_t dll[MAX_PATH], *b;
    SIZE_T groesse;
    LPTHREAD_START_ROUTINE ladefn;
    HANDLE snap;
    PROCESSENTRY32W pe;
    int gefunden = 0, geladen = 0;

    if (argc > 1) {
        GetFullPathNameW(argv[1], MAX_PATH, dll, NULL);
    } else {
        GetModuleFileNameW(NULL, dll, MAX_PATH);
        b = wcsrchr(dll, L'\\');
        wcscpy(b ? b + 1 : dll, L"secapture-v2.dll");   /* Protokollversion wie in secapture.c */
    }
    if (GetFileAttributesW(dll) == INVALID_FILE_ATTRIBUTES) { fwprintf(stderr, L"DLL fehlt: %ls\n", dll); return 2; }
    groesse = (wcslen(dll) + 1) * sizeof(wchar_t);
    /* kernel32 liegt in allen Prozessen an derselben Adresse */
    ladefn = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    pe.dwSize = sizeof(pe);
    if (snap != INVALID_HANDLE_VALUE && Process32FirstW(snap, &pe)) {
        do {
            HANDLE p, t;
            void *fern;
            DWORD code = 0;
            if (_wcsicmp(pe.szExeFile, L"Edge.exe") != 0) continue;
            gefunden++;
            p = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                            FALSE, pe.th32ProcessID);
            if (!p) { wprintf(L"Edge.exe %lu: OpenProcess fehlgeschlagen (%lu)\n", pe.th32ProcessID, GetLastError()); continue; }
            fern = VirtualAllocEx(p, NULL, groesse, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (fern && WriteProcessMemory(p, fern, dll, groesse, NULL) &&
                (t = CreateRemoteThread(p, NULL, 0, ladefn, fern, 0, NULL)) != NULL) {
                WaitForSingleObject(t, 30000);
                GetExitCodeThread(t, &code);
                CloseHandle(t);
                wprintf(L"Edge.exe %lu: %ls\n", pe.th32ProcessID, code ? L"geladen" : L"LoadLibraryW fehlgeschlagen");
                if (code) geladen++;
            } else {
                wprintf(L"Edge.exe %lu: Injektion fehlgeschlagen (%lu)\n", pe.th32ProcessID, GetLastError());
            }
            if (fern) VirtualFreeEx(p, fern, 0, MEM_RELEASE);
            CloseHandle(p);
        } while (Process32NextW(snap, &pe));
    }
    if (snap != INVALID_HANDLE_VALUE) CloseHandle(snap);
    if (!gefunden) { wprintf(L"kein Edge.exe gefunden\n"); return 1; }
    return geladen ? 0 : 2;
}
