#!/usr/bin/env python3
"""Benennt in Mesas libgallium_wgl.dll die Shader-Variable „sampler" um.

Mesa (Zink) gibt einer internen Textur-Variable den Namen „sampler". MoltenVK übersetzt den Shader
nach Metal und übernimmt den Namen – dort ist „sampler" aber ein Typ:
    texture2d<float> sampler [[id(0)]];  sampler samplerSmplr [[id(1)]];   → Kompilierfehler
Die Pipeline fehlt dann, vkQueueSubmit stürzt ab und das ganze Fenster bleibt schwarz.
Der String kommt in der DLL nur einmal vor; er wird gleich lang durch „smplr_0" ersetzt.

Aufruf: python3 -I patch-mesa-sampler-name.py <libgallium_wgl.dll> <ausgabe.dll>
"""
import re
import sys

src, dst = sys.argv[1], sys.argv[2]
data = bytearray(open(src, "rb").read())
treffer = [m.start() for m in re.finditer(rb"(?<=\x00)sampler\x00", data)]
if len(treffer) != 1:
    sys.exit(f"Erwartet genau einen String 'sampler', gefunden: {len(treffer)}")
data[treffer[0]:treffer[0] + 7] = b"smplr_0"
open(dst, "wb").write(data)
print(f"gepatcht bei {hex(treffer[0])}: {dst}")
