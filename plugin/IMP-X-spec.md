# IMP-X — Datenarchiv-Dialog: Google+Kopfdaten zusammen, Kirche-Strings raus, Master-Prompt sichtbar, Kopfdaten-Felder editierbar, Event-Log

Repo: OBS_Textbox. Scope NUR `plugin/` (v3). CRLF in Dateien beachten.

## Anti-Umzugs-Klausel
Keine Dateien verschieben/umbenennen. Root-Dateien, `main/`, `rewrite/` NICHT antasten.
Internbezeichner, DLL-Name, State-JSON-Keys, Dateipfade bleiben (`liedplan-*`,
`plugin_config/liedplan-for-obs`, Settings-Keys) — nur SICHTBARE Texte und Defaults werden generalisiert.

## Erlaubte Dateien
- `plugin/src/liedplan-dock.cpp`
- `plugin/src/liedplan-import.hpp` / `.cpp`
- `plugin/src/liedplan-state.hpp` / `.cpp`
- `plugin/src/liedplan-ai.hpp` / `.cpp`
- `plugin/src/liedplan-scene.cpp`
- `plugin/src/plugin-main.cpp`
- NEU: `plugin/src/liedplan-events.hpp` (header-only, Qt-/libobs-frei, nur std)
- `plugin/src/tests/test-liedplan-import.cpp`, `test-liedplan-state.cpp`
- Doku: `plugin/IMP-X-spec.md` (git add -f), `FUNKTIONSVERGLEICH.md`

## X1 — Dialog合并: Google-Import + Kopfdaten in EINER Gruppe
In `on_open_import_dialog()`:
- Fenstertitel: `Datenarchiv` statt `Liedplan-Import`.
- Gruppe 1 heißt „Daten von Google“ und ENTHÄLT ab jetzt auch die Kopfdaten-Zeilen:
  Info-Label („Antworten aus einem Google-Formular laden“), FORM-ID, TOKEN-PFAD,
  Button „Daten von Google holen“ (statt „Neueste Antwort laden“, objectName `lpBtnApiImport` bleibt),
  Status-Label, darunter die Auswahl+Übernahme-Zeile (war Gruppe 4 „Kopfdaten“; Groupbox-Header entfällt,
  Inhalt rückt in Gruppe 1, weiterhin erst nach erfolgreichem Laden sichtbar).
- Gruppen 2 (Datei-Import) und 3 (AI-Import) bleiben; Reihenfolge: Google, Datei, AI.

## X2 — Kirche-Hartkodierung aus allen SICHTBAREN Strings/Defaults entfernen
Sichtbare Texte (Deutsch) generalisieren — „Gottesdienst“, „Liedplan“, „Programmpunkt(e)“,
„Lieder“, „Gotteslob“, „Kirche“, „liturgisch“ dürfen in UI-/Status-/Fehler-/Dialogtexten nicht mehr vorkommen:
- `plugin-main.cpp`: Dock-Titel `Textbox` statt `Liedplan`; Menüeintrag `Textbox-Dock anzeigen`;
  Logzeile entsprechend (Präfix `[liedplan]` bleibt ok — interner Marker).
- `liedplan-scene.cpp`: `source_name()` liefert `Textbox <id>` statt `Liedplan <id>`.
  Kompatibilität: Quellen-Suche matcht beide Präfixe (`Liedplan <id>` UND `Textbox <id>`); beim
  Self-heal/Migration bestehenden Quellen mit Präfix `Liedplan ` per `obs_source_set_name` auf
  `Textbox <id>` umbenennen; `remove_everywhere`/existence-checks finden beide Namen.
- Dialog-Strings: Titel `Datenarchiv`; Datei-Picker-Titel `Datei importieren` / `Dokument auswählen`
  (kein „Liedplan-…“); AI-Preview „Programmvorschau“ -> „Vorschau“.
- Status-/Ergebnistexte: `%1 Gottesdienste gefunden` -> `%1 Einreichungen gefunden`;
  „Gottesdienst übernommen: Woche …“ -> `Übernommen: <Felder>` (siehe X4);
  Combo-Zeilen und Preview-Zeilen nutzen die Felder aus X4 (kein festes Woche/Tag/Org);
  `%4 Lieder` -> `%4 Einträge`; Blog-Zeile `Import übernommen: %zu Einträge statt Programmpunkte`.
- Fehlermeldungen `liedplan-import.cpp`: „keine Programmpunkt-Spalten des Liedplan-Formulars
  gefunden (erwartet z. B. „1. Eröffnung – GL-Nummer“)“ ->
  „Unbekanntes Dateiformat: keine Datenspalten gefunden (erwartet Tabellen-Columns mit Antworten).“
  AI-Fehler: „… konnte der lokale Parser Programmpunkte erkennen“ -> „… Einträge erkennen“.
- `liedplan-import.hpp`: `kDefaultFormId` und den Fixed-Default in `LiedplanSettings.google_form_id`
  ENTFERNE**n (leer). UI: Placeholder „Google-Form-ID“; leer im State -> Button zeigt Status
  „Bitte zuerst eine FORM-ID eintragen“ (kein Fetch). Persistierte Werte bleiben (Johann hat die ID im State).
  Das feste 12×3-Punkt-Schema (`PROGRAM_POINTS`, header_key-Mapping) bleibt als Daten-Mapping erhalten
  (Import muss weiter funktionieren), aber kein Kommentar/UI nennt es kirchlich — Kommentare neutralisieren.
- `liedplan-ai.cpp`: WinHTTP User-Agent `OBS-Textbox/1.0` (auch in import.cpp), User-Text-Präfix
  `Daten:` statt `Liedplan-Text:`. `kDefaultSystemPrompt` NEU, neutral (JSON-Contract zeile1/2/3 bleibt EXAKT gleich):
  „Du bist ein Parser für strukturierte Dokumente. Du erhältst Dokumenttext und erzeugst daraus die
  Datenzeilen in Dokumentreihenfolge. Regeln: ignoriere Kopfzeilen/Meta (Titel, Datum, Personen).
  Pro Eintrag genau drei Felder: zeile1 = Bezeichnung, zeile2 = Nummer/Kürzel (z. B. „144, 1+4+6“), sonst leer,
  zeile3 = Titel/Name, sonst leer. Zerlege Bezeichnung/Nummer/Titel auch, wenn sie auf einer Zeile stehen.
  Gib AUSSCHLIESSLICH gültiges JSON zurück, ohne Markdown: {"items":[{"zeile1":"...","zeile2":"...","zeile3":"..."}]}“
  (wörtlich so, Umlaute ok — UTF-8).
- Settings-Dialog: Beschriftungen ohne Gotteslob/Liedplan (z. B. „REFERENZ-INDEX“ statt Gotteslob-Index).

## X3 — Master-Prompt bearbeiten im Importdialog
- AI-Gruppe: Groupbox „System-Prompt“ -> „Master-Prompt“, **default AUSGEKLAPPT** (`setChecked(true)`).
- Editieren persistiert wie jetzt (`settings.ai_system_prompt`, Reset-Button bleibt).
- Der bearbeitete Prompt wird im AI-Startpfad verwendet (ist schon so — sicherstellen).

## X4 — Kopfdaten: anzuzeigende Felder editierbar (Edit-Knopf)
Datenmodell (`liedplan-import.hpp/.cpp`):
- `LiedplanService` bekommt `std::vector<std::pair<std::string,std::string>> fields;`
  (Feldname -> Wert, Quellreihenfolge, nur NICHT-leere Spalten; Punkt-Spalten der 12×3-Matrix sind
  NICHT enthalten). `operator==` erweitern.
- CSV/XLSX/ZIP-Pfad: `parse_records` sammelt pro Row zusätzlich die ORIGINAL-Header-Namen (display) + Werte;
  API-Pfad: `api_response_to_record` liefert die Frage-Titel als Feldnamen. Recognized-Kopf-Spalten
  (Timestamp/Woche/Tag/Organist/Datum/Uhrzeit etc.) bleiben als separate keys erhalten (woche/tag/organist/zeitstempel),
  erscheinen aber zusätzlich in `fields`.
- `LiedplanImportResult.service`/`services` transportieren `fields` durch alle Pfade.
Settings (`liedplan-state.hpp/.cpp`):
- `LiedplanSettings.kopf_fields` = `std::vector<std::string>` (ausgewählte Anzeigefelder; leer = alle belegten
  Kopf-Felder anzeigen). JSON-Serialisierung + `operator==` ergänzen.
UI (`liedplan-dock.cpp` Datenarchiv):
- Kopfdaten-Zeile zeigt pro ausgewähltem Feld ein Label `NAME: wert` (dynamisch, Container-Widget mit QLabels).
- ✎-Button `lpBtnKopfEdit` (rename.svg) neben der Kopfdaten-Zeile: öffnet QDialog
  `lpKopfEditDialog` mit CheckBox-Liste ALLER Feldnamen der geladenen Einreichungen
  (Union über services), vorausgewählt = aktuelle Auswahl; OK speichert `settings.kopf_fields`
  (+ save_state) und aktualisiert Labels, Combo-Zeilen und Preview sofort; Abbruch ohne Änderung.
- Combo-Itemtext = ausgewählte Felder joined „ · “ als `name: wert`; Preview „• joined · N Einträge“.
- „In Karten übernehmen“-Status: `Übernommen: joined` ; zeitstempel ist ein Feld wie alle anderen.
- Die vier festen Labels Woche/Tag/Organist/Zeitstempel entfallen (ersetzt durch dynamische Feld-Labels).

## X5 — Ein Nachrichtenfenster + Log für alles
NEU `liedplan-events.hpp` (header-only, std only, Qt-/libobs-frei; in Tests nutzbar):
- `lp_event_log(const std::filesystem::path &log_dir, const std::string &msg)`:
  appendet `2026-11-10T12:34:56Z | msg` (UTC-ISO) an `<log_dir>/events.log`; bei >1 MB
  auf die letzten ~400 Zeilen trimmen; Fehler (Schreibversagen) werden ignoriert.
- Dock nutzt `log_dir` = Ordner der State-Datei (`plugin_config/liedplan-for-obs/events.log`).
Call-sites (Dock): Datenarchiv geöffnet/geschlossen; Einstellungen geöffnet; Google-Fetch Erfolg/Fehler;
Datei-Import Erfolg/Fehler (mit Dateiname); AI Start/Fehler/Übernehmen; Einreichung übernommen;
Kopfdaten-Felder gespeichert; Backup erstellt/fehlgeschlagen; Restore erfolgreich/fehlgeschlagen;
YAML Export/Import; Profil speichern/umbenennen/löschen; Vorlage angewendet; Undo; Alle löschen;
Form-ID/Token-Pfad geändert; Profil-Nachsaat; alle bestehenden `set_dock_status(...)`-Aufrufe
bekommen zusätzlich einen `lp_event_log`-Eintrag mit derselben Meldung.
Regel: Jede dieser Aktionen zeigt zusätzlich KURZ den Status im Dock-Statusbar unten
(`set_dock_status` — besteht, schließbar + Auto-dismiss; Dialog-Open-Zeilen mit is_error=false).
Fehler in Worker-Threads: wie bisher via QMetaObject invoke auf UI-Thread, dort set_dock_status + event log.

## Tests (ctest-Suiten bleiben 8/8, Checks werden ergänzt)
- `test-liedplan-import.cpp`: fields aus CSV-Header (generic Spalten + Kirchschema: Punkt-Spalten excluded,
  Kopf-Spalten inkl. Originalnamen), API-response fields, fields im Result durch alle parse-Pfade (XLSX/ZIP).
- `test-liedplan-state.cpp`: `kopf_fields` JSON-Roundtrip + Backward-Compat (fehlender Key = leer),
  operator== enthält kopf_fields.
- `lp_event_log` gegen Temp-Verzeichnis: Zeile mit UTC-Zeitstamp geschrieben; Append; Trim >1 MB.
- Bestehende Tests anpassen wo Feld-Keys/Schemas sich ändern; keine Suite entfernen.

## Bau, Deploy, Abschluss
1. Build: Arbeitsverzeichnis `plugin/`, `cmake --build --preset windows-x64 --config RelWithDebInfo`.
2. `ctest --test-dir build_x64 -C RelWithDebInfo` -> 8/8 Suiten grün (alte Checks unverändert grün + neue Checks).
3. Deploy (OBS läuft aktuell NICHT — prüfe trotzdem per `tasklist | grep -i obs64`; läuft OBS, Status
   `DEPLOY_SKIPPED_OBS_RUNNING` und keine Restart-Aktion):
   - Kopiere `plugin/build_x64/RelWithDebInfo/liedplan-for-obs.dll` nach
     `C:/Program Files (x86)/Steam/steamapps/common/OBS Studio/obs-plugins/64bit/liedplan-for-obs.dll`.
   - Kopiere Card-Assets falls geändert (normal nicht — Karten-HTML bleibt unverändert).
   - Starte OBS: `"C:/Program Files (x86)/Steam/steamapps/common/OBS Studio/obs64.exe"` als
     Hintergrundprozess mit WorkingDirectory `C:/Program Files (x86)/Steam/steamapps/common/OBS Studio`.
   - Nach ~25 s: `tasklist` zeigt obs64 alive; es existiert
     `C:/Users/Johann/AppData/Roaming/obs-studio/plugin_config/liedplan-for-obs/events.log`
     (Dock registriert sich -> erste Zeile „Dock geöffnet/registriert“ o.ä.).
4. Keine Git-Operationen, keine Repo-Umzüge, keine neuen Secrets; keine Emoji in UI (Yami-SVG-Icons nur).
5. Report am Ende: geänderte Dateien, Testzahlen (alt+neu), Deploy-Status, events.log-Pfad.
