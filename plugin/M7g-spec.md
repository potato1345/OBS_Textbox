# M7g — Abgerundete Kartenecken unten, Mülltonnen-Icon, Profil-Löschung im oberen Dropdown

## Kontext
Repo: `C:\Users\Johann\Desktop\GitHub\OBS_Textbox` (Branch main). OBS-Plugin „Liedplan" (Qt5/C++).
Build-Working-Directory: **`C:\Users\Johann\Desktop\GitHub\OBS_Textbox\plugin`** (CMakePresets.json liegt in `plugin/`, NICHT im Repo-Root).
CMake: `C:\Users\Johann\tools\cmake-3.30.5-windows-x86_64\bin\cmake.exe`

## Dateierlaubnis (STRIKT)
NUR diese Dateien dürfen geändert werden:
- `C:\Users\Johann\Desktop\GitHub\OBS_Textbox\plugin\src\liedplan-dock.cpp`
- `C:\Users\Johann\Desktop\GitHub\OBS_Textbox\plugin\src\liedplan-dock.hpp`
- `C:\Users\Johann\Desktop\GitHub\OBS_Textbox\plugin\src\liedplan-theme.hpp`

Diese Spec-Datei ist read-only. **Kein** Erzeugen, Umbenennen, Verschieben oder Löschen anderer Dateien. **Keine** Git-Befehle (kein add/commit/stage).

## Aufgabe 1: Untere Ecken der Karten sichtbar abgerundet (theme.hpp)
Aktuell sind die unteren Kartenecken mit 3–4px Radius kaum sichtbar. Ziel: deutlich sichtbare untere Rundung (~10px), obere Ecken unverändert.

`QFrame#lpCardOuter` (Block mit `background-color: transparent; border: 1px solid #3C404D;`):
- `border-radius: 4px;` → **`border-radius: 4px 4px 10px 10px;`**

`QFrame#lpCardInner` (Block mit `background-color: #272A33;`):
- `border-radius: 0px 0px 3px 3px;` → **`border-radius: 0px 0px 9px 9px;`**

(9px = 10px minus 1px Border — die Innenfläche muss exakt der Außenrundung folgen.)
Alles andere in diesen Blöcken bleibt unverändert. `QFrame#lpIdBar` bleibt wie ist.

## Aufgabe 2: ✕ in der Karten-ID-Leiste durch Mülltonnen-Symbol ersetzen (dock.cpp)
In `create_row_widget` (~Zeile 356) steht:
```cpp
del_btn->setIcon(theme_icon("close.svg"));
```
→ ändern zu:
```cpp
del_btn->setIcon(theme_icon("trash.svg"));
```
`trash.svg` existiert bereits (wird von `lpToolClear` genutzt). IconSize (16,16), ObjectName und Tooltip („Karte löschen") bleiben unverändert.

## Aufgabe 3: Schnell-Profil direkt im oberen Dropdown löschen (dock.hpp + dock.cpp + theme.hpp)
Aktuell kann ein Schnell-Profil nur im Dialog „Schnell-Profile…" gelöscht werden (dort unten, erst nach Auswahl). Ziel: kleiner Mülltonnen-Button direkt rechts neben dem Profil-Dropdown oben im Dock.

### 3a. liedplan-dock.hpp
- Nach der Deklaration `QComboBox *profil_select_ = nullptr;` neue Member-Deklaration:
  `QToolButton *profil_del_btn_ = nullptr;`
  (Falls `QToolButton` in der .hpp nicht verfügbar ist: `class QToolButton;`-Forward-Declaration oder `#include <QToolButton>` ergänzen — konsistent mit den bestehenden Includes.)
- Direkt nach der Deklaration `void refresh_top_row();` ergänzen:
  `void sync_profil_del_state();`

### 3b. liedplan-dock.cpp — Button-Erzeugung (im Top-Row-Bereich, ~Zeile 139)
Nach `top_row->addWidget(profil_select_, 1);` (und VOR `top_row->addWidget(card_search_, 1);`) einfügen:
```cpp
profil_del_btn_ = new QToolButton(this);
profil_del_btn_->setObjectName(QStringLiteral("lpProfilDelBtn"));
profil_del_btn_->setFixedSize(28, 28);
profil_del_btn_->setAutoRaise(true);
profil_del_btn_->setIcon(theme_icon("trash.svg"));
profil_del_btn_->setIconSize(QSize(16, 16));
profil_del_btn_->setToolTip(QStringLiteral("Gewähltes Schnell-Profil löschen"));
profil_del_btn_->setEnabled(false);
top_row->addWidget(profil_del_btn_);
```

### 3c. liedplan-dock.cpp — Helper-Methode (neue Funktion, z. B. direkt vor `void LiedplanDock::refresh_top_row()`)
```cpp
void LiedplanDock::sync_profil_del_state()
{
	if (!profil_del_btn_ || !profil_select_)
		return;
	const int idx = profil_select_->currentIndex();
	const bool valid = idx > 0 && idx < profil_select_->count() &&
	                   !profil_select_->itemData(idx).toString().isEmpty();
	profil_del_btn_->setEnabled(valid);
}
```

### 3d. liedplan-dock.cpp — Aktivierung/Deaktivierung verdrahten
Im Konstruktor nach der Button-Erzeugung:
```cpp
sync_profil_del_state();
connect(profil_select_, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, [this](int) { sync_profil_del_state(); });
```
UND in `refresh_top_row()`: am Ende des `if (profil_select_) { ... }`-Blocks (nach `profil_select_->setCurrentIndex(active_idx);`, noch innerhalb des if-Blocks) einen Aufruf `sync_profil_del_state();` ergänzen (dort ist ein QSignalBlocker aktiv, daher muss der Aufruf explizit sein).

### 3e. liedplan-dock.cpp — Klick-Handler (im Konstruktor, direkt nach der currentIndexChanged-Verdrahtung)
```cpp
connect(profil_del_btn_, &QToolButton::clicked, this, [this]() {
	const int idx = profil_select_->currentIndex();
	if (idx <= 0 || idx >= profil_select_->count())
		return;
	const QString prof_name = profil_select_->itemData(idx).toString();
	if (prof_name.isEmpty())
		return;
	if (state_.remove_quick_profile(static_cast<std::size_t>(idx) - 1)) {
		if (state_.active_profile == prof_name.toUtf8().toStdString())
			state_.active_profile.clear();
		save_state();
		refresh_top_row();
		const QString msg =
			QStringLiteral("Schnell-Profil gelöscht: %1")
				.arg(prof_name);
		set_dock_status(msg, false);
	}
});
```
Semantik: Dropdown-Item 0 ist „(keines)" mit leeren itemData → Index im `state_.quick_profiles`-Vektor ist immer `idx - 1` (exakt wie `refresh_top_row()` die Items baut). `remove_quick_profile` existiert in `LiedplanState` (liedplan-state.cpp) und löscht nur den Vektor-Eintrag (keine Kaskade auf Karten). Falls das gelöschte Profil in `state_.active_profile` steht, muss dieses geleert werden, damit das Dropdown nach dem Refresh nicht den Namen eines nicht mehr existierenden Profils anzeigt.

### 3f. liedplan-theme.hpp — QSS für den neuen Button
`QToolButton#lpProfilDelBtn` zu den drei bestehenden Selector-Gruppen am Ende der Stylesheet-Funktion ergänzen (gleiche Werte wie die Tool-Buttons):
1. Grund-Gruppe `QToolButton#lpToolAdd, QToolButton#lpToolClear, QToolButton#lpToolImport, QToolButton#lpToolSettings, QToolButton#lpStyleToggle { ... }` → `, QToolButton#lpProfilDelBtn` anhängen
2. `:hover`-Gruppe → `, QToolButton#lpProfilDelBtn:hover` anhängen
3. `:pressed`-Gruppe → `, QToolButton#lpProfilDelBtn:pressed` anhängen

## Build & Tests (aus `C:\Users\Johann\Desktop\GitHub\OBS_Textbox\plugin`)
1. `C:\Users\Johann\tools\cmake-3.30.5-windows-x86_64\bin\cmake.exe --preset windows-x64`
2. `C:\Users\Johann\tools\cmake-3.30.5-windows-x86_64\bin\cmake.exe --build --preset windows-x64 --config RelWithDebInfo`
3. Tests: `C:\Users\Johann\tools\cmake-3.30.5-windows-x86_64\bin\ctest.exe --test-dir build_x64 --build-config RelWithDebInfo` — alle Tests müssen grün bleiben (vorher 6/6).

## Deploy
OBS läuft gerade (obs64.exe aktiv) → **nicht** deployen, nicht killen. Wenn obs64.exe läuft, am Ende den Status `DEPLOY_SKIPPED_OBS_RUNNING` melden.

## Abnahmekriterien
- Build exit 0, keine neuen Compiler-Warnings in den 3 Dateien.
- ctest: alle Tests grün.
- `git status`/Diff enthält NUR Änderungen in den 3 erlaubten Dateien.
- Karte: untere Ecken sichtbar abgerundet (10px Außen / 9px Innen), obere Ecken unverändert (4px).
- del_btn in jeder Karten-ID-Leiste zeigt `trash.svg` (nicht mehr close.svg).
- Neuer Button `lpProfilDelBtn` (28×28, trash.svg 16px) sitzt im oberen Zeilenlayout zwischen `profil_select_` und `card_search_`.
- Button disabled bei „(keines)" oder eingegebener Freitextsuche; enabled bei gewähltem Profil-Item; Klick löscht das Profil, leert `active_profile` falls nötig, speichert, refreshes die Top-Row und zeigt `Schnell-Profil gelöscht: <name>` im Dock-Status.
