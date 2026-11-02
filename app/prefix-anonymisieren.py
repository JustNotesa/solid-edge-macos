#!/usr/bin/env python3
"""Entfernt persönliche Daten aus der Prefix-Kopie, die in die App bzw. DMG kommt.

Aufruf (von build-dmg.sh, auf der Kopie im Bundle, nie auf dem Arbeits-Prefix):
    python3 -I prefix-anonymisieren.py <prefix> <benutzer-im-prefix> [<neutraler-name>]

- Wine-Benutzerprofil drive_c/users/<benutzer> → drive_c/users/<neutral> (Standard „user“); der App-Starter
  benennt es beim ersten Start auf den jeweiligen macOS-Benutzer um.
- Registry: Zuletzt-geöffnet-Listen von Solid Edge/Femap leeren, Einträge mit Mac-Pfaden ins eigene
  Home-Verzeichnis entfernen (Schriften aus ~/Library/Fonts – Wine trägt Schriften beim Start neu ein),
  Installationsquelle neutralisieren, Benutzername ersetzen.
- Rechnername/Domain: Werte unter ComputerName und Tcpip\\Parameters sowie die Sektion [Volatile Environment]
  (LOGONSERVER, USERDOMAIN …) löschen – wineboot setzt sie bei jedem Sitzungsstart aus dem Hostnamen des
  jeweiligen Macs neu. Übrige Vorkommen in der Registry werden neutralisiert.
- Dateien: WebView2-Profile (Verlauf/Cache), Gecko-Profil (wine_gecko, enthält Pfade mit dem 8.3-Kurznamen des
  Benutzerordners), Temp, „Zuletzt verwendet“.
Am Ende werden alle Dateien (Text und binär, auch UTF-16) sowie Datei- und Linknamen nach Benutzername,
vollem Namen, Rechnername, Domain und Kurzname durchsucht. Bei Treffern Exit-Code 1 – build-dmg.sh bricht dann ab.
"""
import os
import re
import shutil
import socket
import subprocess
import sys
import unicodedata

prefix, alt = sys.argv[1], sys.argv[2]
neu = sys.argv[3] if len(sys.argv) > 3 else "user"
home_win = "Z:\\\\Users\\\\" + alt          # so steht es (mit verdoppelten Backslashes) in den .reg-Dateien
home_unix = "/Users/" + alt

MRU_SEKTION = re.compile(r"^\[Software\\\\Siemens\\\\.*\\\\(MRU|MRUex|Recent File List)\]", re.I)
MRU_WERT = re.compile(r'^"(File|Item)\d+"=')
QUELLE = re.compile(r'^"(InstallSource|LastUsedSource)"=')
# Rechnername/Domain: Sektion → Werte, die gelöscht werden (wineboot schreibt sie beim Start neu).
RECHNER_WERTE = [
    (re.compile(r"^\[System\\\\(ControlSet\d+|CurrentControlSet)\\\\Control\\\\ComputerName\\\\"
                r"(Active)?ComputerName\]", re.I), {"computername"}),
    (re.compile(r"^\[System\\\\(ControlSet\d+|CurrentControlSet)\\\\Services\\\\Tcpip6?\\\\Parameters"
                r"(\\\\Interfaces\\\\[^\]]*)?\]", re.I),
     {"hostname", "domain", "nv hostname", "nv domain", "dhcpdomain", "dhcpnameserver", "nameserver"}),
]
VOLATILE = re.compile(r"^\[Volatile Environment(\\\\[^\]]*)?\]", re.I)
WERTNAME = re.compile(r'^"((?:[^"\\]|\\.)*)"=')
# Domains, die nichts über den Rechner verraten (und als Suchbegriff nur Fehltreffer brächten).
ALLGEMEINE_DOMAINS = {"local", "lan", "home", "localdomain", "domain", "box", "intern", "internal", "home.arpa"}


def rechnernamen():
    """Namen dieses Macs, wie sie in den Prefix geraten können: (Hostnamen, Domains)."""
    roh = []
    for befehl in (["scutil", "--get", "ComputerName"], ["scutil", "--get", "LocalHostName"],
                   ["scutil", "--get", "HostName"], ["hostname"]):
        try:
            r = subprocess.run(befehl, capture_output=True, text=True, timeout=10)
            if r.returncode == 0:
                roh.append(r.stdout.strip())
        except (OSError, subprocess.SubprocessError):
            pass
    roh.append(socket.gethostname())
    hosts, domains = set(), set()
    for n in filter(None, roh):
        hosts.add(n)
        hosts.add(n.replace(" ", "-"))               # LocalHostName-Schreibweise des Computernamens
        kurz, _, domain = n.partition(".")
        hosts.add(kurz)
        hosts.add(kurz[:15])                          # NetBIOS-Name, wie Wine ihn als COMPUTERNAME setzt
        if domain and domain.lower() not in ALLGEMEINE_DOMAINS:
            domains.add(domain)
    hosts = {h for h in hosts if len(h) >= 4 and h.lower() not in ("localhost", "wine")}
    return sorted(hosts, key=len, reverse=True), sorted(domains, key=len, reverse=True)


def kurznamen(name):
    """Anfänge der 8.3-Kurznamen eines langen Ordnernamens: Wine „ABCD~5QE“, Windows „ABCDEF~1“."""
    if len(name) <= 8 and "." not in name and " " not in name:
        return []
    gross = re.sub(r'[^A-Z0-9!#$%&\'()@^_`{}-]', "_", name.upper())
    return sorted({gross[:4] + "~", gross[:6] + "~"}, key=len, reverse=True)


def vollname():
    """Voller Name des macOS-Benutzers (nur für die Restprüfung), auch ohne Umlaute geschrieben."""
    try:
        n = subprocess.run(["id", "-F"], capture_output=True, text=True, timeout=10).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return []
    ascii_n = unicodedata.normalize("NFKD", n).encode("ascii", "ignore").decode()
    return sorted({x for x in (n, unicodedata.normalize("NFD", n), ascii_n) if len(x) >= 4})


HOSTS, DOMAINS = rechnernamen()
KURZ = kurznamen(alt) if alt != neu else []


def neutralisieren(text):
    """Übrige Vorkommen von Rechnername, Domain und Kurzname im Registry-Text ersetzen."""
    for k in KURZ:                                   # C:\users\ABCD~5QE\… → C:\users\user\…
        text = re.sub(re.escape(k) + r"[~A-Z0-9_]*", neu, text, flags=re.I)
    for h in HOSTS:
        for d in DOMAINS:
            text = re.sub(re.escape(h + "." + d), "localhost", text, flags=re.I)
        text = re.sub(re.escape(h), "WINE", text, flags=re.I)
    for d in DOMAINS:
        text = re.sub(re.escape(d), "localdomain", text, flags=re.I)
    return text


for name in ("system.reg", "user.reg", "userdef.reg"):
    pfad = os.path.join(prefix, name)
    if not os.path.exists(pfad):
        continue
    zeilen = open(pfad, encoding="utf-8", errors="surrogateescape").read().split("\n")
    aus, in_mru, rechner, in_volatile, entfernt = [], False, set(), False, 0
    for z in zeilen:
        if z.startswith("["):
            in_mru = bool(MRU_SEKTION.match(z))
            in_volatile = bool(VOLATILE.match(z))
            rechner = next((w for s, w in RECHNER_WERTE if s.match(z)), set())
            if in_volatile:
                entfernt += 1
        if in_volatile:                 # ganze Sektion samt Kopf weglassen
            continue
        einzeilig = z.startswith('"') and not z.endswith("\\")
        if einzeilig and in_mru and MRU_WERT.match(z):
            entfernt += 1
            continue
        if einzeilig and rechner and (m := WERTNAME.match(z)) and m.group(1).lower() in rechner:
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
    text = neutralisieren(text)
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
# Ebenso das Gecko-Profil (AppData/Roaming/wine_gecko: secmod.db u. a. mit Pfaden über den Kurznamen des
# Benutzerordners) – Wine legt es bei Bedarf neu an. Die Gecko-Laufzeit selbst liegt unter windows/system32/gecko.
for wurzel, ordner, _ in os.walk(users):
    for o in list(ordner):
        if o in ("EBWebView", "wine_gecko"):
            shutil.rmtree(os.path.join(wurzel, o), ignore_errors=True)
            ordner.remove(o)

# --- Restprüfung: alle Dateien (Text und binär, UTF-8/ASCII und UTF-16) sowie Datei- und Linknamen ----------
begriffe = ([alt] if alt != neu else []) + HOSTS + DOMAINS + KURZ + vollname()
if not begriffe:
    sys.exit("Keine Suchbegriffe für die Restprüfung ermittelt.")
# Begriffe, die einen anderen enthalten, findet die Suche nach dem kürzeren ohnehin (z. B. „rechner.heimnetz.lan“).
begriffe = [b for b in dict.fromkeys(begriffe)
            if not any(a != b and a.lower() in b.lower() for a in begriffe)]
# Schnell statt regulärer Ausdrücke: Block einmal klein schreiben (bytes.lower betrifft nur ASCII), dann je Muster
# suchen. UTF-16 LE ohne das letzte Null-Byte trifft auch UTF-16 BE (dort steht die Null vor jedem Zeichen).
MUSTER = []
for b in begriffe:
    MUSTER.append((b.encode("utf-8").lower(), b))
    MUSTER.append((b.encode("utf-16-le")[:-1].lower(), b + " (UTF-16)"))
SUCHE_TEXT = re.compile("|".join(re.escape(b) for b in begriffe), re.I)
UEBERLAPPUNG = max(len(m) for m, _ in MUSTER)


def datei_treffer(pfad):
    gefunden = set()
    with open(pfad, "rb") as f:
        rest = b""
        while block := f.read(1 << 24):
            daten = (rest + block).lower()
            gefunden.update(name for m, name in MUSTER if m in daten)
            rest = daten[-UEBERLAPPUNG:]
    return gefunden


print("Suche nach:", ", ".join(repr(b) for b in begriffe), flush=True)
rest = []
for wurzel, ordner, dateien in os.walk(prefix):
    for n in ordner + dateien:
        p = os.path.join(wurzel, n)
        rel = os.path.relpath(p, prefix)
        if m := SUCHE_TEXT.search(rel):
            rest.append((rel, "Name", {m.group()}))
        if os.path.islink(p):
            if m := SUCHE_TEXT.search(os.readlink(p)):
                rest.append((rel, "Linkziel", {m.group()}))
            continue
        if n in dateien and os.path.isfile(p):
            try:
                if t := datei_treffer(p):
                    rest.append((rel, "Inhalt", t))
            except OSError as e:
                print(f"Warnung: {rel} nicht lesbar ({e})", file=sys.stderr)

if rest:
    print(f"\nFEHLER: {len(rest)} Rest-Vorkommen persönlicher Daten im Prefix:", file=sys.stderr)
    for rel, wo, t in rest[:50]:
        print(f"  {rel} ({wo}): {', '.join(sorted(t))}", file=sys.stderr)
    if len(rest) > 50:
        print(f"  … und {len(rest) - 50} weitere", file=sys.stderr)
    sys.exit(1)
print("Rest-Vorkommen: keine")
