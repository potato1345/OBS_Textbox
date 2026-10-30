# M7 — Dock-Nativisierung R2 (Spec)

Stand: 2026-10-30 (Auto-Continue). User-Anforderung 29.10, 5 Punkte. Ziel: schlankeres,
natives Dock — schmalere Ränder, Icon-Toolbar unten, Profil-/Karten-Suche oben,
einklappbare Stil-Optionen pro Karte. **Reines UI-Meilenstein: keine State-Schema-Änderung,
kein Karten-Render-Änderung (cards/box*.html bleiben unangetastet).**

## 0. Datei-Allow-List (NUR diese Dateien anlegen/ändern — Anti-Umzug)

- `plugin/src/liedplan-dock.cpp` (ändern)
- `plugin/src/liedplan-dock.hpp` (ändern)
- `plugin/src/liedplan-theme.hpp` (ändern — QSS ergänzen)
- `plugin/src/liedplan-theme-icons.hpp` (ändern — 2 neue Icons + Version-Bump)
- `plugin/tests/test-liedplan-theme-icons.cpp` (ändern — 2 neue Icon-Checks)
- `plugin/M7-spec.md` (diese Datei, schon vorhanden)

Verboten: irgendwelche anderen Dateien erstellen/umbenennen/löschen/verschieben, git
Staging/Commit, State-Schema (`liedplan-state.{hpp,cpp}`), `liedplan-html.*`,
`liedplan-scene.*`, `liedplan-import.*`, `liedplan-ai.*`, CMake-Dateien, Repo-Dateien
außerhalb `plugin/`.

## 1. Schmalere Ränder (M7-A)

Alle Werte in `liedplan-dock.cpp` (aktuelle Werte → neue Werte, 1:1 anwenden):

| Stelle | aktuell | neu |
|---|---|---|
| `root_layout` contentsMargins | (8,8,8,8) | (4,4,4,4) |
| `root_layout` spacing | 6 | 4 |
| `cards_layout_` contentsMargins | (8,8,8,8) | (4,4,4,4) |
| `cards_layout_` spacing | 12 | 8 |
| `dock_status_lbl_` contentsMargins | (8,0,8,0) | (4,0,4,0) |
| `outer_layout` (lpCardOuter) contentsMargins | (2,2,2,2) | (1,1,1,1) |
| `inner_layout` (lpCardInner) contentsMargins | (16,16,16,16) | (10,10,10,10) |
| `inner_layout` spacing | 10 | 8 |
| `id_layout` (lpIdBar) contentsMargins | (12,6,12,6) | (8,4,8,4) |
| `setMinimumSize` | (440,260) | (360,240) |

QSS-Borders (`lpCardOuter`/`lpCardInner` 1px #3C404D, Radius 4px) bleiben UNVERÄNDERT —
"schmalerer Rand" heißt hier schmalere Innen-/Außen-Abstände, keine Border-Änderung.

## 2. Toolbar unten, nur Icons (M7-B)

Die beiden alten oberen Button-Reihen (`button_row`: Neue Karte/Alle löschen/Import und
`button_row_m4`: Schnell-Profile/Backup/Restore/Einstellungen) werden **komplett entfernt**
(Widgets + Layouts + Connects).

Neue **untere** Toolbar (QHBoxLayout `bottom_toolbar`), Layout-Reihenfolge im Dock
(oben → unten): `top_row` (Punkt 3) → `scroll_area` (stretch 1) → `dock_status_lbl_` →
`bottom_toolbar`. `bottom_toolbar`: contentsMargins (4,2,4,4), spacing 6.

4× `QToolButton`, fixedSize 32×32, `setAutoRaise(true)`, iconSize 16×16, kein Text:

| objectName | Icon | Tooltip | Ziel |
|---|---|---|---|
| `lpToolAdd` | plus.svg | "Neue Karte" | `on_add_card` |
| `lpToolClear` | trash.svg | "Alle Karten löschen" | `on_clear_all` (bestehender Confirm-Dialog bleibt) |
| `lpToolImport` | download.svg | "AI-Import / Datei-Import (CSV, XLSX, ZIP, Google)" | `on_open_import_dialog` (bestehender Dialog unverändert) |
| `lpToolSettings` | cogs.svg | "Schnell-Profile / Einstellungen / Backup / Restore" | QMenu (s. u.) |

`lpToolSettings` bekommt `setPopupMode(QToolButton::InstantPopup)` + `QMenu` mit den
Items (diese Reihenfolge, mit Separatoren):
1. "Schnell-Profile…" → `on_open_quick_profiles_dialog`
2. `addSeparator()`
3. "Einstellungen…" → `on_open_settings_dialog`
4. `addSeparator()`
5. "Backup" → `on_backup_state`
6. "Restore" → `on_restore_state`

QSS ergänzen (`liedplan-theme.hpp`, neue Regeln, bestehende `lp*`-Regeln bleiben):
`QToolButton#lpToolAdd, QToolButton#lpToolClear, QToolButton#lpToolImport,
QToolButton#lpToolSettings { background: #3C404D; border: 1px solid #3C404D;
border-radius: 4px; }` + `:hover { background: #476BD7; border-color: #476BD7; }`
+ `:pressed { background: #284CB8; border-color: #284CB8; }`.
`QToolButton#lpStyleToggle` (Punkt 5) bekommt die gleiche Button-Basis ohne Hover-Farbe
Wechsel auf blue2: `:hover { background: #476BD7; }`.

## 3. Obere Zeile: Profil-Dropdown + Karten-Suche (M7-C)

`top_row` (QHBoxLayout): contentsMargins (4,0,4,2), spacing 8.

### 3.1 `profil_select_` (neues Member, QComboBox*)
- editable (`setEditable(true)`), `QCompleter` mit `Qt::CaseInsensitive` +
  `setFilterMode(QCompleter::MatchContains)`, `setMinimumWidth(160)`,
  placeholder-Text via `lineEdit()->setPlaceholderText("Profil suchen…")`.
- Items in dieser Reihenfolge: "(keines)" (Data = leere String) + alle
  `state_.quick_profiles[i].name`.
- Bei echter (nicht programmatischer) Auswahl eines Profils: privater Helper
  `apply_quick_profile(const std::string &name)` anlegen, der EXAKT die bestehende
  "Übernehmen"-Logik aus `on_open_quick_profiles_dialog` ausführt
  (Quick-Profil auf aktuelle Karten laden, `state_.active_profile = name`,
  `sync_all_cards_live(previous_ids)`, `rebuild_rows()`,
  `set_dock_status("Schnell-Profil übernommen: " + name)`). Der Dialog selbst bleibt
  unverändert (Erreichen über `lpToolSettings`-Menu).

### 3.2 `card_search_` (neues Member, QComboBox*)
- editable, `QCompleter` MatchContains + CaseInsensitive, `setMinimumWidth(140)`,
  placeholder "Karte suchen…".
- Items pro Karte: `"box1 · <zeile3 getrimmt, max. 20 Zeichen>"` (ohne " · " + Titel,
  wenn zeile3 leer → nur `"box1"`); userData = card ID.
- Bei `activated(int)` ODER Enter in der lineEdit (`returnPressed`): Karte nach
  userData-ID suchen → `scroll_area_->ensureWidgetVisible(row_widget)` (Scrollbar
  positioniert, dass die Karte sichtbar ist) + **Highlight-Flash**: dynamische
  Property `lpFlash` auf dem `lpCardOuter`-QFrame setzen (`setProperty` +
  `style()->unpolish/polish`), nach 1200 ms per `QTimer::singleShot` entfernen.
  QSS: `QFrame#lpCardOuter[lpFlash="true"] { border: 1px solid #476BD7; }`
  (nur border-color, KEIN 2px — kein Layout-Shift).

### 3.3 Sync
- `rebuild_rows()` füllt `card_search_` neu (blockSignals während des Fillens,
  aktueller Text bleibt erhalten).
- `refresh_top_row()`-Helper: füllt `profil_select_` + `card_search_` — wird von
  `rebuild_rows()` UND am Ende von `on_open_quick_profiles_dialog()` aufgerufen
  (Profil-Anlage/Löschung muss sofort sichtbar sein).

Neue Member in `liedplan-dock.hpp`: `QComboBox *profil_select_`, `QComboBox
*card_search_`, `QScrollArea *scroll_area_` (aktuell nur lokale Variable im
Konstruktor), Forward-Decls `QComboBox; QScrollArea; QToolButton; QMenu;`.
Neue private Methoden: `apply_quick_profile(const std::string &name)`,
`refresh_top_row()`, `flash_row(const std::string &card_id)`.

## 4. Inklappbare Stil-Optionen pro Karte (M7-D)

In `create_row_widget`: alle Style-Widgets in NEUES Container-Widget
`lpStyleSection` (QFrame, objectName "lpStyleSection", QVBoxLayout
`style_layout`: contentsMargins (0,0,0,0), spacing 8):

Reihenfolge in `style_layout`:
1. `prof_layout` (PROFIL: Combo + "Profil +")
2. `text_color_layout` (TEXT-FARBE)
3. `box_color_layout` (BOX-FARBE)
4. `corner_a_layout` (ECKE A)
5. `corner_b_layout` (ECKE B)
6. `opacity_layout` (ECKEN-DECKKRAFT A/B)
7. `textbox_opacity_layout` (TEXT-/BOX-DECKKRAFT TEXT/BOX)
8. `rainbow_cb` (Regenbogen-Ecken)
9. `anim_group` (Animation-GroupBox)

`style_section` wird in `inner_layout` eingesetzt zwischen dem LIEDTITEL-Feld und
`actions_layout` (Zu Szene/Leeren/Entf.). **Start-Zustand: einklapppt
(`setVisible(false)`) — Standard für alle Karten.** Die 3 Textfelder (GL-NUMMER/
VERSE/LIEDTITEL), die ID-Leiste und die Aktionszeile bleiben IMMER sichtbar.

Toggle-Button: `QToolButton` in der `id_bar` (vor `del_btn`), objectName
`lpStyleToggle`, fixedSize 28×28, autoRaise, iconSize 16×16:
- eingeklappt: Icon chevron_right.svg, tooltip "Stil-Optionen ausklappen"
- ausgeklappt: Icon chevron_down.svg, tooltip "Stil-Optionen einklappen"
- Klick: `style_section->setVisible(!style_section->isVisible())` + Icon/Tooltip-Swap.

Keine Persistenz (reiner UI-State, bei jedem OBS-Start einklapppt).
QSS: `QFrame#lpStyleSection { background: transparent; border: none; }`.
Alle Connects der Style-Widgets bleiben exakt wie heute (nur Reparenting via Layout).

## 5. Neue Icons (M7-E, `liedplan-theme-icons.hpp`)

Zwei neue SVGs im Icon-Set (16×16, fill #fefefe, Yami-Stil):

```
{"chevron_right.svg", R"SVGICON(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16"><path fill="#fefefe" d="M5.5 2.75L4.25 4 8 8l-3.75 4 1.25 1.25L10.5 8Z"/></svg> )SVGICON"}
{"chevron_down.svg", R"SVGICON(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16"><path fill="#fefefe" d="M2.75 5.5L4 4.25 8 8l4-3.75L13.25 5.5 8 10.75Z"/></svg> )SVGICON"}
```

**Version-Bump**: `kVersion`-Konstante im Header um 1 erhöhen (sonst schreibt
`write_icons` die neuen Dateien nicht, Gate = icons-version.txt).
Test `tests/test-liedplan-theme-icons.cpp`: um je einen Check ergänzen
(Datei im Set vorhanden + SVG well-formed, bestehende Checks bleiben).

## 6. Build + Tests (agy führt SELBST aus, Ergebnis im Report)

1. Build (von `C:\Users\Johann\Desktop\GitHub\OBS_Textbox\plugin`):
   `C:\Users\Johann\tools\cmake-3.30.5-windows-x86_64\bin\cmake.exe --build --preset windows-x64 --config RelWithDebInfo`
   → muss GRÜN sein (0 Fehler).
2. Tests:
   `C:\Users\Johann\tools\cmake-3.30.5-windows-x86_64\bin\ctest.exe --test-dir build_x64 -C RelWithDebInfo`
   → erwartet: **6/6** (bestehende 6 Suites; theme-icons-Suite enthält jetzt 2 neue Icon-Checks).
   Jede Suite muss grün sein — keine Skip, keine Skip-umdeutungen.

## 7. Deploy + OBS-Restart (BEDINGT — nur wenn OBS nicht läuft)

1. `tasklist | findstr obs64.exe`: wenn OBS **läuft** → Deploy+Restart **überspringen**,
   im Report Status `DEPLOY_SKIPPED_OBS_RUNNING` setzen (User-Session nicht stören!).
   Wenn nicht: `taskkill /IM obs64.exe /F` und bis 30 s auf Prozessende warten.
2. Copy: `build_x64\RelWithDebInfo\liedplan-for-obs.dll` →
   `C:\Program Files (x86)\Steam\steamapps\common\OBS Studio\obs-plugins\64bit\liedplan-for-obs.dll`
3. SHA256 Build-DLL == Installed-DLL prüfen, Werte im Report.
4. Start: PowerShell `Start-Process -FilePath 'C:\Program Files (x86)\Steam\steamapps\common\OBS Studio\bin\64bit\obs64.exe' -WorkingDirectory 'C:\Program Files (x86)\Steam\steamapps\common\OBS Studio\data\obs-studio'`
   (CWD-KRITISCH: locale/en-US.ini wird relativ zum CWD aufgelöst — falscher CWD =
   locale-Dialog + 1-Zeilen-Log).
5. ~45 s warten, dann prüfen: obs64-RAM (gesund ≈430–470 MB; ≈40 MB =
   Crash-Dialog) + neueste Log in `C:\Users\Johann\AppData\Roaming\obs-studio\logs\`:
   "Liedplan"-Plugin-Load-Zeile + Dock ok, keine QSS-Warnungen.
   Crash-Dialog erkennbar, aber NICHT anklickbar (agy hat kein UI-Input) → Status
   `DIALOG_OFFEN` im Report; Hermes klickt dann "Im normalen Modus starten".

## 8. Done-Kriterien (Report muss enthalten)

- geänderte Datei-Liste (muss der Allow-List in Abschnitt 0 entsprechen)
- Build-Status + ctest-Ergebnis (X/6, Namen der fehlgeschlagenen, falls welche)
- Icon-Version-Wert (alt → neu)
- Deploy-Status: `DEPLOY_SKIPPED_OBS_RUNNING` ODER sha256-Werte + OBS-RAM + Log-Zeilen
- `DIALOG_OFFEN`, falls Crash-Dialog erkannt

## 9. Nicht antasten (Positiv-Liste)

- State-Schema (`liedplan-state.*`), Karten-HTML/Render (`liedplan-html.*`,
  `liedplan-scene.*`, `liedplan-card-source.*`), Import/AI (`liedplan-import.*`,
  `liedplan-ai.*`), Profil-Lookup (`liedplan-profiles.hpp`), CMake/Presets,
  bestehende QSS-Regeln (NUR ergänzen), alle bestehenden `lp*`-objectNames (bleiben),
  `plugin/.gitignore`, Repo-Dateien außerhalb `plugin/`.
