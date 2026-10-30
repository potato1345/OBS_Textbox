/*
Liedplan — Standalone OBS-Plugin (T7)
Copyright (C) 2026 Johann

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

/*
Ui3 (27.10): Natives OBS-Look statt Dashboard-Look (Ui1).
Farbwerte = exakte OBS-Yami-Dark-Palette (obs-studio 32.2.2,
frontend/data/themes/Yami.obt, CSS-Variablen aufgelöst):
  grey1 #5B6273  grey3 #464B59  grey4 #3C404D  grey6 #272A33  grey7 #1D1F26
  white1 #FFFFFF white4 #B4B4B4 white5 #969696
  blue2 #476BD7  blue3 #284CB8  blue4 #213E97  blue1 #718CDC
  red1 #E85E75   red3 #C01C37   yellow3 #B88A16
Stil-Merkmale des nativen OBS:
  - 4px-Radius (OBS --border_radius), kein 8px-Card-Look
  - Font 'Open Sans' (OBS-Bundling) mit Fallback Segoe UI, 11pt (OBS-Default)
  - Flache graue Buttons (#3C404D), Primary-Aktionen filled blue (#284CB8)
  - QGroupBox mit bold weissem Titel (kein blauer Titel)
  - OBS-Slider: weisser Pill-Handle, blaue sub-page (#476BD7)
  - 12px-Scrollbars (grey6-Hintergrund, grey4-Handle)
  - Dock-Titelbalken #3C404D, bold (wie alle OBS-Docks)
  - Keine Letter-Spacing / keine Uppercase-Labels
Alle Objekt-Namen (lp*) wie in Ui1/Ui2 — reine QSS-Ebene, null Verhalten.
*/

#pragma once

#include <QString>

namespace liedplan_theme {

inline QString base_qss()
{
	return QStringLiteral(R"QSS(
QWidget#liedplanDock,
QWidget#lpCardsContainer,
QDialog#lpImportDialog,
QDialog#lpQuickProfilesDialog,
QDialog#lpSettingsDialog {
	background-color: #1D1F26;
	color: #FFFFFF;
	font-family: 'Open Sans', 'Segoe UI', sans-serif;
	font-size: 11pt;
}

QWidget#liedplanDock {
	background-color: #1D1F26;
	border: 1px solid #3C404D;
}

QScrollArea {
	background-color: transparent;
	border: none;
}

QToolTip {
	background-color: #272A33;
	color: #FFFFFF;
	border: 1px solid #3C404D;
	padding: 4px 6px;
}

QMenu {
	background-color: #1D1F26;
	border: 1px solid #3C404D;
	padding: 2px;
}

QMenu::item {
	padding: 4px 24px 4px 8px;
	border-radius: 2px;
	border: 1px solid transparent;
}

QMenu::item:selected {
	background-color: #284CB8;
	border: 1px solid #476BD7;
}

QMenu::item:hover {
	background-color: #3C404D;
	border: 1px solid #464B59;
}

QMenu::item:disabled {
	color: #969696;
	background: transparent;
}

QMenu::separator {
	background: #3C404D;
	height: 1px;
	margin: 2px 6px;
}

/* ---- Karten: OBS-Panel statt Web-Card ---- */

QFrame#lpCardOuter {
	background-color: transparent;
	border: 1px solid #3C404D;
	border-top-left-radius: 4px;
	border-top-right-radius: 4px;
	border-bottom-left-radius: 10px;
	border-bottom-right-radius: 10px;
}

QFrame#lpCardInner {
	background-color: #272A33;
	border: none;
	border-top-left-radius: 0px;
	border-top-right-radius: 0px;
	border-bottom-left-radius: 9px;
	border-bottom-right-radius: 9px;
}

QFrame#lpCardInner:hover {
	background-color: #323540;
}

QFrame#lpIdBar {
	background-color: #3C404D;
	border: none;
	border-top-left-radius: 3px;
	border-top-right-radius: 3px;
	border-bottom-left-radius: 0px;
	border-bottom-right-radius: 0px;
}

/* ---- Labels ---- */

QLabel {
	background-color: transparent;
	border: none;
	color: #FFFFFF;
}

QLabel#lpIdPrefix {
	color: #969696;
	font-size: 10pt;
}

QLabel#lpIdValue {
	color: #FFFFFF;
	font-size: 12pt;
	font-weight: bold;
}

QLabel#lpFieldLabel {
	color: #969696;
	font-size: 10pt;
}

QLabel#lpImportStatus,
QLabel#lpDockStatus,
QLabel#lpQuickStatus,
QLabel#lpSettingsStatus,
QLabel#lpAiSourceLabel {
	color: #969696;
	font-size: 10pt;
}

QLabel#lpImportError {
	color: #E85E75;
	font-size: 10pt;
	font-weight: bold;
}

QLabel#lpKopfWochen,
QLabel#lpKopfTag,
QLabel#lpKopfOrganist,
QLabel#lpKopfZeitstempel {
	color: #FFFFFF;
	font-size: 11pt;
}

/* ---- Eingaben: OBS-Input-Style ---- */

QLineEdit,
QPlainTextEdit {
	background-color: #3C404D;
	border: 1px solid #3C404D;
	border-radius: 4px;
	padding: 4px 8px;
	min-height: 22px;
	color: #FFFFFF;
	selection-background-color: #284CB8;
	selection-color: #FFFFFF;
}

QLineEdit:hover,
QPlainTextEdit:hover {
	background-color: #1D1F26;
	border: 1px solid #5B6273;
}

QLineEdit:focus,
QPlainTextEdit:focus {
	border: 1px solid #284CB8;
}

QComboBox,
QComboBox#lpYamlProfileCombo {
	background-color: #3C404D;
	border: 1px solid #3C404D;
	border-radius: 4px;
	padding: 4px 8px;
	min-height: 22px;
	color: #FFFFFF;
}

QComboBox:hover,
QComboBox#lpYamlProfileCombo:hover {
	border: 1px solid #5B6273;
}

QComboBox:focus,
QComboBox:on,
QComboBox#lpYamlProfileCombo:focus,
QComboBox#lpYamlProfileCombo:on {
	border: 1px solid #284CB8;
}

QComboBox::drop-down {
	border: none;
	border-left: 1px solid #272A33;
	width: 22px;
}

QComboBox QAbstractItemView {
	background-color: #272A33;
	border: 1px solid #3C404D;
	color: #FFFFFF;
	padding: 2px;
	selection-background-color: #284CB8;
	selection-color: #FFFFFF;
	outline: 0;
}

QComboBox QAbstractItemView::item {
	padding: 4px 8px;
	border-radius: 2px;
}

QComboBox QAbstractItemView::item:hover,
QComboBox QAbstractItemView::item:selected {
	background-color: #284CB8;
}

/* ---- Buttons: flach grau, Primary filled blue ---- */

QPushButton,
QPushButton#lpBtn,
QPushButton#lpBtnNewProfile,
QPushButton#lpBtnDeleteProfile,
QPushButton#lpBtnExportYaml,
QPushButton#lpBtnImportYaml,
QPushButton#lpColorA,
QPushButton#lpColorB {
	background-color: #3C404D;
	border: 1px solid #3C404D;
	border-radius: 4px;
	padding: 6px 12px;
	color: #FFFFFF;
}

QPushButton#lpColorA,
QPushButton#lpColorB,
QPushButton#lpTextColor,
QPushButton#lpBoxColor {
	padding: 3px 8px;
}

QPushButton:hover,
QPushButton#lpBtn:hover,
QPushButton#lpBtnNewProfile:hover,
QPushButton#lpBtnDeleteProfile:hover,
QPushButton#lpBtnExportYaml:hover,
QPushButton#lpBtnImportYaml:hover,
QPushButton#lpColorA:hover,
QPushButton#lpColorB:hover,
QPushButton#lpTextColor:hover,
QPushButton#lpBoxColor:hover {
	background-color: #464B59;
}

QPushButton:pressed {
	background-color: #1D1F26;
}

QPushButton:disabled {
	background-color: #272A33;
	border: 1px solid #3C404D;
	color: #969696;
}

/* Primary-Aktionen (OBS-OK-Button-Style) */
QPushButton#lpBtnAccent,
QPushButton#lpBtnImport,
QPushButton#lpBtnApiImport,
QPushButton#lpBtnCsvImport,
QPushButton#lpBtnAiStart,
QPushButton#lpBtnAiApply,
QPushButton#lpBtnQuickProfiles,
QPushButton#lpBtnBackup,
QPushButton#lpBtnRestore,
QPushButton#lpBtnSettings,
QPushButton#lpBtnQuickSave,
QPushButton#lpBtnQuickApply {
	background-color: #284CB8;
	border: 1px solid #284CB8;
	color: #FFFFFF;
}

QPushButton#lpBtnAccent:hover,
QPushButton#lpBtnImport:hover,
QPushButton#lpBtnApiImport:hover,
QPushButton#lpBtnCsvImport:hover,
QPushButton#lpBtnAiStart:hover,
QPushButton#lpBtnAiApply:hover,
QPushButton#lpBtnQuickProfiles:hover,
QPushButton#lpBtnBackup:hover,
QPushButton#lpBtnRestore:hover,
QPushButton#lpBtnSettings:hover,
QPushButton#lpBtnQuickSave:hover,
QPushButton#lpBtnQuickApply:hover {
	background-color: #476BD7;
	border: 1px solid #476BD7;
}

QPushButton#lpBtnAccent:pressed,
QPushButton#lpBtnImport:pressed,
QPushButton#lpBtnApiImport:pressed,
QPushButton#lpBtnCsvImport:pressed,
QPushButton#lpBtnAiStart:pressed,
QPushButton#lpBtnAiApply:pressed,
QPushButton#lpBtnQuickProfiles:pressed,
QPushButton#lpBtnBackup:pressed,
QPushButton#lpBtnRestore:pressed,
QPushButton#lpBtnSettings:pressed,
QPushButton#lpBtnQuickSave:pressed,
QPushButton#lpBtnQuickApply:pressed {
	background-color: #213E97;
}

QPushButton#lpBtnApiImport:disabled,
QPushButton#lpBtnCsvImport:disabled,
QPushButton#lpBtnAiStart:disabled,
QPushButton#lpBtnAiApply:disabled,
QPushButton#lpBtnQuickApply:disabled,
QPushButton#lpBtnQuickDelete:disabled {
	background-color: #272A33;
	border: 1px solid #3C404D;
	color: #969696;
}

/* Danger (OBS red3) */
QPushButton#lpBtnRed,
QPushButton#lpBtnQuickDelete {
	background-color: #272A33;
	border: 1px solid #C01C37;
	color: #E85E75;
}

QPushButton#lpBtnRed:hover,
QPushButton#lpBtnQuickDelete:hover {
	background-color: #C01C37;
	border: 1px solid #C01C37;
	color: #FFFFFF;
}

/* Karten-Löschen: flacher Icon-Button */
QPushButton#lpDelBtn {
	background-color: transparent;
	border: none;
	border-radius: 4px;
	padding: 0px;
	color: #969696;
	font-size: 12pt;
	min-width: 24px;
	max-width: 24px;
	min-height: 24px;
	max-height: 24px;
}

QPushButton#lpDelBtn:hover {
	background-color: #3C404D;
	color: #E85E75;
}

/* ---- Scrollbars: 12px OBS-Style ---- */

QScrollBar:vertical {
	background: #272A33;
	width: 12px;
	margin: 0px;
	border: none;
}

QScrollBar::handle:vertical {
	background: #3C404D;
	margin: 2px;
	min-height: 32px;
	border-radius: 2px;
	border: 1px solid #3C404D;
}

QScrollBar::handle:vertical:hover {
	background: #464B59;
	border-color: #464B59;
}

QScrollBar::handle:vertical:pressed {
	background: #5B6273;
	border-color: #5B6273;
}

QScrollBar::add-line:vertical,
QScrollBar::sub-line:vertical {
	height: 0px;
	background: none;
	border: none;
}

QScrollBar::add-page:vertical,
QScrollBar::sub-page:vertical {
	background: none;
}

QScrollBar:horizontal {
	background: #272A33;
	height: 12px;
	margin: 0px;
	border: none;
}

QScrollBar::handle:horizontal {
	background: #3C404D;
	margin: 2px;
	min-width: 32px;
	border-radius: 2px;
	border: 1px solid #3C404D;
}

QScrollBar::handle:horizontal:hover {
	background: #464B59;
	border-color: #464B59;
}

QScrollBar::handle:horizontal:pressed {
	background: #5B6273;
	border-color: #5B6273;
}

QScrollBar::add-line:horizontal,
QScrollBar::sub-line:horizontal {
	width: 0px;
	background: none;
	border: none;
}

QScrollBar::add-page:horizontal,
QScrollBar::sub-page:horizontal {
	background: none;
}

/* ---- Slider: OBS-Style (weisser Handle, blaue Fill) ---- */

QSlider:horizontal {
	min-height: 20px;
	background: transparent;
}

QSlider::groove:horizontal {
	background-color: #3C404D;
	height: 4px;
	border-radius: 2px;
	border: none;
}

QSlider::sub-page:horizontal {
	background-color: #476BD7;
	border-radius: 2px;
	border: none;
}

QSlider::add-page:horizontal {
	background-color: #3C404D;
	border-radius: 2px;
	border: none;
}

QSlider::handle:horizontal {
	background-color: #FFFFFF;
	width: 20px;
	height: 10px;
	margin: -3px 0;
	border-radius: 5px;
	border: none;
}

QSlider::handle:horizontal:hover {
	background-color: #B4B4B4;
}

QSlider::handle:horizontal:pressed {
	background-color: #D2D2D2;
}

/* ---- Checkboxes / GroupBoxes ---- */

QCheckBox {
	background-color: transparent;
	color: #FFFFFF;
	spacing: 8px;
}

QCheckBox::indicator {
	width: 14px;
	height: 14px;
	background-color: #3C404D;
	border: 1px solid #5B6273;
	border-radius: 3px;
}

QCheckBox::indicator:hover {
	border: 1px solid #718CDC;
}

QCheckBox::indicator:checked {
	background-color: #284CB8;
	border: 1px solid #284CB8;
}

QGroupBox {
	background-color: #272A33;
	border: 1px solid #3C404D;
	border-radius: 4px;
	margin-top: 14px;
	padding-top: 14px;
	color: #FFFFFF;
	font-weight: bold;
}

QGroupBox#lpAnimGroup,
QGroupBox#lpAiImportGroup {
	background-color: #272A33;
	border: 1px solid #3C404D;
	border-radius: 4px;
	margin-top: 12px;
	padding-top: 12px;
}

QGroupBox::title {
	subcontrol-origin: margin;
	subcontrol-position: top left;
	left: 8px;
	top: 4px;
	padding: 0px 4px;
	color: #FFFFFF;
}

QGroupBox::indicator {
	width: 14px;
	height: 14px;
	background-color: #3C404D;
	border: 1px solid #5B6273;
	border-radius: 3px;
	margin-left: 2px;
	margin-right: 4px;
}

QGroupBox::indicator:hover {
	border: 1px solid #718CDC;
}

QGroupBox::indicator:checked {
	background-color: #284CB8;
	border: 1px solid #284CB8;
}

/* ---- Tabellen ---- */

QTableWidget,
QListWidget,
QListWidget#lpProfileList {
	background-color: #272A33;
	border: 1px solid #3C404D;
	border-radius: 4px;
	gridline-color: #3C404D;
	color: #FFFFFF;
	selection-background-color: #284CB8;
	selection-color: #FFFFFF;
}

QHeaderView::section {
	background-color: #3C404D;
	color: #FFFFFF;
	padding: 4px 8px;
	border: none;
	border-left: 1px solid #1D1F26;
	border-right: 1px solid #1D1F26;
}

/* ---- Fortschritt (AI-Import) ---- */

QProgressBar {
	background-color: #272A33;
	border: 1px solid #3C404D;
	border-radius: 4px;
	text-align: center;
	color: #FFFFFF;
}

QProgressBar::chunk {
	background-color: #284CB8;
}

/* ---- Dock-Titelbalken: wie alle OBS-Docks ---- */

QDockWidget {
	font-weight: bold;
}

QDockWidget::title {
	text-align: left;
	background-color: #3C404D;
	padding: 5px;
}

QDockWidget::close-button,
QDockWidget::float-button {
	border: none;
	border-radius: 4px;
	background: transparent;
	margin-right: 1px;
	min-width: 20px;
	min-height: 20px;
}

QDockWidget::close-button:hover,
QDockWidget::float-button:hover {
	background: #464B59;
}

QDockWidget::close-button:pressed,
QDockWidget::float-button:pressed {
	background: #1D1F26;
}

/* ---- M7: Untere Icon-Toolbar, Karten-Flash, einklappbare Stil-Sektion ---- */

QFrame#lpCardOuter[lpFlash="true"] {
	border: 1px solid #476BD7;
}

QFrame#lpStyleSection {
	background: transparent;
	border: none;
}

QToolButton#lpToolAdd,
QToolButton#lpToolClear,
QToolButton#lpToolImport,
QToolButton#lpToolSettings,
QToolButton#lpStyleToggle,
QToolButton#lpProfilDelBtn {
	background: #3C404D;
	border: 1px solid #3C404D;
	border-radius: 4px;
}

QToolButton#lpToolAdd:hover,
QToolButton#lpToolClear:hover,
QToolButton#lpToolImport:hover,
QToolButton#lpToolSettings:hover,
QToolButton#lpProfilDelBtn:hover {
	background: #476BD7;
	border-color: #476BD7;
}

QToolButton#lpToolAdd:pressed,
QToolButton#lpToolClear:pressed,
QToolButton#lpToolImport:pressed,
QToolButton#lpToolSettings:pressed,
QToolButton#lpProfilDelBtn:pressed {
	background: #284CB8;
	border-color: #284CB8;
}

QToolButton#lpStyleToggle:hover {
	background: #476BD7;
}
)QSS");
}

/*
Ui4: Natives OBS-Icon (aus dem Yami-Theme, in der Plugin-DLL eingebettet und
beim Modulstart nach <config>/icons/ geschrieben — s. liedplan-theme-icons.hpp).
icon_dir = leeres QString -> Stylesheet ohne Icon-Regeln (Fallback).
WICHTIG: QSS `image:` skaliert NICHT — daher sind alle eingebetteten SVGs
16x16 (Checkbox-Icons: 128er-ViewBox auf 16 skaliert), und der Indicator
wird auf 16x16 gesetzt. qsvg.dll liegt in OBS' Qt-imageformats.
*/
inline QString stylesheet(const QString &icon_dir = QString())
{
	QString s = base_qss();
	if (icon_dir.isEmpty())
		return s;
	QString d = icon_dir;
	d.replace('\\', '/');
	const auto icon = [&d](const char *name) {
		return QStringLiteral("url(\"%1/%2\")").arg(d, QString::fromLatin1(name));
	};
	s += QStringLiteral("\n/* ---- Ui4: Natives OBS-Icon (Yami-Theme) ---- */\n");
	s += QStringLiteral("QCheckBox::indicator { width: 16px; height: 16px; background: transparent; border: none; border-radius: 0px; image: %1; }\n")
		 .arg(icon("checkbox_unchecked.svg"));
	s += QStringLiteral("QCheckBox::indicator:hover { image: %1; }\n").arg(icon("checkbox_unchecked_focus.svg"));
	s += QStringLiteral("QCheckBox::indicator:checked { image: %1; }\n").arg(icon("checkbox_checked.svg"));
	s += QStringLiteral("QCheckBox::indicator:checked:hover { image: %1; }\n").arg(icon("checkbox_checked_focus.svg"));
	s += QStringLiteral("QCheckBox::indicator:disabled { image: %1; }\n").arg(icon("checkbox_unchecked_disabled.svg"));
	s += QStringLiteral("QCheckBox::indicator:checked:disabled { image: %1; }\n").arg(icon("checkbox_checked_disabled.svg"));
	s += QStringLiteral("QGroupBox::indicator { width: 16px; height: 16px; background: transparent; border: none; border-radius: 0px; image: %1; }\n")
		 .arg(icon("checkbox_unchecked.svg"));
	s += QStringLiteral("QGroupBox::indicator:hover { image: %1; }\n").arg(icon("checkbox_unchecked_focus.svg"));
	s += QStringLiteral("QGroupBox::indicator:checked { image: %1; }\n").arg(icon("checkbox_checked.svg"));
	s += QStringLiteral("QGroupBox::indicator:checked:hover { image: %1; }\n").arg(icon("checkbox_checked_focus.svg"));
	s += QStringLiteral("QGroupBox::indicator:disabled { image: %1; }\n").arg(icon("checkbox_unchecked_disabled.svg"));
	s += QStringLiteral("QGroupBox::indicator:checked:disabled { image: %1; }\n").arg(icon("checkbox_checked_disabled.svg"));
	s += QStringLiteral("QComboBox::drop-down { image: %1; }\n").arg(icon("collapse.svg"));
	s += QStringLiteral("QPushButton::menu-indicator { image: %1; width: 16px; }\n").arg(icon("collapse.svg"));
	return s;
}

} // namespace liedplan_theme
