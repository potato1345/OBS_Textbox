# Ui1: Liedplan-Dock im Look des v1-Dashboards (main/controlui)

## Kontext
- Repo: `C:\Users\Johann\Desktop\GitHub\OBS_Textbox\plugin` (OBS-Plugin „Liedplan", T7, OBS 32.2.2).
- Stand: M2a-browser fertig & deployed (Commits `df8e5f4`, `d2b104a`). Karten werden per
  Browser-Source (CEF) mit `main/textbox.html` gerendert, Live-Update via
  `liedplan_card_update` funktioniert.
- Das Dock `plugin/src/liedplan-dock.cpp` ist aktuell ein funktionaler, ungestylter
  Qt-Row-Bau: `[id | GL | Verse | Titel | Profil-Combo | Zu Szene | Leeren | Entf. | ✕]`.
- **Ziel (User-Anforderung 25.10):** Das Dock soll aussehen wie das ORIGINAL v1-Dashboard,
  das browser-basiert war: `C:\Users\Johann\Desktop\GitHub\OBS_Textbox\main\controlui\control.html`
  + `main\controlui\control.css` (produktiv seit August). Beziehe die exakten Werte aus
  diesen beiden Dateien (Design-System steht dort oben als `:root`).
- **Ui1 = reine Optik.** Keine neuen State-Felder, kein Verhalten ändern, keine
  Rendering-/Scene-Änderungen. (Per-Karte-Farben, Ecken, Regenbogen, Animation =
  Folgemeilenstein Ui2, NICHT jetzt.)

## Design-Tokens (aus control.css — 1:1 übernehmen)
```
--bg-primary:    #1C1F26   (Dock-Hintergrund)
--bg-secondary:  #323a44   (Input-Hintergrund)
--panel-bg:      #272A33   (Karten-Innen, Buttons)
--panel-bg-alt:  #414852   (Karten-Hover, Button-Hover)
--card-outer-bg: #3C404D   (Karten-Außenrahmen, ID-Leiste)
--border:        #3C404D
--text-primary:  #eff0f1
--text-secondary:#b0b6bd
--accent-blue:   #3daee9
--accent-blue-soft: rgba(61,174,233,0.14)
--accent-red:    #f87171
--accent-red-soft: rgba(248,113,113,0.15)
--radius:        10px
Font: 'Inter', -apple-system, 'Segoe UI', sans-serif  (Inter ist vermutlich nicht
installiert → Fallback auf Segoe UI ist korrekt, nicht versuchen, Fonts zu installieren)
```

## Ziel-Struktur des Docks
```
LiedplanDock (objectName "liedplanDock", setStyleSheet(liedplan_theme::stylesheet()))
├── Toolbar (QHBoxLayout, OBEN statt unten, wie v1-Header):
│   ├── QPushButton „＋ Neue Karte"     (objectName „lpBtnAccent")
│   ├── QPushButton „🗑 Alle löschen"    (objectName „lpBtnRed")
│   └── stretch
├── QScrollArea (NoFrame, transparent)
│   └── Container (QVBoxLayout, spacing 12, contentsMargins 8)
│       └── pro Karte:
│           QFrame#lpCardOuter (bg #3C404D, border-radius 8, volle Breite, min 320)
│           └── QFrame#lpCardInner (bg #272A33, radius 0 0 8 8, contentsMargins 16)
│               ├── ID-Leiste QFrame#lpIdBar (bg #3C404D, radius 8, padding 6px 12px, mb 12):
│               │   QLabel#lpIdPrefix „ID"  +  QLabel#lpIdValue (Karten-ID, fett 14px)
│               │   + stretch  +  QPushButton#lpDelBtn „✕" (28×28, Karte löschen)
│               ├── 3× Feldzeile (QVBoxLayout pro Feld: label oben, input darunter, gap 4):
│               │   QLabel#lpFieldLabel „GL-NUMMER" / „VERSE" / „LIEDTITEL"
│               │   QLineEdit (existierende Edits, nur neu verdrahten — Connects bleiben!)
│               ├── Profilzeile: QLabel#lpFieldLabel „PROFIL" + QComboBox
│               └── Aktionszeile (QHBoxLayout, gap 8, mt 4):
│                   QPushButton#lpBtnGreen „▶ Zu Szene" (btn-green: border accent-blue-soft,
│                     Text primary — dezent wie in v1)
│                   QPushButton#lpBtnRed  „✕ Leeren"
│                   QPushButton#lpBtn     „Entf." (neutral)
```
- ID bleibt ANZEIGE (Label), keine ID-Editierung in Ui1 (v1 hat ein Input; das ist
  Ui2/Follow-up — bewusst nicht jetzt, weil Rename Source/HTML-Mechanik nötig wäre).
- Karten-Höhe: kompakt halten (QLineEdits fixe Höhe ~34px, kein Stretch auf volle Höhe).

## QSS (neue Datei `plugin/src/liedplan-theme.hpp`)
- GPL-2-Kopf wie die anderen `src/`-Dateien kopieren.
- `namespace liedplan_theme { inline QString stylesheet(); }` — eine QSS-String-Literal,
  die OBEREACHEND nur den Dock-Baum stylet (Widgets per objectName, generische Regeln
  für QLineEdit/QComboBox/QPushButton/QScrollArea/QScrollBar sind OK, weil das
  Stylesheet nur auf dem Dock-Widget gesetzt wird).
- Mindestanforderungen an die QSS (Werte s. Tokens):
  - Dock-Root: bg `#1C1F26`, Text `#EFF0F1`, Font-Serie Inter/Segoe UI, 13px.
  - `QFrame#lpCardOuter` / `QFrame#lpCardInner`: Bgr + Rounding wie oben;
    `QFrame#lpCardInner:hover` → `#414852` (QSS `:hover` auf QFrame funktioniert;
    falls Qt-Eigendunkelung stört: `setAutoFillBackground(true)` nicht nötig).
  - `QFrame#lpIdBar`: bg `#3C404D`, radius 8.
  - `QLineEdit`: bg `#323A44`, border 1px `#3C404D`, radius 8, padding 7px 10px;
    `:focus` → border `#3DAEE9`; Selektion `rgba(61,174,233,0.35)`.
  - `QComboBox`: wie QLineEdit; Dropdown-Popup: bg `#272A33`, border `#3C404D`,
    Selektion `#3DAEE9`/Text dunkel.
  - Buttons: Basis = v1 `.btn` (bg `#272A33`, border `#3C404D`, radius 8, padding 8px 14px,
    600/13px, Text `#EFF0F1`, Hover `#414852`);
    `lpBtnAccent` → border+Text `#3DAEE9`;
    `lpBtnRed` → border+Text `#F87171`, Hover bg `rgba(248,113,113,0.15)`;
    `lpBtnGreen` → border `rgba(61,174,233,0.14)`, Text primary;
    `lpDelBtn` → transparent, radius 6, Hover red-soft/`#F87171`.
  - Scrollbar: dunkel schmal (Handle `#414852`, Hover `#3DAEE9`, keine Arrows).
- QSS-Ränder: Wenn 4-Wert `border-radius: 0px 8px 8px 0px` o.ä. in Qt nicht sauber
  rendert, darf der Innenrahmen durch den 2px-Außen-Offset des `lpCardOuter` (margins)
  seine Rounding-Begrenzung verlieren — Optik-Vergleich mit control.html zählt,
  nicht QSS-Perfektionismus.

## Verhalten — INVARIANT (1:1 behalten, nur neu verdrahten)
- `textChanged` der 3 Edits → `find_card_mut` → State setzen → `save_state()` →
  `liedplan_scene::update_everywhere(*c, false)` (Live-Update, KEIN Row-Rebuild).
- Profil-Combo → Profile aus `liedplan_profiles.hpp` anwenden (text_color/box_color),
  save, update_everywhere.
- „Zu Szene" → `liedplan_scene::send_to_current_scene(*c)`.
- „Leeren" → `liedplan_scene::update_everywhere(*c, true)`.
- „Entf." → `liedplan_scene::remove_everywhere(id)`.
- „✕" (ID-Leiste) → Karte aus State entfernen, Source überall entfernen, Row weg.
- „＋ Neue Karte" / „🗑 Alle löschen" (mit bestehendem QMessageBox-Confirm).
- Persistenz: `state_.load/save` unverändert (gleiche JSON-Datei, gleiche Felder).
- `setMinimumSize(...)`: Breite auf ~440 erhöhen, damit die Karten-Optik atmet.

## Ausgeklammert (UI2/Folgemeilenstein)
- ID-Editierung, per-Karte-Farb-/Ecken-/Regenbogen-/Animation-UI,
  `details`-Abschnitte „🎬 Animation" / „🎨 Farben & Deckkraft".

## Änderungen — ERLAUBTE DATEIEN (nichts anderes!)
1. `plugin/src/liedplan-dock.cpp` (Restructure create_row_widget, Toolbar oben,
   Stylesheet setzen, objectNames).
2. `plugin/src/liedplan-dock.hpp` (nur wenn neue private Helfer nötig sind).
3. NEU: `plugin/src/liedplan-theme.hpp` (Theme-Header, header-only).
4. `plugin/CMakeLists.txt` (nur: `liedplan-theme.hpp` in die `target_sources`-Liste
   aufnehmen, falls nicht schon drin — für IDE/Sichtbarkeit).
- **NICHT anfassen:** `liedplan-scene.*`, `liedplan-html.*`, `liedplan-state.*`,
  `liedplan-profiles.hpp`, `liedplan-card-source.*`, `liedplan-render.*`, `liedplan-textfmt.hpp`,
  alle `tests/`, `main/`, `rewrite/`, Repo-Root, `plugin/build*`.
- **Anti-Umzugs-Klausel:** KEINE Datei im Repo bewegen, umbenennen, löschen oder neu
  anlegen (außer der neuen Theme-Datei). (Vorfall M1: agy verschob Root-Dateien
  ungemeldet nach `main/`.)
- **Kein git:** Nicht committen, nicht stagen — Hermes committet nach eigener Prüfung.
- **Keine OBS-UI-Interaktion** (kein Klicken), keine Screenshots.

## Build + Tests (exakt)
```bash
export PATH="/c/Users/Johann/tools/cmake-3.30.5-windows-x86_64/bin:$PATH"
cd "C:/Users/Johann/Desktop/GitHub/OBS_Textbox/plugin"
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
ctest --test-dir build_x64 -C RelWithDebInfo
```
- Build muss grün sein (0 Fehler; Warnungen ok, aber im Report nennen).
- `ctest`: **3/3** (liedplan-state, liedplan-textfmt, liedplan-render) — Verhalten
  ist unverändert, also alle 22 render-Checks müssen weiterhin grüner bleiben.

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
   - Szene „Szene" lädt (box1/box2/box3 = browser_source, box13 = alter `liedplan_card`-Typ,
     darf schwarz bleiben; Migration NICHT auslösen!)
6. OBS **laufen lassen** (nicht beenden) — Hermes macht danach Screenshots.

## Report (deutsch, am Ende)
- Geänderte/neue Dateien (mit je 1 Zeile, was geändert wurde).
- Build-Status + ctest-Ergebnis (3/3 mit Check-Zahl).
- Deploy: DLL-Timestamp im Zielordner, OBS-PID, RAM-Größe, Log-Auszug (erwartete Zeilen).
- Status: `GRUEN` (alles ok) oder `DIALOG_OFFEN` (OBS am Crash-Dialog) oder
  `FEHLER` + genaue Ursache.
- Auffälligkeiten/Warnungen aus dem Build.
