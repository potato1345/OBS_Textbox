# Ui2 (M2b): Pro-Karte Eckenfarben A/B + Deckkraft + Regenbogen + v1-Optik-UI

## Kontext
- Repo: `C:\Users\Johann\Desktop\GitHub\OBS_Textbox\plugin` (OBS-Plugin „Liedplan", T7, OBS 32.2.2).
- Stand: M2a-browser fertig (Karten = Browser-Source (CEF) pro Karte mit `main/textbox.html`,
  injizierter `:root`-CSS-Block + Listener-Script `liedplan_card_update` für Live-Updates ohne
  Reload), Ui1 fertig (Dock im v1-Look, QSS in `liedplan-theme.hpp`). Commits: `df8e5f4`,
  `d2b104a`, `ae4162b`.
- Ziel: v2-Parität für Ecken (aus `rewrite/dashboard/js/`): pro Karte **Eckfarbe A**,
  **Eckfarbe B**, **Deckkraft A/B** und **Regenbogen-Toggle** — State, Rendering (CSS-Injektion),
  Live-Update und Dock-UI (v1-Look) — plus **F1-Fix** aus `hermes-memory/notes/agy-m2a-browser-review.md`
  (Fallback-Listener-Schema ≠ push_update-Schema).

## Design-Entscheidungen (FIXIERT in dieser Spec — keine Abweichung)
1. **State** (`liedplan-state.hpp`, `LiedplanCard`):
   ```cpp
   std::string corner_color_a = "#fce647";
   std::string corner_color_b = "#fce647";
   int corner_opacity_a = 255; // 0..255
   int corner_opacity_b = 255; // 0..255
   bool  rainbow_corners  = false;
   ```
2. **JSON** (`liedplan-state.cpp`): save schreibt ALLE neuen Felder (Schlüssel: `corner_color_a`,
   `corner_color_b`, `corner_opacity_a`, `corner_opacity_b`, `rainbow_corners` — snake_case wie
   die bestehenden Felder). load: fehlendes Feld → Default (alte State-Dateien bleiben lesbar,
   keine Migration nötig).
3. **`build_inject_css`** (`liedplan-html.cpp`):
   - `--triangle-color-a: ` + `format_rgba(card.corner_color_a leere→"#fce647", 0xFCE647u, card.corner_opacity_a)`
   - `--triangle-color-b: ` + `format_rgba(card.corner_color_b leere→"#fce647", 0xFCE647u, card.corner_opacity_b)`
   - Ersetzt die heutige hartkodierte `kDefaultTriangleColor`/`kDefaultTriangleOpacity` (beide
     Variablen bekamen dasselbe hardcoded-Werte; ab jetzt separate A/B aus State).
   - **Kein** `--rainbow-corners`-CSS-Variablen-Overengineering: das v1-`textbox.html` liest
     die Variable nirgends — der Regenbogen wird per bedingtem CSS-Block + Platzhalter umgesetzt
     (Punkt 4). Nicht setzen.
4. **Regenbogen — v2-Mechanik 1:1 porten** (Referenz: `rewrite/dashboard/js/css-builder.js`
   Zeilen ~63-85 — dort lesen und 1:1 übernehmen; nur Selektoren-Adaption im Fallback erlaubt):
   - In der renderten Kartendatei (REPO-Pfad UND Fallback) steht IMMER ein
     `<style id="lp-rainbow">`-Platzhalter-Element:
     - REPO-Pfad: direkt NACH dem injizierten `<style>`-Block, VOR dem letzten `</head>`.
     - Fallback: im eigenen `<head>` des `card_html`-Templates, nach dem Haupt-`<style>`.
     - Inhalt: `rainbow_corners == true` → der Rainbow-CSS-Block; `false` → leer.
   - Rainbow-CSS für den REPO-Pfad (1:1 wie css-builder.js, Selektoren `.textbox-container`
     bzw. `.text-line`):
     ```css
     .textbox-container { background-image: none !important; }
     .textbox-container::before {
       content: "" !important; display: block !important; position: absolute; top: 0; left: 0;
       width: 50px; height: 50px; border-top-left-radius: 4px;
       background: linear-gradient(135deg, var(--triangle-color-a) 50%, transparent 50%);
       pointer-events: none; z-index: 0; animation: rainbow-rotate 12s linear infinite;
     }
     .textbox-container::after {
       content: "" !important; display: block !important; position: absolute; bottom: 0; right: 0;
       width: 50px; height: 50px; border-bottom-right-radius: 4px;
       background: linear-gradient(315deg, var(--triangle-color-b) 50%, transparent 50%);
       pointer-events: none; z-index: 0; animation: rainbow-rotate 12s linear infinite;
     }
     .text-line { position: relative; z-index: 1; }
     @keyframes rainbow-rotate {
       from { filter: hue-rotate(0deg); }
       to   { filter: hue-rotate(360deg); }
     }
     ```
   - Fallback-Template (`card_html`): gleicher Block, aber Selektoren an die Fallback-Struktur
     adaptiert: `.textbox-container` → `#box` (50px-Pseudoelemente, Corner-Radius 24px auf den
     betroffenen Ecken: `border-top-left-radius`/`border-bottom-right-radius`), `.text-line` →
     `#lines` (`position: relative; z-index: 1`). Gleicher `@keyframes rainbow-rotate`.
   - **Eine C++-Konstante pro Pfad** (z. B. `kRainbowCssRepo`, `kRainbowCssFallback`) — diese
     wird (a) in den Datei-Platzhalter geschrieben und (b) in das Listener-Script eingebettet
     (als JS-String-Literal, korrekt escaped). DOPPELUNG VERMEIDEN: gleiche Konstante an beiden
     Stellen. (Der CSS-Text enthält keine `</script>`-Sequenz → Einbettung ins `<script>` ist
     sicher; Backslashes/Anführungszeichen im JS-String-Literal escapen.)
5. **Live-Update** (`push_update` + `kListenerScript`):
   - `push_update`-Payload (JSON, QJsonObject — bleibt, Signatur UNVERÄNDERT, wird von
     `liedplan-scene.cpp` aufgerufen) ERWEITERN um:
     - `triangle_color_a` / `triangle_color_b`: rgba-Strings (format_rgba mit State-Color+Opacity)
     - `rainbow`: 0 oder 1
   - `kListenerScript` (Repo-Pfad-Listener) erweitern:
     ```js
     if (d.triangle_color_a != null) setVar("--triangle-color-a", d.triangle_color_a);
     if (d.triangle_color_b != null) setVar("--triangle-color-b", d.triangle_color_b);
     if (d.rainbow != null) applyRainbow(d.rainbow);
     ```
     plus `function applyRainbow(v) { var el = document.getElementById("lp-rainbow");
     if (el) el.textContent = v ? RAINBOW_CSS : ""; }` mit `RAINBOW_CSS` = eingebetteter
     JS-String aus der C++-Konstante (Punkt 4).
   - **F1-FIX (Fallback-Listener `applyCard` in `card_html`):** auf das push_update-Schema
     ausrichten:
     - `d.box_bg` (rgba-STRING) → `#box.style.background`; `d.box_opacity` (0/1) → `#box`
       display none bei `<= 0` (statt des alten hex-`box_color` + 0..255-Schemas).
     - `d.text_color` (rgba-STRING) → `#lines.style.color`.
     - `d.triangle_color_a/b` → `setVar` auf `#box` bzw. documentElement (damit
       `linear-gradient(…, var(--triangle-color-a), …)` greift — Variablen dort setzen, wo sie
       referenziert werden; im Fallback auf `#box` als inline `style.setProperty`).
     - `d.rainbow` → `applyRainbow` auf den Fallback-Platzhalter (gleicher Mechanismus).
     - Fallback-`#box` bekommt für die Ecken `background-image:
       linear-gradient(135deg, var(--triangle-color-a) 50%, transparent 50%),
       linear-gradient(315deg, var(--triangle-color-b) 50%, transparent 50%);`
       `background-size: 50px 50px, 50px 50px; background-position: top left, bottom right;
       background-repeat: no-repeat;` (wie `.textbox-container` im v1-HTML).
     - Die alten hex-basierten `hexToRgb`-Code-Pfade im Fallback-Listener entfernen (Schema
       wird NIE mehr hex senden).
   - Ecken-Color/Opacity-Änderungen = reine CSS-Variablen-Updates (live, kein Reload).
     Rainbow-Toggle = `textContent`-Schalt des Platzhalters (live, kein Reload). Die
     Kartendatei wird ohnehin bei jedem `update_everywhere` neu geschrieben (State-Sync) —
     das bleibt so, ändert aber nichts am Live-Pfad.
6. **Dock-UI** (`liedplan-dock.cpp`, v1-Look wie Ui1; neue Sektion zwischen PROFIL-Zeile und
   Aktionszeile, pro Karte):
   - `QLabel#lpFieldLabel` „ECKE A" + `QPushButton#lpColorA` (fixed height 34 wie die Edits;
     Text = aktueller Hex UPPERCASE, z. B. `#FCE647`; Base-Button-Stil). Klick →
     `QColorDialog::getColor(QColor(hex), this, „Eckfarbe A")` → isValid: State setzen,
     `save_state()`, `liedplan_scene::update_everywhere(*c, false)`.
   - „ECKE B" + `QPushButton#lpColorB` — analog.
   - „ECKEN-DECKKRAFT" mit zwei Slidern: `QSlider#lpOpacityA` (horizontal, range 0–255) und
     `QSlider#lpOpacityB` — `valueChanged` → State setzen, `save_state()`, `update_everywhere`.
     (Pro-Tick-Schreiben der kleinen HTML-Datei ist ok — KEIN Debouncing nötig.)
   - `QCheckBox#lpRainbow` „🌈 Regenbogen-Ecken" → `toggled` → State setzen, `save_state()`,
     `update_everywhere`.
   - Layout-Kompaktheit: Feld-Zeilen wie das bestehende `make_field_row`-Muster (Label oben,
     Control darunter, gap 4); Slider-Zeilen dürfen label+slider in einer HBox-Row sein,
     solange die v1-Tokens gelten. `QCheckBox`/`QSlider` in die QSS.
   - QSS-Zusätze in `liedplan-theme.hpp`:
     - `QSlider` (horizontal): groove bg `#323A44`, height 6px, radius 3px, keine border;
       handle `#3DAEE9`, 12px Kreis, Hover `#3DAEE9` (leuchtender/`#5cc0f0` ok), margin so der
       Handle nicht abgeschnitten wird.
     - `QCheckBox`: color `#EFF0F1`, spacing 8px; indicator 16px, bg `#323A44`, border 1px
       `#3C404D`, radius 4px; `:hover` border `#3DAEE9`; `:checked` bg+border `#3DAEE9`.
     - `QSlider::sub-page`/`add-page` dürfen beide neutral bleiben (kein gefüllter Fortschritt
       nötig) — einfach halten.
   - ALLES ÜBRIGE im Dock bleibt 1:1 (Text-Edits, Profil-Combo, Buttons, ID-Leiste).
     Neue Controls werden beim Row-Rebuild mit den State-Werten initialisiert
     (`rebuild_rows` liest `state_.cards` — neue Felder einfach aus dem Card lesen).
7. **CTest:**
   - `tests/render/test-liedplan-render.cpp` (Ziel `liedplan-render`):
     - `build_inject_css`: custom Corner-Colors/Opacity → exakte rgba-Strings im Block
       (z. B. `#ff0000`/128 → `rgba(255,0,0,0.502)`-Format wie `format_rgba` es liefert —
       exakten Wert aus der Implementierung ableiten, nicht raten); leere Color-Felder →
       Default `#fce647`.
     - `render_card_html` mit REPO-HTML (inkl. echtem `main/textbox.html`-Fallback-Pattern,
       wie Test 8h): `rainbow=false` → `<style id="lp-rainbow"></style>`-Platzhalter VOR
       `</head>` vorhanden und LEER, keine `rainbow-rotate`-Zeichenkette in der Datei;
       `rainbow=true` → Platzhalter enthält `@keyframes rainbow-rotate`,
       `.textbox-container::before` UND `.textbox-container::after` (12 s linear infinite);
       `.anim-wrapper` bleibt erhalten, Listener vor `</body>`, Injektion vor `</head>`
       (8h-Behauptungen dürfen nicht brechen).
     - `card_html` (Fallback): enthält `<style id="lp-rainbow">`, `rainbow=true` →
       `rainbow-rotate` + `#box::before`; `applyCard`/Listener enthält `box_bg`-Schema
       (String-Check: z. B. `d.box_bg != null`), altes hex-Schema (`d.box_color`) NICHT mehr
       vorhanden.
     - Escape-Tests (9a–9f) bleiben grün.
   - `src/tests/test-liedplan-state.cpp` (Ziel `liedplan-state`):
     - Roundtrip: Card mit nicht-default Werten (`corner_color_a="#123456"`,
       `corner_opacity_b=17`, `rainbow_corners=true`, …) → save → load → alle Felder gleich.
     - Alte-Datei-Test: handgeschriebenes JSON OHNEDIE neuen Felder (wie heute gespeichert)
       → load → Defaults (`#fce647`, 255/255, false), Card ansonsten unversehrt.
8. **`liedplan-html.hpp`-Doku** anpassen: Kommentar von `build_inject_css`/`push_update`/
   `render_card_html` auf das neue Verhalten bringen (Ecken aus State, lp-rainbow-Platzhalter,
   erweitertes Payload-Schema).

## Änderungen — ERLAUBTE DATEIEN (nichts anderes!)
1. `plugin/src/liedplan-state.hpp`
2. `plugin/src/liedplan-state.cpp`
3. `plugin/src/liedplan-html.hpp`
4. `plugin/src/liedplan-html.cpp`
5. `plugin/src/liedplan-dock.cpp`
6. `plugin/src/liedplan-dock.hpp` (nur falls neue private Helfer nötig)
7. `plugin/src/liedplan-theme.hpp` (QSS-Zusätze Sliders/Checkbox)
8. `plugin/src/tests/test-liedplan-state.cpp`
9. `plugin/tests/render/test-liedplan-render.cpp`
- **NICHT anfassen:** `liedplan-scene.*` (push_update-Signatur bleibt!), `liedplan-profiles.hpp`,
  `liedplan-card-source.*`, `liedplan-render.*`, `liedplan-textfmt.*`,
  `src/tests/test-liedplan-textfmt.cpp`, `plugin/CMakeLists.txt` (keine neuen Dateien → nicht nötig;
  NUR ändern, falls ein Ziel eine neue Quelle bräuchte — vorher im Report begründen), `main/`,
  `rewrite/`, Repo-Root, `plugin/build*`, `plugin/.deps`.
- **Anti-Umzugs-Klausel:** KEINE Datei im Repo bewegen, umbenennen, löschen oder neu anlegen.
  (Vorfall M1: agy verschob Root-Dateien ungemeldet nach `main/`.)
- **Kein git:** Nicht committen, nicht stagen — Hermes committet nach eigener Prüfung.
- **Keine OBS-UI-Interaktion** (kein Klicken), keine Screenshots, box13 NICHT migrieren.

## Build + Tests (exakt)
```bash
export PATH="/c/Users/Johann/tools/cmake-3.30.5-windows-x86_64/bin:$PATH"
cd "C:/Users/Johann/Desktop/GitHub/OBS_Textbox/plugin"
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
ctest --test-dir build_x64 -C RelWithDebInfo
```
- Build grün (0 Fehler; Warnungen im Report nennen).
- `ctest`: **3/3** — ALLE alten Checks bleiben grün (render: 22 + neue; state: 10 + neue),
  dazu die neuen Checks aus Punkt 7. Kein einziger alter Check darf rot/skipped werden.

## Deploy + OBS-Start (nach grünem Build)
1. OBS beenden: `taskkill /F /IM obs64.exe` („läuft nicht" = ok). Warten bis
   `tasklist | grep obs64` leer ist (max. 15 s, sonst nochmal taskkill).
2. DLL kopieren:
   `cp "C:/Users/Johann/Desktop/GitHub/OBS_Textbox/plugin/build_x64/RelWithDebInfo/liedplan-for-obs.dll" "C:/Program Files (x86)/Steam/steamapps/common/OBS Studio/obs-plugins/64bit/liedplan-for-obs.dll"`
   (Steam-Verzeichnis user-schreibbar; Fehlschlag → exakte Meldung in Report, KEIN UAC-Admin-Hack.)
3. OBS starten (WorkingDirectory ist wichtig):
   `powershell.exe -NoProfile -Command "Start-Process -FilePath 'C:\Program Files (x86)\Steam\steamapps\common\OBS Studio\bin\64bit\obs64.exe' -WorkingDirectory 'C:\Program Files (x86)\Steam\steamapps\common\OBS Studio\bin\64bit'"`
4. 45 s warten, dann:
   - `tasklist | grep obs64` (PID).
   - **RAM-Check** (Dialog-Erkennung): `powershell.exe -NoProfile -Command "(Get-Process obs64).WorkingSet64 / 1MB"`
     → gesund ≈ 430–500 MB. **≈ 40 MB = „Crash detected"-Dialog offen** (nach taskkill /F
     normal). In dem Fall: KEIN Weitermachen, Report mit Status `DIALOG_OFFEN` (Hermes klickt
     „Im normalen Modus starten" selbst), Rest der Schritte 5–6 ausführen.
5. Log verifizieren (neueste Datei in `C:\Users\Johann\AppData\Roaming\obs-studio\logs\`):
   - `[liedplan-for-obs] plugin loaded successfully`
   - `Liedplan dock registered (ok=1)`
   - KEIN „crash", KEIN „failed to load" für Liedplan/browser
   - Szene „Szene" lädt (box1/box2/box3 = browser_source, box13 = alter `liedplan_card`-Typ
     → bleibt schwarz, Migration NICHT auslösen!)
6. OBS **laufen lassen** (nicht beenden) — Hermes macht danach Screenshots.

## Report (deutsch, am Ende)
- Geänderte/neue Dateien (mit je 1 Zeile, was geändert wurde).
- Build-Status + ctest-Ergebnis (3/3 mit Check-Zahlen alt/neu).
- Deploy: DLL-Timestamp im Zielordner, OBS-PID, RAM-Größe, Log-Auszug (erwartete Zeilen).
- Status: `GRUEN` (alles ok) oder `DIALOG_OFFEN` (OBS am Crash-Dialog) oder `FEHLER` + genaue
  Ursache.
- Auffälligkeiten/Warnungen aus dem Build.
