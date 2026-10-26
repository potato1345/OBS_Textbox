# M3-Spec: Forms-/CSV-Import für das Liedplan-Plugin

Stand: 2026-10-26 (Orchestrator: Hermes). Ziel: Meilenstein M3 aus `hermes-memory/notes/t7-obs-plugin-plan.md` — Google-Forms-API + CSV-Import füllt die 12 Programmpunkt-Karten im Dock.

## 1. Scope (nicht verhandelbar)

**Drin:**
1. CSV-Parser (C++-Port von `rewrite/server/forms_import.py`, 1:1-Verhalten, s. §4).
2. Google-Forms-API: OAuth-Refresh (WinHTTP) + `forms.get` (Titel-Mapping) + `responses.list` (Pagination) + JSON→Record-Mapping (offline testbar).
3. Dock-UI: „📥 Import“-Button in der Toolbar + Import-Dialog (Google ODER CSV-Datei), Kopfdaten-Anzeige, Statuszeile.
4. Import füllt die **ersten 12 Karten in Listenreihenfolge** (zeile1=GL-Nummer, zeile2=Verse, zeile3=Liedtitel); bei <12 Karten werden so viele neue Karten angelegt (nächste freie ID `boxN`), bis es 12 gibt. Karten >12 bleiben unberührt. Farben/Profile bleiben. Danach `save_state()` + pro befüllter Karte `liedplan_scene::update_everywhere(*c, false)` (bestehende Browser-Sources live aktualisieren — **kein** automisches Anlegen in der Szene).
5. Neue CTest-Suite `liedplan-import` (OBS-frei) mit den portierten Python-Testfällen.

**Nicht drin (→ M4):** XLSX-Import, ZIP-Import, Settings-Dialog für Pfade, AI-Import.

## 2. Repo-Kontext (Fakten, nicht suchen)

- Repo: `C:\Users\Johann\Desktop\GitHub\OBS_Textbox`, Plugin: `plugin/` (Qt6, obs-plugintemplate, OBS 32.2.2).
- Referenz-Parser (1:1-Portierung, Datei lesen): `rewrite/server/forms_import.py` — insbesondere `normalize_header`, `header_key`, `parse_csv_bytes`, `rows_to_records`, `record_to_service`, `response_to_record`.
- Python-Tests mit Erwartungswerten (portieren): `rewrite/tests/test_forms_import.py`.
- CSV-Samples (Testpfade, existieren): `C:/Users/Johann/Desktop/GitHub/OBS_Textbox/rewrite/tests/samples/antwort_comma.csv`, `antwort_semicolon.csv`, `antwort_bom.csv`, `antwort_timestamp_en.csv`. (Das Render-Test-File nutzt dasselbe Pattern: harter absoluter Repo-Pfad — `plugin/tests/render/test-liedplan-render.cpp` Zeile ~266.)
- State-Modell: `plugin/src/liedplan-state.hpp` — `LiedplanCard {id, zeile1, zeile2, zeile3, …}`; Dock: `plugin/src/liedplan-dock.{hpp,cpp}` (Toolbar-Buttons Zeilen ~68–71: „＋ Neue Karte“ `add_btn`, „🗑 Alle löschen“ `clear_btn`).
- Scene-API: `plugin/src/liedplan-scene.hpp` — `send_to_current_scene`, `update_everywhere(card, clear)`, `remove_everywhere`.
- QSS: ALLE Dock-Styles leben in `plugin/src/liedplan-theme.hpp` (v1-Tokens #1C1F26 / #272A33 / #3C404D / Akzent #3DAEE9). Dialog-Styles dort **ergänzen** (kein Hardcoding in cpp, keine Neu-Styles aus control.css).
- nlohmann_json: bereits `find_package(nlohmann_json REQUIRED)` + `nlohmann_json::nlohmann_json` (CMakeLists). WinHTTP: Windows-SDK-Header `winhttp.h`, Link-Library `winhttp`.
- Google-Auth (Pfade, KEINE Werte im Code/Repo):
  - Token-Datei (PRIMARY, enthält ALLES Notwendige): `C:\Users\Johann\AppData\Local\hermes\google_token.json` — JSON mit Feldern `refresh_token`, `client_id`, `client_secret`, `token_uri` (default `https://oauth2.googleapis.com/token`). **NUR LESEN, NIEMALS BESCHREIBEN.**
  - Fallback für `client_id`/`client_secret`, falls in der Token-Datei fehlend: `C:\Users\Johann\AppData\Local\hermes\google_client_secret.json` (JSON `installed.{client_id,client_secret}`, auch nur lesen).
  - Access-Token-Cache (PLUGIN-eigene Datei, schreiben erlaubt): `<plugin-data-dir>/google_access_token.json` mit `{"token": "…", "expiry_unix": <epoch>}` — nur wiederverwenden, wenn `expiry_unix - 60 > now()`.
  - Formular-ID (Konstante, kein Secret): `1jreAuzd-jt1x48WDmjaP7v0OusdVO2lb55Kt4JrhEgY`.
- Fehlerfälle mit klaren deutschen Meldungen (kein Crash, keine leere UI): Token-Datei fehlt, API-Fehler-Status, „keine Antworten im Formular“, „unbekanntes Dateiformat“ (keine Programmpunkt-Spalten), „keine Antworten (nur Kopfzeile)“, Netzwerkfehler/Timeout.

## 3. Neue/änderte Dateien (erlaubte Liste — NICHTS anderes anfasst!)

| Datei | Aktion |
|---|---|
| `plugin/src/liedplan-import.hpp` | NEU |
| `plugin/src/liedplan-import.cpp` | NEU |
| `plugin/src/tests/test-liedplan-import.cpp` | NEU |
| `plugin/CMakeLists.txt` | ÄNDERN (Main-Target + Test-Target) |
| `plugin/src/liedplan-dock.hpp` | ÄNDERN |
| `plugin/src/liedplan-dock.cpp` | ÄNDERN |
| `plugin/src/liedplan-theme.hpp` | ÄNDERN (Dialog-QSS ergänzen) |
| `plugin/M3-spec.md` | NEU (diese Datei) |

**Anti-Umzugs-Klausel (verbindlich):** Keine Datei außerhalb der Liste anlegen, umbenennen, verschieben oder löschen. KEINE git-Befehle (kein Stage/Commit/Restore). `main/`, `rewrite/`, `hermes-memory` bleiben unangetastet.

## 4. CSV-Parser — exaktes Verhalten (Port von forms_import.py, nicht neu erfinden)

Namespace `liedplan_import` (OBS-frei, C++17, nur `<string>`, `<vector>`, `<filesystem>`, `<nlohmann/json.hpp>`, auf Windows `<winhttp.h>`).

```
struct LiedplanPoint { std::string gl, verse, titel; };
struct LiedplanImportResult {
  std::string woche, tag, organist, zeitstempel;
  std::vector<LiedplanPoint> punkte;   // exakt 12 Einträge (fehlende leer)
};
enum class ImportError { None, Format, NoResponses, File };
```

- `normalize_header(std::string_view)`: BOM (U+FEFF) entfernen, Unicode-Striche U+2012/U+2013/U+2014/U+2015/U+2212 → „-“, Whitespace-Ketten → ein Leerzeichen, trimmen. (UTF-8-Input, multibyte-korrekt behandeln.)
- `header_key(normalisiert)`: (1) lowercased Simple-Keys: `zeitstempel|timestamp→"zeitstempel"`, `woche→"woche"`, `tag→"tag"`, `organist→"organist"`. (2) Punkt-Regex (analog Python `^(\d{1,2})\.\s*(.+?)\s+-\s+(.+)$`, Strich ist hier immer ASCII „-“): Index 1..12, Name muss casefold-gleich `PROGRAM_POINTS[index-1]` sein (Liste 1:1 aus forms_import.py: Eröffnung, Kyrie, Gloria, Zwischengesang, Halleluja, Credo, Gabenbereitung, Heilig, Agnus Dei, Kommunion, Danklied, Schlusslied), Feld: `GL-Nummer→gl`, `Verse→verse`, `Liedtitel→titel` (case-insensitiv) → Key `"{index}.{gl|verse|titel}"`. Sonst `nullopt`.
- `parse_csv(const std::string& text) -> std::vector<std::vector<std::string>>` + Fehler: UTF-8 (BOM optional); Trennzeichen-Erkennung auf der ERSTEN Zeile: `count(';') > count(',')` → „;“ sonst „,“ (Roh-Zeichen zählen, wie Python). CSV-Escapes: Zitat öffnet/schließt Feld, `""` = Literal-Zitat, Trennzeichen/Zeilenende in Zitaten = Literal. Zitat wirkt nur zu Feldbeginn (nach Trenner/Zeilenanfang); Zitat in der Mitte eines unquoteden Felds = Literal (Python-csv-Verhalten). Zeile = `\r\n` | `\n` | `\r`.
- `parse_records(header, rows)`: Spalten → Key-Map (erster Treffer gewinnt, wie Python `if key not in columns`); **Fehler `Format`** wenn kein Key mit Präfix „1.“ existiert (Meldung: „Unbekanntes Dateiformat: Es wurden keine Programmpunkt-Spalten des Liedplan-Formulars gefunden (erwartet z. B. „1. Eröffnung – GL-Nummer“).“); komplett leere Zeilen überspringen; **Fehler `NoResponses`** wenn keine Datenzeile bleibt. Werte: `strip()`.
- `parse_csv_bytes`-Äquivalent: `parse_csv_file(path)` → File-Fehler bei Nicht-lesbar.
- `to_import_result(records) -> LiedplanImportResult`: Kopfdaten + 12 Punkte (fehlende Keys = leer). `latest()` = letztes Record (Google sortiert chronologisch) — bei API: letztes nach `lastSubmittedTime`-Sort (s. §5).
- API-JSON-Mapping (offline, testbar): `api_response_to_record(const nlohmann::json& response, const nlohmann::json& title_map) -> std::map<std::string,std::string>` — 1:1 wie Python `response_to_record`: `zeitstempel = lastSubmittedTime || createTime`; für jedes `answers[questionId]`: `key = header_key(title_map[questionId])`, Werte aus `textAnswers.answers[].value` (nicht-leere), joined mit `", "`.

## 5. Google-API (WinHTTP, Worker-Thread)

Funktionsbau (alles in `liedplan-import.{hpp,cpp}`, WinHTTP-Teile kompilieren auch im Test-Target, werden dort aber NICHT aufgerufen):
1. `get_access_token(token_path)`: Token-Datei lesen (nlohmann), `refresh_token`+`client_id`+`client_secret` (+`token_uri`) extrahieren (Fallback-Datei s. §2); POST `token_uri` (Content-Type `application/x-www-form-urlencoded`, Body `grant_type=refresh_token&refresh_token=…&client_id=…&client_secret=…`, URL-encoded) → `access_token`. Cache s. §2.
2. `fetch_titles(form_id, token)`: GET `https://forms.googleapis.com/v1/forms/{form_id}` (Header `Authorization: Bearer …`) → `items[]` mit `questionItem.question.questionId` → `title_map`.
3. `fetch_responses(form_id, token)`: GET `https://forms.googleapis.com/v1/forms/{form_id}/responses?pageSize=100[&pageToken=…]`, loop bis `nextPageToken` leer → `responses[]`.
4. `import_from_api() -> std::optional<LiedplanImportResult> + Fehlermeldung`: 1–3, `responses` nach `lastSubmittedTime`/`createTime` absteigend sortieren, NEUESTES → `api_response_to_record` → `to_import_result`. Leer → Fehler „Im Google-Formular liegen noch keine Antworten vor.“
5. WinHTTP: `WinHttpOpen`/`WinHttpConnect`/`WinHttpOpenRequest`/`WinHttpSendRequest`/`WinHttpReceiveResponse`, TLS (SERVER_CERT_FLAG_DEFAULT… = `WINHTTP_FLAG_SECURE`), Timeouts: Resolve 10 s, Connect 10 s, Send 10 s, Receive 30 s. **Niemals im UI-Thread** — Dock-Button startet `std::thread`, Ergebnis via `QMetaObject::invokeMethod`/Signal auf den Dock zurück. Button während Lauf deaktiviert + Status „Lade …“.
6. HTTP-Fehler → deutsche Meldung mit Status-Code (z. B. „Google-API-Fehler 401 — Token abgelaufen oder ungültig“).

## 6. Dock-UI (v1-Look, Tokens aus liedplan-theme.hpp)

- Toolbar: neuer Button `QPushButton("📥 Import")`, objectName `lpBtnImport`, **nach** „🗑 Alle löschen“ (Zeilen ~68–71 in liedplan-dock.cpp).
- Dialog `QDialog` (objectName `lpImportDialog`), Titel „Liedplan-Import“, Breite ~480 px:
  - Gruppe „Von Google holen“: Label „Neueste Antwort aus dem Google-Formular laden“ + Button „📥 Neueste Antwort laden“ (objectName `lpBtnApiImport`) + Status-Label (objectName `lpImportStatus`).
  - Gruppe „CSV-Datei“: Button „📂 CSV-Datei importieren …“ (objectName `lpBtnCsvImport`) → `QFileDialog::getOpenFileName` (Filter „CSV-Dateien (*.csv)“).
  - Kopfdaten-Gruppe (erst nach erfolgreichem Laden sichtbar): 4 Label-Zeilen `Woche: … / Tag: … / Organist: … / Zeitstempel: …` (objectNames `lpKopfWochen` u. a.).
  - Ergebnis-/Fehler-Label unterhalb (Fehler rot via QSS, objectName `lpImportError`).
- Erfolgsverhalten (beide Wege identisch): s. §1.4 — Karten befüllen, `save_state()`, `update_everywhere` pro Karte, Dialog zeigt „12 Karten befüllt — im Dock editierbar, „▶ Zu Szene“ legt sie in die Szene.“, Karten-Widgets im Dock neu rendern (bestehender Rebuild-Mechanismus des Docks nutzen).
- Fehlerverhalten: Fehler-Label befüllen, Karten bleiben unverändert, Status-Label mit Meldung.
- QSS für `lpImportDialog`/Groups/Status/Errors in `liedplan-theme.hpp` ergänzen (v1-Tokens, Akzent #3DAEE9, Fehler #E85D5D).

## 7. CMake

- `liedplan-import.cpp/hpp` in den Main-Target `target_sources` (neben liedplan-html & friends) + `target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE winhttp)`.
- Neues Test-Target (OBS-frei, ohne Qt, wie `liedplan-tests`):
  ```cmake
  add_executable(liedplan-import-tests
    src/tests/test-liedplan-import.cpp
    src/liedplan-import.cpp
    src/liedplan-import.hpp
  )
  target_link_libraries(liedplan-import-tests PRIVATE nlohmann_json::nlohmann_json winhttp)
  add_test(NAME liedplan-import COMMAND liedplan-import-tests)
  ```

## 8. Tests (test-liedplan-import.cpp — 1:1-Port der Python-Fälle + Extras)

Test-Harness: wie `test-liedplan-state.cpp` (einfache `check(bool, name)`-Zählung, Exit-Code). Pfad-Konstante `const char* kSamplesDir = "C:/Users/Johann/Desktop/GitHub/OBS_Textbox/rewrite/tests/samples/";`.

Pflicht-Checks (Erwartungswerte EXAKT aus `test_forms_import.py`):
1. `normalize_header("  1.  Eröffnung   –   GL-Nummer ")` == `"1. Eröffnung - GL-Nummer"`
2. `normalize_header("Woche\u2013Tag")` == `"Woche-Tag"`
3. `header_key`: „Zeitstempel“→zeitstempel, „Timestamp“→zeitstempel, „Woche“→woche, „Tag“→tag, „Organist“→organist, „1. Eröffnung - GL-Nummer“→1.gl, „1. Eröffnung – Verse“→1.verse, „1. Eröffnung - Liedtitel“→1.titel, „12. Schlusslied – Liedtitel“→12.titel, „Unbekannte Spalte“→nullopt
4. `antwort_comma.csv`: 1 Antwort; tag==„Sonntag“, organist==„E. Stolper“, 12 Punkte; P1: gl==„144“, verse==„1+4+6“, titel==„Nun jauchzt dem Herren, aller Welt“ (**Komma IM quoteden Feld — Regressionstest**)
5. `antwort_semicolon.csv`: P1.gl==„144“
6. `antwort_bom.csv`: P1.gl==„144“
7. `antwort_timestamp_en.csv`: zeitstempel==„20.10.2026 09:15:32“
8. Extra: Inline-CSV mit `""`-Escapes (`"He""llo"` → `He"llo`) und leeren Zeilen zwischen Daten → korrekt geparst, leere Zeilen übersprungen
9. Extra: Header ohne Punkt-Spalten → Fehler `Format`
10. Extra: nur Kopfzeile → Fehler `NoResponses`
11. API-Fixture (offline): `answers` mit q_woche/q_tag/q_organist/q_p1_gl/q_p1_verse/q_p1_titel/q_unbekannt + title_map (Titel exakt wie echte Formular-Titel, z. B. „1. Eröffnung – GL-Nummer“) → Record enthält `zeitstempel` (aus `lastSubmittedTime`), `woche/tag/organist`, `1.gl`==„144“, `1.verse`==„1+4+6“, `1.titel`, **kein** Key für q_unbekannt
12. API-Multi: zwei `textAnswers.answers[]`-Werte → joined „a, b“

**Alte Tests bleiben grün:** `ctest` am Ende = **4/4** (liedplan-state, liedplan-textfmt, liedplan-render + neues liedplan-import).

## 9. Build + Test + Deploy (exakte Befehle)

```bat
:: Build (aus beliebigem Verzeichnis):
set PATH=C:\Users\Johann\tools\cmake-3.30.5-windows-x86_64\bin;%PATH%
cd C:\Users\Johann\Desktop\GitHub\OBS_Textbox\plugin
cmake --build --preset windows-x64 --config RelWithDebInfo

:: Tests:
ctest --test-dir build_x64 -C RelWithDebInfo --output-on-failure
```
- **Deploy (nach grünem Build):** OBS läuft (Nutzer-Session!) → `taskkill /IM obs64.exe /F` (einfacher Slash, nicht //IM) → DLL `build_x64\RelWithDebInfo\liedplan-for-obs.dll` nach `C:\Program Files (x86)\Steam\steamapps\common\OBS Studio\obs-plugins\64bit\` kopieren (byte-identisch; mtime verifizieren) → OBS per PowerShell starten: `Start-Process -FilePath 'C:\Program Files (x86)\Steam\steamapps\common\OBS Studio\bin\64bit\obs64.exe' -WorkingDirectory 'C:\Program Files (x86)\Steam\steamapps\common\OBS Studio\data\obs-studio'`.
- **⚠️ WorkingDirectory-Pitfall (26.10 verifiziert):** OBS 32.2.2 löst `locale/en-US.ini` relativ zur CURRENT WORKING DIRECTORY auf (obsproject/obs-studio#2966, "not supported to run from outside the CWD"). Im Steam-Layout liegt `locale/` NUR unter `data\obs-studio\` — weder am Install-Root noch in `bin\64bit\`. Falscher CWD → Dialog „Failed to find locale/en-US.ini" + 1-Zeilen-Log + RAM ≈ 40–45 MB. **Immer `data\obs-studio` als WorkingDirectory verwenden.**
- **Log-Verifikation (nach ~45 s):** neueste Datei in `C:\Users\Johann\AppData\Roaming\obs-studio\logs\` muss enthalten: `plugin loaded successfully` + `Liedplan dock registered (ok=1)`; obs64-Prozess-RAM ≈ 430–500 MB. **Wenn RAM ≈ 40 MB oder Plugin-Zeilen fehlen → Dialog offen** (agy kann ihn NICHT klicken): entweder Locale-Fehler (falscher CWD, s. oben) oder Absturz-Dialog „OBS Studio Absturz erkannt" → Buttons „Im abgesicherten/normalen Modus starten" (agy/Hermes klickt „Im normalen Modus starten") → Status `DIALOG_OFFEN` im Report melden.

## 10. Done-Kriterien (Report muss ALLES enthalten)

1. Build-RelWithDebInfo grün (letzte Zeilen).
2. `ctest` 4/4 grün (Output).
3. Geänderte/neue Dateien mit Zeilenzahlen (git status/diff --stat).
4. Deploy: DLL-mtime + `fc`/Hash-Check Build vs. Installat.
5. OBS-Neustart-Log-Auszug (Plugin-Zeilen) oder `DIALOG_OFFEN`.
6. Selbst-Review: 5 Achsen (Korrektheit/Security/Einfachheit/Performance/Tests) in 1–2 Zeilen je Achse.

## 11. Stil
C++17, 4-Space- oder Tab-Indent wie die bestehenden `liedplan-*.cpp` (Tabs), deutsche Log-/UI-Strings, GPL-2+-Header wie die bestehenden Dateien, keine neuen Abhängigkeiten.
