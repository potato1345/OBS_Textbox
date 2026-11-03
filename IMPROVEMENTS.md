# OBS_Textbox — Audit & Priorisierte Verbesserungsliste (`IMPROVEMENTS.md`)

**Stand:** 2026-11-03 · **Umfang:** Read-only Gesamtaudit über `main/` (v1), `rewrite/` (v2), `plugin/` (v3, Stand M7h) sowie Root-Dokumentation (`README.md`, `FUNKTIONSVERGLEICH.md`, `CODE_TODO.md`).

## Übersicht nach Analyse-Achsen

| Achse | Abgedeckte Kandidaten |
|---|---|
| **1. Robustheit** (Fehlerpfade, Threading, I/O, Cross-Platform) | #2, #3, #4, #5, #6, #12, #15 |
| **2. UX für Johann — Gottesdienst-Praxis** (Wochen-Auswahl, Vorschau, Undo, Flüssigkeit) | #1, #3, #8, #9, #10, #11, #14 |
| **3. Code-Qualität** (Schema-Konsistenz, Duplikate, toter Code, Tests) | #7, #13, #14, #15, #17 |
| **4. Sicherheit** (Zip-Bomb/DoS, XSS, Command-Quoting, Pfad-/LAN-Schutz) | #2, #9, #12, #16 |
| **5. Doku-Konsistenz** (`FUNKTIONSVERGLEICH.md`, `README.md`, `CODE_TODO.md`) | #18 |

---

## Priorisierte Kandidaten (P1 → P2 → P3)

### #1 · [P1] Auswahl mehrerer Gottesdienste pro Woche + Vorschau im Plugin-Import
- **Priorität:** `P1`
- **Betroffene Version(en):** `v3 (plugin/)`, `v1 (main/)`
- **Datei:Zeile:** `plugin/src/liedplan-import.cpp:600-605`, `plugin/src/liedplan-import.cpp:1838-1846`, `plugin/src/liedplan-dock.cpp:1784-1885`, `main/forms_import.py:204-207`, `main/server.py:124-131`
- **Problem:**
  Reichen Organisten für eine Woche mehrere Gottesdienste ein (z. B. Vorabendmesse Samstag, Hochamt Sonntag, Werktagsmesse), verwirft das Plugin in `to_import_result()` (`records.back()`) bzw. `import_from_api()` (`responses->front()`) alle übrigen Einträge und überschreibt sofort die 12 Karten ohne Vorschau.
  Johann kann dadurch im nativen OBS-Dock (wie auch in `v1`) keinen gezielten Gottesdienst auswählen, obwohl `v2` (`rewrite/server/http_app.py:178-198`, `rewrite/dashboard/js/modals.js:709-734`) bereits alle Einreichungen samt Auswahl-Dropdown und Vorschau-Tabelle bereitstellt.
- **Verbesserung:**
  `ImportResult` in `plugin/src/liedplan-import.hpp:69-82` um `std::vector<LiedplanService> services` (neueste zuerst sortiert) erweitern, sodass `parse_answers_csv/xlsx/zip` und `import_from_api` sämtliche gefundenen Gottesdienste zurückgeben. Im Tab „📋 Google Forms & Datei" (`plugin/src/liedplan-dock.cpp:1249-1350`) eine `QComboBox` (`"<Zeitstempel> · Woche <woche> · <tag> · Org: <organist>"`) sowie eine kompakte Vorschau-Liste mit explizitem Button „In Karten übernehmen" ergänzen (analog zum bestehenden AI-Tab).
- **Risiko der Umsetzung:** `low`
- **Geschätzter Aufwand:** ~2,5 h

---

### #2 · [P1] Zip-Bomb-Schutz (Decompress-Cap) im XLSX/ZIP-Parser & Upload-Größenlimit in v1
- **Priorität:** `P1`
- **Betroffene Version(en):** `v3 (plugin/)`, `v2 (rewrite/)`, `v1 (main/)`
- **Datei:Zeile:** `plugin/src/liedplan-import.cpp:763-793`, `plugin/src/liedplan-import.cpp:801-834`, `main/server.py:111-122`, `main/server.py:160-175`, `rewrite/server/forms_import.py:140-156`
- **Problem:**
  `inflate_data()` im C++-Plugin (`liedplan-import.cpp:763-793`) dekomprimiert ZIP/XLSX-Einträge ohne Obergrenze in den Arbeitsspeicher (`out.append(buf, have)` in unbegrenzter Schleife; `uncompressed_hint < 64 MB` steuert nur `reserve()`, siehe `CODE_TODO.md` M4-K1).
  Ebenso liest `main/server.py:112,161` `self.rfile.read(content_length)` ohne Größenprüfung auf `0.0.0.0:8080` und `rewrite/server/forms_import.py:153` ruft `zf.read()` ohne Prüfung der unkomprimierten `ZipInfo.file_size` auf, was bei defekten oder manipulierten Archiven zum sofortigen OOM-Absturz von OBS Studio bzw. Server führt.
- **Verbesserung:**
  In `plugin/src/liedplan-import.cpp:763` (`inflate_data`) ein hartes Limit `constexpr size_t kMaxInflateBytes = 64 * 1024 * 1024;` sowie ein Gesamtbudget über alle ZIP-Einträge in `extract_zip_entries()` erzwingen und bei Überschreitung `inflateEnd(&zs)` + `std::nullopt` zurückgeben (inkl. Unit-Test in `test-liedplan-import.cpp`). In `main/server.py:112,161` Requests `> 25 * 1024 * 1024` Bytes mit HTTP `413` ablehnen und in `rewrite/server/forms_import.py:152` `info.file_size > 64 * 1024 * 1024` vor `zf.read()` abfangen.
- **Risiko der Umsetzung:** `low`
- **Geschätzter Aufwand:** ~45 min

---

### #3 · [P1] Debouncing für Disk-Saves, HTML-Schreibvorgänge & doppelte Base64-Kodierung bei Eingaben
- **Priorität:** `P1`
- **Betroffene Version(en):** `v3 (plugin/)`
- **Datei:Zeile:** `plugin/src/liedplan-dock.cpp:761-802`, `plugin/src/liedplan-dock.cpp:929-978`, `plugin/src/liedplan-dock.cpp:1002-1022`, `plugin/src/liedplan-dock.cpp:1290-1302`, `plugin/src/liedplan-scene.cpp:171-200`, `plugin/src/liedplan-html.cpp:530`, `plugin/src/liedplan-html.cpp:797`
- **Problem:**
  Jeder einzelne Tastendruck in `zeile1..3` / `anim_file` / `edit_form_id` und jeder 1-Pixel-Tick der Deckkraft- und Animations-Slider ruft synchron auf dem Qt-UI-Thread `save_state()` (`liedplan-state.json` schreiben) sowie `update_everywhere(*c, false, ...)` auf.
  Letzteres schreibt bei jedem Tick `boxN.html` komplett neu auf Disk und liest/kodiert eine lokale Animationsdatei (bis 16 MB GIF/WebM) **zweimal pro Event** synchron als Base64 (`write_card` → `build_inject_css` → `normalize_anim_src` in `liedplan-html.cpp:530` und nochmals in `push_update` → `normalize_anim_src` in `liedplan-html.cpp:797`), was beim Tippen und Slider-Ziehen UI-Lags und hohe SSD-Schreiblast verursacht.
- **Verbesserung:**
  1. In `LiedplanDock` einen Single-Shot-`QTimer` (150 ms Debounce, analog zu `v2` `cards.js:18-21`) für `save_state()` und `liedplan_html::write_card()` einführen, während `liedplan_html::push_update()` für verzögerungsfreie Live-Vorschau nur das geänderte Feld schickt.
  2. In `liedplan-html.cpp:302` (`normalize_anim_src`) die erzeugte Base64-Data-URI pro `(Pfad, std::filesystem::last_write_time, Dateigröße)` in einer kleinen Map cachen, statt die Datei bei jedem Tastenanschlag zweimal von Disk zu lesen.
  3. In `liedplan-dock.cpp:1290-1302` (`edit_form_id`, `edit_token_path`) auf `editingFinished` statt `textChanged` speichern (`CODE_TODO.md` M4-T2).
- **Risiko der Umsetzung:** `low`
- **Geschätzter Aufwand:** ~1,5 h

---

### #4 · [P1] Schutz vor stillem Überschreiben einer korrupten `liedplan-state.json` & atomares Speichern
- **Priorität:** `P1`
- **Betroffene Version(en):** `v3 (plugin/)`
- **Datei:Zeile:** `plugin/src/liedplan-dock.cpp:315-327`, `plugin/src/liedplan-state.cpp:307-360`
- **Problem:**
  Im Konstruktor von `LiedplanDock` (`liedplan-dock.cpp:317`) wird der Fehler-Rückgabestring von `state_.load(state_path_)` komplett verworfen; ist `liedplan-state.json` beschädigt (z. B. nach PC-Absturz), bleibt `state_.cards` leer, `file_exists` ist jedoch `true`, sodass das Dock mit 0 Karten startet und beim nächsten Klick die bestehende Datei unwiderruflich mit einem leeren State überschreibt.
  Darüber hinaus öffnet `LiedplanState::save()` (`liedplan-state.cpp:349`) die Zieldatei direkt mit `std::ios::trunc`, sodass ein Absturz während des Schreibvorgangs eine 0-Byte-Datei hinterlässt.
- **Verbesserung:**
  In `LiedplanDock::LiedplanDock()` (`liedplan-dock.cpp:317`) den Rückgabewert `const std::string err = state_.load(state_path_);` auswerten: wenn `!err.empty()`, die beschädigte Datei nach `state_path_ + ".bak"` kopieren, die 12 Standardkarten initialisieren und `set_status("Warnung: liedplan-state.json war beschädigt (Backup .bak angelegt)", true)` anzeigen. In `LiedplanState::save()` (`liedplan-state.cpp:349`) zuerst nach `path + ".tmp"` schreiben und anschließend per `std::filesystem::rename` atomar auf `path` verschieben.
- **Risiko der Umsetzung:** `low`
- **Geschätzter Aufwand:** ~1 h

---

### #5 · [P1] Hardcodierte `C:\Users\Johann\...`-Pfade im Plugin auflösen & `textbox.html` bundeln
- **Priorität:** `P1`
- **Betroffene Version(en):** `v3 (plugin/)`
- **Datei:Zeile:** `plugin/src/liedplan-html.cpp:47`, `plugin/src/liedplan-state.hpp:67-71`, `plugin/src/liedplan-import.hpp:37-40`, `plugin/src/liedplan-html.cpp:296-300`, `plugin/CMakeLists.txt:22`
- **Problem:**
  `kRepoTextboxPath` (`"C:/Users/Johann/Desktop/GitHub/OBS_Textbox/main/textbox.html"` in `liedplan-html.cpp:47`), `google_token_path` (`liedplan-state.hpp:67`) und `lieder_ref_path` (`liedplan-state.hpp:71`) sind fest auf Johanns lokalen Entwickler-Pfad verdrahtet.
  Wird das Plugin auf einem anderen PC (z. B. dedizierter Kirchen-Streaming-PC) oder nach Verschieben des Git-Ordners ausgeführt, fehlt `kRepoTextboxPath` und `write_card()` fällt stillschweigend auf `card_html()` (`liedplan-html.cpp:607`) zurück, dem Animationen, Custom Fonts und `max-content`-Boxbreite fehlen; zudem prüft `is_abs_win_path()` (`liedplan-html.cpp:296`) nur `C:\`-Laufwerksbuchstaben und `CMakeLists.txt:22,101,114` linkt `winhttp` unkonditional ohne `if(WIN32)`.
- **Verbesserung:**
  `main/textbox.html` und `main/controlui/lieder.md` per `CMakeLists.txt` in das Plugin-Data-Verzeichnis mitinstallieren und zur Laufzeit primär über `obs_module_file("textbox.html")` bzw. `obs_module_file("lieder.md")` auflösen (mit `LiedplanSettings`-Override und bisherigem Pfad als Dev-Fallback). Für `google_token_path` dynamisch `%LOCALAPPDATA%/hermes/google_token.json` (`std::getenv("LOCALAPPDATA")`) ermitteln, `is_abs_win_path()` durch `std::filesystem::path(u8path).is_absolute()` ersetzen und `winhttp` in `CMakeLists.txt` unter `if(WIN32)` kapseln.
- **Risiko der Umsetzung:** `low`
- **Geschätzter Aufwand:** ~1,5 h

---

### #6 · [P1] Kooperatives Abbrechen / Synchronisieren von Hintergrund-Threads beim Schließen von OBS
- **Priorität:** `P1`
- **Betroffene Version(en):** `v3 (plugin/)`
- **Datei:Zeile:** `plugin/src/liedplan-dock.cpp:1739`, `plugin/src/liedplan-dock.cpp:1834`, `plugin/src/liedplan-dock.cpp:1884`, `plugin/src/liedplan-dock.cpp:2063`, `plugin/src/liedplan-dock.cpp:2109`, `plugin/src/liedplan-ai.cpp:432`
- **Problem:**
  Alle 5 Hintergrund-Jobs im Dock (AI-Dokumentextraktion mit 120 s Timeout, OpenRouter-Loop mit bis zu 12 × 70 s Timeout, Google Forms API, Datei-Import) starten unkontrolliert per `std::thread(...).detach()`.
  Schließt Johann OBS Studio während ein langsamer OpenRouter-Request oder `markitdown`-Subprozess noch läuft, entlädt OBS die Plugin-DLL (`obs_module_unload`), während der abgekoppelte Thread weiter im bereits freigegebenen Adressraum der DLL ausgeführt wird (Crash beim Beenden von OBS, siehe auch `CODE_TODO.md` M5-3).
- **Verbesserung:**
  Einen `std::shared_ptr<std::atomic<bool>> cancel_requested` in `LiedplanDock` halten, der in `~LiedplanDock()` auf `true` gesetzt wird: in `extract_document_text()` (`liedplan-ai.cpp:432`) statt `WaitForSingleObject(pi.hProcess, 120000)` in 200-ms-Schritten pollen und bei `*cancel_requested` `TerminateProcess(pi.hProcess, 1)` aufrufen; in `process_with_llm()` zwischen Retries/Modellen `*cancel_requested` prüfen; und aktive Worker-Threads über einen gemeinsamen Shutdown-Mutex/Condition-Variable in `obs_module_unload()` kurz abwarten.
- **Risiko der Umsetzung:** `med`
- **Geschätzter Aufwand:** ~2 h

---

### #7 · [P2] Inkonsistenz zwischen Fallback-HTML (`card_html`) und `push_update`-Schema beheben
- **Priorität:** `P2`
- **Betroffene Version(en):** `v3 (plugin/)`
- **Datei:Zeile:** `plugin/src/liedplan-html.cpp:607-747`, `plugin/src/liedplan-html.cpp:781-835`
- **Problem:**
  Gemäß `push_update`-Schema (`liedplan-html.cpp:794-795`) enthält `box_bg` die Karten-Deckkraft (`rgba(r,g,b,a)` aus `card.box_opacity` 0–255), während `box_opacity` (`0` oder `1`) ausschließlich angibt, ob die Karte überhaupt Text enthält (`has_any_text(card)`).
  Im Fallback-Generator `card_html()` (`liedplan-html.cpp:609,618`) wird beim initialen Rendern jedoch `if (box_alpha <= 0) box_style += "display:none;";` anhand von `card.box_opacity` geprüft: dadurch zeigt eine leere Karte (`zeile1..3 == ""`, `box_opacity == 191`) beim Laden zunächst eine sichtbare leere Box, während eine Karte mit `box_opacity == 0` (transparenter Hintergrund, aber sichtbarer Text + Ecken laut Tooltip `liedplan-dock.cpp:606`) komplett samt Text und Ecken ausgeblendet wird; zudem fehlen in `card_html()` Animationen (`.anim-wrapper`) vollständig.
- **Verbesserung:**
  In `card_html()` (`liedplan-html.cpp:609-619`) die Sichtbarkeit von `#box` exakt wie in `build_inject_css()` (`liedplan-html.cpp:544`) und `push_update()` (`liedplan-html.cpp:795`) über `const bool visible = has_any_text(card);` (`opacity:0; pointer-events:none;` wenn leer) steuern und `.anim-wrapper` + Animations-JS analog zu `inject_into_textbox_template()` ergänzen.
- **Risiko der Umsetzung:** `low`
- **Geschätzter Aufwand:** ~1 h

---

### #8 · [P2] Fehlende Gotteslob-Titel-Anreicherung (`lieder.md`) bei Offline-AI-Fallback & Forms-Import
- **Priorität:** `P2`
- **Betroffene Version(en):** `v3 (plugin/)`, `v2 (rewrite/)`, `v1 (main/)`
- **Datei:Zeile:** `plugin/src/liedplan-dock.cpp:1645-1663`, `plugin/src/liedplan-dock.cpp:1229-1248`, `rewrite/dashboard/js/modals.js:649-662`, `main/controlui/pdf-automation.js:783-791`
- **Problem:**
  Die automatische Vervollständigung fehlender Liedtitel (`zeile3`) anhand der Gotteslob-Nummer (`zeile2`) aus `lieder.md` (`enrich_titles_from_reference`) wird in allen drei Versionen **nur** innerhalb von `process_with_llm()` nach einer erfolgreichen OpenRouter-Antwort aufgerufen (`liedplan-ai.cpp:849`, `pdf-ai-importer.js:309`).
  Greift der lokale Offline-Fallback-Parser (weil das OpenRouter Free-Tier belegt ist oder kein API-Key eingetragen wurde) oder trägt ein Organist im Google-Formular nur die GL-Nummer ohne Liedtitel ein, bleibt `zeile3` auf den Karten leer.
- **Verbesserung:**
  Den Aufruf von `load_song_reference()` + `enrich_titles_from_reference()` in `plugin/src/liedplan-dock.cpp:1663` nach dem `if (!api_key.empty()) ... else ...`-Block für **alle** AI-Pfade (auch `parse_liedplan_locally`) sowie in `apply_import_result()` (`liedplan-dock.cpp:1238`) ausführen; analog in `rewrite/dashboard/js/modals.js:663` (`enrichTitlesFromReference` nach `parseLiedplanLocally`) und `main/controlui/pdf-automation.js:785` (`completeTitlesLocally(localItems, songRef)`) ergänzen.
- **Risiko der Umsetzung:** `low`
- **Geschätzter Aufwand:** ~45 min

---

### #9 · [P2] Schnell-Profile in v2 (`rewrite/`) senden beim Klick nicht an OBS & ungeescapte `innerHTML`-Injektion
- **Priorität:** `P2`
- **Betroffene Version(en):** `v2 (rewrite/)`, `v3 (plugin/)`
- **Datei:Zeile:** `rewrite/dashboard/js/modals.js:419-431`, `rewrite/dashboard/js/app.js:35-43`, `plugin/src/liedplan-dock.cpp:1129-1143`
- **Problem:**
  In `v2` aktualisiert ein Klick auf ein Schnell-Profil (`modals.js:423-430`) zwar den lokalen State (`'load_quick_profile'`) und die Karten im Dashboard, ruft aber **niemals** `pushStateToOBS()` auf (da `app.js:37` nur auf `'import_json'` reagiert) — im Gottesdienst bewirkt ein Profil-Wechsel in OBS daher nichts, bis manuell gesendet wird.
  Zudem wird in `modals.js:422` `btnLoad.innerHTML = \`<span class="chip-icon">📋</span> ${profile.name}\`` ohne `escapeHtml(profile.name)` gesetzt (DOM-XSS beim Import einer manipulierten JSON-Konfiguration), und im Plugin (`liedplan-dock.cpp:1138`, `CODE_TODO.md` M7-3) fehlt beim Klick auf ein Schnell-Profil das Status-Feedback im Header.
- **Verbesserung:**
  In `rewrite/dashboard/js/modals.js:422` `${escapeHtml(profile.name)}` verwenden und im Click-Handler (`modals.js:429`) `if (getState().autoSend) pushStateToOBS();` aufrufen; im Plugin (`plugin/src/liedplan-dock.cpp:1139`) `set_status(QString("Profil \"%1\" geladen").arg(QString::fromStdString(name)), false);` ergänzen.
- **Risiko der Umsetzung:** `low`
- **Geschätzter Aufwand:** ~30 min

---

### #10 · [P2] Bereinigung veralteter Folge-Karten und dynamische Kartenanzahl beim AI-/Forms-Import im Plugin
- **Priorität:** `P2`
- **Betroffene Version(en):** `v3 (plugin/)`
- **Datei:Zeile:** `plugin/src/liedplan-dock.cpp:1229-1248`, `plugin/src/liedplan-dock.cpp:1747-1768`
- **Problem:**
  Erkennt der AI-Import weniger Programmpunkte als Karten im Dock vorhanden sind (z. B. 7 Lieder bei einer Werktagsmesse oder Andacht auf 12 Karten), überschreibt `ai_apply_btn` (`liedplan-dock.cpp:1753-1760`) lediglich die Karten `0..6`, während auf den Karten `7..11` die alten Lieder der Vorwoche unverändert stehen bleiben und in OBS sichtbar geschaltet werden können.
  Hat Johann zuvor einzelne Karten gelöscht (z. B. `state_.cards.size() == 8`) und importiert anschließend 12 Punkte, füllt `ai_apply_btn` (`liedplan-dock.cpp:1747`) nur dann auf 12 Karten auf, wenn `state_.cards.empty()` ist, und verwirft andernfalls die Punkte 9–12.
- **Verbesserung:**
  In `ai_apply_btn` (`liedplan-dock.cpp:1747-1760`) und `apply_import_result()` (`liedplan-dock.cpp:1232-1241`):
  1. Falls `state_.cards.size() < total_items`, fehlende Karten per `make_default_card(i)` ergänzen.
  2. Für alle überzähligen Bestands-Karten `i >= total_items` die Felder `zeile2` und `zeile3` (sowie bei Nicht-Standard-Karten `zeile1`) leeren, damit keine Lieder aus dem vorherigen Gottesdienst zurückbleiben.
- **Risiko der Umsetzung:** `low`
- **Geschätzter Aufwand:** ~45 min

---

### #11 · [P2] Einstufiges Undo („↶ Rückgängig") für destruktive Aktionen im Dock („Alle leeren", Import, Löschen)
- **Priorität:** `P2`
- **Betroffene Version(en):** `v3 (plugin/)`, `v2 (rewrite/)`
- **Datei:Zeile:** `plugin/src/liedplan-dock.cpp:1182-1219`, `plugin/src/liedplan-dock.cpp:1229-1248`, `plugin/src/liedplan-dock.cpp:1747-1778`
- **Problem:**
  Ein versehentlicher Klick auf „Alle leeren" (`clear_all_cards()` in `liedplan-dock.cpp:1182`), das Löschen einer Karte (`delete_card()`) oder ein Überschreiben per Import löscht während des Live-Gottesdienstes sofort und ohne Rückfrage alle Texte in OBS und in `liedplan-state.json`.
  Ohne Undo-Möglichkeit muss Johann im laufenden Gottesdienst den Liedplan hektisch neu importieren oder manuell nachtippen.
- **Verbesserung:**
  In `LiedplanDock` ein Member `std::vector<LiedplanCard> undo_snapshot_;` vor jedem Aufruf von `clear_all_cards()`, `delete_card()`, `apply_import_result()` und `ai_apply_btn` befüllen und in der Toolbar neben „Alle leeren" einen kompakten Button „↶" (inkl. `QShortcut(QKeySequence::Undo)`) aktivieren, der `state_.cards = undo_snapshot_;` wiederherstellt, speichert und per `apply_all_to_scene(false)` an OBS pusht.
- **Risiko der Umsetzung:** `low`
- **Geschätzter Aufwand:** ~1 h

---

### #12 · [P2] Sicheres Command-Line-Quoting für MarkItDown & Pfad-/URL-Restriktion für `anim_file`
- **Priorität:** `P2`
- **Betroffene Version(en):** `v3 (plugin/)`
- **Datei:Zeile:** `plugin/src/liedplan-ai.cpp:382-395`, `plugin/src/liedplan-html.cpp:302-360`
- **Problem:**
  `extract_document_text()` (`liedplan-ai.cpp:391-392`) konkateniert `file_path` unescaped in den `CreateProcessW`-Befehlsstring (`L"... -m markitdown \"" + utf8_to_wide(file_path) + L"\""`, siehe `CODE_TODO.md` M5-4), was bei Dateinamen mit `"` oder abschließendem `\` das Argument-Parsing bricht.
  Zudem akzeptiert `normalize_anim_src()` (`liedplan-html.cpp:322-358`) beliebige externe `http(s)://`-URLs sowie relative `..`-Pfade aus importierten JSON-Dateien und liest bis zu 16 MB große lokale Dateien als Base64-Data-URI ein.
- **Verbesserung:**
  In `liedplan-ai.cpp:391` eine Hilfsfunktion `quote_win32_arg(const std::wstring&)` verwenden, die Backslashes vor `"` und am String-Ende verdoppelt und `"` als `\"` maskiert; in `normalize_anim_src()` (`liedplan-html.cpp:337`) nach `std::filesystem::weakly_canonical()` prüfen, dass relative Animationspfade innerhalb des erlaubten Animationsverzeichnisses bleiben, und für externe `http(s)://`-URLs im UI zumindest einen Hinweis anzeigen oder nur lokale Dateien/Data-URIs zulassen.
- **Risiko der Umsetzung:** `low`
- **Geschätzter Aufwand:** ~45 min

---

### #13 · [P2] Obsolete `liedplan_card_source_info`-Quelle aus OBS-Registrierung & `[liedplan-debug]`-Logs entfernen
- **Priorität:** `P2`
- **Betroffene Version(en):** `v3 (plugin/)`
- **Datei:Zeile:** `plugin/src/plugin-main.cpp:102`, `plugin/src/liedplan-card-source.cpp:34-42`, `plugin/src/liedplan-card-source.cpp:106-225`, `plugin/CMakeLists.txt:92`
- **Problem:**
  `obs_module_load()` registriert in `plugin-main.cpp:102` weiterhin `liedplan_card_source_info` („Liedplan Karte"), obwohl dieser M2a-QPainter-Ansatz laut `FUNKTIONSVERGLEICH.md:12,112` im E2E-Test schwarz blieb und vollständig durch `browser_source` ersetzt wurde.
  Dadurch erscheint im OBS-Dialog „Quelle hinzufügen" ein funktionsloser Eintrag „Liedplan Karte", und `liedplan-card-source.cpp` enthält zahlreiche temporäre `[liedplan-debug]`-Log-Ausgaben (`Zeilen 35, 106, 172, 178, 188, 220`).
- **Verbesserung:**
  Den Aufruf `obs_register_source(&liedplan_card_source_info);` in `plugin/src/plugin-main.cpp:102` sowie `src/liedplan-card-source.cpp` aus dem Produktions-Target `obs-liedplan` in `plugin/CMakeLists.txt:92` entfernen (das reine Render-Modul `liedplan-render.cpp` bleibt für `test-liedplan-render` erhalten).
- **Risiko der Umsetzung:** `low`
- **Geschätzter Aufwand:** ~20 min

---

### #14 · [P2] Vereinheitlichung des lokalen Offline-Parsers zwischen v1, v2 und v3 bei mehrzeiligen Liedplänen
- **Priorität:** `P2`
- **Betroffene Version(en):** `v3 (plugin/)`, `v2 (rewrite/)`, `v1 (main/)`
- **Datei:Zeile:** `plugin/src/liedplan-ai.cpp:898-990`, `rewrite/dashboard/js/pdf-ai-importer.js:347-399`, `main/controlui/pdf-automation.js:466-521`
- **Problem:**
  Der deterministische Offline-Parser divergiert zwischen den drei Versionen: Steht in einem PDF/DOCX die GL-Nummer auf der Zeile unter dem Programmpunkt (`"Kyrie\n437, 1-4"`), ordnet `v1` (`pdf-automation.js:487-489`) sie korrekt dem vorherigen Punkt zu (`if (current && !current.zeile2)`), während `v2` (`pdf-ai-importer.js:363-368`) und `v3` (`liedplan-ai.cpp:940-949`) einen neuen Programmpunkt mit leerem `zeile1 = ""` anlegen und `"Kyrie"` ohne Nummer zurücklassen.
  Umgekehrt verwirft `v1` (`pdf-automation.js:483-493`) bei einzeiligem `"Eröffnung 144, 1+4+6 Nun jauchzt dem Herren"` den auf die Nummer folgenden Liedtitel (`after`) komplett.
- **Verbesserung:**
  In `plugin/src/liedplan-ai.cpp:940-949` und `rewrite/dashboard/js/pdf-ai-importer.js:363-368` prüfen: wenn `before.empty()` und `current != nullptr && current->zeile2.empty()`, dann `current->zeile2 = num` (und ggf. `current->zeile3 = after`) setzen statt einen neuen Punkt zu erzeugen; in `main/controlui/pdf-automation.js:484-491` den String `after` als `zeile3` übernehmen; in `plugin/src/tests/test-liedplan-ai.cpp` einen Testfall für mehrzeilige Label+Nummer-Paare ergänzen.
- **Risiko der Umsetzung:** `low`
- **Geschätzter Aufwand:** ~45 min

---

### #15 · [P3] YAML-Escaping für Farbprofilnamen & UTF-16-Surrogat-Filterung im XML-Entity-Decoder
- **Priorität:** `P3`
- **Betroffene Version(en):** `v3 (plugin/)`, `v2 (rewrite/)`, `v1 (main/)`
- **Datei:Zeile:** `plugin/src/liedplan-state.cpp:491-501`, `plugin/src/liedplan-import.cpp:843-860`, `rewrite/dashboard/js/color-profiles.js:140-143`
- **Problem:**
  `liedplan_profile_to_yaml()` (`liedplan-state.cpp:493`) und `colorProfileToYAML()` (`color-profiles.js:142`) geben `name: "` + `p.name` + `"` ohne Maskierung von `"` oder `\` aus, sodass ein Profilname mit Anführungszeichen ungültiges YAML erzeugt, das beim Re-Import abgeschnitten wird.
  Außerdem kodiert `append_utf8_codepoint()` in `plugin/src/liedplan-import.cpp:843-860` (`CODE_TODO.md` M4-T3) numerische XML-Entities im UTF-16-Surrogatbereich (`0xD800..0xDFFF`) ungefiltert als 3-Byte-Sequenzen und erzeugt dadurch ungültiges UTF-8.
- **Verbesserung:**
  In `liedplan_profile_to_yaml()` (`liedplan-state.cpp:493`) und `colorProfileToYAML()` (`color-profiles.js:142`) `\` und `"` maskieren (und in `unquote()` in `liedplan-state.cpp:508` wieder entmaskieren). In `append_utf8_codepoint()` (`liedplan-import.cpp:851`) `else if (cp <= 0xFFFF && (cp < 0xD800 || cp > 0xDFFF))` prüfen und für Surrogate `U+FFFD` (`\xEF\xBF\xBD`) anhängen.
- **Risiko der Umsetzung:** `low`
- **Geschätzter Aufwand:** ~30 min

---

### #16 · [P3] Netzwerk- und Dateiauslieferungs-Härtung im Legacy-Server `main/server.py`
- **Priorität:** `P3`
- **Betroffene Version(en):** `v1 (main/)`
- **Datei:Zeile:** `main/server.py:92-97`, `main/server.py:345`
- **Problem:**
  Während `v2` (`rewrite/server/http_app.py:42-69`, `config.py:21`) standardmäßig nur auf `127.0.0.1` lauscht und sensible Dateien blockiert, bindet `main/server.py:345` auf `('', 8080)` (alle Netzwerkinterfaces), setzt `Access-Control-Allow-Origin: *` (`main/server.py:94`) und liefert über `SimpleHTTPRequestHandler` das gesamte Verzeichnis aus.
  Wird `v1` im Kirchen-WLAN gestartet, kann jeder Teilnehmer im selben Netz `http://<ip>:8080/api.env` (OpenRouter-API-Key) abrufen oder über `/api/form-responses` und `/api/parse-document` den Server fernsteuern.
- **Verbesserung:**
  In `main/server.py:345` die Standard-Bind-Adresse auf `('127.0.0.1', PORT)` beschränken, `Access-Control-Allow-Origin` auf Loopback-Origins eingrenzen und in `do_GET()` (`main/server.py:99`) Zugriffe auf Punkt-Dateien (`/.`), `.py`-Dateien sowie `/api.env` von Nicht-Loopback-IPs mit `self.send_error(404)` abweisen.
- **Risiko der Umsetzung:** `low`
- **Geschätzter Aufwand:** ~30 min

---

### #17 · [P3] Unit-Test-Suite für `liedplan-html.cpp` (CSS-Injektion, `#L89`-Schutz, `push_update`-Schema)
- **Priorität:** `P3`
- **Betroffene Version(en):** `v3 (plugin/)`
- **Datei:Zeile:** `plugin/src/liedplan-html.cpp:1-837`, `plugin/CMakeLists.txt:118-155`
- **Problem:**
  Obwohl `plugin/` 6 CTest-Suiten für State, Textformatierung, Import, AI, Icons und QPainter-Render besitzt, hat das für die tatsächliche OBS-Darstellung zentrale Modul `liedplan-html.cpp` (837 Zeilen: CSS-Generierung, Font-Skalierung, `textbox.html`-Template-Injektion, Base64-Animationen, `push_update`-JSON) **keinen einzigen Unit-Test**.
  Fehler im HTML/CSS-Generator oder Abweichungen zwischen `inject_into_textbox_template()`, `card_html()` und `push_update()` (siehe Kandidat #7) fallen daher erst im manuellen OBS-E2E-Betrieb auf.
- **Verbesserung:**
  Die von LibOBS unabhängigen String-/HTML-Funktionen in `liedplan-html.cpp` (oder mit einem minimalen Stub für `obs_source_get_proc_handler`) in einer 7. CTest-Suite `plugin/src/tests/test-liedplan-html.cpp` testen: Prüfung von `build_inject_css()`, Erhalt der geschützten Zeile `.textbox-container { padding: 15px 60px 20px 50px; }`, `normalize_anim_src()` und Konsistenz der `box_opacity`-Logik.
- **Risiko der Umsetzung:** `low`
- **Geschätzter Aufwand:** ~1,5 h

---

### #18 · [P3] Doku-Synchronisation: `FUNKTIONSVERGLEICH.md`, `README.md` und `CODE_TODO.md` auf Stand M7h bringen
- **Priorität:** `P3`
- **Betroffene Version(en):** `Alle (Root-Docs)`
- **Datei:Zeile:** `FUNKTIONSVERGLEICH.md:3-6`, `FUNKTIONSVERGLEICH.md:85-107`, `README.md:11-65`, `CODE_TODO.md:77-81`
- **Problem:**
  `FUNKTIONSVERGLEICH.md` trägt im Header noch „Stand: 2026-10-28 — nach M5.1 + Ui3 … ctest 5/5" (`Zeile 3, 85`) und endet in der Meilenstein-Tabelle bei `M7b` (`Zeile 106`), obwohl das Plugin bei `M7h` und `ctest 6/6` (inkl. `liedplan-theme-icons`) steht.
  Zudem beschreibt `README.md:20-65` in der Einrichtungsanleitung ausschließlich `v1 (main/)` (mit doppelter Schrittnummerierung `3.` in `Zeile 59-60`), ohne Build-/Installationshinweise für `v3 (plugin/)` oder `v2 (rewrite/)`, und `CODE_TODO.md:77` führt `lpStyleSection` noch als default-eingeklappt (in `M7b` `liedplan-dock.cpp:727` auf sichtbar geändert).
- **Verbesserung:**
  In `FUNKTIONSVERGLEICH.md` Kopfzeile, Test-Suiten-Anzahl (`6/6`) und Meilenstein-Tabelle um `M7c`–`M7h` ergänzen; in `README.md` Kurzanleitungen für `plugin/` (DLL nach `obs-plugins/64bit/` kopieren) und `rewrite/` (`rewrite/start_server.bat`) hinzufügen sowie die doppelte `3.` korrigieren; in `CODE_TODO.md` die erledigten Punkte aus M7b–M7h abhaken.
- **Risiko der Umsetzung:** `low`
- **Geschätzter Aufwand:** ~30 min

---

## NICHT empfohlen (Verworfene Ideen mit Begründung)

1. **Änderung des asymmetrischen Innenabstands (`padding: 15px 60px 20px 50px`) in `main/textbox.html:89` / `rewrite/textbox.html:63` (ehemals F12):**
   - *Begründung:* Von Johann bewusst so festgelegt und im Plugin (`plugin/src/liedplan-html.cpp:447`) explizit vor Überschreibung geschützt. Die `60px`/`50px`-Polsterung verhindert, dass Text in die 50×50 px großen Ecken-Dreiecke (`::before`/`::after`) ragt, und das `body`-Padding (`350px 20px 20px 250px`) hält Platz für hochskalierte Animationen über der Box frei.
2. **Schreibender Token-Refresh oder Scope-Überschreibung auf `google_token.json`:**
   - *Begründung:* Verstößt gegen die harte Sicherheitsvorgabe („Google-Token nur lesend nutzen"). In `plugin/src/liedplan-import.cpp:1466` wird der Access-Token daher korrekt nur im Arbeitsspeicher erneuert; ein Überschreiben der Datei oder das Senden abweichender Scopes führt bei Google OAuth zu `403 ACCESS_TOKEN_SCOPE_INSUFFICIENT` (`main/forms_import.py:186-188`).
3. **Ersetzen der `browser_source`-basierten Textboxen im Plugin durch die eigene QPainter-Quelle (`liedplan_card_source`):**
   - *Begründung:* Wurde in Meilenstein M2a/M2b getestet (`plugin/src/liedplan-card-source.cpp`) und blieb im OBS-E2E-Betrieb schwarz (`FUNKTIONSVERGLEICH.md:12`). Zudem unterstützt QPainter ohne großen Zusatzaufwand weder CSS-`hue-rotate`-Animationen für Regenbogen-Ecken noch geloopte WebM/GIF-Overlays, während `browser_source` + `liedplan_update`-Proc-Handler bereits flackerfreie 0-ms-Updates liefert.
4. **Einbau eines eingebetteten HTTP- oder WebSocket-Servers in das native C++-Plugin (`v3`):**
   - *Begründung:* Die Architektur von `v3` verzichtet bewusst auf lokale Netzwerk-Ports (`8080`/`4455`), Passwort-Setup und Firewall-Freigaben, weil das Dock direkt im OBS-Prozess über `obs_frontend_api` und `proc_handler` mit den Quellen spricht.
5. **Einführung eines JS-Bundlers/Frameworks (Vite/React/npm-Build) für `v2` (`rewrite/dashboard/`):**
   - *Begründung:* `v2` ist als 0-Build-Vanilla-ES-Modul-Anwendung konzipiert (`rewrite/README.md:12`), die ohne `node_modules` oder Build-Schritt direkt im OBS Custom Browser Dock läuft; ein Build-System würde den Betrieb auf dem Gottesdienst-PC unnötig verkomplizieren.
