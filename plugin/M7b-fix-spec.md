# M7b: Dock-Fixes (User-Feedback 30.10, nach M7 `18987fb`)

## Kontext
M7 (Icon-Toolbar unten, Such-Dropdown, einklappbare Style-Optionen, schmalere Ränder) ist fertig und committed (`18987fb`). Der User sieht im LIVE-Dock vier Probleme — diese exakt beheben. Repo-Root: `C:\Users\Johann\Desktop\GitHub\OBS_Textbox`.

Betroffene Dateien: `plugin/src/liedplan-dock.cpp`, `plugin/src/liedplan-dock.hpp`, `plugin/src/liedplan-theme.hpp` (+ `plugin/src/tests/*.cpp` nur falls nötig).

## Problem 1: Rand der einzelnen Boxen zu fett
**Ist:** `lpCardOuter` = Hintergrund `#3C404D` + `border: 1px solid #3C404D` + `outer_layout` margins `(1,1,1,1)`; `lpCardInner` = Hintergrund `#272A33` + `border: 1px solid #3C404D` (`border-top: none`). Doppelter Aufbau → sichtbarer grauer Rahmen ~2–3 px um jede Karte.
**Soll:** Exakt **ein 1-px-Rand** um jede Karte:
- QSS `QFrame#lpCardOuter { background-color: transparent; border: 1px solid #3C404D; border-radius: 4px; }`
- QSS `QFrame#lpCardInner { background-color: #272A33; border: none; border-radius: 0px 0px 3px 3px; }` (Hover-Regel `#323540` behalten)
- `outer_layout->setContentsMargins(0, 0, 0, 0)` (war `(1,1,1,1)`)
- `lpIdBar` unverändert (Hintergrund `#3C404D`)
- Flash-QSS `QFrame#lpCardOuter[lpFlash="true"] { border: 1px solid #476BD7; }` bleibt (greift weiterhin auf den 1-px-Rand)
**Abnahme:** Karte = `#272A33`-Panel mit genau 1-px `#3C404D`-Rahmen, kein zusätzlicher grauer Ring; Hover wie vorher.

## Problem 2: Doppelte "Entfernen"-Buttons (zwei pro Box + einer unten)
**Ist:** Pro Karte gibt es (a) ✕-Button in der ID-Leiste (`lpDelBtn`, close.svg, Tooltip „Karte löschen") → ruft `state_.remove_card(card_id)` UND `liedplan_scene::remove_everywhere(card_id)` (Karte UND Szenen-Quelle werden gelöscht); (b) Button „Entf." in der Actions-Zeile (`remove_src_btn`, lpBtn, Tooltip „Quelle aus allen Szenen entfernen") → nur `remove_everywhere`; (c) unten in der Toolbar der Trash-Button (`tool_clear` = „Alle löschen").
**Fix:** Button (b) **komplett entfernen** — funktional redundant, weil (a) bereits beides löst.
- In `create_row_widget`: `remove_src_btn` löschen, zugehörigen `connect` (Tooltip „Quelle aus allen Szenen entfernen") mitlöschen; Actions-Zeile behält genau „Zu Szene" (`lpBtnGreen`) + „Leeren" (`lpBtnRed`), je stretch 1.
- (a) ✕ in der ID-Leiste und (c) Trash unten bleiben unverändert.
- QSS für `remove_src_btn`-spezifische Selektoren entfernen, falls vorhanden (globaler `lpBtn`-Selektor bleibt).
**Abnahme:** Pro Karte genau EIN Entfernen-Steuerung (✕ in ID-Leiste) und genau EIN „Alle löschen" (Trash unten). Es existiert kein weiterer Button, der Karten/Quellen entfernt.
**Hinweis:** Tests referenzieren `remove_src_btn`/„Entf." nicht (verifiziert). Falls CTest-Checks trotzdem bröckeln: an die neue Struktur anpassen, keine Checks für andere Funktionen streichen.

## Problem 3: Kein dünner Fensterrand am ganzen Dock
**Ist:** Das Dock (`QWidget#liedplanDock`, `WA_StyledBackground = true`) hat keine sichtbare äußere Begrenzung.
**Soll:** Dünn sichtbarer **1-px-Rand um das gesamte Dock-Fenster**:
- QSS `QWidget#liedplanDock { background-color: #1D1F26; border: 1px solid #464B59; }` (OBS-Yami grey3 — auf hellem Desktop sichtbar, dezent)
- `root_layout` margins bleiben `(4,4,4,4)` (QSS-Border liegt außerhalb des Layouts)
**Abnahme:** Um das Dock-Fenster herum ist ein 1-px-Rand sichtbar.

## Problem 4: Style-Optionen nicht (mehr) sichtbar
**Ist:** M7 macht die Style-Sektion pro Karte einklappbar mit **Start-Zustand eingeklappt** (`style_section->setVisible(false)`); der kleine Chevron-Toggle (`lpStyleToggle`, 28×28, autoRaise) wird vom User nicht gefunden → „ich sehe die Styling Optionen auch nicht mehr".
**Fix:** **Start-Zustand = ausgeklappt (sichtbar):**
- `style_section->setVisible(true)` (statt `false`)
- Start-Icon `chevron_down.svg`, Start-Tooltip „Stil-Optionen einklappen"
- Toggle bleibt bidirektional: Klick klappt ein (→ `chevron_right.svg` + Tooltip „Stil-Optionen ausklappen"), zweiter Klick klappt auf.
**Abnahme:** Beim Laden des Docks sind die Style-Optionen (PROFIL/„Profil +", TEXT-FARBE, BOX-FARBE, ECKE A/B, ECKEN-DECKKRAFT, TEXT-/BOX-DECKKRAFT, Regenbogen-Ecken, Animation) auf jeder Karte **ohne Klick sichtbar**; Ein-/Auskappen per Chevron funktioniert weiter in beide Richtungen.

## Build + Tests (Pflicht, exakt diese Kommandos)
1. Build:
   `C:\Users\Johann\tools\cmake-3.30.5-windows-x86_64\bin\cmake.exe --build --preset windows-x64 --config RelWithDebInfo`
   (Working-Directory: `C:\Users\Johann\Desktop\GitHub\OBS_Textbox\plugin` — die `CMakePresets.json` liegt in `plugin/`, NICHT im Repo-Root) → Fehlerfrei.
2. Tests:
   `C:\Users\Johann\tools\cmake-3.30.5-windows-x86_64\bin\ctest.exe --test-dir "C:\Users\Johann\Desktop\GitHub\OBS_Textbox\plugin\build_x64" -C RelWithDebInfo`
   → **alle Suiten grün, erwartet 6/6** (vorhandene Checks bleiben grün, nur Struktur-Checks bei Bedarf anpassen).

## Deploy
Erst `tasklist | findstr /i obs64.exe` prüfen.
- **OBS läuft NICHT:** `plugin\build_x64\RelWithDebInfo\liedplan-for-obs.dll` nach `C:\Program Files (x86)\Steam\steamapps\common\OBS Studio\obs-plugins\64bit\` kopieren → Report `DEPLOYED` + SHA256 der DLL.
- **OBS läuft:** NICHT kopieren (läuft weiter) → Report `DEPLOY_SKIPPED_OBS_RUNNING` (Hermes verifiziert SHA256-Gleichheit später nach OBS-Neustart).

## kVersion / Icons
Keine Icon-Änderung → **kein** `kVersion`-Bump, keine neuen SVGs, `icons-version.txt` anrühren.

## Constraints (strikt)
- **Datei-Allowlist (NUR diese dürfen angefasst werden):** `plugin/src/liedplan-dock.cpp`, `plugin/src/liedplan-dock.hpp`, `plugin/src/liedplan-theme.hpp`, `plugin/src/tests/*.cpp` (nur wenn nötig).
- **Kein Git:** kein `git add/commit/stash/checkout` — Hermes committet selbst nach eigenständiger Verifikation.
- **Anti-Move-Klausel:** Keine Datei anlegen, verschieben, umbenennen oder löschen (außer: `plugin/src/tests/*.cpp` dürfen angepasst werden). Keine neuen Header/Dateien.
- Keine Änderungen an State-Schema, Persistenz, Import, AI, Profilen, Scene-Logik (außer der entfernten `remove_src_btn`-Klammerung in dock.cpp).

## Finaler Report (im Abschluss)
Pro Problem: geänderte Dateien + konkrete Stellen (Zeilen), Build-Status, ctest-Ergebnis (X/6), Deploy-Status (DEPLOYED + SHA256 / DEPLOY_SKIPPED_OBS_RUNNING).
