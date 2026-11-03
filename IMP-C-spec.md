# Spec IMP-C: Behaviour-Fixes — Debounce/Caching, Fallback-Schema, Titel-Anreicherung, Parser-Unify — OBS_Textbox

Datum: 2026-11-03. Repo: `C:\Users\Johann\Desktop\GitHub\OBS_Textbox`. Quelle: IMPROVEMENTS.md (#3,#7,#8,#14).

## Erlaubte Dateien
`plugin/src/liedplan-dock.cpp`, `plugin/src/liedplan-html.cpp`, `plugin/src/liedplan-ai.cpp`,
`plugin/src/tests/test-liedplan-ai.cpp`, `plugin/CMakeLists.txt` (nur falls Test-Regel-Anpassung nötig),
`rewrite/dashboard/js/pdf-ai-importer.js`, `rewrite/dashboard/js/modals.js`,
`main/controlui/pdf-automation.js`, `rewrite/tests/*` + `rewrite/node tests/test_js_modules.js` (nur wenn
ein JS-Test wegen Parser-Unify angepasst werden MUSS — minimal). NICHTS anderes. Kein git.

## Aufgabe 1 — Debounce + Base64-Cache (#3)
- `LiedplanDock`: ein shared single-shot `QTimer` (~150 ms) für disk-writes: Feld-/Slider-Änderungen rufen
  künftig `schedule_save()` auf (Timer startet save_state() + write_card() verzögert, einmal), statt synchron
  pro Tastendruck/Tick. Live-Vorschau an OBS bleibt sofort: `update_everywhere(..., true)` bzw. push_update
  ohne Verzögerung (nur Disk-Save/debounce write_card verzögert). Verhalten am Ende identisch (letzter
  Zustand wird gespeichert); kein Tick spammt mehr Disk.
- `edit_form_id` / `edit_token_path`: speichern bei `editingFinished` statt `textChanged` (bestehende
  Semantik: Wert wird beim Verlassen/Enter übernommen).
- `liedplan-html.cpp normalize_anim_src()`: Cache `std::map<std::string, std::pair<std::filesystem::file_time_type,
  size_t>> ... → data-uri` (key Pfad, value mtime+size+uri); Hit ohne Disk-Lese; Dateiänderung invalidiert.
  Ziel: 16 MB GIF nur einmal pro Änderung lesen.
- Doppel-Base64: `write_card()`-Pfad (build_inject_css → normalize_anim_src) und `push_update()`-Pfad lesen
  künftig beide aus dem Cache (kein Verhaltensunterschied, nur Cache-Hits).

## Aufgabe 2 — card_html Fallback-Schema (#7)
- `card_html()` (liedplan-html.cpp): Box-Sichtbarkeit exakt wie `build_inject_css()`/`push_update()`:
  `const bool visible = has_any_text(card);` → leer: `opacity:0; pointer-events:none;` statt
  `display:none` nach `box_opacity<=0`; Hintergrund-Alpha weiterhin aus `card.box_opacity` (rgba).
  `.anim-wrapper` + Animations-CSS/JS analog zu `inject_into_textbox_template()` ergänzen (falls anim_file
  gesetzt). Konsistenz-Test nicht nötig (bestehende Suites grün lassen).

## Aufgabe 3 — Gotteslob-Titel-Anreicherung überall (#8)
- liedplan-dock.cpp: `load_song_reference()` + `enrich_titles_from_reference()` nach dem API-key/no-key
  Verzweigungsblock ALSO ausführen (auch offline `parse_liedplan_locally` Pfad) und zusätzlich in
  `apply_import_result()` (Forms/File-Import) — nur wenn zeile2 GL-Nummer hat und zeile3 leer ist
  (Bestandsverhalten von enrich_titles_from_reference unverändert).
- modals.js: nach `parseLiedplanLocally` `enrichTitlesFromReference` aufrufen (Referenz-Tabelle dort
  verfügbar machen, ohne neue Dateien).
- main/controlui/pdf-automation.js: analog `completeTitlesLocally(localItems, songRef)` im Offline-Pfad
  (songRef exists dort bereits für Liederreferenz-Nutzung prüfen).

## Aufgabe 4 — Offline-Parser vereinheitlichen (#14)
- `liedplan-ai.cpp` parse_liedplan_locally + `rewrite/dashboard/js/pdf-ai-importer.js` local parser:
  Zeile nur aus GL-Nummer (`num`), `before.empty()` UND `current != nullptr && current.zeile2.empty()`
  → Nummer dem aktuellen Punkt zuordnen (`zeile2 = num`, `zeile3 = after` falls after nicht leer), KEIN
  neuer Punkt mit leerem zeile1.
- `main/controlui/pdf-automation.js`: einzeiliger Fall `label num after` → `after` als zeile3 übernehmen
  (statt zu verwerfen).
- test-liedplan-ai.cpp: neuer Fall "Kyrie\n437, 1-4" → ein Punkt zeile1=Kyrie, zeile2=437, 1-4; und
  "Eröffnung 144, 1+4+6 Nun jauchzt" → zeile3="Nun jauchzt".

## Tests
- plugin Build + ctest 6/6 grün (neue ai-Fälle in Suite 5).
- `node tests/test_js_modules.js` grün (falls JS-Parser-Test existiert und旧 Verhalten testete: Test an
  neue einheitliche Semantik anpassen — minimalster Diff).
- python v2 discover + v1 forms-import grün.
OBS läuft → kein Deploy.

## Report
`STATUS`, `GEÄNDERTE_DATEIEN`, `BUILD`, `CTEST: 6/6`, `JS_V2`, `PY_V2`, `PY_V1`, je Aufgabe DONE-Zeile,
`BEHAVIOUR_CHANGE:` Liste der geänderten sichtbaren Verhalten ( bewusst laut Spec).
