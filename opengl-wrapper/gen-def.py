#!/usr/bin/env python3
"""Erzeugt opengl32.def für die Vorschaltung: alle Exporte der echten Mesa-opengl32.dll, abgefangene als eigene
Funktion, alle anderen als Forwarder auf mesaogl32.<name>.

Aufruf: python3 -I gen-def.py <mesa-opengl32.dll> <ausgabe.def>
"""
import struct
import sys

ABGEFANGEN = {
    'wglChoosePixelFormat', 'wglDescribePixelFormat', 'wglGetPixelFormat', 'wglSetPixelFormat',
    'wglCreateContext', 'wglDeleteContext', 'wglMakeCurrent', 'wglGetCurrentDC', 'wglSwapBuffers',
    'wglGetProcAddress', 'wglShareLists', 'wglUseFontOutlinesA', 'wglUseFontOutlinesW', 'glCallList', 'glCallLists',
    'glFlush', 'glFinish', 'glDrawBuffer', 'glReadBuffer', 'glCopyPixels', 'glDrawPixels', 'glLogicOp', 'glEnable', 'glDisable', 'glBegin',
    # nur Diagnose (Protokoll, Verhalten unverändert):
    'glGetString', 'wglCreateLayerContext', 'wglDescribeLayerPlane', 'wglSwapLayerBuffers',
    'wglRealizeLayerPalette', 'wglSetLayerPaletteEntries', 'wglGetLayerPaletteEntries',
}


def exporte(pfad):
    d = open(pfad, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    n = struct.unpack_from('<H', d, pe + 6)[0]
    opt = struct.unpack_from('<H', d, pe + 20)[0]
    magic = struct.unpack_from('<H', d, pe + 24)[0]
    dd = pe + 24 + (96 if magic == 0x10b else 112)
    exp_rva = struct.unpack_from('<I', d, dd)[0]
    secs = [struct.unpack_from('<8sIIII', d, pe + 24 + opt + 40 * i) for i in range(n)]

    def off(rva):
        for _, vs, va, rs, ra in secs:
            if va <= rva < va + max(vs, rs):
                return ra + rva - va
        raise ValueError(hex(rva))

    o = off(exp_rva)
    anz_namen, namen_rva = struct.unpack_from('<I', d, o + 24)[0], struct.unpack_from('<I', d, o + 32)[0]
    namen = []
    for i in range(anz_namen):
        p = off(struct.unpack_from('<I', d, off(namen_rva) + 4 * i)[0])
        namen.append(d[p:d.index(b'\0', p)].decode())
    return namen


def main():
    namen = exporte(sys.argv[1])
    fehlt = ABGEFANGEN - set(namen)
    if fehlt:
        sys.exit(f'Mesa-opengl32 exportiert nicht: {sorted(fehlt)}')
    zeilen = ['LIBRARY opengl32.dll', 'EXPORTS']
    for name in namen:
        zeilen.append(f'    {name}' if name in ABGEFANGEN else f'    {name} = mesaogl32.{name}')
    open(sys.argv[2], 'w').write('\n'.join(zeilen) + '\n')
    print(f'{len(namen)} Exporte, davon {len(ABGEFANGEN)} abgefangen, {len(namen) - len(ABGEFANGEN)} weitergeleitet')


if __name__ == '__main__':
    main()
