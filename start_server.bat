@echo off
:: Setze das Konsolen-Encoding auf UTF-8, damit die ASCII-Zeichen richtig dargestellt werden
chcp 65001 >nul
title OBS Textbox lokaler Server
color 0A

echo  ██████╗ ██████╗ ███████╗     ████████╗███████╗██╗  ██╗████████╗██████╗  ██████╗ ██╗  ██╗
echo ██╔═══██╗██╔══██╗██╔════╝     ╚══██╔══╝██╔════╝╚██╗██╔╝╚══██╔══╝██╔══██╗██╔═══██╗╚██╗██╔╝
echo ██║   ██║██████╔╝███████╗        ██║   █████╗   ╚███╔╝    ██║   ██████╔╝██║   ██║ ╚███╔╝ 
echo ██║   ██║██╔══██╗╚════██║        ██║   ██╔══╝   ██╔██╗    ██║   ██╔══██╗██║   ██║ ██╔██╗ 
echo ╚██████╔╝██████╔╝███████║███████╗██║   ███████╗██╔╝ ██╗   ██║   ██████╔╝╚██████╔╝██╔╝ ██╗
echo  ╚═════╝ ╚═════╝ ╚══════╝╚══════╝╚═╝   ╚══════╝╚═╝  ╚═╝   ╚═╝   ╚═════╝  ╚═════╝ ╚═╝  ╚═╝
echo.
echo ===================================================
echo          Lokaler Webserver wird gestartet
echo ===================================================
echo.
echo Bitte dieses schwarze Fenster im Hintergrund offen lassen!
echo Sobald du dieses Fenster schliesst, ist der Server offline.
echo.
echo URL fuer das OBS Browser-Dock:
echo http://localhost:8080/controlui/control.html
echo ===================================================
echo.

:: Wechselt in den Ordner, in dem die .bat Datei liegt
cd /d "%~dp0"

:: Startet den Python HTTP Server auf Port 8080
python -m http.server 8080

pause
