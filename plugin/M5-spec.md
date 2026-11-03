# M5 — Pro-Karte-Animationen (A10) + AI-Import (D3)

**Ziel:** T7-OBS-Plugin bekommt die letzten beiden v2-Funktionen (Scope-Entscheidung Johann
2026-10-24, final, s. `FUNKTIONSVERGLEICH.md` §Entscheidungen-3):
1. **A10 — Pro-Karte-Animation:** GIF/Webm/Video-Datei pro Karte, Skala, X/Y-Offset (±1200 px), Show/Hide.
2. **D3 — AI-Import:** PDF/DOCX → Text-Extraktion (Python-Sidecar MarkItDown) → OpenRouter-LLM
   (Free-Tier, Modell-Discovery, Fallback-Chain) → strukturierte Programmpunkte → Preview → Karten.
   Inkl. konfigurierbarem System-Prompt, deterministischem Offline-Fallback-Parser
   (`parseLiedplanLocally`) + Titel-Enrichment aus `lieder.md` (D4, Teil der AI-Pipeline).

Das Plugin baut auf M4 (Commit `8539313`): State mit cards/settings/quick_profiles, Import-Dialog
(CSV/XLSX/ZIP + Google) im Dock, `liedplan-html.{hpp,cpp}` rendert Browser-Karten
(`main/textbox.html` + INJECT-CSS-Block + Live-Listener), CTest 4/4 grün.

**WICHTIG — A10-Plumbing existiert bereits (nicht neu bauen):**
- `main/textbox.html` (Karten-HTML, Zeilen ~121–193) rendert die Animation VOLLSTÄNDIG:
  `.anim-wrapper#anim-wrapper` + JS liest `--show-anim`/`--anim-file`/`--anim-x`/`--anim-y`/
  `--anim-scale` aus den CSS-Variablen, erzeugt `<img>` (GIF) oder `<video>` (.webm/.mp4).
- Der injizierte Live-Listener (`liedplan-html.cpp`, Zeilen ~146–150) setzt die 5 CSS-Variablen
  schon aus dem Update-JSON (`anim_file, show_anim, anim_scale, anim_x, anim_y`).
- `build_inject_css()` (Zeilen ~289–320) und `push_update()` (Zeilen ~560–585) emittieren die
  5 Variablen/JSON-Keys bereits — aber mit **hardcoded Empty/Werten** (`""`, 0, 3, 0px).

→ A10 = State-Felder + echte Werte in `build_inject_css`/`push_update` + Pfad-Normalisierung +
Dock-UI. `main/textbox.html` und der Listener werden NICHT verändert.

## 0. Grundregeln für agy (WICHTIG — Anti-Umzug & Scope)
- **DATEI-ALLOWLIST** (nur diese anlegen/ändern; NICHTS anderes):
  - `plugin/src/liedplan-state.hpp` (erweitern: 5 Anim-Felder pro Karte + 5 Settings-Felder)
  - `plugin/src/liedplan-state.cpp` (erweitern: Serialize/Deserialize + Defaults)
  - `plugin/src/liedplan-html.hpp` (erweitern: Signatur `build_inject_css` + `normalize_anim_src`)
  - `plugin/src/liedplan-html.cpp` (erweitern: echte Anim-Werte + Normalisierung)
  - `plugin/src/liedplan-scene.{hpp,cpp}` (nur wenn nötig, um Settings an `build_inject_css`/
    `push_update` zu reichen)
  - `plugin/src/liedplan-ai.hpp` (NEU)
  - `plugin/src/liedplan-ai.cpp` (NEU)
  - `plugin/src/liedplan-dock.hpp` (erweitern)
  - `plugin/src/liedplan-dock.cpp` (erweitern: Anim-Sektion + AI-Dialog)
  - `plugin/src/liedplan-theme.hpp` (erweitern: QSS für neue Widgets, v1-Tokens WIEDERVERWENDEN)
  - `plugin/src/liedplan-import.{hpp,cpp}` (erweitern: AI-Option im Import-Dialog)
  - `plugin/src/tests/test-liedplan-ai.cpp` (NEU)
  - `plugin/src/tests/test-liedplan-state.cpp` (erweitern: Anim-Roundtrip)
  - `plugin/tests/render/test-liedplan-render.cpp` (erweitern: Anim-Checks im INJECT-CSS)
  - `plugin/CMakeLists.txt` (neues Test-Executable `liedplan-ai-tests`)
- **ANTI-UMZUG:** keine Datei im Repo verschieben, umbenennen, löschen oder außerhalb der
  Allowlist neu anlegen. `main/textbox.html`, `main/api.env`, `main/lieder.md`, `animation/*`,
  `rewrite/*`, `main/*` bleiben BITTE UNANGEGRIFFEN (Lesezugriff ja).
- **Kein `git add`/`git commit`/`git stage`** — Hermes committet selbst.
- **Keine neue externe Abhängigkeit** (kein Vendor-Download, kein neues `find_package`).
  WinHTTP ist bereits verlinkt (s. `plugin/CMakeLists.txt`: `winhttp ZLIB::ZLIB`).
- UI-Thread NIEMALS blockieren: Datei-Lesen, Sidecar, HTTP-Requests in Worker-Thread
  (`std::thread`, wie im M3-Import), Ergebnis via Qt-Signal/Event zurück.
- Alle neuen Dock-Widgets/Strings **auf Deutsch**.
- **Keine Secrets in Dateien:** Tests nutzen nur Dummy-Werte (z. B. `sk-or-dummy-test-123`).
  Der echte OpenRouter-Key in `main/api.env` wird NIE kopiert/geschrieben/loggt.

## 1. A10 — State-Modell (liedplan-state.{hpp,cpp})
`LiedplanCard` bekommt 5 neue Felder (analog zu `rainbow_corners`, mit Backward-Compat-Defaults):
```cpp
std::string anim_file = "";  // Dateiname, absoluter Pfad oder URL (roher Wert, wie eingegeben)
bool        show_anim   = false;
int         anim_scale  = 3;   // 1,2,3,4,6,8,10,12 (v2-Optionen); Default 3
int         anim_x      = 0;   // -1200..1200 px
int         anim_y      = 0;   // -1200..1200 px
```
JSON-Keys exakt: `anim_file`, `show_anim`, `anim_scale`, `anim_x`, `anim_y` (passen bereits zum
Bestands-Listener-JSON). Deserialize: fehlende Keys → Defaults (`.value("anim_file", "")` usw.),
**nie** `state["x"]` ohne Default. `operator==` erweitern. Quick-Profile-Snapshots (cards-Kopien)
erben die Felder automatisch — kein Extra-Code.

`LiedplanSettings` bekommt 5 neue Felder (alle Default-Pfade, Backward-Compat):
```cpp
std::string openrouter_key_path = "C:\\Users\\Johann\\Desktop\\GitHub\\OBS_Textbox\\main\\api.env";
std::string markitdown_python   = "C:\\Users\\Johann\\Desktop\\GitHub\\OBS_Textbox\\.venv\\Scripts\\python.exe";
std::string animation_dir       = "C:\\Users\\Johann\\Desktop\\GitHub\\OBS_Textbox\\animation";
std::string lieder_path         = "C:\\Users\\Johann\\Desktop\\GitHub\\OBS_Textbox\\rewrite\\dashboard\\lieder.md";
std::string ai_system_prompt    = ""; // leer = LiedplanAi::kDefaultSystemPrompt verwenden
```
Persistieren in `state.settings` (Serialize/Deserialize + Defaults). `operator==` erweitern.

## 2. A10 — HTML/Update (liedplan-html.{hpp,cpp})
**NEU — `normalize_anim_src`** (frei, testbar, in `liedplan_html`-Namespace):
```cpp
// Wandelt den rohen anim_file-Wert in einen für CEF ladbaren src-Wert um:
// - "" → ""
// - beginnt mit "http://" / "https://" / "file://" → unverändert
// - absoluter Windows-Pfad (^[A-Za-z]:[\\/] ...) → "file:///" + Pfad mit FORWARD-SLASHES,
//   Spaces als %20 (z. B. "C:\a b\x.gif" → "file:///C:/a%20b/x.gif")
// - sonst (Bare-Name/relativ): wenn settings.animation_dir + "/" + s existiert
//   (std::filesystem::exists) → file-URL davon; sonst unverändert (relativ zur Card-HTML, Best-Effort)
std::string normalize_anim_src(const std::string &raw, const LiedplanSettings &settings);
```
**`build_inject_css`:** Signatur um optionalen Settings-Parameter erweitern
(`const LiedplanSettings &settings = LiedplanSettings()`). Die 5 hardcodeden Anim-Zeilen
(`--anim-file: ""`, `--show-anim: 0`, `--anim-scale: 3`, `--anim-x: 0px`, `--anim-y: 0px`)
ersetzen durch die echten Werte aus `card`:
```
  --anim-file: "<normalize_anim_src(card.anim_file, settings)>";   (escape_css_string auf dem NORMALISIERTEN Wert)
  --show-anim: <0|1>;
  --anim-scale: <card.anim_scale>;
  --anim-x: <card.anim_x>px;
  --anim-y: <card.anim_y>px;
```
Zeilenformat/exakte Schlüssel 1:1 wie bisher (nur die Werte ändern).
**`push_update`:** JSON-Keys `anim_file` (normalisiert!), `show_anim`, `anim_scale`, `anim_x`,
`anim_y` aus dem Card-State befüllen statt der Hardcodes (Settings dafür verfügbar machen —
Signatur erweitern, `liedplan-scene.cpp` passt die Aufrufstellen an, falls nötig).
**Fallback-Template** (`card_html`, Zeilen ~308–312): UNVERÄNDERT (Defaults bleiben).

## 3. A10 — Dock-UI (liedplan-dock.{hpp,cpp} + liedplan-theme.hpp)
In jeder Karten-Panel (nach der "🌈 Regenbogen-Ecken"-Checkbox, s. Zeile ~327) eine
**ausklappbare Sektion "🎬 Animation"** — als `QGroupBox` mit `setCheckable(true)` +
`setTitle("🎬 Animation")` + default UNGECKT (eingeklappt, wie v2s `<details>`). Inhalt:
1. **Datei:** `QLineEdit` (objectName `lpAnimFile`, ReadOnly false) + `QPushButton "📁"`
   (`lpAnimFilePick`) → `QFileDialog::getOpenFileName`, start-Verzeichnis =
   `settings.animation_dir`, Filter:
   `Animationen (*.gif *.webm *.mp4 *.mov *.m4v);;Bilder (*.png *.jpg *.jpeg);;Alle Dateien (*.*)`
2. **Skalierung:** `QComboBox` (`lpAnimScale`) mit Items "1x","2x","3x","4x","6x","8x","10x","12x"
   (Datenwerte 1,2,3,4,6,8,10,12), Default "3x" (1:1 v2 `cards.js` Zeilen ~81–84).
3. **X-Versatz:** `QSlider` horizontal, range -1200..1200, step 5, objectName `lpAnimX`
   + Wert-Label "0px" (Live-Update beim Slide, 1:1 v2).
4. **Y-Versatz:** `QSlider` `lpAnimY`, identisch.
5. **Aktiv:** `QCheckBox` ("Aktiv", objectName `lpAnimShow`).
Jede Änderung → Card-State setzen → `save_state` → `update_everywhere` (Gleiches Muster wie die
bestehende Rainbow-Checkbox, Zeile ~442). Beim (Re-)Build der Karte aus dem State alle 5 Widgets
befüllen (inkl. Slider-Labels "<n>px"). QSS für QGroupBox/QSlider/Combos in `liedplan-theme.hpp`
(v1-Tokens, `lpCardInner`-Look) — NICHT im cpp hardcoden.

## 4. D3 — AI-Import-Modul (liedplan-ai.{hpp,cpp}, NEU)
Eigenes Modul, **ohne OBS-Header-Abhängigkeit** (nur nlohmann_json + WinHTTP + Windows-CRT,
damit es in ein eigenes Test-Executable linkt wie `liedplan-import`).

```cpp
namespace liedplan_ai {
struct AiPoint  { std::string zeile1, zeile2, zeile3; };
struct AiResult { std::vector<AiPoint> items; std::string model_label; }; // z. B. "google/gemma-…", "Offline-Fallback"

constexpr const char *kDefaultSystemPrompt = /* 1:1-Kopie aus rewrite/dashboard/js/pdf-ai-importer.js
   (DEFAULT_SYSTEM_PROMPT, Zeilen 7–21) — String exakt übernehmen, NICHT umformulieren */;
constexpr const char *kModelPriorities[] = { "gemma-4-31b","gemma-4-26b","nemotron-3-super-120b",
   "llama-3.3-70b","llama-4","deepseek","qwen-2.5","qwen3","mistral","gemini","phi-4" }; // 1:1 v2

std::string  read_env_key(const std::string &env_file_path, std::string &err);
// liest OPENROUTER_API_KEY=<wert> aus api.env (Zeile, '=' trennt, trim, Wert nicht loggen).
// Datei fehlt / kein Key → "" + err (deutsche Meldung). Datei wird NIE geschrieben.

std::string  extract_document_text(const std::string &file_path, const std::string &python_exe,
                                   std::string &err);
// Python-Sidecar: CreateProcessW  <python_exe> -m markitdown "<file_path>"  → stdout (UTF-8).
// Timeout 120 s (Job-Object oder äquivalent). Fehler (deutsche Meldungen in `err`):
// Python-Exe fehlt → "MarkItDown-Python nicht gefunden: <pfad>"; Prozessfehler/Exit≠0 →
// "Dokument-Extraktion fehlgeschlagen (Exit <n>)"; leerer stdout → "Kein Text im Dokument gefunden."
// Erfolg → Text; Fehler → "" + err.

nlohmann::json parse_json_clean(const std::string &raw);
// 1:1 v2 `parseJsonClean`: ``` -Fences trimmen, ersteres '{' bis letztes '}', JSON.parse;
// kein JSON → throw std::runtime_error("Kein valides JSON im Modell-Ergebnis gefunden.").

std::vector<std::string> discover_free_models(const nlohmann::json &models_json);
// PURE (für Tests, kein HTTP): aus {data:[{id, context_length}]} → nur Ids mit Suffix ":free",
// ausschließen wenn die Id eines dieser Teilstücke enthält: "-vl", "content-safety", "2.6b";
// Score: erstes v2-kModelPriorities-Teilstück in der Id → 1000 - i*10, sonst 100;
// Sortierung Score-desc, dann context_length-desc; Rückgabe sortierte Id-Liste.

AiResult process_with_llm(const std::string &api_key, const std::string &document_text,
                          const std::string &system_prompt,
                          const std::string &lieder_path,
                          std::function<void(const std::string &)> on_progress);
// BLOCKIERT (nur in Worker-Thread aufrufen). Flow 1:1 v2 `processWithLLM`:
// 1. GET https://openrouter.ai/api/v1/models (WinHTTP, Authorization: Bearer <key>)
//    → discover_free_models → top 6 Kandidaten. Keine Kandidaten → throw
//    "Keine kostenlosen Modelle auf OpenRouter verfügbar."
// 2. Pro Modell max. 2 Versuche: POST https://openrouter.ai/api/v1/chat/completions,
//    Body {"model": m, "messages":[{role:"system",content:<prompt oder kDefaultSystemPrompt wenn leer>},
//    {role:"user", content:"Liedplan-Text:\n" + document_text}], "temperature":0, "max_tokens":3500};
//    Headers: Authorization Bearer, Content-Type application/json,
//    HTTP-Referer: http://localhost, X-Title: OBS-Textbox. Timeout 70 s pro Request.
//    on_progress-Meldungen (deutsch) wie v2: "Ermittle verfügbare freie Modelle...",
//    "Modell <m> (Versuch <n>/2)...", "Warte 3s vor neuem Versuch...".
//    Fehler-Status: HTTP-Fehler → msg aus JSON error.message (fallback "HTTP <status>");
//    retryable = status ∈ {429,502,503,504,524,529}; leere Antwort → retryable.
//    Nicht-retryable → nächstes Modell; retryable → 3 s warten, nächster Versuch.
// 3. Erfolg: parse_json_clean → {"items":[{zeile1,zeile2,zeile3}]} (items-Array nicht leer,
//    sonst Fehler wie v2) → enrich_titles_from_reference(items, parse_song_reference(lieder.md))
//    → return {items, modelUsed (aus Antwort-JSON "model", fallback = angefragtes Modell)}.
// 4. Alle Kandidaten gescheitert → throw "Alle AI-Modelle fehlgeschlagen:\n" + letzte 4 Errors.

std::map<std::string, std::pair<std::string,std::string>> parse_song_reference(const std::string &md);
// 1:1 v2 `parseSongReference`: Zeilen; Header = erste Pipe-Zelle "Nr" (case-insensitiv) → danach
// parsen, sonst ab Zeile 0; nur Zeilen beginnend mit '|'; Separator-Zeilen (nur [ -|]) überspringen;
// split('|') → number=parts[1], title=parts[2] mit entferntem optionalem suffix " (Wort)" am Ende,
// rubrik=parts[4]; key=number → {title, rubrik}.

void enrich_titles_from_reference(std::vector<AiPoint> &items,
                                  const std::map<std::string, std::pair<std::string,std::string>> &ref);
// 1:1 v2: Base-Lookup key→key-auf-','-gekürzt (nur wenn title nicht leer, erster Eintrag gewinnt);
// pro Item mit leerem zeile3: erstes \b\d{1,3}\b in zeile2 → Base-Lookup → zeile3 füllen.

std::vector<AiPoint> parse_liedplan_locally(const std::string &text);
// 1:1-Port von v2 `parseLiedplanLocally` (pdf-ai-importer.js Zeilen ~330–399):
// PP_KEYWORDS-Liste exakt (29 Begriffe), isHeader-Regeln (leer / \bliedplan\b / \bwoche\b /
// \borganist\b / Tagesangabe am Zeilenanfang), GL-Nummern-Regex
// \b\d{1,3}\b(?:(?:[.,;]\s*[\d+\-– ]*[\d]))?  (std::regex ECMAScript), Keyword-Zeilen
// (lower === kw oder lower beginnt mit kw + ' '), Fortsetzungszeilen an zeile3 anhängen
// (Leerzeichen-getrennt), am Ende trimmen + Items mit allen leeren Feldern filtern.
}
```

**AI-Import-Flow im Dock (1:1 v2 `handleAiUpload`, modals.js Zeilen 596–689):**
1. Datei wählen (PDF/DOCX) → `extract_document_text` (Sidecar). Leerer Text → Fehler.
2. Key vorhanden? (Eingabefeld, vgl. §5) → `process_with_llm`; bei Exception →
   `parse_liedplan_locally(text)`; Ergebnis leer → originalen LLM-Fehler melden.
   `model_label = "Offline-Fallback"`.
3. Kein Key → direkt `parse_liedplan_locally`; leer → Fehler
   "Weder API-Key vorhanden noch konnte der lokale Parser Programmpunkte erkennen."
   `model_label = "Lokal (ohne API-Key)"`.
4. Items leer → Fehler "Keine Programmpunkte erkannt."
5. **Preview** anzeigen: Tabelle (Zeile 1/Zeile 2/Zeile 3) + Quellen-Label
   `AI-Import (<model_label>)` + Button "Übernehmen".
6. **Übernehmen:** erste `min(items.size(), cards.size())` Karten IN PLATZ befüllen
   (zeile1/2/3) — dieselbe Semantik wie der M3-Datei-Import. Keine Karten im State →
   zuerst 12er-Vorlage anlegen (wie M1-Vorlage), dann befüllen. Mehr Items als Karten →
   Rest ignorieren, Status "Übernommen: N Punkte (M weitere ignoriert)". Danach
   `save_state` + `update_everywhere`, Dialog schließen, Statuszeile
   "AI-Import: N Punkte (Quelle: <label>)".

## 5. D3 — Import-Dialog-Erweiterung (liedplan-import.{hpp,cpp})
Im bestehenden M3-Import-Dialog (Button "📥 Import") die **dritte Option** ergänzen:
- Dialog-Struktur wie gehabt (CSV/Datei importieren, Neueste Antwort laden) + NEU:
  **"🤖 AI-Import (PDF/Dokument)"** (Button oder Radio, nach den vorhandenen).
- AI-Panel:
  - Datei-Picker (Filter: `Dokumente (*.pdf *.docx);;Alle Dateien (*.*)`)
  - **"API-Key":** `QLineEdit` (EchoMode Password, toggle-bar "👁"), vorbelegt aus
    `read_env_key(settings.openrouter_key_path)` (leer, wenn Datei/Key fehlt — dann läuft der
    lokale Parser, Statuszeile "Kein API-Key – lokale Offline-Analyse")
  - **"System-Prompt":** einklappbare `QPlainTextEdit` (Default `kDefaultSystemPrompt`,
    "Zurücksetzen"-Button) → bei Start in `settings.ai_system_prompt` persistieren
  - **"Starten"** → Worker-Thread-Flow aus §4, Fortschritt in Statuszeile +
    `on_progress`-Texte live im Dialog (Qt-Signale; UI-Thread nie blockieren)
  - Fehler → deutsche Meldung im Dialog (QMessageBox oder Statuszeile), Dialog bleibt offen.
- M4-Settings-Dialog ("⚙ Einstellungen") um die 5 neuen Settings-Felder erweitern
  (QLineEdit je Feld: Key-Pfad, Python-Pfad, Anim-Ordner, lieder.md-Pfad — System-Prompt nur im
  AI-Panel), deutsche Labels, in `state.settings` speichern.

## 6. CMake / Build
- NEUES Test-Executable (analog `liedplan-import-tests`):
  ```cmake
  add_executable(liedplan-ai-tests src/tests/test-liedplan-ai.cpp src/liedplan-ai.cpp src/liedplan-ai.hpp)
  target_link_libraries(liedplan-ai-tests PRIVATE nlohmann_json::nlohmann_json winhttp)
  add_test(NAME liedplan-ai COMMAND liedplan-ai-tests)
  ```
  (`liedplan-ai.cpp` darf NICHT obs-Header einbinden, sonst würde das Test-Executable OBS brauchen.)
- `liedplan-ai.{hpp,cpp}` in die Plugin-`target_sources` hängen.
- **Build-Befehle (exakt, s. Skill obs-textbox):**
  ```
  cmake --preset windows-x64
  cmake --build --preset windows-x64 --config RelWithDebInfo
  ```

## 7. Tests (CTest — offline, OHNE OBS, OHNE Netzwerk; keine Skip)
**Bestehende Suites bleiben GRÜN** (Regression, 1:1-Referenz-Checks): `liedplan-state`,
`liedplan-textfmt`, `liedplan-import`, `liedplan-render` (22 Checks + neue Anim-Checks).
Erwartet nach M5: **ctest 5/5** (neue Suite `liedplan-ai` dazukommt).

**NEU `test-liedplan-ai.cpp`** (Fixtures exakt aus `rewrite/tests/test_js_modules.js` Zeilen 98–139):
1. `parse_liedplan_locally(sampleText)` (sampleText = JS-Fixtur "Liedplan St. Johannes /
   Woche… / Sonntag / Eröffnung 144, 1+4+6 …" 1:1): `items.size() >= 4` UND
   `items[0] == {"Eröffnung", "144, 1+4+6", "Nun jauchzt dem Herren"}`.
2. `parse_song_reference(liederSample)` (JS-Fixtur mit "Nr"-Header): exakt 2 Keys
   {"31,1","144"}; `songRef["31,1"] = {"Selig der Mensch, der seine Freude hat (Kv zu Ps 1)", "Psalmen"}`;
   `songRef["144"].title == "Nun jauchzt dem Herren, aller Welt mit Freuden"`; KEIN Key "Nr".
3. `parse_song_reference(liederNoHeader)` (JS-Fixtur ohne Header): 2 Keys {"5","6"},
   Titles korrekt, Separator-Zeile übersprungen.
4. `enrich_titles_from_reference`: Item {zeile2:"144, 1+4+6", zeile3:""} + Ref aus Test 2 →
   zeile3 wird "Nun jauchzt dem Herren, aller Welt mit Freuden"; Item mit unbekannter GL-Zahl
   bleibt leer; Item mit bereits gefülltem zeile3 bleibt unverändert.
5. `parse_json_clean`: (a) "```json\n{\"items\":[…]\n```" → parsed ok; (b)
   "Hier der Plan: {…} Ende" → parsed ok (erstes '{' bis letztes '}'); (c) "kein json" → throw.
6. `discover_free_models` (statistische Fixtur, KEIN Netzwerk): JSON mit z. B.
   {"data":[{"id":"google/gemma-4-31b-it:free","context_length":1000},
   {"id":"nvidia/nemotron-3-super-120b:free","context_length":2000},
   {"id":"x/y-vl:free","context_length":9000},
   {"id":"a/b-content-safety:free","context_length":8000},
   {"id":"c/d-2.6b-instruct:free","context_length":7000},
   {"id":"mistralai/m:free","context_length":3000}]}
   → Ergebnis enthält KEINE "-vl"/"content-safety"/"2.6b"-Ids; Reihung: gemma-4-31b zuerst,
   nemotron zweitens, mistralai/m danach.
7. `read_env_key`: Temp-Datei mit Zeile `OPENROUTER_API_KEY=sk-or-dummy-test-123` →
   genau `"sk-or-dummy-test-123"`; nicht existierender Pfad → `""` + nicht-leerer `err`;
   Datei ohne Key-Zeile → `""`.
8. `parse_liedplan_locally("")` → leer; `parse_liedplan_locally("nur Kopfzeilen\nLiedplan X\nWoche Y")` → leer.
9. `normalize_anim_src` (in `test-liedplan-render.cpp` ODER hier, je nach Link-Bedarf):
   (a) `"C:\\Users\\J\\animation\\1_advent.gif"` → `"file:///C:/Users/J/animation/1_advent.gif"`;
   (b) `"https://example.com/a.webm"` → unverändert; (c) `"1_advent.gif"` + settings mit
   animation_dir, in dem die Datei existiert (Test-Fixtur-Verzeichnis) → file-URL;
   (d) nicht existierender Bare-Name → unverändert; (e) Pfad mit Space → `%20`.
10. Sidecar `extract_document_text`: (a) mit vorhandenem `settings.markitdown_python` (Repo-Venv,
    liegt auf dieser Maschine): Temp-Datei `test_doc.txt` mit Inhalt "Liedplan Test 144, 1+4+6" →
    Ergebnis enthält "144"; (b) nicht vorhandene Python-Exe → `""` + `err` enthält
    "MarkItDown-Python nicht gefunden". (Kein Netzwerk, keine OBS.)

**NEU in `test-liedplan-state.cpp`:**
11. Anim-Roundtrip: Karte mit allen 5 Anim-Feldern gesetzt → serialisieren → laden → `==`.
12. Backward-Compat: State-JSON OHNE Anim-Felder (alte Datei) → lädt, Felder = Defaults,
    keine Crash/`[]`-Fehler. Settings-JSON OHNE die 5 neuen Keys → Defaults.

**NEU in `test-liedplan-render.cpp` (INJECT-CSS, zu den 22 bestehenden Checks):**
13. `build_inject_css(card)` mit anim_state {anim_file:"C:\\x\\a.gif", show_anim:true,
    anim_scale:4, anim_x:-1200, anim_y:50} + Settings → CSS enthält exakt:
    `--anim-file: "file:///C:/x/a.gif";`, `--show-anim: 1;`, `--anim-scale: 4;`,
    `--anim-x: -1200px;`, `--anim-y: 50px;`. (Escape-Check: Filename mit `"` wird maskiert.)
14. Card ohne Anim (Defaults) → `--anim-file: "";`, `--show-anim: 0;` (alte Zeilen bleiben).

**Test-Pfad-Regel:** Sample-/Fixtur-Pfade via CMake `target_compile_definitions` auf absolute
Pfade (Muster aus dem bestehenden `test-liedplan-import.cpp`) — KEIN relativer Pfad, der je
nach CWD bricht.

## 8. Deploy + OBS-Start (agy macht das, Hermes verifiziert)
1. Build (oben) → DLL `build_x64/RelWithDebInfo/liedplan-for-obs.dll`.
2. **OBS killen** (genehmigt für Deploy-Runs): `taskkill /IM obs64.exe /F`.
3. DLL → `C:\Program Files (x86)\Steam\steamapps\common\OBS Studio\obs-plugins\64bit\`
   (NUR `obs-plugins\64bit\`, NICHT `bin\64bit`).
4. **OBS starten mit korrektem CWD** (CWD-Pitfall, verifiziert 2026-10-26):
   ```
   Start-Process -FilePath 'C:\Program Files (x86)\Steam\steamapps\common\OBS Studio\bin\64bit\obs64.exe' -WorkingDirectory 'C:\Program Files (x86)\Steam\steamapps\common\OBS Studio\data\obs-studio'
   ```
5. **Log-Verifikation** (neueste Datei in `C:\Users\Johann\AppData\Roaming\obs-studio\logs\`):
   `plugin loaded successfully` + kein Plugin-Fehler. DIALOG_OFFEN-Erkennung wie M4
   (~45 s, RAM ≈ 40 MB oder keine `plugin loaded`-Zeile → `DIALOG_OFFEN` melden).
6. **Kein Commit.**

## 9. Akzeptanzkriterien (fertig, wenn ALLE wahr)
- [ ] Build RelWithDebInfo grün (keine Fehler/Warnungen-Regression).
- [ ] **ctest 5/5 selbst grün** (4 alte + neue `liedplan-ai`, kein Skip).
- [ ] `build_inject_css` emittiert die 5 echten Anim-Werte; `push_update` JSON befüllt;
      Normalisierung korrekt (Tests 9, 13, 14).
- [ ] `main/textbox.html` + Live-Listener UNVERÄNDERT (git status zeigt nur Allowlist-Dateien).
- [ ] Dock: "🎬 Animation"-Sektion pro Karte (Datei/📁, Skala, X/Y, Aktiv), v1-Look-QSS in
      `liedplan-theme.hpp`, deutsche Labels.
- [ ] Import-Dialog: "🤖 AI-Import" + Key-Feld + System-Prompt + Starten + Preview-Tabelle
      + Übernehmen; Settings-Dialog um 5 Felder erweitert.
- [ ] AI-Flow-Logik 1:1 v2 (Fallback-Chain, Modell-Priorität, Retry-Regeln, Enrichment).
- [ ] Deploy: DLL im OBS-Verzeichnis, OBS lädt Plugin, Log ohne Fehler (oder `DIALOG_OFFEN`).
- [ ] Kein File-Umzug, kein git, keine neue externe Abhängigkeit, keine Secrets in Dateien.

## 10. Referenzen (genau, 1:1 — NICHT neu ableiten)
- **AI-Logik (1:1):** `rewrite/dashboard/js/pdf-ai-importer.js` —
  `DEFAULT_SYSTEM_PROMPT` (Zeilen 7–21), `MODEL_PRIORITIES`/`FREE_EXCLUDE` (Zeilen 172–177),
  `discoverFreeOpenRouterModels` (179–198), `callOpenRouterModelOnce` (200–246),
  `parseJsonClean` (248–257), `enrichTitlesFromReference` (259–280), `processWithLLM` (282–325),
  `parseSongReference` (31–68), `parseLiedplanLocally` + `PP_KEYWORDS` + `isHeader` (330–399).
- **Flow-Referenz (1:1):** `rewrite/dashboard/js/modals.js` Zeilen 596–689 (`handleAiUpload`).
- **Test-Fixtures (1:1):** `rewrite/tests/test_js_modules.js` Zeilen 98–139 (sampleText,
  liederSample, liederNoHeader + erwartete Werte).
- **Animation-Rendering (Runtime, UNBERÜHRT):** `main/textbox.html` Zeilen ~121–193
  (`.anim-wrapper`, `--anim-*`-Variablen, img/video-Logik) + `plugin/src/liedplan-html.cpp`
  Zeilen ~146–150 (Listener setzt die 5 Variablen aus dem Update-JSON).
- **v2-UI-Referenz (Optik/Bedienung):** `rewrite/dashboard/js/cards.js` Zeilen 54–95
  (Animation-`<details>`: Datei, X/Y ±1200 step 5, Skala-Optionen, Aktiv) +
  `rewrite/dashboard/js/css-builder.js` Zeilen 40–54 (CSS-Variablen-Namen/Formate).
- **State/Theme-Referenz:** `plugin/src/liedplan-state.{hpp,cpp}` (Backward-Compat-Muster),
  `plugin/src/liedplan-theme.hpp` (v1-Tokens), `plugin/src/liedplan-dock.cpp` (Checkbox-Muster ~442).
- **Env-Key-Format:** `main/api.env` (Zeile `OPENROUTER_API_KEY=sk-or-...` — NUR LESEN, nie
  kopieren; `rewrite/server/config.py` Zeilen 73–80 als Parse-Referenz).
- **markitdown:** Repo-Venv `C:\Users\Johann\Desktop\GitHub\OBS_Textbox\.venv\Scripts\python.exe`
  (CLI: `python -m markitdown <datei>` → Markdown auf stdout; PDF + DOCX + TXT).

## 11. Was agy NICHT macht (Hermes übernimmt)
- Kein `git commit`/`git add` — Hermes committet nach eigener Verifikation.
- Kein E2E per UI-Klick (agy kann nicht klicken) — Hermes macht die Dock-E2E per computer_use:
  (1) A10: Anim-Sektion auf box1 → Datei `animation\1_advent.gif`, Skala 4x, X -50, Aktiv
  → On-Disk-Check `cards/box1.html` enthält `--anim-file: "file:///…/1_advent.gif"`,
  `--show-anim: 1`, `--anim-scale: 4`, `--anim-x: -50px` + State.json konsistent + (falls die
  CEF-Vorschau rendert) Pixel-Screenshot; (2) D3: "📥 Import" → "🤖 AI-Import" →
  `main/liedplan.pdf` → Preview (erwartet ~12 Punkte) → Übernehmen → 12/12 Karten + On-Disk.
  Hinweis: CEF-Vorschau ist auf diesem PC aktuell evtl. schwarz (bekanntes offenes Problem) —
  dann On-Disk + headless-Edge-Render als Beweis, Pixel-E2E nachträglich.
- Kein Commit der untracked `main/`/`rewrite/`/Root-Docs — die bleiben unverändert.
