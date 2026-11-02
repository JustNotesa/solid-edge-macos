#!/usr/bin/env bash
# Startet Solid Edge 2026 in der Wine-Umgebung.
set -euo pipefail
source "$(dirname "$0")/env.sh"

[ -f "$SE_EXE" ] || { echo "Solid Edge ist noch nicht installiert – erst ./install.sh ausführen." >&2; exit 1; }

cd "$(dirname "$SE_EXE")"
exec "$WINE" "$SE_EXE" "$@" >>"$SE_LOGS/start.log" 2>&1
