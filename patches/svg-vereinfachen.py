#!/usr/bin/env python3
"""Schreibt Solid-Edge-Menüband-SVGs in eine für nanosvg (SVG im gepatchten d2d1) taugliche Form um.

nanosvg ignoriert u. a. clip-path, <style>-Klassen und <use>. Häufigstes Problem in Images/Ribbon:
eine (Verlaufs-)Fläche, die per clip-path auf einen Umriss beschnitten ist – ohne Beschnitt deckt sie das
ganze Symbol zu. Umformungen:
  1. <style>-Klassen → style-Attribute (Klassenregeln vor vorhandenem style, wie im Browser).
  2. <use href="#id"> → Kopie des Ziels (mit x/y als translate).
  3. Nur mit --clip: clip-path auf Einzelform/Gruppe durch die Clip-Form ersetzen. NICHT Standard – bei den
     Solid-Edge-Symbolen ist die beschnittene Fläche meist kleiner als der Clip (Titelleisten-Streifen in einem
     abgerundeten Fenster), die Ersetzung füllt dann das ganze Symbol. Ohne Clip zeichnet nanosvg diese Symbole
     bis auf die Eckenrundung richtig (geprüft an allen 40 Dateien mit clip-path in Images/Ribbon/Dark).
Unverändert bleiben: <text>, mask, filter, <image> (nanosvg kann sie nicht, eine Umformung ist nicht sinnvoll).

Aufruf:
  python3 -I svg-vereinfachen.py [--clip] <eingabe.svg|ordner> [<ausgabe-ordner>]
Ohne Ausgabe-Ordner wird nur berichtet (Trockenlauf). Dateien, an denen nichts zu ändern ist, werden nicht
geschrieben.
"""
import copy
import os
import re
import sys
import xml.etree.ElementTree as ET

SVG = 'http://www.w3.org/2000/svg'
XLINK = 'http://www.w3.org/1999/xlink'
FORMEN = {'rect', 'path', 'polygon', 'polyline', 'circle', 'ellipse', 'line'}
GEOMETRIE = {'x', 'y', 'width', 'height', 'rx', 'ry', 'd', 'points', 'cx', 'cy', 'r', 'x1', 'y1', 'x2', 'y2'}

ET.register_namespace('', SVG)
ET.register_namespace('xlink', XLINK)


def tag(e):
    return e.tag.split('}')[-1]


def style_dict(s):
    out = {}
    for teil in (s or '').split(';'):
        if ':' in teil:
            k, v = teil.split(':', 1)
            out[k.strip()] = v.strip()
    return out


def style_str(d):
    return '; '.join(f'{k}: {v}' for k, v in d.items())


def clip_ref(e):
    """Liefert die clip-path-ID eines Elements (Attribut oder style) und entfernt den Verweis."""
    st = style_dict(e.get('style'))
    ref = e.get('clip-path') or st.get('clip-path')
    if not ref:
        return None
    m = re.match(r'url\(\s*#([^)\s]+)\s*\)', ref)
    return m.group(1) if m else None


def drop_clip(e):
    e.attrib.pop('clip-path', None)
    st = style_dict(e.get('style'))
    if st.pop('clip-path', None) is not None:
        if st:
            e.set('style', style_str(st))
        else:
            e.attrib.pop('style', None)


def inline_styles(root, log):
    regeln = {}
    for st in list(root.iter(f'{{{SVG}}}style')):
        for sel, body in re.findall(r'([^{}]+)\{([^}]*)\}', st.text or ''):
            for s in sel.split(','):
                s = s.strip()
                if s.startswith('.'):
                    regeln.setdefault(s[1:], {}).update(style_dict(body))
    if not regeln:
        return
    for e in root.iter():
        klassen = (e.get('class') or '').split()
        if not klassen:
            continue
        neu = {}
        for k in klassen:
            neu.update(regeln.get(k, {}))
        neu.update(style_dict(e.get('style')))
        if neu:
            e.set('style', style_str(neu))
        e.attrib.pop('class', None)
    for parent in root.iter():
        for st in [c for c in parent if tag(c) == 'style']:
            parent.remove(st)
    log.append(f'style→inline({len(regeln)} Klassen)')


def resolve_use(root, ids, log):
    n = 0
    for parent in list(root.iter()):
        for i, c in enumerate(list(parent)):
            if tag(c) != 'use':
                continue
            href = c.get(f'{{{XLINK}}}href') or c.get('href') or ''
            ziel = ids.get(href.lstrip('#'))
            if ziel is None:
                continue
            kopie = copy.deepcopy(ziel)
            kopie.attrib.pop('id', None)
            g = ET.Element(f'{{{SVG}}}g')
            tr = c.get('transform', '')
            x, y = c.get('x'), c.get('y')
            if x or y:
                tr = f'{tr} translate({x or 0} {y or 0})'.strip()
            if tr:
                g.set('transform', tr)
            for k, v in c.attrib.items():
                if k not in ('x', 'y', 'width', 'height', 'transform', 'href', f'{{{XLINK}}}href'):
                    g.set(k, v)
            if tag(kopie) == 'symbol':
                kopie.tag = f'{{{SVG}}}g'
            g.append(kopie)
            parent.remove(c)
            parent.insert(i, g)
            n += 1
    if n:
        log.append(f'use→kopie({n})')


def clip_form(clips, cid):
    cp = clips.get(cid)
    if cp is None or cp.get('transform') or cp.get('clipPathUnits') == 'objectBoundingBox':
        return None
    kinder = [c for c in cp if tag(c) in FORMEN]
    if len(kinder) != 1 or kinder[0].get('transform') or len(list(cp)) != 1:
        return None
    return kinder[0]


def ersetze_geometrie(form, quelle):
    """Neues Element mit der Geometrie der Clip-Form und Stil/Transformation/ID der beschnittenen Form."""
    neu = ET.Element(quelle.tag)
    for k, v in quelle.attrib.items():
        if k in GEOMETRIE:
            neu.set(k, v)
    for k, v in form.attrib.items():
        if k not in GEOMETRIE:
            neu.set(k, v)
    drop_clip(neu)
    return neu


def resolve_clips(root, log):
    clips = {c.get('id'): c for c in root.iter(f'{{{SVG}}}clipPath')}
    if not clips:
        return
    ersetzt = gruppen = offen = 0
    for parent in list(root.iter()):
        for i, c in enumerate(list(parent)):
            cid = clip_ref(c)
            if not cid:
                continue
            quelle = clip_form(clips, cid)
            if quelle is None:
                offen += 1
                continue
            if tag(c) in FORMEN and tag(c) != 'line':
                parent.remove(c)
                parent.insert(i, ersetze_geometrie(c, quelle))
                ersetzt += 1
            elif tag(c) == 'g' and not c.get('transform'):
                formen = [k for k in c if tag(k) in FORMEN and tag(k) != 'line']
                if formen and len(formen) == len(list(c)):
                    for j, k in enumerate(list(c)):
                        c.remove(k)
                        c.insert(j, ersetze_geometrie(k, quelle))
                    drop_clip(c)
                    gruppen += 1
                else:
                    offen += 1
            else:
                offen += 1
    if ersetzt or gruppen:
        log.append(f'clip→form({ersetzt}+{gruppen} Gruppen)')
    if offen:
        log.append(f'clip-offen({offen})')


MIT_CLIP = False


def bearbeite(text):
    root = ET.fromstring(text)
    ids = {e.get('id'): e for e in root.iter() if e.get('id')}
    log = []
    inline_styles(root, log)
    resolve_use(root, ids, log)
    if MIT_CLIP:
        resolve_clips(root, log)
    rest = [k for k, p in (('text', r'<text\b'), ('mask', r'<mask\b|\bmask='), ('filter', r'<filter\b'),
                           ('image', r'<image\b')) if re.search(p, text)]
    if rest:
        log.append('unverändert:' + ','.join(rest))
    if not MIT_CLIP and re.search(r'clip-path', text):
        log.append('clip ignoriert (nanosvg zeichnet ohne Beschnitt)')
    geaendert = any(not x.startswith(('clip', 'unverändert')) for x in log) or any(x.startswith('clip→') for x in log)
    return (ET.tostring(root, encoding='unicode') if geaendert else None), log


def main():
    global MIT_CLIP
    args = sys.argv[1:]
    if args and args[0] == '--clip':
        MIT_CLIP = True
        args = args[1:]
    quelle = args[0]
    ziel = args[1] if len(args) > 1 else None
    dateien = ([os.path.join(quelle, f) for f in sorted(os.listdir(quelle)) if f.lower().endswith('.svg')]
               if os.path.isdir(quelle) else [quelle])
    if ziel:
        os.makedirs(ziel, exist_ok=True)
    stat = {'geändert': 0, 'unverändert': 0, 'fehler': 0}
    for f in dateien:
        try:
            neu, log = bearbeite(open(f, encoding='utf-8').read())
        except ET.ParseError as e:
            print(f'FEHLER {os.path.basename(f)}: {e}')
            stat['fehler'] += 1
            continue
        if neu is None:
            stat['unverändert'] += 1
            if log:
                print(f'  {os.path.basename(f)}: {"; ".join(log)}')
            continue
        stat['geändert'] += 1
        print(f'* {os.path.basename(f)}: {"; ".join(log)}')
        if ziel:
            with open(os.path.join(ziel, os.path.basename(f)), 'w', encoding='utf-8') as out:
                out.write('<?xml version="1.0" encoding="UTF-8"?>\n' + neu)
    print(f'Ergebnis: {stat}')


if __name__ == '__main__':
    main()
