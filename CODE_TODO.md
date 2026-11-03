# CODE_TODO — OBS_Textbox

Stand: 2026-10-26 (M4-Review)
Details zur Bewertung: `hermes-memory/notes/rewrite-bewertung.md` (F1–F18) — außerhalb des Repos.

## ✅ Fertig (T5-Batch 2026-10-22/23)

- [x] **F1 (Security-Blocker)** XSS: Import-Vorschau (`showImportPreview` in `modals.js`) rendert alle Formular-/Dateinamen-Werte nur noch HTML-maskiert; `escapeHtml` zentral in `dashboard/js/box-utils.js`.
- [x] **F2** Test-Pfad `tests/test_documents.py`: `ROOT.parent/"main"/"liedplan.docx"` statt Repo-Root.
- [x] **F3** Box-ID-Duplikat-Check: Umbenennen bricht bei Kollision mit Toast ab; AI-Import nutzt `uniqueBoxId()`.
- [x] **F4** Duplizierte Box-Defaults in `box-utils.js` ausgelagert: `createDefaultBox()` / `sanitizeBoxId()` / `uniqueBoxId()` / `escapeHtml()` — genutzt von `cards.js`, `modals.js`, `forms-importer.js`.
- [x] **F5** Forms-API-Pagination: `fetch_all_services()` (pageSize=100 + pageToken), Datei-Pfad liefert `all_services_from_file()`, neues `GET /api/forms/responses` (alle Einreichungen), Upload antwortet mit allen Services + Count.
- [x] **F6** README-Bildpfad: `../main/preview.png`.
- [x] **F7** `rewrite/lieder.md`-Duplikat gelöscht (bit-identische Root-Kopie); geladen wird `dashboard/lieder.md`.
- [x] **F8** ~75 Zeilen SHA-256-Fallback aus `obs-ws.js` entfernt (WebCrypto ist in OBS-Docks immer vorhanden).
- [x] **F9** `forms-importer.js` liest Auto-Send-State über `getState()` statt rohem `localStorage`.
- [x] **F10** pdf.js 3.11.174 lokal: `dashboard/vendor/pdf.min.js` (statt CDN) — PDF-Pfad funktioniert offline; Google Fonts bleiben CDN (degradieren offline bewusst).
- [x] **F11** `textbox.html`: periodischer Render 250 ms → 2000 ms (Sicherheitsnetz; beide MutationObserver decken den Normalfall ab).
- [x] **F13** `server/console.py`: `RequestStats.clients` wird stündlich geprüft und Einträge >24 h inaktiv ausgemistet.
- [x] **F14** `server/config.py`: kaputte `config.json`-Werte → `SystemExit` mit Feld-Namen statt ungefangenem Traceback.
- [x] **F15** `obs-sync.js`: `SetInputSettings` nur, wenn `newCss !== lastAppliedCss` **und** `newCss !== currentCss` — erster Push nach Dashboard-Reload lädt OBS-Quellen nicht mehr unnötig neu.
- [x] **F16** `getTargetBox()` (Dead Code) aus `cards.js` entfernt.
- [x] **F17** `parseSongReference()` in `pdf-ai-importer.js`: Header-Zeile der `lieder.md`-Tabelle wird per erster Zelle ("Nr") erkannt, Separator übersprungen; kein Header → Parsing ab Zeile 0 (robuster als fester Zeilenindex).
- [x] **F18** README: `GET /api/openrouter-key` als bewusstes Design dokumentiert (loopback-only, `403` für LAN) — Sektion "Sicherheitsnotizen".
- [x] **"Alle senden" (btn-send-all) entfernt** — Pro-Karte-Senden bleibt (Entscheidung Johann, 2026-10-22).
- [x] **Einheitlicher Import-Dialog:** AI (PDF/Word) + Datei (CSV/XLSX/ZIP) + Google Forms als Tabs in `#import-dialog`; Formular-Abruf listet **alle** Einreichungen (neueste zuerst) mit Auswahl-Dropdown, gemeinsame HTML-maskierte Vorschau.
- [x] **`app.js`** an die neue Modul-API synchronisiert (`initImportModal`, `import-dialog`, `btn-open-import`).

## ☐ Offen

- [ ] **T7 — Standalone OBS-Plugin:** `plugin/` mit C++/CMake/MSVC-Umsetzung (Karten-Anzeige + Import), KEIN lokaler Python-Server, externe Verbindungen erlaubt (Google Forms API, OpenRouter); README mit Build-Anleitung. → **M1–M4 fertig (Commit 2026-10-26)**; offener Rest siehe „Echtes Formular-End-to-End“.
- [ ] **Echtes Formular-End-to-End:** Liedplan-Formular einmal ausfüllen → `GET /api/forms/responses` mit echten Daten testen + echten CSV-Export aus Google importieren.
- [ ] **Optional (nur auf Wunsch):** Wochenplan-Auswahl — mehrere Gottesdienste pro Woche parallel auswählen.
- [ ] **T6 (Repo-Außen):** Vollständige Repo-Kopie auf den Desktop (`OBS_Textbox-Kopie`, inkl. `.git`, ohne `.venv`) — erst nach T5+T7 ansetzen.

## 🔎 M4 5-Achsen-Review (2026-10-26, Hermes selbst — Gemini-Free-Tier-Sperre)

Diff: XLSX/ZIP-Import (Mini-ZIP + Mini-XML + zlib), Backup/Restore, Schnell-Profile, Settings, Backward-Compat.
Ergebnis: **Approve, kein Blocker.** ctest 4/4, import 19/19, state 15/15. E2E per computer_use: Schnell-Profile speichern/löschen im LIVE-OBS-Dock, Status-Labels + Persistenz verifiziert.

### Korrektheit (sollte fixen, nicht blockierend)
- [ ] **M4-K1:** `inflate_data()` in `liedplan-import.cpp` — keine harte Obergrenze für dekomprimierte Größe (zip-bomb-DoS per lokaler Datei: kleines ZIP → mehr-GB-Allokation). Vorschlag: Cap ~256 MB, bei Überschreitung `nullopt` + deutscher Fehler „Datei im Archiv zu groß“.

### Trimmen (niedrige Priorität, als Batch)
- [ ] **M4-T1:** `build_zip_archive()` ist nur Test-Hilfe, steht aber im öffentlichen `liedplan-import.hpp` (exportiert von der Produktions-DLL). Können in Test-Helper wandern.
- [ ] **M4-T2:** Import-Dialog: Form-ID/Token-Pfad-Fields rufen `save_state()` pro Tastendruck (Disk-Write/Keystroke). Besser: bei blur/Dialog-Schließen speichern.
- [ ] **M4-T3:** `decode_xml_entities()`: Surrogate-CPs (0xD800–0xDFFF) nicht verworfen → invalides UTF-8 nur bei defektem XLSX.

### Positiv (NICHT antasten)
- EOCD-Backward-Scan mit Comment-Länge-Toleranz + uint64-Overflow-Guards (`cd_offset+cd_size`).
- `restore_backup()` atomar über Temp-State (Fehler lässt State unverändert).
- M3-Pipeline-Reuse: XLSX/ZIP münden in `parse_records`/`to_import_result`/`row_has_content` — keine Duplikate.
- QPointer + detached-Thread + QueuedConnection konsistent mit M3-API-Import (UI-Blocker vermieden).
- Deutsche Fehlermeldungen durchgängig; 10 neue Tests (Roundtrips, Backward-Compat, Entities, Hidden-File-Ignorierung `._`/`__`).

## 🔎 M5 5-Achsen-Review (2026-10-27, Hermes — eigenverifiziert: Build grün, ctest 5/5, AI-Suite 10/10, DLL byte-identisch deployed, OBS-Log „plugin loaded“, Dock ok=1)

Diff: Animationen A10 (State-Felder + `normalize_anim_src` + `build_inject_css`/`push_update` real + Dock-UI „🎬 Animation“) + AI-Import D3 (neu `liedplan-ai.{hpp,cpp}`: markitdown-Sidecar, OpenRouter Free-Tier WinHTTP, 1:1-Ports von `pdf-ai-importer.js`, Offline-Fallback, lieder.md-Enrichment, Import-Dialog „🤖 AI-Import“ + Prompt-Editor + Preview-Tabelle) + Settings +5 Pfade.
Ergebnis: **Approve, kein Blocker.** ctest 5/5 (neu `liedplan-ai`), state-Suite 18 Checks grün.

### Korrektheit (minor, nicht blockierend)
- [ ] **M5-1:** `path_to_file_url()` in `liedplan-html.cpp` percent-encodiert nur `\` und Leerzeichen — nicht-ASCII-Dateinamen (Umlaute) landen roh im `file://`-URL. Aktuelle Animationen (`1_advent.gif` …) sind ASCII, daher unkritisch; bei Umlaut-Datei würde CEF das Laden verweigern.
- [ ] **M5-2:** AI-Test 10a (`extract_document_text`) verlangt Repo-`.venv` + markitdown — ohne Venv wird ctest rot (maschinenabhängig). Besser: Venv-Check → Test sauber skippen (PASS mit Vermerk).
- [ ] **M5-3:** Worker-Threads (AI- + CSV-Import) prüfen `QPointer` kurz vor `QMetaObject::invokeMethod` — enges Race-Fenster (Dock-Zerstörung zwischen Check und Post). Modaler Dialog hält den Dock in der Praxis am Leben; Musters aus M3, hier nur dokumentiert.
- [ ] **M5-4:** `extract_document_text()` in `liedplan-ai.cpp`: Kommandozeile mit einfachem `%s`-Quoting — Dateinamen mit Doppel-Quote brechen die Kommandokonstruktion (lokale Datei, vom User gewählt; Low). Besser: `"` in `""` escapen oder Argument-Passing ohne Shell.

### Positiv (NICHT antasten)
- AI-Flow auf `std::thread` + `Qt::QueuedConnection` — UI-Thread nie blockiert (Worst-Case ~14 min Retry-Chain läuft im Hintergrund).
- `extract_document_text`: Job-Object `KILL_ON_JOB_CLOSE` + 120 s-Wait + separate Reader-Threads (Pipe-Deadlock vermieden), deutsche Fehlermeldungen.
- Key-Handling: `read_env_key` nur lesen, Key nur in HTTP-Header, nie loggen/persistieren, `QLineEdit::Password` + Toggle.
- State-Backward-Compat: alle 5 Anim-Felder + 5 Settings nur `contains()`-geprüft geladen; Legacy-State lädt unverändert.
- Retry-Logik 1:1 v2 (429/502/503/504/524/529, 2 Versuche/Modell, 6 Modelle, 3 s Pause); Offline-Fallback bei LLM-Fehler UND bei leerem Key.

## 🔎 M7 5-Achsen-Review (2026-10-30, Hermes — eigenverifiziert: Build RelWithDebInfo grün, ctest 6/6 selbst ausgeführt; agy-Run proc_36439f76c223 nur Build/ctest/Report)

Diff: Dock-Nativisierung R2 — schmalere Ränder (root 8→4, Karten-Container 8→4, Panel-Innen 16→10, ID-Leiste 12/6→8/4), Toolbar nach unten (4× QToolButton Icons-only, Settings-QMenu mit Schnell-Profile/Einstellungen/Backup/Restore), Top-Row mit `profil_select_` + `card_search_` (QCompleter-Search, MatchContains, case-insensitiv), per-Karte einklappbare Stil-Sektion (`lpStyleSection` default-einklapppt, Chevron-Toggle), 2 neue Chevron-SVGs + `kVersion` 1→2. 5 Allow-List-Dateien, +411/−130.
Ergebnis: **Approve, kein Blocker.** ctest 6/6 (theme-icons-Suite um Chevron-Checks erweitert).

### Trimmen (niedrige Priorität, Batch-Kandidat)
- [ ] **M7-1:** Label-Format `"id · titel"` (20-Zeichen-Truncation) ist 2× dupliziert (zeile3-`textChanged`-Lambda + `refresh_top_row`) — kleiner Helper `card_search_label(card)` in dock.cpp deduplizieren.
- [ ] **M7-2:** `flash_row` testet `style_section`-Äquivalent via `isVisible()` statt `isHidden()` (Chevron-Toggle-Lambda) — bei verstecktem Dock (Parent-Chain) flippt das Icon vor dem tatsächlichen State-Change; self-correcting, aber `isHidden()` wäre robuster.
- [ ] **M7-3:** Quick-Profiles-Dialog „Übernehmen“ meldet „Schnell-Profil übernommen“ auch, wenn `apply_quick_profile(name)` fehlschlägt (Name nicht gefunden) — Dialog-Lambda bekommt keinen bool zurück; in Praxis kein Race (Name kommt direkt aus `state_`), Low.

### Positiv (NICHT antasten)
- `refresh_top_row()` mit `QSignalBlocker` + `prev_text`-Restore der Such-LineEdits — kein Cursor-Sprung/Signal-Feedback beim Rebuild.
- `flash_row`-QTimer mit `QPointer`-Guard (Widget kann zwischen Schedule und Fire weg sein) + unpolish/polish für Property-Selector-Update — sauberes Qt-Pattern.
- Quick-Profiles-„Übernehmen“ im Dialog geht jetzt durch denselben `apply_quick_profile(name)`-Helper wie die Top-Row (Duplikat von prev_ids-Collection + save/sync/rebuild weg).
- `lpCardId`-Property + `ensureWidgetVisible` für Scroll-to-Card — kein zusätzliches Map zu pflegen.

## 🚫 Nicht umsetzen (beschluss)

- **F12:** `textbox.html` `padding: 350px 20px 20px 250px` bleibt absichtlich so (Entscheidung Johann, 2026-10-22) — kein Code-Change.

## Entscheidungen (Protokoll)

- 2026-10-20: 1 abgeschicktes Formular = 1 Gottesdienst; 12 feste Punkte × 3 Felder (GL-Nummer / Verse / Liedtitel).
- 2026-10-22: „Alle senden" weg, Pro-Karte-Senden bleibt.
- 2026-10-22: textbox-Padding bleibt (F12).
