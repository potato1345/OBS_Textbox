# Spec: Kommentierung + Debug-Logs im gesamten Code (OBS_Textbox)

Datum: 2026-11-03. Repo: `C:\Users\Johann\Desktop\GitHub\OBS_Textbox`

## Ziel
Alle Quellcode-Dateien der drei Versionen werden mit aussagekräftigen **deutschen Kommentaren** versehen
(Datei-Header, Klassen-/Funktion-Doku, Inline-Kommentare für nicht-triviale Logik) und erhalten **teils
Debug-Logs** an strategischen Punkten. **Die Behaviour ist zu 100 % unverändert** — dies ist eine
Doku-/Logging-Runde, kein Feature- oder Refactor-Run.

## Erlaubte Dateien (Allow-List — nichts anderes anfassen)
- `plugin/src/*.cpp`, `plugin/src/*.hpp`, `plugin/src/tests/*.cpp` — neu erlaubt: `plugin/src/liedplan-log.hpp`
- `plugin/CMakeLists.txt` nur falls `liedplan-log.hpp` als Header eingetragen werden muss
- `rewrite/server/*.py`, `rewrite/tests/*.py`
- `rewrite/dashboard/js/*.js`, `rewrite/dashboard/index.html`, `rewrite/textbox.html`
- `main/server.py`, `main/forms_import.py`, `main/controlui/*.js`, `main/tests/test_forms_import.py`

## Verboten (hart)
- Keine Umbenennungen, Verschiebungen, Löschungen, neuen Ordner; keine anderen Dateien erstellen/ändern.
- Kein `git add`/`git commit`/`git push`/`git checkout`/`git restore` — Git totally forbidden.
- Keine Logik-/Wert-/API-Änderungen: Control-Flow, Bedingungen, Konstanten, Strings, Funktionssignaturen,
  QSS, CSS, JSON-Keys bleiben bitgleich im Verhalten. Kommentare und Logs sind die einzigen Adds.
- Bestehende Kommentare/Logs entfernen oder umformulieren ist erlaubt nur wenn identische Aussage.

## Kommentier-Regeln
- Sprache: Deutsch. Identifiers/Code bleiben englisch.
- Jede Datei: Header-Kommentar (Zweck, gehört zu v1/v2/v3, Zusammenspiel).
- Jede Klasse/public Funktion: kurze Doku (Was, Warum, Seiteneffekte, wichtige Randfälle).
- Inline nur dort, wo Logik erklärungsbedürftig ist (Parser-Ecken, CEF-Base64-URI, OAuth-Refresh,
 (border-radius-Pitfalls, Backward-Compat-Defaults, Race/Thread-Grenzen). Kein Kommentar-Müll bei
  Selbsterklärem.
- Keine TODO-/FIXME-Erfindungen; bestehende TODOs unverändert lassen.

## Debug-Logs
1. **plugin (C++)**: neuer Header `plugin/src/liedplan-log.hpp`: inline-Helfer
   `lp_log(const std::string& msg)` → ruft `blog("[liedplan] %s", msg.c_str())` ONLY wenn
   `LiedplanDebugEnabled()` true ist; Enabled = statisch, gesetzt via env `LIEDPLAN_DEBUG=1`
   (getenv beim ersten Aufruf, cached). Normale Lifecycle-Ereignisse (plugin load/unload, dock registered,
   state geladen/saved, Import gestartet/abgeschlossen mit Zahlen, AI-Import Pfad Wahl, Szene-push pro Karte,
   Quellen-Migration) loggen UNBEDINGT immer mit Präfix `[liedplan]` (blog direkt,selten, max ~1 Zeile/Ereignis).
   Verbose Zeilen (pro Feld, pro HTTP-Response-Header) nur hinter LIEDPLAN_DEBUG.
   Die Qt-OBS-freien Test-Targets (liedplan-tests/import/textfmt/profiles/theme-icons suites) dürfen
   `liedplan-log.hpp` NICHT include-verwenden (kein blog dort) — Kommentar only.
2. **rewrite/server (Python)**: `logging` Modul, Logger-Name `liedplan`, Server-Entry setzt basicConfig
   (INFO, Timestamp, stdout) nur wenn nicht bereits konfiguriert; Events: Start (Port/Form-ID), Request-Routing
   (nur neue Requests, INFO), Import-Pipeline (Schritte mit Counts), OAuth-Refresh (nur Erfolg/Fehler, nie Token!),
   Fehler-Paths. NIE Secrets/Token-Werte loggen.
3. **rewrite/dashboard (JS)**: `console.debug` nur hinter Flag: `const LP_DEBUG = new URLSearchParams(location.search).has("debug")`
   zentral einmal (in einem vorhandenen utils-Modul, z. B. box-utils.js exportieren); loggt App-Init, WS-Verbindung,
   Import-Schritte. Kein Log-Spam pro Render-Frame.
4. **main/ (v1)**: Python `logging` analog (Logger `liedplan-v1`), nur Server-Start, Request-Kategorien,
   Import/Parse-Events. controlui/*.js: Kommentare only (kein Logging nötig).

## Tests (alle MÜSSEN grün bleiben — vor Abschluss selbst ausführen)
- Plugin-Build: cwd `plugin/` → PATH um `C:\Users\Johann\tools\cmake-3.30.5-windows-x86_64\bin` ergänzt:
  `cmake --build --preset windows-x64 --config RelWithDebInfo`
- `ctest --test-dir build_x64 -C RelWithDebInfo` aus `plugin/` → **6/6 passed**
- Python v2: cwd Repo-Root → `.venv/Scripts/python.exe -m unittest discover rewrite/tests` → **18 OK**
- JS v2: cwd `rewrite/` → `node tests/test_js_modules.js` → **Alle JavaScript-Modultests erfolgreich**
- v1: cwd `main/` → `../.venv/Scripts/python.exe tests/test_forms_import.py` → **ALL TESTS PASSED (12)**

## Deploy
OBS läuft aktuell → **NICHT deployen**, Status `DEPLOY_SKIPPED_OBS_RUNNING` ist erlaubt. Kein OBS-Restart.

## Report-Format (Ende der Ausgabe)
Zeilenweise:
- `STATUS: DONE | BLOCKED:<grund>`
- `GEÄNDERTE_DATEIEN: <liste>`
- `BUILD: ok|fail`, `CTEST: n/n`, `PY_V2: ok|fail`, `JS_V2: ok|fail`, `PY_V1: ok|fail`
- `DEPLOY: SKIPPED_OBS_RUNNING`
- `BEHAVIOUR_DIFF: keine (Kommentare+Logs only)` — kurz begründen
