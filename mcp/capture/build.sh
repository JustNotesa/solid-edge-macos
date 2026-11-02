#!/usr/bin/env bash
# Baut secapture.dll und secapture-inject.exe (mingw-w64, x86_64).
set -euo pipefail
cd "$(dirname "$0")"
N="$HOME/.claude/tools/niedrig.sh"; [ -x "$N" ] || N=""
CC=x86_64-w64-mingw32-gcc
$N $CC -shared -O2 -s -Wall -o secapture-v2.dll secapture.c -lgdi32 -luser32
$N $CC -O2 -s -Wall -municode -o secapture-inject.exe secapture-inject.c
echo "gebaut: $(pwd)/secapture-v2.dll, secapture-inject.exe"
