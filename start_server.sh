#!/usr/bin/env bash

# Wechselt in den Ordner, in dem das Script liegt
cd "$(dirname "$0")"

# Pruefen, ob python3 oder python verfuegbar ist
if command -v python3 &>/dev/null; then
    python3 server.py 8080
elif command -v python &>/dev/null; then
    python server.py 8080
else
    echo "Fehler: Python ist nicht installiert!"
    exit 1
fi
