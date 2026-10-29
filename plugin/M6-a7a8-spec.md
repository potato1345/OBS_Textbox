# M6-A7A8-Spec — Pro-Karte Text-/Box-Deckkraft + freie Farbwahl

**Status:** geplant (Auto-Continue 28.10)
**FUNKTIONSVERGLEICH-Items:** A7 (Text-/BG-Deckkraft pro Karte), A8 (freier Color-Picker Text + Box)

## Ausgangslage (bereits vorhanden — NICHT antasten)

- State-Felder `text_color`, `text_opacity`, `box_color`, `box_opacity` pro Karte existieren,
  sind serialisiert (liedplan-state.cpp L52–59 / L91–94) und werden gerendert:
  `build_inject_css` + `push_update` + `card_html` (liedplan-html.cpp) schicken
  `--box-bg`/`--text-color` als **rgba() mit Alpha** (= Deckkraft) und
  `--box-opacity` als Show/Hide (has_any_text — bleibt so).
- `main/textbox.html` wendet an: `background-color: var(--box-bg)`,
  `color: var(--text-color)`, `opacity: var(--box-opacity, 1)` → **Live-Pfad braucht
  keine Änderung.**
- Dock-UI hat bisher nur: Ecken-Farben A/B + Ecken-Deckkraft A/B.

## Ziel (reine Dock-UI, kein State-/Render-Code)

In `add_card_row()` (liedplan-dock.cpp), nach dem PROFIL-Dropdown:

1. **A8 — zwei Farb-Buttons** (Muster = ECKE A/B, QPushButton + Hex-Label + QColorDialog):
   - `lpTextColor`, Label **TEXT-FARBE**, initial `card.text_color` (Fallback `#000000`),
     Click → `QColorDialog::getColor` → `c->text_color` = hex, Button-Label = HEX (uppercase),
     `save_state()` + `liedplan_scene::update_everywhere`.
   - `lpBoxColor`, Label **BOX-FARBE**, initial `card.box_color` (Fallback `#ffffff`),
     Click → `c->box_color` analog.
2. **A7 — Deckkraft-Block** „TEXT-/BOX-DECKKRAFT“ (Muster = ECKEN-DECKKRAFT):
   - Zeile **TEXT**: `lpOpacityText`, QSlider 0–255, initial `card.text_opacity` → `c->text_opacity`.
   - Zeile **BOX**: `lpOpacityBox`, QSlider 0–255, initial `card.box_opacity` → `c->box_opacity`,
     Tooltip „0 = Box-Hintergrund unsichtbar (Text und Ecken bleiben sichtbar)“.
3. **PROFIL-Connect erweitern:** nach `c->text_color/box_color = Profilsatz` die Labels der
   beiden neuen Farb-Buttons aktualisieren (Profile bleiben „Reset auf Profilsatz“).
4. **QSS (liedplan-theme.hpp):** `#lpTextColor`/`#lpBoxColor` in die bestehenden
   `lpColorA`/`lpColorB`-Selector-Listen ergänzen (identischer Look).

## Verhalten

- Profil-Dropdown = Reset auf Profilsatz (Text- + Box-Farbe) — bleibt so, dokumentiert.
- `text_opacity 0` → Text transparent, Box sichtbar; `box_opacity 0` → Box-Hintergrund
  transparent, Text + Ecken sichtbar.
- `--box-opacity` (Show/Hide der Box) bleibt has_any_text-gesteuert — **nicht** mit
  `box_opacity` verwechseln.

## Tests

1. Build RelWithDebInfo grün (MSVC).
2. `ctest` voll grün (5 Suiten — State/Render unverändert; Regressionswächter).
3. Deploy + E2E (falls OBS läuft): Dock-Screenshot (neue Controls sichtbar) +
   GetSourceScreenshot mit Testwert (z. B. text_opacity 128) → danach Testwert
   wiederherstellen. Sonst: als nächsten Schritt dokumentieren.

## Doku

- FUNKTIONSVERGLEICH.md: A7/A8 → ✅ M6, Meilenstein-Tabelle + „Offen/Kandidaten“ aktualisieren.
