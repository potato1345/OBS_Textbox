# Spec IMP-E: Kooperativer Thread-Shutdown beim OBS-Beenden — OBS_Textbox

Datum: 2026-11-03. Repo: `C:\Users\Johann\Desktop\GitHub\OBS_Textbox`. Quelle: IMPROVEMENTS.md (#6). Risiko med.

## Erlaubte Dateien
`plugin/src/liedplan-dock.cpp`, `plugin/src/liedplan-dock.hpp` (falls Member nötig),
`plugin/src/liedplan-ai.cpp`, `plugin/src/plugin-main.cpp`. NICHTS anderes. Kein git.

## Problem
Alle 5 Hintergrund-Jobs starten per `std::thread(...).detach()` (dock.cpp:1739,1834,1884,2063,2109).
Beim OBS-Beenden wird die DLL entladen während detached threads noch im DLL-Code laufen → Exit-Crash.

## Design
1. In `LiedplanDock`: `std::shared_ptr<std::atomic<bool>> cancel_ = make_shared<std::atomic<bool>>(false);`
   — in `~LiedplanDock()` auf `true` setzen (BEFORE andere teardowns).
2. Worker-Jobs capturen `cancel_` (copy des shared_ptr, NICHT `this`) und prüfen an sicheren Punkten
   (vor jedem retry, vor jedem Karten-push, nach markitdown-Warten): if `*cancel_` → early-return, keine
   Qt-Aufrufe/this-Zugriffe danach.
3. `extract_document_text()` (liedplan-ai.cpp): Warten auf markitdown-Kind in 200 ms Schritten
   (`WaitForSingleObject(pi.hProcess, 200)` Loop bis 120 s Deadline); wenn `cancel_` (optional
   `std::shared_ptr<std::atomic<bool>>` Parameter mit default nullptr) gesetzt → `TerminateProcess` + return.
   Signatur nur erweitern falls aufrufbar ohne Breaking (default-Argument).
4. Aktive Worker zählen: global `std::atomic<int> g_active_jobs = 0;` im plugin (RAII counter im Job:
   ++ am Start, -- am Ende). In `obs_module_unload()`: kurz warten (Loop max 5 s, sleep 100 ms) bis
   `g_active_jobs == 0` — danach erst unload-Rückgabe. Dock-Zerstörung setzt cancel_ vorher, damit Jobs
   schnell abbrechen.
5. AI/Forms/File-Jobs: Qt-Ergebnis-Callbacks via existing queued/invoked path bleiben unverändert für
   Normalbetrieb; nur bei cancel_ keine Callbacks mehr feuern.

## Tests
- Build ok; ctest 6/6 (Qt-freie Suites unberührt — liedplan-ai.cpp wird in Suite 5 kompiliert: die
  changed ai.cpp MUSS weiterhin ohne libobs kompilierbar sein (shared_ptr/atomic nur, kein blog/obs API
  in ai.cpp — außer bereits vorhandenen Includes).
- Manuell: OBS mit laufendem AI-Import schließen → kein Crash (wird vom Orchestrator E2E geprüft, nicht hier).

## Report
`STATUS`, `GEÄNDERTE_DATEIEN`, `BUILD`, `CTEST: 6/6`, Design-Punkte 1-5 je DONE.
