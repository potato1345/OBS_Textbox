#!/usr/bin/env bash
# Startet den OBS-Textbox-Server aus dem Ordner dieses Skripts.
set -euo pipefail
cd "$(dirname "$0")"

for candidate in .venv/bin/python ../.venv/bin/python python3 python; do
    if command -v "$candidate" >/dev/null 2>&1 || [ -x "$candidate" ]; then
        exec "$candidate" -m server "$@"
    fi
done

echo "Fehler: Python wurde nicht gefunden." >&2
echo "Einmalig einrichten:  python3 -m venv .venv && .venv/bin/pip install -r requirements.txt" >&2
exit 1
