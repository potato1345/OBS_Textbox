# Spec IMP-A: Quick-Wins (Robustheit/Sicherheit) — OBS_Textbox

Datum: 2026-11-03. Repo: `C:\Users\Johann\Desktop\GitHub\OBS_Textbox`. Quelle: IMPROVEMENTS.md (#2,#4,#9,#10,#12,#13,#15).

## Erlaubte Dateien
`plugin/src/liedplan-import.cpp`, `plugin/src/liedplan-dock.cpp`, `plugin/src/liedplan-state.cpp`,
`plugin/src/liedplan-ai.cpp`, `plugin/src/liedplan-html.cpp`, `plugin/src/plugin-main.cpp`,
`plugin/CMakeLists.txt`, `plugin/src/tests/test-liedplan-import.cpp`,
`rewrite/server/forms_import.py`, `rewrite/dashboard/js/modals.js`, `rewrite/dashboard/js/color-profiles.js`,
`main/server.py`. NICHTS anderes.

## Verboten (hart)
Kein git. Keine Umbenennungen/Verschiebungen/Löschungen von Dateien. Kein neuer Ordner.
Bestehende Kommentare behalten (Kommentarrunde ist bereits durch — neue Kommentare nur für NEW code).

## Aufgaben (alle 7)
1. **Zip-Bomb-Cap (#2)**: `inflate_data()` in liedplan-import.cpp: hartes Limit
   `constexpr size_t kMaxInflateBytes = 64 * 1024 * 1024;` — beim Überschreiten `inflateEnd`, `std::nullopt`.
   `extract_zip_entries()` zusätzlicher Gesamtbudget-Zähler über ALLe Einträge (64 MB) → Fehler
   "ZIP-Archiv zu groß (Zip-Bomb-Schutz)". Neuer Test in test-liedplan-import.cpp: mit zlib `compress2`
   ~100 MB Null-Bytes kompressieren, `inflate_data` damit → `nullopt` (Cap greift).
   `rewrite/server/forms_import.py`: vor `zf.read()` `info.file_size > 64*1024*1024` → `ValueError("ZIP-Eintrag zu groß")`.
   `main/server.py`: beide POST-Pfade: `content_length > 25*1024*1024` → `self.send_error(413)`, Body nicht lesen.
2. **State-Härtung (#4)**: `LiedplanDock`- ctor: `const std::string err = state_.load(state_path_);` — wenn
   `!err.empty()`: Datei nach `state_path_ + ".bak"` kopieren (std::filesystem::copy_file), 12 Standardkarten,
   `set_status("Warnung: liedplan-state.json war beschädigt (Backup .bak angelegt)", true)`.
   `LiedplanState::save()`: erst nach `path + ".tmp"` schreiben, dann `std::filesystem::rename(tmp, path)`
   (atomar); tmp bei Fehler löschen.
3. **Win32-Quoting + anim-Pfadguard (#12)**: helper `quote_win32_arg(const std::wstring&)` in liedplan-ai.cpp
   (Backslashes vor/inside `"` verdoppeln, `"` als `\"`) — in der CreateProcessW-Kommandoleile für markitdown
   verwenden. `normalize_anim_src()` (liedplan-html.cpp): relative Pfade nach `weakly_canonical` müssen
   innerhalb des Animation-Base-Verzeichnisses bleiben (Repo `main/animation` bzw. settings-Ordner) — sonst
   leer lassen wie bisher ungültig; externe `http(s)`-URLs bleiben erlaubt (Verhaltenskompatibilität).
4. **YAML-Escape + Surrogate (#15)**: `liedplan_profile_to_yaml()`: `\` und `"` in name maskieren; `unquote()`
   demaskiert beide wieder. `color-profiles.js colorProfileToYAML()`: analog escape für name.
   `append_utf8_codepoint()` in liedplan-import.cpp: `cp` im Bereich 0xD800..0xDFFF → `U+FFFD` (EF BF BD).
5. **Tote Quelle entfernen (#13)**: `obs_register_source(&liedplan_card_source_info);` aus plugin-main.cpp
   löschen; `liedplan-card-source.cpp` aus Produktions-Target `obs-liedplan` in CMakeLists entfernen
   (test-liedplan-render nutzt liedplan-render.cpp — prüfen und NICHT kaput machen); die `[liedplan-debug]`
   blog-Zeilen in card-source.cpp löschen (Datei bleibt für Render-Tests).
6. **Profil-XSS + Push (#9)**: modals.js: `escapeHtml(profile.name)` im Button-innerHTML; im Klick-Handler
   nach load_quick_profile: `if (getState().autoSend) pushStateToOBS();` (Imports prüfen, was dort verfügbar ist).
   Dock: beim Schnell-Profil-Klick `set_status(QString("Profil \"%1\" geladen").arg(...), false)`.
7. **Import-Kartenbereinigung (#10)**: in `ai_apply_btn` UND `apply_import_result()`: wenn
   `state_.cards.size() < items.size()`: fehlende Karten mit `make_default_card(i)` ergänzen; für alle
   Bestandskarten `i >= items.size()`: `zeile2`/`zeile3` leeren und `zeile1` nur wenn nicht Standard-Karte.

## Tests (selbst ausführen, alle grün)
- cwd `plugin/`: `cmake --build --preset windows-x64 --config RelWithDebInfo` (PATH += `C:\Users\Johann\tools\cmake-3.30.5-windows-x86_64\bin`)
- `ctest --test-dir build_x64 -C RelWithDebInfo` → **7 Suites? NEIN: 6 Suites müssen grün bleiben** (neuer Zip-Test in Suite 3, keine neue Suite)
- cwd Repo-Root: `.venv/Scripts/python.exe -m unittest discover rewrite/tests` → 18 OK
- cwd `rewrite/`: `node tests/test_js_modules.js` → grün
- cwd `main/`: `../.venv/Scripts/python.exe tests/test_forms_import.py` → ALL TESTS PASSED (12)
OBS läuft → kein Deploy (`DEPLOY_SKIPPED_OBS_RUNNING`).

## Report
`STATUS`, `GEÄNDERTE_DATEIEN`, `BUILD`, `CTEST: n/n`, `PY_V2`, `JS_V2`, `PY_V1`, `DEPLOY`, je Aufgabe 1 Zeile DONE.
