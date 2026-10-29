# M6-B2B3-Spec — Eigene Farbprofile (B2) + YAML-Export/Import (B3)

**Status:** geplant (Hermes, 29.10)
**FUNKTIONSVERGLEICH-Items:** B2 (eigene Farbprofile anlegen/speichern/löschen), B3 (YAML-Export/-Import)
**Referenz (1:1-Verhalten):** `rewrite/dashboard/js/color-profiles.js` (YAML-Parser/Writer, COLOR_KEYS, isDefaultColorProfile) + `rewrite/dashboard/js/modals.js` L100–233 (speichern/löschen/export/import + Namens-Regeln).

## §0 Datei-Allowlist (NUR diese ändern/erstellen)

- `src/liedplan-state.hpp`, `src/liedplan-state.cpp` (State-Feld + Lookup + YAML-Funktionen + Tests-Helfer bleiben privat)
- `src/liedplan-profiles.hpp` (nur wenn Lookup-Helper dort hingehört — darf auch ganz unverändert bleiben)
- `src/liedplan-dock.cpp`, `src/liedplan-dock.hpp` (per-Karte-Button + Settings-Gruppe)
- `src/liedplan-theme.hpp` (QSS für neue Widgets, Muster = bestehende lp*-Regeln)
- `src/tests/test-liedplan-state.cpp` (neue Checks)
- `CMakeLists.txt` nur, falls wirklich nötig (es dürfen KEINE neuen Exe-Targets entstehen — Tests in die bestehende `liedplan-state`-Suite)

**Anti-Umzugs-Klausel:** Keine Datei außerhalb `plugin/` anrühren, keine Datei verschieben/umbenennen, nichts aus dem Repo-Root nach `plugin/` ziehen oder umgekehrt. `main/`, `rewrite/`, `CODE_TODO.md`, `FUNKTIONSVERGLEICH.md` bleiben untracked/unverändert. **Kein git** (Hermes committet).

## Ausgangslage (NICHT antasten)

- Standard-Profile: 6× `LiedplanProfile {name, text_color, box_color}` in `liedplan-profiles.hpp` (compiliert).
- Per Karte: `LiedplanCard::profile` (Name-String) + `text_color`/`box_color` (eigenständig, A7/A8-UI schon da — Button-Labels `#lpTextColor`/`#lpBoxColor`, Slider `#lpOpacityText`/`#lpOpacityBox`).
- Profil-Dropdown je Karte (`profile_combo`) + Connect = Reset auf Profilsatz (kopiert text/box colors auf die Karte + aktualisiert Button-Labels + save_state + update_everywhere).
- M4-Settings-Dialog: `on_open_settings_dialog()` (Animation-Settings). M4-Schnell-Profile-Dialog `on_open_quick_profiles_dialog()` = UI-Muster für Listen-Dialoge (QGroupBox + QListWidget + Buttons).
- State-Seriale: `liedplan-state.cpp` (nlohmann/json), Backward-Compat-Defaults-Muster = bestehende Felder.

## B2 — Eigene Farbprofile

### State
- `LiedplanState` + Feld `std::vector<LiedplanProfile> color_profiles;` (Default leer).
  Serialisieren/Deserialisieren in `save`/`load` (Key `"color_profiles"`, Array von `{name, text_color, box_color}`; fehlend → leer), `operator==` + `reset()` erweitern.
- Lookup: neue Helper-Funktion (z. B. in `liedplan-state.hpp` oder `-profiles.hpp`):
  `const LiedplanProfile *liedplan_profile_lookup(const std::string &name, const std::vector<LiedplanProfile> &custom)`
  → zuerst die 6 Standards (exakt, wie heute `profile_by_name`), danach `custom` (exakt). `profile_by_name` bleibt unverändert.

### UI per Karte (Dock)
- `profile_combo` füllt mit: 6 Standards, danach (falls vorhanden) `custom`-Profile aus `state_.color_profiles` (QGroupBox-ähnliche Trennung per `QStandardItem`-InsertSeparator oder einfach hintereinander — OBS-Style: schlichte Liste, kein Emoji).
- Neuer kleiner Button neben dem Dropdown: Text **"Profil +"** (objectName `lpBtnNewProfile`, kompakt, OBS-Button-Look via bestehende QSS-Klassen), ToolTip: "Aktuelle Text-/Box-Farbe dieser Karte als Profil speichern".
- Click → `QInputDialog::getText` ("Profilname", Pflichtfeld) → Regeln (1:1 v2 `modals.js`):
  1. leer/nur Whitespace → abbrechen (Dialog erneut oder Status-Meldung).
  2. Name (case-insensitive) = Standard-Profilname → **ablehnen**, Status: `"X" ist ein geschütztes Standard-Profil.`
  3. sonst upsert in `state_.color_profiles` (gleicher Name → ersetzt) mit `text_color`/`box_color` = **aktuelle Werte der Karte** (nicht leer — A8-Fallback `#000000`/`#ffffff` gilt, wenn Karte leere Felder hat).
- Nach Erfolg: `save_state()` + **alle Karten-Rows neu aufbauen** (Dropdowns aller Karten müssen das neue Profil zeigen; Muster = `rebuild_rows()`/`sync_all_cards_live` wie nach Restore/Import) + Status `Farbprofil "X" gespeichert`.
- Karten, deren `profile` auf ein gelöschtes Profil zeigt: bleibt harmlos (Farben sind pro Karte gebaked; Lookup verfehlt einfach) — NICHT automatisch zurücksetzen, dokumentieren.

### UI Settings-Dialog (neue Gruppe "Farbprofile")
- `on_open_settings_dialog()`: neue QGroupBox **nach** der Animation-Gruppe.
- QListWidget (`lpProfileList`) mit den CUSTOM-Profilen, Item-Text: `NAME  —  TEXT #HEX · BOX #HEX` (Uppercase-HEX).
- Button **"Löschen"** (`lpBtnDeleteProfile`): nur bei Auswahl wirksam; löscht aus `state_.color_profiles`, `save_state()` + Rows-Rebuild + Status `Farbprofil "X" gelöscht`. Standard-Profile erscheinen NICHT in der Liste (geschützt).
- B3-Buttons s. u. in derselben Gruppe.

## B3 — YAML-Export/Import

### Pure Funktionen (OBS-/Qt-frei, `liedplan-state.{hpp,cpp}`, CTest-testbar)
- `std::string liedplan_profile_to_yaml(const LiedplanProfile &p)`:
  ```
  # Liedplan Farbprofil
  name: "<NAME>"
  colors:
    textColor: "<text_color>"
    cardOuterBg: "<box_color>"
  ```
  Key-Namen = v2-`COLOR_KEYS` (`textColor`, `cardOuterBg`) → **v2-Dateien importierbar**. Name in `"`-Quotes, Hex-Werte lowercase wie im State (QColor::HexRgb-Format).
- `bool liedplan_yaml_to_profile(const std::string &text, LiedplanProfile &out, std::string &err)`:
  1:1-Port der v2-`yamlToColorProfile`-Zeilenlogik (trim; leer/`#`-Zeilen skip; `name:` → Quote-Strip; `colors:` → Modus; darunter `key: value` mit Quote-Strip). **Nur** `textColor` + `cardOuterBg` extrahieren (alle anderen v2-Keys ignorieren — 18-Key-Dashboard-YAML muss funktionieren). Validierung:
  - kein Name → `err = "Profilname fehlt."`
  - weder textColor noch cardOuterBg gefunden → `err = "Keine Farben gefunden (textColor / cardOuterBg)."`
  - Hex-Format-Check: `#RGB` oder `#RRGGBB` (case-insensitive), sonst → `err = "Ungültige Farbformat: <val>"`.
  - fehlendes einzelnes Feld → Default (`#000000` bzw. `#ffffff`), kein Fehler (v2-Verhalten: Default bei undefined).
  - Rückgabe: true/false + `out` gefüllt.

### UI (in der "Farbprofile"-Gruppe)
- QComboBox (`lpYamlProfileCombo`) mit ALLEN Profilen (6 Standards + Custom) + Button **"YAML exportieren …"** (`lpBtnExportYaml`):
  `QFileDialog::getSaveFileName` (Default `farbprofil_<Name-ge sanitized>.yaml` — sanitize 1:1 v2: `[^a-zA-Z0-9_-]` → `_`; Filter `YAML-Dateien (*.yaml *.yml)`) → Datei synchron schreiben (klein, kein Thread nötig; UTF-8, LF) → Status `Profil "X" exportiert` bzw. Fehler.
- Button **"YAML importieren …"** (`lpBtnImportYaml`):
  `QFileDialog::getOpenFileName` (Filter wie oben) → Datei lesen (QFile, UTF-8) → `liedplan_yaml_to_profile` → Fehler → Status mit `err`-Text; Erfolg: Name-Kollision mit Standard (case-insensitive) → `name += " (Import)"` (1:1 v2) → upsert in `state_.color_profiles` → `save_state()` + Rows-Rebuild + Status `Profil "X" importiert`.

## QSS (`liedplan-theme.hpp`)

- Neue objectNames in bestehende Selector-Listen hängen (Buttons `#lpBtnNewProfile`/`#lpBtnDeleteProfile`/`#lpBtnExportYaml`/`#lpBtnImportYaml` → gleiche Regeln wie `#lpBtn`; Liste/Combo → gleiche Regeln wie bestehende lp-Widgets). **Keine neuen Tokens, keine Hardcodes im cpp.**

## Tests (`src/tests/test-liedplan-state.cpp`, Suite bleibt `liedplan-state`)

Neue Checks (alle deterministic, keine Dateien außer tmp-Strings):
1. **YAML-Roundtrip:** Profil `{Mein Profil, #aabbcc, #ddeeff}` → `liedplan_profile_to_yaml` → `liedplan_yaml_to_profile` → identisch (Name + beide Farben).
2. **v2-18-Key-YAML-Import:** Multi-Line-Fixture im Stil von `rewrite/dashboard/color-profiles.yaml` (Kommentare, `profiles:`-Wrapper NICHT nötig — v2-Export-Format = `name:` + `colors:`-Block) mit allen 18 COLOR_KEYS → Parser extrahiert exakt `textColor` + `cardOuterBg`, ignoriert die übrigen 16.
3. **YAML-Fehlerpfade:** leerer Name → false + "Profilname fehlt."; keine Color-Keys → false + "Keine Farben…"; `textColor: "zzz"` → false + "Ungültige Farbformat…".
4. **YAML-Defaults/Quoting:** 3-Digit-Hex akzeptiert; Werte mit `'`-Quotes; fehlendes box_color → `#ffffff`-Default, kein Fehler.
5. **Lookup:** Standard vor Custom (Custom mit identischem Namen wird übersprungen); Custom wird gefunden; unbekannter Name → nullptr.
6. **State-Seriale:** State mit 2 Custom-Profilen → `save` → `load` → `color_profiles` identisch (Reihenfolge + Werte); JSON OHNE Key → leer (Backward-Compat); `reset()` → leer.
7. Alle bestehenden Checks bleiben grün (Regressionswächter).

## Verifikation (agy)

1. Build `RelWithDebInfo` grün (MSVC, `cmake --build --preset windows-x64 --config RelWithDebInfo`, CMake-3.30.5-Path).
2. `ctest --test-dir build_x64 -C RelWithDebInfo` → **alle 6 Suiten grün** (inkl. neuer Checks in `liedplan-state`).
3. Deploy: OBS killen → `build_x64/RelWithDebInfo/liedplan-for-obs.dll` → `C:\Program Files (x86)\Steam\steamapps\common\OBS Studio\obs-plugins\64bit\` → OBS starten via PowerShell `Start-Process -FilePath '...\bin\64bit\obs64.exe' -WorkingDirectory '...\OBS Studio\data\obs-studio'` → ~45 s warten → neuester Log unter `C:\Users\Johann\AppData\Roaming\obs-studio\logs\` muss `plugin loaded successfully` + Liedplan-LOG_INFO zeigen + RAM-Check (~430 MB gesund; ~40 MB = DIALOG_OFFEN → Status `DIALOG_OFFEN` melden, NICHT selbst klicken).
4. Report an Hermes: geänderte Dateien + Zeilen, ctest-Auszug, OBS-Log-Zeilen, Deploy-Mtimes, Status.

## Doku (Hermes macht es)

- `FUNKTIONSVERGLEICH.md`: B2/B3 → ✅ M6; A7/A8 → ✅ M6; Meilenstein-Tabelle + "Offen/Kandidaten" aktualisieren.
