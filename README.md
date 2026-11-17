# Textbox Studio — OBS-Plugin

Text-Karten als OBS-Overlays (Browser-Sources) mit einem Dock-Panel direkt in OBS:
Karten erstellen, bearbeiten, importieren und live ins Scene senden. Reines C++/Qt-Plugin
(C++17, Qt-freier Core) — kein Server, keine externen Prozesse außer dem optionalen
AI-Sidecar (Python).

Aktuelle Version: **1.2.0** · gebaut und getestet gegen **OBS Studio 32.2.2**
(Windows x64, macOS Universal, Linux x64).

## Features

- **Karten**: dynamische Anzahl (im Zustand gespeichert, z. B. 12), jede als eigene
  Browser-Source mit drei unabhängig positionierten Textzeilen
  (`zeile1/2/3`, je `x, y, w, h, fsize`).
- **Vorlagen (Templates)**: mitgelieferte Beispiel-Vorlagen im Vorlagen-Ordner
  (`templates/`), pro Karte per Feld **VORLAGE** einstellbar:
  - `standard.html` — klassische Karte (3 Zeilen, Rand-Ecken, Animation)
  - `display.html` — große Display-Karte (Zeile 1 dominant, Zahlen-Stil)
  - `minimal.html` — transparente Kompakt-Karte ohne Box-Hintergrund
  - `badge.html` — Pill-Badge mit Akzentfarbe und Bezeichnung
  Jede Vorlage nutzt die offiziellen `{{token}}`-Platzhalter (`zeile1..3`,
  `box_color`, `text_color`, `corner_a/b`, `anim_src`, `rainbow` …). Eigene Vorlagen:
  einfacher Name im Feld = Vorlagen-Ordner; voller `.html`-Pfad = direkte Datei.
- **16 Style-Properties** pro Karte: Schrift (`Arial`, `Comic Sans MS`, `Courier`),
  Farbe/Transparenz je Zeile, Box-Farbe/-Transparenz (0–100), Corner-Farbe/-Opacity,
  Animations-Size und Regenbogen-Ecken.
- **Animations**: relative Pfade, HTTP/HTTPS-URLs oder Data-URIs (Data-URIs werden
  als temporäre Datei materialisiert). Mitgeliefert: `data/animations/`
  (`sparkle.webp`, `pulse.webp` — transparent, loopend). Relative Namen werden im
  eigenen Animations-Ordner und im mitgelieferten Ordner gesucht.
- **Dock „Textbox Studio“**: Kartenliste mit allen Feldern, CSS-Regeln pro Karte,
  Schnell-Profile, Live-Vorschau (QWebEngineView), Undo/Redo; docked in OBS oder
  floating als natives Fenster (Titelbar, verschiebbar/resizable).
- **Toolbar**: Undo/Redo, Datenarchiv (⤓), Zahnrad-Menü (Schnell-Profile,
  Vorlagen-Ordner öffnen, Vorlage aus index.html kopieren, Backup erstellen/
  wiederherstellen, Einstellungen).
- **OBS-WebSocket**: externe Steuerung über die Events `textbox_card_update`
  (JSON mit allen Feldern einer Karte) und `textbox_clear`.
- **Persistence**: Einstellungen via OBS-Plugin-Settings; Karten-Zustand in
  `plugin_config/textbox-studio/textbox-state.json`; Templates zur Laufzeit injiziert.

## Datenarchiv-Dialog (Import-Setup)

Der Dialog (Toolbar-Icon ⤓) hat drei unabhängige Import-Wege:

### 1. Daten von URL (Google Forms / JSON)
- **FORM-ID**: Formular-ID (aus dem Formular-Edit-Link).
- **TOKEN-PFAD**: Pfad zur OAuth-Token-JSON (Standard: `hermes/google_token.json`
  unter `%LOCALAPPDATA%`); der Token wird automatisch refreshed.
- Google-seitig nötig: Cloud-Projekt + OAuth-Client (Desktop), Scope `forms`,
  Test-User, veröffentlichtes Formular. Der OAuth-Link nutzt
  `accounts.google.com/o/oauth2/v2/auth`.

### 2. Datei-Import (CSV / XLSX / ZIP)
- `.xlsx`, `.csv`, `.txt` (UTF-8 oder UTF-16LE mit BOM; Excel-Semicolon-Trennung)
  sowie ZIP-Archive mit solchen Dateien.
- Erwartetes Format: Blöcke mit den Spalten `Block N – Nr`, `Block N – Titel`,
  `Block N – Zeilen` (auch flache Nr/Titel/Zeilen-Tabellen).

### 3. AI-Import (PDF / DOCX)
- **Dokument**: PDF- oder DOCX-Datei.
- **API-KEY**: OpenRouter-API-Key — im Feld nur als Punkte maskiert; Key-Pfad
  (`api.env`, Feld `OPENROUTER-API-KEY-PFAD`) in den Einstellungen.
- **Sidecar**: lokales Python mit `markitdown` wandelt PDF/DOCX → Text
  (Python-Pfad in den Einstellungen). Ohne Key oder Python bleibt Starten ohne Effekt.
- **Master-Prompt** (an-ablebar): erzeugt pro Karte einen Eintrag mit bis zu drei
  Zeilen (Titel + Bezeichnung/Label, zeile2 = Nummer/Kürzel, sonst leer).
- Optional Referenztabelle: ergänzt Nummern zu Titeln.

## OBS-Kompatibilität

Gebaut gegen **OBS Studio 32.2.2** (libobs + obs-frontend-api aus `plugin/.deps/`).
Release-Assets tragen die Ziel-OBS-Version im Namen
(`textbox-studio-<ver>-obs-<OBSVER>-<plattform>.zip`); die Release-Notes listen die
getestete Version. `obs-frontend-api` ist seit OBS 28 ABI-stabil — ältere OBS-
Versionen funktionieren meist, sind aber nicht offiziell getestet. Andere OBS-
Version: Abhängigkeiten neu ziehen (CMake-Preset lädt die OBS-Version automatisch)
und neu bauen.

## Bau

Voraussetzungen: CMake ≥ 3.16, Git; OBS Studio und Abhängigkeiten werden beim ersten
Configure automatisch nach `plugin/.deps/` heruntergeladen.

```bash
cd plugin
cmake --preset windows-x64        # Linux: ubuntu-x86_64 / macOS: macos
cmake --build build_x64 --config RelWithDebInfo
```

Das DLL liegt danach in `build_x64/RelWithDebInfo/` bzw. unter
`build_x64/obs-studio/bin/64bit/`.

### Tests

```bash
ctest --test-dir build_x64 -C RelWithDebInfo
```

8 Test-Suiten decken den Qt-freien Core ab: Controls (Normalization und
Boundary-Guards), HTML-Rendering (CSS-Injektion, Padding-Schutz, JSON-Shape),
Tabellen-Import (Fixtures in `plugin/src/tests/samples/`), AI-Import (Stub-LLM ohne
Netzwerk) und der Qt6-Render-Test (echtes QWebEngine-Rendering mit Layout-Bounds).

### UI-Audit (Spacing & Theme)

`docs/DESIGN.md` ist die verbindliche Design-Spec (Spacing-Raster 4/6/8/12 px,
Theme-Tokens #1D1F26/#3C404D/#284CB8, Fenster-Metriken). Nach UI-Änderungen:

```bash
python tools/ui_spacing_check.py <dialog-crop.png>
```

Prüft Content-Segment-Gaps, Border-Runs (`#3C404D`), untere Restfläche und
Passwort-Maskierung gegen die Spec. FAIL → Layout-Fix in `textbox-dock.cpp` → re-audit.

### Deploy nach OBS Studio

DLL + PDB nach `<OBS Studio>\obs-plugins\64bit\` **und** `<OBS Studio>\bin\64bit\`
kopieren; Plugin-Data unter `<OBS Studio>\data\obs-plugins\textbox-studio\`
(`index.html`, `templates/`, `locale/`, `animations/` — via `cmake --install build_x64`).

## Nutzung

1. OBS starten — das Dock „Textbox Studio“ erscheint automatisch.
2. Tab für die gewünschte Karte wählen, Felder setzen — die Vorschau aktualisiert live.
3. Overlay: das Plugin legt die Browser-Source automatisch an; alternativ eine
  bestehende Browser-Source mit der Overlay-HTML verwenden (mind. 1200×600 px).
4. Externe Steuerung: OBS-WebSocket mit `textbox_card_update`-Events.

## Release

GitHub Actions baut bei **Tag-Push** `v*` für Windows/macOS/Linux und veröffentlicht
als GitHub-Release (`on: push: tags: v*` + `workflow_dispatch`; ein plain main-Push
triggert keinen Run). Tag-Update:

```bash
git tag -f vX.Y.Z <commit> && git push -f origin vX.Y.Z
```

Release-Content-Prüfung statt Hash: ZIP-DLL-Size + embedded Farb-Marker im DLL-Blob
(`#3C404D` vorhanden, altes Theme `#7A86A2` nicht); Hash-Vergleich mit lokalem Build
ist nicht aussagekräftig (PE-TimeDateStamp variiert pro Build).

## Struktur

```
plugin/                     OBS-Plugin (Quellcode, CMake, Data, Tests)
plugin/src/                 Quellen (Controls, Dock, Overlay, HTML, Import, AI, Theme)
plugin/src/tests/           Qt-freie Test-Suiten + samples
plugin/templates/           Overlay-Vorlagen (standard/display/minimal/badge)
docs/DESIGN.md              UI-Design-Spec (Spacing-Raster, Theme-Tokens)
docs/native-style-notes.md  Referenz: natives OBS-Plugin-Styling
tools/ui_spacing_check.py   UI-Audit-Tool (Pixel-Gaps gegen DESIGN.md)
assets/screenshots/         README-Screenshots (nur lokal, nicht im Repo)
```

## Lizenz

MIT — siehe `plugin/LICENSE`.
