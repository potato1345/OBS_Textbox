# Spec IMP-F: Karten-Farbprofile trennen + Karten-Templates — OBS_Textbox

Datum: 2026-11-04. Repo: `C:\Users\Johann\Desktop\GitHub\OBS_Textbox`. Status: **Planung, noch nicht implementiert.** Zwei unabhängige Runden: **F1 (Profile)** und **F2 (Templates)**.

## Ausgangslage (IST — aus dem Code verifiziert)

Drei Systeme mit verwechselbaren Namen:

1. **Dock-Theme** (Plugin-Farben): `liedplan-theme.hpp` + `ui_profiles.json` (IMP-A) — Presets Dark/Light/OBS Dark für die Dock-Widgets, nur im Zahnrad.
2. **Karten-Farbprofile**: `liedplan-profiles.hpp` — Built-in-Palette mit Namen **„OBS Dark“, „OBS Light“**, Midnight Blue, Forest Green, Sunset Warm, High Contrast. Profil = nur `text_color` + `box_color`; Ecken/Deckkräfte sind nicht im Profil. Custom-Profile in `state.color_profiles`. `card.profile` (Default „OBS Dark“), `default_card()` (dock.cpp:179–185) kopiert Profilfarben in die Karte.
3. **Schnellprofile**: `state.quick_profiles` (Name + kompletter Karten-Snapshot), bedient vom editierbaren Top-Combo `profil_select_` (dock.cpp:246–279, Trash-Button).

**Problem:** Kartenprofile heißen exakt wie die Dock-Themes („OBS Dark“) und werden deshalb als Plugin-Farben gelesen; außerdem deckt ein Kartenprofil nur Text+Box (Ecken/Deckkraft/Regenbogen bleiben unprofilt).

## F1 — Farbprofile korrigieren (kleine Runde)

1. **Umbenennen der Built-in-Palette** (kartenbezogene Namen):
   „OBS Dark“ → **„Karte Dunkel“**, „OBS Light“ → **„Karte Hell“**, „Midnight Blue“ → **„Karte Mitternacht“**, „Forest Green“ → **„Karte Wald“**, „Sunset Warm“ → **„Karte Sonne“**, „High Contrast“ → **„Karte Kontrast“**.
   **Legacy-Alias beim Laden:** alter String „OBS Dark“ im State/Settings mappt transparent auf „Karte Dunkel“ (Migration in `LiedplanState::load`, Muster wie IMP-D); `profile_by_name` akzeptiert beide Namen.
2. **Profil erweitern** (`LiedplanProfile`): optionale Felder `corner_a`, `corner_b`, `corner_op_a`, `corner_op_b`, `text_opacity`, `box_opacity`, `rainbow` (Defaults wie heute: Ecken #FCE647, Opacity 1.0). Apply schreibt alle Felder in die Karte; bestehende 2-Feld-Profile bleiben gültig (Defaults greifen).
3. **UI-Klarstellung** (dock.cpp): Top-Combo = „Schnellprofile“ (Tooltip + Placeholder eindeutig); Per-Card-Label „PROFIL“ mit Tooltip „Karten-Farbprofil (Text/Box/Ecken/Deckkraft)“; Dock-Theme-Auswahl bleibt ausschließlich im Zahnrad mit Label „Dock-Theme“. Kein geteiltes Naming mehr.
4. **Serialization:** neue Profilfelder optional im YAML (`liedplan_profile_to_yaml` + `liedplan_yaml_to_profile`; fehlende Felder = Defaults); `operator==` erweitern. `color_profiles` ist bereits serialisiert.
5. **Tests:** Lookup neue Namen + Legacy-Aliase; YAML round-trip mit/ohne neue Felder; apply setzt alle Kartenfelder; Html-Suite bleibt grün (Rendering liest weiterhin nur card-Felder).

Erlaubte Dateien: `liedplan-profiles.hpp`, `liedplan-state.hpp/cpp`, `liedplan-dock.cpp`, `src/tests/test-liedplan-state.cpp` (bzw. neue Suite). Aufwand: 1 Runde.

## F2 — Generalisierung der gerenderten Box (Template-System)

Ziel: Karte ≠ fest „1 Box + 2 Dreiecke + 3 Textzeilen + Anim-Overlay“. Das gerenderte HTML-Dokument ist pro Karte austauschbar; Formen/Strukturen kommen aus Templates, nicht aus Plugin-Code.

### Architektur

1. **Templates = ganze HTML-Dokumente mit Platzhaltern** in
   `%APPDATA%/obs-studio/plugin_config/liedplan-for-obs/templates/<id>.html`.
   Token `{{...}}`, substituiert bei `write_card` (String-Replace, UTF-8-safe wie heute):
   `{{zeile1}} {{zeile2}} {{zeile3}} {{text_color}} {{box_color}} {{corner_a}} {{corner_b}} {{corner_op_a}} {{corner_op_b}} {{text_opacity}} {{box_opacity}} {{rainbow}} {{anim_src}} {{show_anim}} {{anim_scale}} {{anim_x}} {{anim_y}} {{card_id}} {{clear}}`.
   Farb-Tokens hex-validiert (wie `build_inject_css` heute); unbekannte/fehlende Tokens = No-op.
2. **Kartenfeld `template_id`** (Default `""` = „classic“):
   - **classic** = heutiger Weg unverändert (CSS-Injektion in `settings.textbox_path`) — Backward-Kompat ohne Migration.
   - **custom** = Template-Datei komplett gelesen, Tokens substituiert, als `boxN.html` geschrieben (keine CSS-Injektion; das Template kontrolliert sein eigenes CSS/SVG).
3. **Form-Freiheit im Template, nicht im Plugin:** Formen (abgerundet, Kreis, Rahmen, Mehrfach-Boxen, SVG-clip-path) sind CSS/SVG im Template. Plugin bleibt shape-agnostisch — keine Shape-Typen in C++.
4. **Live-Push bleibt reload-frei:** `push_update` sendet das feste JSON (`liedplan_card_update`, keys wie heute) plus `template_id`. Templates nach CSS-Var-Contract (`--text-color`, `--box-bg`, `--triangle-color-a/b`, Opacity-Vars) pushen wie classic; eigene Templates können eigenes Handler-JS enthalten. Template-Wechsel → Browser-Source-Reload.
5. **UI:** pro Karte Dropdown „Vorlage“ (classic + gefundene ids aus dem Ordner); Zahnrad: „Vorlagen-Ordner öffnen“ + „Vorlage aus classic kopieren“ (Starter = aktuelle classic-HTML als neue Datei). Editing extern; Re-Render beim Speichern der Karte.
6. **Fallback:** fehlende/defekte Template-Datei → classic-Pfad + Log-Warnung (Karte rendert immer).
7. **Migration:** `template_id` fehlt in alt-state.json → `""` (classic); Serialization symmetrisch wie `textbox_path` (leerer String überschreibt nicht); `operator==` erweitern.

### Built-in-Beispiel-Templates (via cmake install in templates/)

`classic.html` (Referenz, aktuelles textbox.html-Prinzip), `rounded.html` (abgerundete Box, keine Dreiecke), `frame.html` (nur Rahmen + Text). Alle mit vollständigen Tokens, dienen als Copy-Starter.

### Tests (neue Suite `test-liedplan-templates`)

- Token-Substitution vollständig + UTF-8 + unbekannte Tokens No-op
- Hex-Validierung (invalid → Fallback-Werte)
- fehlende Template-Datei → classic-Fallback
- State-Round-trip mit/ohne `template_id`; alt-state ohne Feld → classic
- push-Payload keys unverändert + `template_id` vorhanden

Erlaubte Dateien: `liedplan-html.hpp/cpp`, `liedplan-state.hpp/cpp`, `liedplan-dock.cpp`, `liedplan-card-source.cpp`, `plugin-main.cpp`, `CMakeLists.txt` (install templates/), `src/tests/`. Aufwand: 2 Runden (F2a Engine+Migration+Tests, F2b UI+Beispiel-Templates+Deploy/E2E).

## Reihenfolge & Risiken

- **F1 zuerst** (klein, behebt die Naming-Verwirrung sofort), dann F2.
- Risiko F2: Push-Contract für custom-Templates → gelöst über optionales Handler-JS im Template; classic-Contract bleibt Pflicht nur für classic.
- Keine Commits ohne Johanns OK; nach jeder Runde build + ctest + alle Testserien + Deploy/E2E wie üblich.
