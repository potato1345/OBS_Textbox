# M4 — XLSX/ZIP-Import + JSON-Backup/Restore + Schnell-Profile

**Ziel:** T7-OBS-Plugin bekommt die drei restlichen v2-Import/State-Funktionen, die noch
fehlen (Scope-Entscheidung Johann 2026-10-24, final, s. `FUNKTIONSVERGLEICH.md` §Entscheidungen):
1. **XLSX-Import** (verlinktes Antwort-Sheet als Excel-Datei)
2. **ZIP-Import** (Google-Forms-Download, der die CSV enthält)
3. **JSON-Backup/Restore** (kompletter State als Datei exportieren/importieren)
4. **Schnell-Profile** (gespeicherte Voll-State-Snapsots: name + cards, apply = kompletter
   State-Replace) — im Plan-§1 auf M4 gelegt.

Das Plugin baut bereits auf M3 (Commit `383d0d0`): `liedplan-import.{hpp,cpp}` hat den
CSV-Parser (1:1-Port von `rewrite/server/forms_import.py`) + Google-Forms-API (WinHTTP) +
`LiedplanImportResult` + Import-Dialog im Dock. CTest-Suite `liedplan-import` läuft grün
(ctest 4/4). **Diese Spec erweitert EXAKT dieses Modul + Dock + State — nichts anderes.**

## 0. Grundregeln für agy (WICHTIG — Anti-Umzug & Scope)
- **DATEI-ALLOWLIST** (nur diese anlegen/ändern; NICHTS anderes):
  - `plugin/src/liedplan-import.hpp` (erweitern)
  - `plugin/src/liedplan-import.cpp` (erweitern)
  - `plugin/src/liedplan-state.hpp` (erweitern: Backup-/Schnellprofil-Felder)
  - `plugin/src/liedplan-state.cpp` (erweitern)
  - `plugin/src/liedplan-dock.hpp` (erweitern)
  - `plugin/src/liedplan-dock.cpp` (erweitern: Buttons + Dialoge)
  - `plugin/src/liedplan-theme.hpp` (erweitern: QSS für neue Buttons/Dialoge, v1-Tokens WIEDERVERWENDEN)
  - `plugin/src/tests/test-liedplan-import.cpp` (erweitern: neue Tests)
  - `plugin/src/tests/test-liedplan-state.cpp` (erweitern: Backup-/Schnellprofil-Roundtrip)
  - `plugin/CMakeLists.txt` (nur, wenn neue Libs/Quellen nötig — s. §5)
- **ANTI-UMZUG:** keine Datei im Repo verschieben, umbenennen, löschen oder außerhalb der
  Allowlist neu anlegen. Kein `git add`/`git commit`/`git stage` — Hermes committet selbst.
- **Keine neue externe Abhängigkeit** (kein Vendor-Download, kein `find_package` auf etwas,
  das nicht schon in `.deps` liegt). XLSX/ZIP werden per §2/§3 mit Bordmitteln + `zlib`
  gelöst (liegt in `.deps` als `zlibstatic.lib`).
- UI-Thread NIEMALS blockieren: File-I/O + Parsing in Worker-Thread (`std::thread`),
  Ergebnis via Qt-Signal/Event zurück (wie M3-API-Import).
- Alle neuen Dock-Widgets/Strings **auf Deutsch** (UI ist deutsch, s. Plan §6 M4 "deutsches UI").

## 1. Datenmodell-Erweiterung (liedplan-state.{hpp,cpp})
Der State liegt in `AppData\Roaming\obs-studio\plugin_config\liedplan-for-obs\liedplan-state.json`
(global, NICHT pro Profil). Derzeit enthält er `cards[]` (je: id, zeile1/2/3, text_color,
box_color, box_opacity, corner_color_a/b, corner_opacity_a/b, rainbow_corners) + `active_profile`.

**NEU — drei zusätzliche Top-Level-Objekte, alle mit Backward-Compat-Defaults (fehlend = leer),
damit alte State-Dateien ohne Migration laden:**
```jsonc
{
  "cards": [ /* unverändert */ ],
  "active_profile": "OBS Dark",
  "settings": {                       // NEU (M4-Backup trägt dies mit)
    "google_form_id": "1jreAuzd-…",   // Default = kDefaultFormId
    "token_path": "C:\\…\\google_token.json"   // Default = kDefaultTokenPath
  },
  "quick_profiles": [                 // NEU — Schnell-Profile (Voll-State-Snapsots)
    { "name": "So 19.10.", "cards": [ /* Kopie der cards[] */ ] }
  ]
}
```
- `settings`: optional; beim Laden fehlende Keys → Defaults. Beim Save immer mitschreiben.
- `quick_profiles`: `std::vector<nlohmann::json>` (name + cards-Kopie). apply = `cards` durch
  Snapsot ersetzen + Save + `update_everywhere()` (live).
- **WICHTIG (F1-Regel, Ui2):** jedes neue State-Feld braucht einen Backward-Compat-Default in
  `liedplan-state.cpp` (fehlend → leer/Default). NIE `state["x"]` ohne `.value_or(...)`/`contains`-Check.

## 2. XLSX-Import (ohne openpyxl — C++ mit Bordmitteln)
Ein `.xlsx` ist ein ZIP mit XML-Teilen. Das Formular-Sheet hat einen **festen, bekannten Aufbau**
(40 Spalten, 1 Antwort-Zeile = 1 Gottesdienst; s. §Referenz-Sample unten). Kein generischer
XLSX-Parser nötig — nur die zwei relevanten Teile lesen:

**Pfad (Byte-Input `data`, Filename-Endung `.xlsx`):**
1. ZIP öffnen (§3) → Member `xl/worksheets/sheet1.xml` + `xl/sharedStrings.xml` (optional) lesen.
2. **sharedStrings.xml** parsen: `<sst …><si><t …>TEXT</t></si>…</sst>` → `vector<std::string> sharedStrings`
   (Index = Reihenfolge). `<t xml:space="preserve">` möglich → Inhalt zwischen `<t…>` und `</t>`
   als UTF-8 string, HTML-Entities `&amp; &lt; &gt; &quot; &apos;` + `&#NNN;`/`&#xHH;` dekodieren.
3. **sheet1.xml** parsen: `<sheetData><row …>…<c r="A1" [t="s|str|inlineStr|n"]>…</c>…</row>…`.
   Pro `<c>`: Attribut `r` → Spaltenindex (Buchstaben → 0-basiert, z. B. `A1`→0, `B1`→1, `AA1`→26).
   Typ `t`:
   - `t="s"` → Wert = `sharedStrings[<v>INDEX</v>]`
   - `t="str"` / kein `t` (Zahl) → Wert = Inhalt von `<v>` (Zahlen als String, `.` als Dezimalpunkt)
   - `t="inlineStr"` → Wert = `<is><t …>TEXT</t></is>`
   → `vector<vector<string>> rows` (erste Zeile = Header, danach Daten).
4. `rows_to_records(header, rows)` — **GENAU der vorhandene Code aus `liedplan-import.cpp`**
   (`parse_records`) wiederverwenden (gleiche Header-Normalisierung + `header_key` + 40-Spalten-Mapping
   + `to_import_result`). **NICHT neu schreiben** — XLSX liefert nur `rows`, den Rest macht der
   existierende Code.
5. Ergebnis `LiedplanImportResult` (gleiche Struktur wie CSV) → Dock füllt die ersten 12 Karten.

**Mini-XML-Scan:** nur die Tags/Attribute oben (String-`find`-basiert, keine DOM-Parser-Bibliothek).
Genug für den fixen Google-Forms-Schema. Robust gegen: fehlendes sharedStrings (inlineStr),
leere Zellen (kein `<c>` = leerer String, Spaltenlücken via `r`-Index), `xml:space="preserve"`.

**Referenz-Sample (vorhanden, MUST parsen):** `rewrite/tests/samples/antwort.xlsx`
- Sheet `Form Responses 1`, 2 Zeilen.
- Zeile 0 = Header: `Zeitstempel, Woche, Tag, Organist, 1. Eröffnung – GL-Nummer, 1. Eröffnung – Verse,
  1. Eröffnung – Liedtitel, 2. Kyrie – GL-Nummer, …` bis `12. Schlusslied – Liedtitel` (40 Spalten).
- Zeile 1 (Daten): `20.10.2026 09:15:32, 19.10. – 25.10.2026, Sonntag, E. Stolper, 144, 1+4+6,
  Nun jauchzt dem Herren, aller Welt, 356, , Komm, o Gott, durch den Heiligen Geist, 136, 1-3, …`.
  (Achtung: einige `Verse`/`Liedtitel` sind leer → `None` in openpyxl → leerer String hier.)
- **Erwartetes Ergebnis = EXAKT das gleiche `LiedplanImportResult`** wie `antwort_comma.csv` /
  `antwort_semicolon.csv` (gleiche 12 Punkte × 3 Felder + woche/tag/organist/zeitstempel).

## 3. ZIP-Import (ohne libzip — C++ mit Bordmitteln + zlib)
ZIP = End-Of-Central-Directory-Record (EOCD) → Central-Directory-Einträge → Local-File-Header
→ Daten (Deflate via `zlib` `uncompress`/`inflate`, oder Store). Nur **Lesen** (Extract) nötig.

**Pfad (Byte-Input `data`, Filename-Endung `.zip`):**
1. **EOCD finden** (Signatur `PK\x05\x06`), von Ende rückwärts (≤ 65557 Byte, Padding möglich).
2. **Central Directory** parsen (Einträge `PK\x01\x02`): pro Eintrag → `name` + `compression_method`
   (0=Store, 8=Deflate) + `local_header_offset` + `compressed_size`.
3. **CSV-Member wählen:** exakt wie Python-Referenz —
   `names = sort(name for name in members if name.lower().endswith(".csv") and not basename.startswith((".", "__")))`
   → erstes. Kein CSV → `ImportError::Format` ("Die ZIP-Datei enthält keine CSV-Datei.").
4. **Member lesen** (Local-File-Header `PK\x03\x04` am `local_header_offset`, Datei-Header-Skip
   per dessen `filename_len`/`extra_len`), `compressed_size` Bytes →
   - method 0 (Store): rohes Byte-String
   - method 8 (Deflate): `zlib::uncompress` (wenn `zlibstatic.lib` statisch gelinkt) ODER
     `inflate()` mit `z_stream` (wenn `zlib.lib`/`zlib.dll` dynamisch). **zlib liegt in `.deps`**
     (`obs-deps-2026-10-23-x64/lib/zlibstatic.lib` + `include/zlib.h`). Link-Option in §5.
5. → `parse_csv_string(csvText, …)` (vorhanden) → `LiedplanImportResult`.

**Mini-ZIP-Reader:** nur EOCD + Central Directory + Local Header + Store/Deflate. Kein ZIP64
(Google-Forms-CSVs sind klein, < 4 GB). Robust gegen: Padding vor EOCD, mehrere `.csv` (erstes
sortiert), Store ODER Deflate. Deflate via zlib — `zlib` ist die EINZIGE externe Lib, die
verlinkt wird (liegt schon in `.deps`, kein Vendor).

**Referenz-Sample:** kein fertiges `.zip`-Sample im Repo → **eins erzeugen** (Test-Fixtur):
`tests/samples/antwort.zip` = ZIP, das `antwort_comma.csv` (bzw. identischen Inhalt) enthält.
Test: `antwort.zip` → gleiches Ergebnis wie `antwort_comma.csv`. (agy erzeugt die Fixture per
`zip`-Befehl oder einem kleinen C++-Test-Setup, das ein Store- oder Deflate-ZIP baut; s. §6 Tests.)

## 4. Dock-UI-Extension (liedplan-dock.{hpp,cpp} + liedplan-theme.hpp)
Im vorhandenen Dock (v1-Look, Ui1/Ui2) die neuen Funktionen ergänzen. QSS **in `liedplan-theme.hpp`**
erweitern (v1-Tokens WIEDERVERWENDEN — `lpCardOuter`/`lpCardInner`/accent `#3DAEE9`), NICHT im cpp hardcoden.

**A) Import-Dialog erweitern (vorhanden aus M3):**
- Dateityp-Filter auf CSV **UND** XLSX **UND** ZIP:
  `CSV-Dateien (*.csv);;Excel-Dateien (*.xlsx);;ZIP-Archive (*.zip);;Alle Dateien (*.*)`.
- Bei Datei-Auswahl: Endung → Parser (`parse_xlsx_file`/`parse_zip_file`/`parse_csv_file` — alle
  liefern `LiedplanImportResult`). Ergebnis → erste 12 Karten live (`update_everywhere`).
- Statuszeile: bei Erfolg "Importiert: 12 Punkte (Quelle: antwort.xlsx)", bei Fehler die
  `ImportError`-Meldung (deutsch).

**B) NEU — Schnell-Profile (Voll-State-Snapsots):**
- Toolbar/Button **"⭐ Schnell-Profile"** (eigene Sektion im Dock oder Dialog).
- **Speichern:** Dialog/Inline: Name (QLineEdit, Default = z. B. "Profil N" + Datum), Button
  "Speichern" → `quick_profiles` +{ name, cards: aktuelle cards[] } → Save → List-Refresh.
- **Liste:** QComboBox/QListWidget mit allen Schnell-Profilen + je:
  - **"▶ Übernehmen"** (apply): `cards` = Snapsot-cards (live `update_everywhere`) → ganzes
    Dashboard wird zum Snapsot (v2-Semantik: apply ersetzt den kompletten State, NICHT nur Farben).
  - **"🗑 Löschen"**: Eintrag entfernen.
- Persistenz in `liedplan-state.json` → überlebt OBS-Neustart.

**C) NEU — JSON-Backup/Restore:**
- Button **"💾 Backup"** → `QFileDialog::getSaveFileName` (Filter `JSON-Backup (*.json)`) →
  schreibt den **kompletten** State (cards + active_profile + settings + quick_profiles) als
  sauberes JSON (`nlohmann::json::dump(2)`) nach Datei. Status: "Backup nach <pfad>".
- Button **"♻️ Restore"** → `QFileDialog::getOpenFileName` (Filter `JSON-Backup (*.json)`) →
  JSON laden → `cards`/`active_profile`/`settings`/`quick_profiles` setzen (fehlende Top-Level-Keys
  → Defaults, Backward-Compat) → Save + `update_everywhere` (live). Bei kaputtem JSON:
  Statuszeile "Restore fehlgeschlagen: <msg>", State unverändert.

**D) NEU — Settings (Google-Form-ID + Token-Pfad editierbar):**
- Im Import-Dialog (oder eigene Sektion) zwei QLineEdits: **Form-ID** (Default `kDefaultFormId`)
  + **Token-Pfad** (Default `kDefaultTokenPath`). In `state.settings` persistieren. Google-Import
  liest diese (statt der Hardcoded-Defaults). Bei fehlender Token-Datei: deutsche Fehlermeldung
  (Plan §6 M4 "Fehlermeldung bei fehlenden OAuth-Dateien").

## 5. CMake / Build (NUR wenn nötig)
- **zlib** für Deflate: `find_package(ZLIB)` (liegt in `.deps`; `zlibstatic.lib`/`zlib.lib` +
  `include/zlib.h`). `target_link_libraries(… PRIVATE ZLIB::ZLIB)` (oder das obs-deps-Target —
  agy prüft den korrekten CMake-Target-Namen in `.deps/…/lib/cmake/`; falls kein
  `ZLIB::ZLIB`-Config da, direkt `$<TARGET…>`/Pfad auf `zlibstatic.lib` + Include-Dir auf
  `obs-deps-2026-10-23-x64/include` linkbar machen — **aber kein Vendor-Download**).
- Neue Test-Quellen in die bestehenden `add_executable`-Blöcke hängen
  (`liedplan-import-tests`, `liedplan-state-tests`) — keine neuen Executables, es sei denn nötig.
- **Build-Befehl (exakt, s. Skill obs-textbox):**
  ```
  cmake --preset windows-x64
  cmake --build --preset windows-x64 --config RelWithDebInfo
  ```
  (CMake 3.30.5 via `/c/Users/Johann/tools/cmake-3.30.5-windows-x86_64/bin` auf PATH.)

## 6. Tests (CTest — offline, OHNE OBS, OHNE Netzwerk)
**Bestehende Suites bleiben GRÜN** (Regression): `liedplan-state`, `liedplan-textfmt`,
`liedplan-import`, `liedplan-render` (22 Checks). Erwartet nach M4: **ctest 4/4 (Suites) grün**
mit den neuen Checks drin.

**NEU in `test-liedplan-import.cpp`** (offline, auf Bytes/Pfade — kein Netzwerk):
1. **XLSX:** `parse_xlsx_file` auf `rewrite/tests/samples/antwort.xlsx` (absoluter Pfad im Test,
   via `#define`/`-D` oder relativ `../../rewrite/tests/samples/antwort.xlsx`) →
   `LiedplanImportResult` **==** dem von `antwort_comma.csv` (gleiche 12 Punkte, woche/tag/
   organist/zeitstempel). Einzel-Check: `punkte[0]` = {gl:"144", verse:"1+4+6",
   titel:"Nun jauchzt dem Herren, aller Welt"}; `punkte[4]` (Halleluja) gl:"175", titel:"Halleluja!".
2. **ZIP-Deflate:** Test baut (oder agy erzeugt Fixture) `antwort.zip` mit Deflate-komprimierter
   CSV → `parse_zip_file` → **==** `antwort_comma.csv`. (Deflate-Pfad muss geübt sein.)
3. **ZIP-Store:** ZIP mit Store-Method (unkomprimiert) → gleiches Ergebnis. (Beide Methoden,
   da §3 Store ODER Deflate behauptet.)
4. **ZIP ohne CSV:** ZIP mit nur `foo.txt` → `ImportError::Format`.
5. **XLSX leer (nur Header):** → `ImportError::NoResponses`.
6. **Falsche Endung:** `parse_answers`-Dispatch auf `.txt` → `ImportError::Format`.
7. **Shared-Strings-Entity:** kleines hand-konstruiertes XLSX-Byte-Fixtur mit `&amp;`/`&#x41;`
   in `<t>` → korrekt dekodiert (regelt den Entity-Pfad ohne echte Excel-Datei).

**NEU in `test-liedplan-state.cpp`:**
8. **Backup-Roundtrip:** State anlegen (cards + quick_profiles + settings) → serialisieren →
   deserialisieren → **==** (Tiefenvergleich, nlohmann `==`).
9. **Schnellprofil-Roundtrip:** `quick_profiles` +{ name, cards } → serialisieren → laden →
   Snapsot vorhanden + apply-Logik ersetzt `cards` korrekt (Backward-Compat: alte Datei
   OHNE `quick_profiles`/`settings` → leer/Default, kein Crash, kein `[]`-Crash).
10. **Backward-Compat:** JSON OHNE die drei neuen Top-Level-Keys → lädt sauber, neue Felder
    leer/Default, alte `cards` unverändert.

**Test-Pfad-Problem (bekannt, F2 aus T1):** Sample-Pfade im CTest — agy stellt sicher, dass
`antwort.xlsx` im Test **real gefunden** wird (absoluter Pfad via CMake `-D` oder
`target_compile_definitions` auf den absoluten Sample-Ordner; NICHT ein relativer Pfad, der
je nach CWD bricht). Test-Muster aus dem existierenden `test-liedplan-import.cpp` (M3)
wiederverwenden.

## 7. Deploy + OBS-Start (agy macht das, Hermes verifiziert)
1. Build (oben) → DLL `build_x64/RelWithDebInfo/liedplan-for-obs.dll`.
2. **OBS killen** (Nutzer-Session kann live sein — genehmigt für Deploy-Runs):
   `taskkill /IM obs64.exe /F` (Git-Bash: einfach `taskkill /IM obs64.exe /F`, KEIN `//IM`).
3. DLL → `C:\Program Files (x86)\Steam\steamapps\common\OBS Studio\obs-plugins\64bit\`
   (NUR `obs-plugins\64bit\`, NICHT `bin\64bit`).
4. **OBS starten mit korrektem CWD** (CWD-Pitfall, verifiziert 2026-10-26, obsproject#2966):
   ```
   Start-Process -FilePath 'C:\Program Files (x86)\Steam\steamapps\common\OBS Studio\bin\64bit\obs64.exe' -WorkingDirectory 'C:\Program Files (x86)\Steam\steamapps\common\OBS Studio\data\obs-studio'
   ```
5. **Log-Verifikation** (neueste Datei in `C:\Users\Johann\AppData\Roaming\obs-studio\logs\`):
   `plugin loaded successfully` + kein Plugin-Fehler. **DIALOG_OFFEN-Erkennung** (agy kann nicht
   klicken): ~45 s nach Start, wenn Prozess-RAM ≈ 40 MB ODER keine `plugin loaded`-Zeile →
   Status `DIALOG_OFFEN` melden (Hermes klickt dann per computer_use).
6. **Kein Commit** (agy macht KEIN git). Hermes committet nach eigener Verifikation.

## 8. Akzeptanzkriterien (fertig, wenn ALLE wahr)
- [ ] Build RelWithDebInfo grün (neue Libs verlinkt, keine Fehler/Warnungen-Regression).
- [ ] **ctest 4/4 selbst grün** (alte Suites + neue Checks, kein Skip).
- [ ] `antwort.xlsx` + `antwort.zip` (Deflate) + `antwort.zip` (Store) liefern **identische
      `LiedplanImportResult`** wie `antwort_comma.csv`.
- [ ] ZIP-ohne-CSV + XLSX-leer + falsche Endung → korrekte `ImportError` (kein Crash).
- [ ] Shared-String-Entity-Decoding korrekt.
- [ ] Backup-Roundtrip + Schnellprofil-Roundtrip + Backward-Compat-Tests grün.
- [ ] Dock: neue Buttons sichtbar (Schnell-Profile, Backup, Restore, Settings), QSS in
      `liedplan-theme.hpp` (v1-Tokens), deutsche Labels.
- [ ] Deploy: DLL im OBS-Verzeichnis, OBS lädt Plugin, Log ohne Fehler (oder `DIALOG_OFFEN`).
- [ ] Kein File-Umzug, kein git, keine neue externe Abhängigkeit (nur zlib aus `.deps`).

## 9. Referenzen (genau, 1:1 — NICHT neu ableiten)
- **Parser-Referenz (Python, 1:1):** `rewrite/server/forms_import.py` (BOM, Komma/Semikolon,
  quoted Fields, DE/EN-Timestamp, 40 Spalten, `header_key`, `rows_to_records`, `to_import_result`).
  Der C++-Port dafür existiert SCHON in `liedplan-import.cpp` (M3) — XLSX/ZIP liefern nur `rows`
  bzw. CSV-Text, den Rest macht der vorhandene Code.
- **State-Modell (v2):** `rewrite/dashboard/js/storage.js` (`DEFAULT_STATE`: version, autoSend,
  settings{activeColorProfile, colorProfiles, uiColors}, profiles[], boxes[]) +
  `rewrite/dashboard/js/color-profiles.js` (6 Profile, COLOR_KEYS, hexToRgba).
  **Schnell-Profile = `profiles[]`** (Voll-State-Snapsots, apply ersetzt kompletten State).
- **XLSX-Sample:** `rewrite/tests/samples/antwort.xlsx` (2 Zeilen, 40 Spalten, Sheet `Form Responses 1`).
- **Farbprofile/Theme:** `rewrite/dashboard/color-profiles.yaml` (6 Profile, 11 Farbe-Keys +
  6 Opacity-Keys) — liegt schon als `liedplan-profiles.hpp` im Plugin (M1).
- **Deploy/CWD/Obs-API:** Skill `obs-textbox` (Abschnitte "Standalone OBS Plugin" + Pitfalls).

## 10. Was agy NICHT macht (Hermes übernimmt)
- Kein `git commit`/`git add` — Hermes committet nach eigener Verifikation.
- Kein E2E per UI-Klick (agy kann nicht klicken) — Hermes macht die Dock-E2E per computer_use
  (Schnellprofil speichern/apply, Backup/Restore, XLSX/ZIP-Import per Datei-Dialog) +
  On-Disk-Verifikation (state.json + cards/*.html) + Pixel-Screenshots.
- Kein Commit der untracked `main/`/`rewrite/`/Root-Docs — die bleiben unverändert.
