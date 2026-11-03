# Spec IMP-B: UX-Features — Mehr-Gottesdienste-Auswahl + Undo — OBS_Textbox

Datum: 2026-11-03. Repo: `C:\Users\Johann\Desktop\GitHub\OBS_Textbox`. Quelle: IMPROVEMENTS.md (#1,#11).

## Erlaubte Dateien
`plugin/src/liedplan-import.hpp`, `plugin/src/liedplan-import.cpp`, `plugin/src/liedplan-dock.cpp`,
`plugin/src/liedplan-state.hpp` (nur falls für Undo nötig), `plugin/src/tests/test-liedplan-import.cpp`.
NICHTS anderes. Kein git, keine Umbenennungen/Verschiebungen/Löschungen.

## Aufgabe 1 — Mehrere Gottesdienste (#1)
- `ImportResult` (liedplan-import.hpp) erweitern um `std::vector<LiedplanService> services`
  (sortiert newest-first nach timestamp; der bisherige Einzel-`service`-Field bleibt gefüllt mit dem
  neuesten — Abwärtskompatibilität, alle bestehenden Aufrufer bleiben unverändert gültig).
- `parse_answers_csv` / `parse_answers_xlsx` / `parse_answers_zip` / `to_import_result` / `import_from_api`:
  statt `records.back()` / `responses->front()` alle Services sammeln und in `services` legen.
- Tab „📋 Google Forms & Datei" (liedplan-dock.cpp): nach Fetch/Datei-Import eine `QComboBox` mit allen
  Services anzeigen, Label `"<zeit> · Woche <woche> · <tag> · Org: <organist>"`; vorausgewählt = neuester.
  Daneben eine kompakte Vorschau (Liste/QLabel pro Service: Woche, Tag, Org, Liedanzahl aus service.cards)
  und Button „In Karten übernehmen" — ruft apply_import_result mit dem gewählten Service
  (ImportResult mit nur diesem Service als .service aufbauen). Bestehender Auto-Übernehmen-Pfad des
  neuesten Service bleibt wie heute (Button ersetzt nicht den bestehenden Button, ergänzt ihn).
- Statuszeile zeigt Anzahl gefundener Gottesdienste ("3 Gottesdienste gefunden").

## Aufgabe 2 — Einstufiges Undo (#11)
- Member `std::vector<LiedplanCard> undo_snapshot_;` + `bool undo_available_ = false;`
- Vor jedem Aufruf von `clear_all_cards()`, `delete_card()`, `apply_import_result()`, `ai_apply_btn`
  (und Profil-Apply analog wenn destruktiv): Snapshot der aktuellen `state_.cards` sichern.
- Toolbar (nahe „Alle leeren") kompakter Button „↶ Rückgängig": disabled wenn !undo_available_; beim Klick
  `state_.cards = undo_snapshot_;` → save_state → alle Karten neu rendern → `apply_all_to_scene(false)` →
  undo_available_=false, set_status("Vorheriger Zustand wiederhergestellt").
- `QShortcut(QKeySequence::Undo, this)` → gleicher Handler.

## Tests
- test-liedplan-import.cpp: neuer Test — CSV/ZIP mit 2 Services liefert `services.size()==2`, newest first,
  `service` == neuester (bestehende Tests unverändert grün).
- cwd `plugin/`: Build + `ctest --test-dir build_x64 -C RelWithDebInfo` → 6/6 grün.
- rewrite/v1 nicht betroffen → trotzdem kurz ausführen (unittest discover rewrite/tests, node tests).
OBS läuft → kein Deploy.

## Report
`STATUS`, `GEÄNDERTE_DATEIEN`, `BUILD`, `CTEST: 6/6`, `DEPLOY: SKIPPED_OBS_RUNNING`, je Aufgabe DONE-Zeile.
