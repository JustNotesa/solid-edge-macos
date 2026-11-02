/* Minimaler Ersatz für <windows.h> – nur für die Syntax-/Typprüfung ohne mingw (clang -fsyntax-only). */
#pragma once
#include <stddef.h>
#define WINAPI __attribute__((ms_abi))
#define APIENTRY WINAPI
#define CALLBACK WINAPI
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define TLS_OUT_OF_INDEXES ((DWORD)0xFFFFFFFF)
typedef int BOOL; typedef unsigned char BYTE; typedef unsigned short WORD; typedef unsigned int DWORD, UINT;
typedef long LONG; typedef unsigned short WCHAR; typedef const char *LPCSTR; typedef void *LPVOID, *HANDLE;
typedef long long LRESULT, LPARAM; typedef unsigned long long WPARAM; typedef unsigned short ATOM;
typedef struct HDC__ *HDC; typedef struct HWND__ *HWND; typedef struct HGLRC__ *HGLRC; typedef struct HMODULE__ *HMODULE;
typedef HMODULE HINSTANCE; typedef void *HGDIOBJ; typedef long long (WINAPI *PROC)(void); typedef PROC FARPROC;
typedef struct { void *a; long b; long c; void *d, *e; unsigned long long f; } CRITICAL_SECTION;
typedef struct { WORD nSize, nVersion; DWORD dwFlags; BYTE iPixelType, cColorBits, cRedBits, cRedShift, cGreenBits,
  cGreenShift, cBlueBits, cBlueShift, cAlphaBits, cAlphaShift, cAccumBits, cAccumRedBits, cAccumGreenBits,
  cAccumBlueBits, cAccumAlphaBits, cDepthBits, cStencilBits, cAuxBuffers, iLayerType, bReserved;
  DWORD dwLayerMask, dwVisibleMask, dwDamageMask; } PIXELFORMATDESCRIPTOR;
typedef struct { LONG bmType, bmWidth, bmHeight, bmWidthBytes; WORD bmPlanes, bmBitsPixel; void *bmBits; } BITMAP;
typedef struct { DWORD biSize; LONG biWidth, biHeight; WORD biPlanes, biBitCount; DWORD biCompression, biSizeImage;
  LONG biXPelsPerMeter, biYPelsPerMeter; DWORD biClrUsed, biClrImportant; } BITMAPINFOHEADER;
typedef struct { BITMAPINFOHEADER bmiHeader; DWORD bmiColors[1]; } BITMAPINFO;
typedef struct { BITMAP dsBm; BITMAPINFOHEADER dsBmih; DWORD dsBitfields[3]; HANDLE dshSection; DWORD dsOffset; } DIBSECTION;
typedef LRESULT (WINAPI *WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef struct { UINT style; WNDPROC lpfnWndProc; int cbClsExtra, cbWndExtra; HINSTANCE hInstance; void *hIcon, *hCursor, *hbrBackground; const WCHAR *lpszMenuName, *lpszClassName; } WNDCLASSW;
#define PFD_DOUBLEBUFFER 0x1
#define PFD_DRAW_TO_WINDOW 0x4
#define PFD_DRAW_TO_BITMAP 0x8
#define PFD_SUPPORT_GDI 0x10
#define PFD_SUPPORT_OPENGL 0x20
#define PFD_GENERIC_FORMAT 0x40
#define PFD_GENERIC_ACCELERATED 0x1000
#define PFD_DOUBLEBUFFER_DONTCARE 0x40000000
#define PFD_TYPE_RGBA 0
#define OBJ_BITMAP 7
#define OBJ_MEMDC 10
#define BI_RGB 0
#define DIB_RGB_COLORS 0
#define CS_OWNDC 0x20
#define WS_POPUP 0x80000000
#define WS_EX_TOOLWINDOW 0x80
#define WS_EX_NOACTIVATE 0x8000000
#define WM_PAINT 0xF
#define WM_ERASEBKGND 0x14
#define SWP_NOMOVE 2
#define SWP_NOZORDER 4
#define SWP_NOACTIVATE 0x10
#define DLL_PROCESS_ATTACH 1
DWORD WINAPI GetObjectType(HGDIOBJ); HGDIOBJ WINAPI GetCurrentObject(HDC, UINT); int WINAPI GetObjectW(HGDIOBJ, int, void *);
BOOL WINAPI GdiFlush(void); int WINAPI SetDIBitsToDevice(HDC, int, int, DWORD, DWORD, int, int, UINT, UINT, const void *, const BITMAPINFO *, UINT);
HDC WINAPI GetDC(HWND); BOOL WINAPI ValidateRect(HWND, const void *); LRESULT WINAPI DefWindowProcW(HWND, UINT, WPARAM, LPARAM);
ATOM WINAPI RegisterClassW(const WNDCLASSW *); HMODULE WINAPI GetModuleHandleW(const WCHAR *);
HWND WINAPI CreateWindowExW(DWORD, const WCHAR *, const WCHAR *, DWORD, int, int, int, int, HWND, void *, HINSTANCE, void *);
BOOL WINAPI SetWindowPos(HWND, HWND, int, int, int, int, UINT);
void WINAPI EnterCriticalSection(CRITICAL_SECTION *); void WINAPI LeaveCriticalSection(CRITICAL_SECTION *); void WINAPI InitializeCriticalSection(CRITICAL_SECTION *);
DWORD WINAPI TlsAlloc(void); void *WINAPI TlsGetValue(DWORD); BOOL WINAPI TlsSetValue(DWORD, void *);
DWORD WINAPI GetModuleFileNameW(HMODULE, WCHAR *, DWORD); HMODULE WINAPI LoadLibraryW(const WCHAR *); FARPROC WINAPI GetProcAddress(HMODULE, LPCSTR);
BOOL WINAPI DisableThreadLibraryCalls(HMODULE);
WCHAR *wcsrchr(const WCHAR *, WCHAR); size_t wcslen(const WCHAR *); WCHAR *wcscat(WCHAR *, const WCHAR *);
