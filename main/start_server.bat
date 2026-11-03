@echo off
cd /d "%~dp0"
if exist ".venv\Scripts\python.exe" (
    .venv\Scripts\python.exe server.py 8080
) else (
    echo Hinweis: venv nicht gefunden - nutze System-Python.
    python server.py 8080
)
