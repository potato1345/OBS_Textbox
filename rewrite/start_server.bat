@echo off
setlocal
cd /d "%~dp0"
set OBS_TEXTBOX_FROM_BAT=1
if exist ".venv\Scripts\python.exe" (
    set "PY=.venv\Scripts\python.exe"
) else if exist "..\.venv\Scripts\python.exe" (
    set "PY=..\.venv\Scripts\python.exe"
) else (
    echo Hinweis: keine .venv gefunden - nutze System-Python.
    set "PY=python"
)
"%PY%" -m server %*
