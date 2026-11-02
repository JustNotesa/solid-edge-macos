#!/usr/bin/env python3
# Patcht Wines d2d1.dll (x86_64) für den Betrieb mit DXVK-macOS 1.10.3:
# d2d_wic_render_target_Release gibt erst dxgi_inner (D2D-Kontext → Direct3D-Gerät) und danach
# dxgi_surface frei. DXVK 1.10 hält das Gerät nicht über die Oberfläche am Leben → Use-after-free.
# Der Patch tauscht die beiden Feld-Offsets (+0x20 ↔ +0x10), damit die Oberfläche zuerst freigegeben wird,
# und entfernt die „Wine builtin DLL“-Kennung, damit Wine die Datei aus system32 als native DLL lädt.
#
# Erkannte Builds: Wine Staging 11.18 (Basisregister rdi) und d2d1 aus Wine 11.21 mit SVG-Patch
# (JustNotesa/solid-edge-wine-fixes, Basisregister rbx). Gesucht wird die Folge
#   mov rcx,[reg+0x30]; mov rax,[rcx]; call [rax+0x10]   (IWICBitmap_Release)
#   mov rcx,[reg+0x28]; …                                  (ID3D10Texture2D_Release)
#   mov rcx,[reg+0x20]; …                                  (IUnknown_Release dxgi_inner)
#   mov rcx,[reg+0x10]; …                                  (IDXGISurface_Release)
# mit beliebigem Basisregister (ModRM 0x48|rm, rm ≠ rsp).
import re, sys

src, dst = sys.argv[1], sys.argv[2]
d = bytearray(open(src, 'rb').read())

ruf = rb'\x48\x8b\x01\xff\x50\x10'
muster = re.compile(rb'\x48\x8b([\x48-\x4b\x4d-\x4f])\x30' + ruf +
                    rb'\x48\x8b\1\x28' + ruf +
                    rb'\x48\x8b\1\x20' + ruf +
                    rb'\x48\x8b\1\x10' + ruf)
treffer = list(muster.finditer(d))
if len(treffer) != 1:
    sys.exit(f'Bytefolge nicht eindeutig gefunden ({len(treffer)} Treffer) – unbekannte d2d1.dll-Version?')

pos = treffer[0].start()
schritt = 10                          # eine Gruppe: 4 Byte „mov rcx,[reg+x]“ + 6 Byte „mov rax,[rcx]; call [rax+0x10]“
d[pos + 2 * schritt + 3] = 0x10       # dritte Freigabe: jetzt dxgi_surface
d[pos + 3 * schritt + 3] = 0x20       # vierte Freigabe: jetzt dxgi_inner

marke = b'Wine builtin DLL'
i = d.find(marke)
if 0 < i < 0x100:
    d[i:i + len(marke)] = b'Patched d2d1 DLL'

open(dst, 'wb').write(d)
print(f'gepatcht: Offset {pos:#x}, Basisregister ModRM {treffer[0].group(1)[0]:#x}')
