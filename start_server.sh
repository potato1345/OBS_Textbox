#!/usr/bin/env bash

echo " ██████╗ ██████╗ ███████╗     ████████╗███████╗██╗  ██╗████████╗██████╗  ██████╗ ██╗  ██╗"
echo "██╔═══██╗██╔══██╗██╔════╝     ╚══██╔══╝██╔════╝╚██╗██╔╝╚══██╔══╝██╔══██╗██╔═══██╗╚██╗██╔╝"
echo "██║   ██║██████╔╝███████╗        ██║   █████╗   ╚███╔╝    ██║   ██████╔╝██║   ██║ ╚███╔╝ "
echo "██║   ██║██╔══██╗╚════██║        ██║   ██╔══╝   ██╔██╗    ██║   ██╔══██╗██║   ██║ ██╔██╗ "
echo "╚██████╔╝██████╔╝███████║███████╗██║   ███████╗██╔╝ ██╗   ██║   ██████╔╝╚██████╔╝██╔╝ ██╗"
echo " ╚═════╝ ╚═════╝ ╚══════╝╚══════╝╚═╝   ╚══════╝╚═╝  ╚═╝   ╚═╝   ╚═════╝  ╚═════╝ ╚═╝  ╚═╝"
echo ""
echo "==================================================="
echo "         Lokaler Webserver wird gestartet"
echo "==================================================="
echo ""
echo "Bitte dieses Terminal-Fenster im Hintergrund offen lassen!"
echo "Sobald du dieses Fenster schliesst (oder Strg+C drueckst), ist der Server offline."
echo ""
echo "URL fuer das OBS Browser-Dock:"
echo "http://localhost:8080/controlui/control.html"
echo "==================================================="
echo ""

# Wechselt in den Ordner, in dem das Script liegt
cd "$(dirname "$0")"

# Pruefen, ob python3 oder python verfuegbar ist
if command -v python3 &>/dev/null; then
    python3 -m http.server 8080
elif command -v python &>/dev/null; then
    python -m http.server 8080
else
    echo "Fehler: Python ist nicht installiert!"
    exit 1
fi
