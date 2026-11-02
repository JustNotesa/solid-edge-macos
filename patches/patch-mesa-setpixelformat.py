#!/usr/bin/env python3
# Patcht Mesas libgallium_wgl.dll (mesa-dist-win 26.2.4 x64, msvc): DrvSetPixelFormat liefert bei einem
# zweiten SetPixelFormat auf dasselbe Fenster FALSE (Mesa: „SetPixelFormat must be called only once“,
# außer bei Pbuffern). Wines gdi32 reicht SetPixelFormat ungefiltert an das geladene opengl32 weiter, Solid Edge
# ruft es mehrfach auf → Kontext fehlt („called without a rendering context“). Windows liefert hier TRUE.
# Patch: „xor esi,esi; cmp ebx,1; sete sil; mov eax,esi“ → „mov eax,1“ + NOPs (Rückgabe immer TRUE).
import sys

src, dst = sys.argv[1], sys.argv[2]
d = bytearray(open(src, 'rb').read())
alt = bytes.fromhex('8b5844488bc8ff15') 
code = bytes.fromhex('33f683fb01400f94c68bc6')
neu = bytes.fromhex('b801000000' '909090909090')
treffer = [i for i in range(len(d)) if d.startswith(code, i) and d[i - 12:i - 4] == alt]
if len(treffer) != 1:
    sys.exit(f'Bytefolge nicht eindeutig gefunden ({len(treffer)} Treffer) – andere Mesa-Version?')
d[treffer[0]:treffer[0] + len(code)] = neu
open(dst, 'wb').write(d)
print(f'gepatcht: Offset {treffer[0]:#x}')
