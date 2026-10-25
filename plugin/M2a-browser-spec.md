# M2a-Browser v2: Liedplan-Karten = OBS Browser-Source mit textbox.html

## Kontext & Architektur-Entscheidung (User, 25.10.2026)
Karten werden als **OBS Browser-Source** (CEF) gerendert – NICHT mehr über den Custom Source
`liedplan_card` (QPainter rendert schwarz, `video_render` wird nie aufgerufen).
Grund: alle Web-Features müssen nutzbar sein (GIF-Animationen, Ecken-Dreiecke A/B,
Regenbogen-Corner, CSS-Animationen).

**Wichtig: Das Projekt hat bereits ein Browser-Textbox-System** (`main/textbox.html`,
7870 Byte, vom User seit August produktiv im Einsatz – siehe Source "Browser" in der Szene "Szene"):
- Text wird über CSS-Variablen gerendert: `--zeile-1/2/3` (via `::before content`),
  Box `--box-bg`, Text `--text-color`, Fade `--box-opacity`, Ecken-Dreiecke
  `--triangle-color-a/b` (Gradient im Container-Hintergrund), Animation
  `--anim-file/--show-anim/--anim-scale/--anim-x/--anim-y` (GIF/webm/MP4 in
  `.anim-wrapper`), `--box-id`.
- Die Seite pollt `getComputedStyle(document.documentElement)` **alle 150 ms**
  (`updateLines()`): leere Zeilen werden ausgeblendet, Anim-Wrapper wird
  angelegt/zurückgesetzt. ⇒ Wer die CSS-Variablen zur Laufzeit ändert, wird ohne
  Reload angezeigt.
- Production-Dashboard (`main/controlui/control.js`): schreibt bei jeder Änderung das
  `css`-Property der Source (Block `OBS_TEXTBOX_INJECT_START/END`) → Browser-Reload.
  Das Format dieses Blocks ist die Referenz (unten 1:1 wiedergegeben).

**Unser Ansatz (besser als Dashboard-Reload, gleiche Visuals):**
Per Karte wird eine **Kopie** von `main/textbox.html` nach
`<obs-data>/plugins/liedplan-for-obs/cards/<id>.html` geschrieben, die zusätzlich enthält:
1. ein `<style>`-Block mit dem `:root`-Injektionsblock (aktueller Karten-Zustand) –
   dadurch ist der Dateizustand IMMER aktuell (Reload/OBS-Restart zeigt korrekten Stand).
2. ein kleines `<script>`-Listener, der das `javascript_event`
   `liedplan_card_update` empfängt und die CSS-Variablen live per
   `documentElement.style.setProperty(...)` setzt (die 150-ms-Poll zeigt es an –
   **kein Browser-Reload pro Keystroke**, kein Flackern).
Die Browser-Source zeigt per `local_file` auf die Kopie; das `css`-Property bleibt
LEER (bleibt konstant ⇒ keine Reloads durch Setting-Änderungen).
Repo-Pfad konstant: `C:/Users/Johann/Desktop/GitHub/OBS_Textbox/main/textbox.html`.
Falls die Datei fehlt: Fallback auf das eingebaute Basis-Template `card_html()`
(bereits implementiert) – Karte rendert dann ohne Anim-Wrapper/Dreiecke-System.

## Verifizierte Browser-Source-API (OBS 32.2.2, obs-browser-Submodul commit 3f0a2c)
Source-ID: `browser_source` (in der installierten obs-browser.dll registriert – verifiziert).
- `is_local_file` + `local_file` (string) → intern `http://absolute/<encoded path>`
  (Custom Scheme; Zugriff auf andere lokale Dateien möglich).
- `width`, `height` (int), `fps` (int), `shutdown` (bool; true = CEF wird beim
  Verstecken der Source zerstört ⇒ RAM, **immer true**).
- `css`: nicht setzen (bleibt leer).
- Update: gleiche Settings ⇒ kein Reload. Anderer local_file ⇒ DestroyBrowser.
- Proc-Handler `javascript_event(eventName, jsonString)`: jsonString MUSS gültiges
  JSON sein (nlohmann::json::parse mit disable-exceptions; ungültig → detail=null).
  JS-Seite: `window.addEventListener(name, ev => ev.detail)`.

## Aktueller Stand (AGY-Run heute, Build grün)
- `liedplan-html.hpp/cpp` existiert: `cards_dir()`, `card_html(card)` (eingebautes
  Basis-Template 800×400), `write_card(card, path_out)`, `push_update(src, card, clear)`
  (javascript_event "liedplan_card_update", JSON via Qt QJsonObject).
- `liedplan-scene.cpp` ist bereits browser-basiert: `make_settings` (is_local_file,
  local_file, width=800, height=400, fps=30, shutdown=true), `send_to_current_scene`
  mit Migration alter `liedplan_card`-Items, `update_everywhere` (nur
  browser_source-Items; write_card + push_update), `remove_everywhere`.
- `CMakeLists.txt`: `liedplan-html.cpp` im Plugin-Target; Test-Targets existieren
  (u. a. `liedplan-render-tests`), Build-Verzeichnis **`build_x64`**, Konfiguration
  RelWithDebInfo, CMake 3.30.5 (`C:/Users/Johann/tools/cmake-3.30.5-windows-x86_64/bin/cmake.exe`).
- `tests/render/test-liedplan-render.cpp`: Tests 8a–8e + 9a–9d für card_html/write_card.

## Aufgaben (Delta zu obigem Stand)

### 1. `liedplan-html.hpp/cpp`: `build_inject_css` + `render_card_html`
```cpp
// Exakt das Production-Format aus control.js (nicht-Rainbow-Variante):
//
// /* OBS_TEXTBOX_INJECT_START */
// :root {
//   --zeile-1: "<escaped>";
//   --zeile-2: "<escaped>";
//   --zeile-3: "<escaped>";
//   --anim-file: "";
//   --show-anim: 0;
//   --anim-scale: 3;
//   --anim-x: 0px;
//   --anim-y: 0px;
//   --box-bg: rgba(r,g,b,a);
//   --triangle-color-a: rgba(252,230,71,1);
//   --triangle-color-b: rgba(252,230,71,1);
//   --text-color: rgba(r,g,b,a);
//   --box-opacity: <0|1>;
// }
// html, body { background-color: rgba(0, 0, 0, 0) !important; }
// /* OBS_TEXTBOX_INJECT_END */
//
// Regeln:
// - zeile-N: String mit umgebenden Doppel-Anführungszeichen wie im Dashboard;
//   Rückwärtsschrägstrich, doppelte Anführungszeichen und < > & escapen.
// - --box-bg: card.box_color + card.box_opacity/255 als Alpha
//   (z. B. "#3C404D", 255 → "rgba(60,64,77,1)").
// - --text-color: card.text_color + card.text_opacity/255.
// - --triangle-color-a/b: M2a = Dashboard-Default "#fce647", Opacity 255
//   (Profile haben noch keine Corner-Farben – M2b erweitert das).
// - --box-opacity: 1 wenn mind. eine Zeile nicht leer, sonst 0
//   (Dashboard-Verhalten: leere Box fade-out).
// - anim-*: Defaults wie oben (M2b befüllt sie).
std::string build_inject_css(const LiedplanCard &card);
```
- Gemeinsamer Helper `hex_to_rgba(const std::string &hex, int opacity_0_255)`
  → "rgba(r,g,b,a)" (von build_inject_css UND push_update genutzt).

```cpp
// Rendert die komplette HTML-Datei der Karte (reines String, testbar):
// 1. repo_html leer → card_html(card) (bestehendes Fallback-Template).
// 2. sonst (Inhalt von main/textbox.html):
//    a) vor dem LETZTEN "</head>": einfügen
//       <style>\n + build_inject_css(card) + "\n</style>\n
//    b) vor dem LETZTEN "</body>": einfügen das Listener-Script unten.
// 3. Fehlt ein Marker (</head> oder </body>) im repo_html → Fallback card_html(card).
std::string render_card_html(const LiedplanCard &card, const std::string &repo_html);
```
`write_card` wird dünn: liest die Konstante
`kRepoTextboxPath = "C:/Users/Johann/Desktop/GitHub/OBS_Textbox/main/textbox.html"`
(UTF-8 lesen; std::filesystem::u8path), rendert via `render_card_html`, schreibt
nach `cards_dir()/<id>.html` (bestehendes Verhalten bleibt). Fehlt die Repo-Datei:
Fallback + LOG_INFO "Liedplan: textbox.html nicht gefunden, nutze Fallback-Template".

**Listener-Script (exakt, von render_card_html vor </body> einbauen):**
```html
<script>
// Liedplan-Plugin: Live-Updates ohne Reload (javascript_event → CustomEvent)
(function () {
    function setVar(name, value) {
        document.documentElement.style.setProperty(name, value);
    }
    window.addEventListener("liedplan_card_update", function (ev) {
        var d = ev.detail;
        if (!d) return;
        if (d.zeile1 != null) setVar("--zeile-1", '"' + String(d.zeile1) + '"');
        if (d.zeile2 != null) setVar("--zeile-2", '"' + String(d.zeile2) + '"');
        if (d.zeile3 != null) setVar("--zeile-3", '"' + String(d.zeile3) + '"');
        if (d.box_bg != null) setVar("--box-bg", d.box_bg);
        if (d.text_color != null) setVar("--text-color", d.text_color);
        if (d.box_opacity != null) setVar("--box-opacity", String(d.box_opacity));
        if (d.anim_file != null) setVar("--anim-file", '"' + String(d.anim_file) + '"');
        if (d.show_anim != null) setVar("--show-anim", String(d.show_anim));
        if (d.anim_scale != null) setVar("--anim-scale", String(d.anim_scale));
        if (d.anim_x != null) setVar("--anim-x", String(d.anim_x) + "px");
        if (d.anim_y != null) setVar("--anim-y", String(d.anim_y) + "px");
        // Die 150-ms updateLines()-Poll der Seite wendet die Variablen an
        // (Zeilen ein/ausblenden, Anim-Wrapper).
    });
})();
</script>
```

### 2. `push_update`: Payload erweitern
JSON-Felder: `zeile1, zeile2, zeile3, box_bg, text_color, box_opacity,
anim_file:"", show_anim:0, anim_scale:3, anim_x:0, anim_y:0`.
- `box_bg`/`text_color`: dieselben rgba()-Strings wie in `build_inject_css`
  (gemeinsamer Helper).
- `box_opacity`: 0/1; clear=true ⇒ alle zeileN = "" und box_opacity = 0,
  sonst hasText.

### 3. `liedplan-scene.cpp`: `make_settings`
- **`height` = 600** statt 400 (neue Konstante, z. B. `kBrowserCardHeight = 600`
  in scene.cpp oder liedplan-render.hpp). width bleibt 800.
  Grund: textbox.html hat `padding-top: 350px` (Platz für Animationen) – bei 400px
  Höhe läuft die Box aus dem Sichtbereich (README-Warnung). Die Production-Source
  läuft mit 800×600.
- `css`-Property: **nicht** setzen.
- Sonst unverändert (local_file, fps 30, shutdown true, Migration, push_update-Flow).

### 4. Tests (`tests/render/test-liedplan-render.cpp`)
Bestehende 8a–9d ANPASSEN + ergänzen. Repo-Inhalt testseitig per
std::filesystem aus `C:/Users/Johann/Desktop/GitHub/OBS_Textbox/main/textbox.html`
lesen (existiert auf dieser Maschine; falls nicht → Fallback-Pfade testen und
repo-Tests überspringen, nicht scheitern):
- 8a: card_html()-Fallback enthält die drei Texte (wie bisher).
- NEU: `build_inject_css(card)` (Testkarte: zeilen "GL 290"/"1-4"/"Großer Glaube",
  box "#3C404D" Opacity 255, text "#eff0f1" Opacity 255): enthält
  `--zeile-1: "GL 290"`, `--box-bg: rgba(60,64,77,1)`,
  `--text-color: rgba(239,240,241,1)`, `--box-opacity: 1`,
  `--triangle-color-a: rgba(252,230,71,1)`, `--show-anim: 0`,
  Marker `OBS_TEXTBOX_INJECT_START` und `OBS_TEXTBOX_INJECT_END`.
- NEU: alle Zeilen leer → `--box-opacity: 0`.
- NEU: `render_card_html(card, echterRepoInhalt)`: enthält den Injektionsblock
  (Marker) VOR `</head>`, das Listener-Script (enthält
  `liedplan_card_update`) VOR `</body>`, und `.anim-wrapper` (Repo-Inhalt erhalten).
- NEU: `render_card_html(card, "")` → Fallback: enthält `liedplan_card_update`
  UND die drei Texte.
- NEU: Escape: Zeile `An & <Bö>"se` → im Injektionsblock korrekt escaped
  (kein rohes `"` innerhalb des Strings, kein rohes `<`).
- 9e (bisher 8e): write_card() schreibt die Datei; Inhalt enthält Injektionsblock
  ODER (Fallback-Fall) Fallback-Template + Listener; Pfade wie bisher.

### 5. Build + Tests (agy führt aus)
```bash
export PATH="/c/Johann/tools/cmake-3.30.5-windows-x86_64/bin:$PATH"
cd "C:/Users/Johann/Desktop/GitHub/OBS_Textbox/plugin"
cmake --build build_x64 --config RelWithDebInfo
cd build_x64 && ctest -C RelWithDebInfo -V
```
- **NICHT** deployen, **NICHT** OBS starten/stoppen (OBS läuft, DLL belegt).
  Deploy + OBS-Restart macht Hermes danach selbst.

## Done-Kriterien
1. Build 0 Fehler; DLL `build_x64/RelWithDebInfo/liedplan-for-obs.dll` frisch erzeugt.
2. Alle ctest-Suiten grün (vollständige Ausgaben im Report).
3. `render_card_html` erzeugt aus echtem `main/textbox.html`: gültige HTML-Kopie mit
   Injektionsblock + Listener; Fallback ohne Repo-Datei funktioniert.
4. Report (deutsch): geänderte Dateien, Build-Status, ctest-Ausgaben, offene Punkte.

## Constraints
- Keine neuen Abhängigkeiten. Code-Stil: Tabs, deutsche Kommentare, GPL-Kopfzeile.
- Nicht anfassen: liedplan-dock.cpp, liedplan-card-source.cpp, liedplan-render.cpp,
  plugin-main.cpp, **main/textbox.html (READ-ONLY für das Plugin)**, main/controlui/,
  autocontinue-Dateien, alles außerhalb des plugin/-Verzeichnisses.
- Keine Konsole/Popups erzeugen; Änderungen nur in plugin/src/, plugin/tests/,
  plugin/CMakeLists.txt und dieser Spec-Datei.
