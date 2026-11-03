#!/usr/bin/env bash

# Wechselt in den Ordner, in dem das Script liegt
cd "$(dirname "$0")"

# Prioritaet: virtueller Umgebung (enth MarkItDown), dann python3, dann python
if [ -f ".venv/bin/python" ]; then
    exec .venv/bin/python server.py 8080
elif command -v python3 &>/dev/null; then
    exec python3 server.py 8080
elif command -v python &>/dev/null; then
    exec python server.py 8080
else
    echo "Fehler: Python ist nicht installiert!"
    echo "(Einmal:  uv venv .venv  &&  uv pip install --python .venv \"markitdown[all]\")"
    exit 1
fi
