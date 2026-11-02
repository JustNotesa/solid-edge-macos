#!/usr/bin/env python3
"""Entfernt persönliche Daten aus der Prefix-Kopie, die in die App bzw. DMG kommt.

Aufruf (von build-dmg.sh, auf der Kopie im Bundle, nie auf dem Arbeits-Prefix):
    python3 -I prefix-anonymisieren.py <prefix> <benutzer-im-prefix> [<neutraler-name>]

- Wine-Benutzerprofil drive_c/users/<benutzer> → drive_c/users/<neutral> (Standard „user“); der App-Starter
  benennt es beim ersten Start auf den jeweiligen macOS-Benutzer um.
- Registry: Zuletzt-geöffnet-Listen von Solid Edge/Femap leeren, Einträge mit Mac-Pfaden ins eigene
  Home-Verzeichnis entfernen (Schriften aus ~/Library/Fonts – Wine trägt Schriften beim Start neu ein),
  Installationsquelle neutralisieren, Benutzername ersetzen.
- Dateien: WebView2-Profile (Verlauf/Cache), Temp, „Zuletzt verwendet“.
Gibt am Ende aus, ob der alte Name noch irgendwo vorkommt.
"""
import os
import re
import shutil
import sys

prefix, alt = sys.argv[1], sys.argv[2]
neu = sys.argv[3] if len(sys.argv) > 3 else "user"
home_win = "Z:\\\\Users\\\\" + alt          # so steht es (mit verdoppelten Backslashes) in den .reg-Dateien
home_unix = "/Users/" + alt

MRU_SEKTION = re.compile(r"^\[Software\\\\Siemens\\\\.*\\\\(MRU|MRUex|Recent File List)\]", re.I)
MRU_WERT = re.compile(r'^"(File|Item)\d+"=')
QUELLE = re.compile(r'^"(InstallSource|LastUsedSource)"=')

for name in ("system.reg", "user.reg", "userdef.reg"):
    pfad = os.path.join(prefix, name)
    if not os.path.exists(pfad):
        continue
    zeilen = open(pfad, encoding="utf-8", errors="surrogateescape").read().split("\n")
    aus, in_mru, entfernt = [], False, 0
    for z in zeilen:
        if z.startswith("["):
            in_mru = bool(MRU_SEKTION.match(z))
        einzeilig = z.startswith('"') and not z.endswith("\\")
        if einzeilig and in_mru and MRU_WERT.match(z):
            entfernt += 1
            continue
        im_home = home_win.lower() in z.lower() or home_unix.lower() in z.lower()
        if einzeilig and QUELLE.match(z) and im_home:
            z = re.sub(r'=(str\(2\):)?".*"', r'="C:\\\\installer\\\\"', z)
        elif einzeilig and z.startswith('"PackageName"=') and im_home:
            z = re.sub(r'=".*[/\\]([^/\\"]+)"$', r'="\1"', z)      # nur den Dateinamen der MSI behalten
        elif einzeilig and im_home:
            entfernt += 1               # Schriften aus ~/Library/Fonts, Pfade in den Projektordner usw.
            continue
        aus.append(z)
    text = "\n".join(aus)
    text = re.sub(re.escape(alt), neu, text, flags=re.I)
    open(pfad, "w", encoding="utf-8", errors="surrogateescape").write(text)
    print(f"{name}: {entfernt} Einträge entfernt")

users = os.path.join(prefix, "drive_c", "users")
if os.path.isdir(os.path.join(users, alt)) and alt != neu:
    shutil.rmtree(os.path.join(users, neu), ignore_errors=True)
    os.rename(os.path.join(users, alt), os.path.join(users, neu))
profil = os.path.join(users, neu)

for rel in ("AppData/Local/Temp", "AppData/Roaming/Microsoft/Windows/Recent", "Temp",
            "AppData/Local/Siemens/San",                                   # Nutzungsstatistik
            "AppData/Roaming/Siemens/Solid Edge/Version 226/Log Files"):
    d = os.path.join(profil, rel)
    if os.path.isdir(d):
        for e in os.listdir(d):
            p = os.path.join(d, e)
            shutil.rmtree(p) if os.path.isdir(p) and not os.path.islink(p) else os.remove(p)

# WebView2-Benutzerdaten (Verlauf, Cookies, Cache) liegen im Benutzerprofil, z. B.
# AppData/Roaming/Siemens/Solid Edge/Version 226/EBWebView. NICHT unter Program Files suchen: Dort heißt ein
# Teil der WebView2-Laufzeit ebenfalls „EBWebView“ (EmbeddedBrowserWebView.dll) – ohne ihn startet die Startseite nicht.
for wurzel, ordner, _ in os.walk(users):
    for o in list(ordner):
        if o == "EBWebView":
            shutil.rmtree(os.path.join(wurzel, o), ignore_errors=True)
            ordner.remove(o)

rest = []
for name in ("system.reg", "user.reg", "userdef.reg"):
    pfad = os.path.join(prefix, name)
    if os.path.exists(pfad) and alt.lower() in open(pfad, errors="ignore").read().lower():
        rest.append(name)
print("Rest-Vorkommen von", repr(alt) + ":", ", ".join(rest) if rest else "keine")
