# Spec IMP-D: Pfad-Portable, v1-Härtung, HTML-Testsuite, Doku-Sync — OBS_Textbox

Datum: 2026-11-03. Repo: `C:\Users\Johann\Desktop\GitHub\OBS_Textbox`. Quelle: IMPROVEMENTS.md (#5,#16,#17,#18).

## Erlaubte Dateien
`plugin/src/liedplan-html.cpp`, `plugin/src/liedplan-state.hpp`, `plugin/src/liedplan-state.cpp`,
`plugin/src/plugin-main.cpp`, `plugin/CMakeLists.txt`, `plugin/src/tests/test-liedplan-html.cpp` (NEU),
`main/server.py`, `FUNKTIONSVERGLEICH.md`, `README.md`, `CODE_TODO.md`. NICHTS anderes. Kein git.

## Aufgabe 1 — Pfad-Portable (#5)
- CMakeLists: `install(DIRECTORY ...)` bzw. `install(FILES main/textbox.html main/controlui/lieder.md DESTINATION
  obs-plugins/64bit/data analog obs-studio plugin data dir)` — real prüfen wie OBS plugin data üblich installiert
  (obs_module_path()). Falls Install-Route zu komplex: zur Laufzeit Auflösung über `obs_module_file("textbox.html")`
  MIT Fallback auf bestehenden kRepoTextboxPath und settings-Override — Portabilität ohne Build-Komplikation ist
  das Ziel; dokumentieren was gewählt wurde.
- `kRepoTextboxPath`-Nutzung: zuerst settings-Override, dann obs_module_file-Auflösung, dann Dev-Fallback.
- `google_token_path` Default: `%LOCALAPPDATA%/hermes/google_token.json` via getenv("LOCALAPPDATA") (Windows),
  Fallback auf bestehenden Johann-Pfad wenn getenv leer. Bestehende stored settings behalten Vorrang.
- `is_abs_win_path()` → `std::filesystem::path(u8path).is_absolute()`-Check (auch UNC/Linux-ok).
- `winhttp`-Link nur unter `if(WIN32)`.
WICHTIG: aktuelle Runtime auf diesem PC muss unverändert funktionieren (Dev-Fallback bleibt).

## Aufgabe 2 — v1-Server-Härtung (#16)
- `main/server.py`: Default-Bind `('127.0.0.1', port)`; Env `OBS_TEXTBOX_BIND` (z. B. `0.0.0.0`) erlaubt
  opt-in LAN wie bisher. CORS: wenn Bind loopback → `Access-Control-Allow-Origin` auf loopback/localhost-Origins
  beschränken; bei opt-in LAN bleibt `*` (bewusste Kompatibilität für Phones im Kirchen-WLAN).
- `do_GET()`: Zugriffe auf Punkt-Dateien (`/.…`), `.py`, `api.env` mit 404 ablehnen (von jeder IP).
- README/v1-Doku: Env-Var erklären.

## Aufgabe 3 — HTML-Testsuite (#17)
- NEU `plugin/src/tests/test-liedplan-html.cpp` + CMake test target `test-liedplan-html` (Suite 7):
  liedplan-html.cpp ist libobs-frei? prüfen — falls obs-API drin ist: Mini-Stub oder die reinen String-Funktionen
  isoliert testen (build_inject_css, card_html, push_update JSON-Shape, normalize_anim_src für Data-URI/relative/
  http Fälle, Erhalt der geschützten Padding-Zeile `.textbox-container { padding: 15px 60px 20px 50px; }`).
- Tests decken #7-Konsistenz: card_html leer → opacity:0 nicht display:none; box_bg alpha aus box_opacity.
- ctest damit **7/7** grün.

## Aufgabe 4 — Doku-Sync (#18)
- FUNKTIONSVERGLEICH.md: Header-Stand → M7h + ctest-Anzahl aktuell; Meilenstein-Tabelle M7c–M7h ergänzen
  (Inhalte aus plugin git log `git log --oneline` lesen — Lesend erlaubt).
- README.md: kurze Build-/Install-Anleitung für v3 (cmake presets, DLL nach obs-plugins/64bit) und v2
  (rewrite/start); doppelte "3." fixen.
- CODE_TODO.md: erledigte M7b–M7h-Punkte abhaken.

## Tests
- plugin Build + ctest → **7/7**.
- python v2 + JS + v1 (test_forms_import.py 12) grün.
OBS läuft → kein Deploy.

## Report
`STATUS`, `GEÄNDERTE_DATEIEN`, `BUILD`, `CTEST: 7/7`, `PY_V2`, `JS_V2`, `PY_V1`, je Aufgabe DONE-Zeile.
