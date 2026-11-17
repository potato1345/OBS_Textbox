/*
 * textbox-theme.hpp — QSS-Styling des Textbox-Docks, abgeleitet von der OBS-App-Palette (v4).
 *
 * Zweck:
 *   - Erzeugt Qt-Stylesheets (QSS) für TextboxDock, Import-Dialog und Einstellungsfenster.
 *   - Nutzt zur Runtime die OBS-App-QPalette (Window/WindowText/Base/Button/Highlight)
 *     und füllt Fehlendes mit den Yami-Tokens.
 *   - Bei Palette-Wechsel (QEvent::ApplicationPaletteChange) wird der QSS-String
 *     neu generiert.
 *   - Icon-Pfade (%1) werden dynamisch substituiert.
 *
 * Farb-Referenz (Yami-Tokens, Fallback für helle Themes):
 *   #1D1F26 (grey7)   → Window (Panel-BG)
 *   #272A33 (grey6)   → Window (dunklerer BG)
 *   #3C404D (grey4)   → Button/Base (Eingabe-BG, border)
 *   #464B59 (grey3)   → Button hover
 *   #5B6273 (grey1)   → hover-borders
 *   #FFFFFF (white1)  → WindowText
 *   #969696 (white5)  → muted text
 *   #284CB8 (blue3)   → Highlight (primary)
 *   #476BD7 (blue2)   → Highlight hover
 *   #213E97 (blue4)   → Highlight pressed
 *   #E85E75 (red1)    → error/danger
 *   #C01C37 (red3)    → danger border
 */

#pragma once

#include <QPalette>
#include <QString>
#include <QApplication>
#include <QStyle>

namespace textbox_theme {

struct ThemeColors {
	QString window_bg;        // dunkelster Hintergrund
	QString panel_bg;         // Panel/Container-BG
	QString input_bg;         // Eingabe-BG
	QString border;           // Standard-Rahmen
	QString border_hover;     // Hover-Rahmen
	QString text;             // Primärtext
	QString text_muted;       // Sekundärtext/Label
	QString text_placeholder; // Platzhaltertext
	QString highlight;        // Primär (blau)
	QString highlight_hover;  // Hover-Primär
	QString highlight_pressed;// Gedrückt-Primär
	QString danger;           // Rot-Text
	QString danger_border;    // Rot-Rahmen
	QString scrollbar_track;
	QString scrollbar_thumb;
	QString scrollbar_thumb_hover;
	QString icon_color;       // Icon-Buttons, Pfeile
};

// Prüft anhand der Window-Palette, ob ein dunkles Theme aktiv ist.
inline bool is_dark_palette()
{
	const QPalette pal = qApp ? qApp->palette() : QPalette();
	const QColor w = pal.color(QPalette::Window);
	// Helle Themes haben Window > 128
	return w.red() + w.green() + w.blue() < 384;
}

// Baut ThemeColors aus App-Palette mit Yami-Fallback.
inline ThemeColors resolve_colors()
{
	ThemeColors c;
	const QPalette pal = qApp ? qApp->palette() : QPalette();

	if (is_dark_palette()) {
		// Yami-Tokens (dark)
		c.window_bg       = QStringLiteral("#1D1F26");
		c.panel_bg        = QStringLiteral("#272A33");
		c.input_bg        = QStringLiteral("#3C404D");
		c.border          = QStringLiteral("#3C404D");
		c.border_hover    = QStringLiteral("#5B6273");
		c.text            = QStringLiteral("#FFFFFF");
		c.text_muted      = QStringLiteral("#C7CCDB");
		c.text_placeholder = QStringLiteral("#C7CCDB");
		c.highlight       = QStringLiteral("#284CB8");
		c.highlight_hover = QStringLiteral("#476BD7");
		c.highlight_pressed = QStringLiteral("#213E97");
		c.danger          = QStringLiteral("#E85E75");
		c.danger_border   = QStringLiteral("#C01C37");
		c.scrollbar_track = QStringLiteral("#272A33");
		c.scrollbar_thumb = QStringLiteral("#5B6273");
		c.scrollbar_thumb_hover = QStringLiteral("#718CDC");
		c.icon_color      = QStringLiteral("#A2AAB8");
	} else {
		// Helles Theme: QPalette-Farben
		auto col = [&](QPalette::ColorRole role) {
			return pal.color(role).name();
		};
		c.window_bg       = col(QPalette::Window);
		c.panel_bg        = col(QPalette::Window); // hell = Window selbst
		c.input_bg        = col(QPalette::Base);
		c.border          = col(QPalette::Mid);
		c.border_hover    = col(QPalette::Dark);
		c.text            = col(QPalette::WindowText);
		c.text_muted      = col(QPalette::Mid);
		c.text_placeholder = col(QPalette::Mid);
		c.highlight       = col(QPalette::Highlight);
		c.highlight_hover = QColor(pal.color(QPalette::Highlight).lighter(120)).name();
		c.highlight_pressed = QColor(pal.color(QPalette::Highlight).darker(120)).name();
		c.danger          = QStringLiteral("#E85E75");
		c.danger_border   = QStringLiteral("#C01C37");
		c.scrollbar_track = col(QPalette::Window);
		c.scrollbar_thumb = col(QPalette::Mid);
		c.scrollbar_thumb_hover = col(QPalette::Dark);
		c.icon_color      = col(QPalette::Mid);
	}
	return c;
}

inline QString base_qss()
{
	const auto c = resolve_colors();

	return QStringLiteral(R"QSS(
QWidget#textboxDock,
QWidget#tbCardsContainer,
QDialog#tbImportDialog,
QDialog#tbQuickProfilesDialog,
QDialog#tbSettingsDialog {
	background-color: %1;
	color: %2;
	font-family: 'Open Sans', 'Segoe UI', sans-serif;
	font-size: 11pt;
}

QWidget#textboxDock {
	background-color: %1;
	border: 1px solid %3;
	border-radius: 0px;
}

QScrollArea {
	background-color: transparent;
	border: none;
}

QToolTip {
	background-color: %4;
	color: %5;
	border: 1px solid %3;
	padding: 4px 6px;
}

QMenu {
	background-color: %1;
	border: 1px solid %3;
	padding: 2px;
}

QMenu::item {
	padding: 4px 24px 4px 8px;
	border-radius: 2px;
	border: 1px solid transparent;
}

QMenu::item:selected {
	background-color: %6;
	border: 1px solid %7;
}

QMenu::item:hover {
	background-color: %3;
	border: 1px solid %8;
}

QMenu::item:disabled {
	color: %9;
	background: transparent;
}

QMenu::separator {
	background: %3;
	height: 1px;
	margin: 2px 6px;
}

/* ---- Karten ---- */

QFrame#tbCardOuter {
	background-color: transparent;
	border: 1px solid %3;
	border-radius: 4px;
}

QFrame#tbCardInner {
	background-color: %4;
	border: none;
	border-top-left-radius: 0px;
	border-top-right-radius: 0px;
	border-bottom-left-radius: 4px;
	border-bottom-right-radius: 4px;
}

QFrame#tbCardInner:hover {
	background-color: %8;
}

QFrame#tbIdBar {
	background-color: %3;
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
	color: %2;
}

QLabel#tbIdPrefix {
	color: %9;
	font-size: 10pt;
}

QLabel#tbIdValue {
	color: %2;
	font-size: 12pt;
	font-weight: bold;
}

QLabel#tbFieldLabel {
	color: %5;
	font-size: 10pt;
}

QLabel#tbImportStatus,
QLabel#tbDockStatus,
QLabel#tbQuickStatus,
QLabel#tbSettingsStatus,
QLabel#tbAiSourceLabel {
	color: %9;
	font-size: 10pt;
}

QLabel#tbImportError {
	color: %10;
	font-size: 10pt;
	font-weight: bold;
}

QLabel#tbKopfWochen,
QLabel#tbKopfTag,
QLabel#tbKopfVerantwortlich,
QLabel#tbKopfZeitstempel {
	color: %2;
	font-size: 11pt;
}

/* ---- Eingaben ---- */

QLineEdit,
QPlainTextEdit {
	background-color: %11;
	border: 1px solid %3;
	border-radius: 4px;
	padding: 4px 8px;
	min-height: 22px;
	color: %2;
	selection-background-color: %6;
	selection-color: %2;
}

QLineEdit:hover,
QPlainTextEdit:hover {
	background-color: %1;
	border: 1px solid %12;
}

QLineEdit:focus,
QPlainTextEdit:focus {
	border: 1px solid rgba(40, 76, 184, 102); /* #284CB8 at 40% alpha */
}

QLineEdit:disabled,
QPlainTextEdit:disabled {
	background-color: %4;
	color: %9;
}

QComboBox {
	background-color: %11;
	border: 1px solid %3;
	border-radius: 4px;
	padding: 4px 8px;
	min-height: 22px;
	color: %2;
}

QComboBox:hover {
	border: 1px solid %12;
}

QComboBox:focus,
QComboBox:on {
	border: 1px solid rgba(40, 76, 184, 102);
}

QComboBox::drop-down {
	border: none;
	border-left: 1px solid %4;
	width: 22px;
}

QComboBox QAbstractItemView {
	background-color: %4;
	border: 1px solid %3;
	color: %2;
	padding: 2px;
	selection-background-color: %6;
	selection-color: %2;
	outline: 0;
}

QComboBox QAbstractItemView::item {
	padding: 4px 8px;
	border-radius: 2px;
}

QComboBox QAbstractItemView::item:hover,
QComboBox QAbstractItemView::item:selected {
	background-color: %6;
}

/* ---- Buttons ---- */

QPushButton {
	background-color: %3;
	border: 1px solid %3;
	border-radius: 4px;
	padding: 6px 12px;
	color: %2;
	min-height: 24px;
}

QPushButton:hover {
	background-color: %8;
}

QPushButton:pressed {
	background-color: %1;
}

QPushButton:disabled {
	background-color: %4;
	border: 1px solid %3;
	color: %9;
}

QToolButton {
	color: %13;
}

QToolButton::hover {
	color: %2;
}

/* Einheitliche Icon-Button-Klasse (24x24) */
.iconbtn,
QPushButton#tbBtnDeleteProfile,
QPushButton#tbBtnRenameProfile,
QPushButton#tbTemplatePick,
QPushButton#tbDelBtn {
	min-width: 24px;
	max-width: 24px;
	min-height: 24px;
	max-height: 24px;
	padding: 0px;
	background: transparent;
	border: none;
	border-radius: 4px;
	color: %13;
	font-size: 10pt;
}
.iconbtn:hover,
QPushButton#tbBtnDeleteProfile:hover,
QPushButton#tbBtnRenameProfile:hover,
QPushButton#tbTemplatePick:hover,
QPushButton#tbDelBtn:hover {
	background-color: %3;
	color: %2;
}
.iconbtn:pressed,
QPushButton#tbBtnDeleteProfile:pressed,
QPushButton#tbBtnRenameProfile:pressed,
QPushButton#tbTemplatePick:pressed,
QPushButton#tbDelBtn:pressed {
	background-color: %8;
}

QPushButton#tbDelBtn {
	color: %9;
	font-size: 12pt;
}
QPushButton#tbDelBtn:hover {
	background-color: %3;
	color: %10;
}

/* Primary-Aktionen (blau) */
QPushButton#tbBtnAccent,
QPushButton#tbBtnImport,
QPushButton#tbBtnApiImport,
QPushButton#tbBtnCsvImport,
QPushButton#tbBtnAiStart,
QPushButton#tbBtnAiApply,
QPushButton#tbBtnQuickProfiles,
QPushButton#tbBtnBackup,
QPushButton#tbBtnRestore,
QPushButton#tbBtnSettings,
QPushButton#tbBtnQuickSave,
QPushButton#tbBtnQuickApply {
	background-color: %6;
	border: 1px solid %6;
	color: %2;
}

QPushButton#tbBtnAccent:hover,
QPushButton#tbBtnImport:hover,
QPushButton#tbBtnApiImport:hover,
QPushButton#tbBtnCsvImport:hover,
QPushButton#tbBtnAiStart:hover,
QPushButton#tbBtnAiApply:hover,
QPushButton#tbBtnQuickProfiles:hover,
QPushButton#tbBtnBackup:hover,
QPushButton#tbBtnRestore:hover,
QPushButton#tbBtnSettings:hover,
QPushButton#tbBtnQuickSave:hover,
QPushButton#tbBtnQuickApply:hover {
	background-color: %7;
	border: 1px solid %7;
}

QPushButton#tbBtnAccent:pressed,
QPushButton#tbBtnImport:pressed,
QPushButton#tbBtnApiImport:pressed,
QPushButton#tbBtnCsvImport:pressed,
QPushButton#tbBtnAiStart:pressed,
QPushButton#tbBtnAiApply:pressed,
QPushButton#tbBtnQuickProfiles:pressed,
QPushButton#tbBtnBackup:pressed,
QPushButton#tbBtnRestore:pressed,
QPushButton#tbBtnSettings:pressed,
QPushButton#tbBtnQuickSave:pressed,
QPushButton#tbBtnQuickApply:pressed {
	background-color: %14;
}

QPushButton#tbBtnApiImport:disabled,
QPushButton#tbBtnCsvImport:disabled,
QPushButton#tbBtnAiStart:disabled,
QPushButton#tbBtnAiApply:disabled,
QPushButton#tbBtnQuickApply:disabled,
QPushButton#tbBtnQuickDelete:disabled {
	background-color: %4;
	border: 1px solid %3;
	color: %9;
}

/* Danger */
QPushButton#tbBtnRed,
QPushButton#tbBtnQuickDelete {
	background-color: %4;
	border: 1px solid %15;
	color: %10;
}

QPushButton#tbBtnRed:hover,
QPushButton#tbBtnQuickDelete:hover {
	background-color: %15;
	border: 1px solid %15;
	color: %2;
}

/* ---- Scrollbars: 8px ---- */

QScrollBar:vertical {
	background: %16;
	width: 8px;
	margin: 0px;
	border: none;
}

QScrollBar::handle:vertical {
	background: %17;
	margin: 1px;
	min-height: 32px;
	border-radius: 4px;
	border: none;
}

QScrollBar::handle:vertical:hover {
	background: %18;
}

QScrollBar::handle:vertical:pressed {
	background: %7;
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
	background: %16;
	height: 8px;
	margin: 0px;
	border: none;
}

QScrollBar::handle:horizontal {
	background: %17;
	margin: 1px;
	min-width: 32px;
	border-radius: 4px;
	border: none;
}

QScrollBar::handle:horizontal:hover {
	background: %18;
}

QScrollBar::handle:horizontal:pressed {
	background: %7;
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

/* ---- Slider ---- */

QSlider:horizontal {
	min-height: 20px;
	background: transparent;
}

QSlider::groove:horizontal {
	background-color: %3;
	height: 4px;
	border-radius: 2px;
	border: none;
}

QSlider::sub-page:horizontal {
	background-color: %7;
	border-radius: 2px;
	border: none;
}

QSlider::add-page:horizontal {
	background-color: %3;
	border-radius: 2px;
	border: none;
}

QSlider::handle:horizontal {
	background-color: %2;
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

/* ---- Checkboxes ---- */

QCheckBox {
	background-color: transparent;
	color: %2;
	spacing: 8px;
}

QCheckBox::indicator {
	width: 14px;
	height: 14px;
	background-color: %3;
	border: 1px solid %12;
	border-radius: 3px;
}

QCheckBox::indicator:hover {
	border: 1px solid %7;
}

QCheckBox::indicator:checked {
	background-color: %6;
	border: 1px solid %6;
}

QGroupBox {
	background-color: %4;
	border: 1px solid %3;
	border-radius: 4px;
	margin-top: 14px;
	padding-top: 14px;
	color: %2;
	font-weight: bold;
}

QGroupBox#tbAnimGroup,
QGroupBox#tbAiImportGroup {
	background-color: %4;
	border: 1px solid %3;
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
	color: %2;
}

QGroupBox::indicator {
	width: 14px;
	height: 14px;
	background-color: %3;
	border: 1px solid %12;
	border-radius: 3px;
	margin-left: 2px;
	margin-right: 4px;
}

QGroupBox::indicator:hover {
	border: 1px solid %7;
}

QGroupBox::indicator:checked {
	background-color: %6;
	border: 1px solid %6;
}

/* ---- Tabellen ---- */

QTableWidget,
QListWidget,
QListWidget#tbProfileList {
	background-color: %4;
	border: 1px solid %3;
	border-radius: 4px;
	gridline-color: %3;
	color: %2;
	selection-background-color: %6;
	selection-color: %2;
}

QHeaderView::section {
	background-color: %3;
	color: %2;
	padding: 4px 8px;
	border: none;
	border-left: 1px solid %1;
	border-right: 1px solid %1;
}

/* ---- Fortschritt ---- */

QProgressBar {
	background-color: %4;
	border: 1px solid %3;
	border-radius: 4px;
	text-align: center;
	color: %2;
}

QProgressBar::chunk {
	background-color: %6;
}

/* ---- Dock-Titelbalken ---- */

QDockWidget {
	font-weight: bold;
}

QDockWidget::title {
	text-align: left;
	background-color: %3;
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
	background: %8;
}

QDockWidget::close-button:pressed,
QDockWidget::float-button:pressed {
	background: %1;
}

/* ---- Footer-Toolbar / Flash / Stil-Sektion ---- */

QFrame#tbCardOuter[tbFlash="true"] {
	border: 1px solid %7;
}

QFrame#tbStyleSection {
	background: transparent;
	border: none;
}

QToolButton#tbToolAdd,
QToolButton#tbToolClear,
QToolButton#tbToolImport,
QToolButton#tbToolSettings,
QToolButton#tbProfilDelBtn {
	background: %3;
	border: 1px solid %3;
	border-radius: 4px;
	color: %2;
}

QToolButton#tbToolAdd:hover,
QToolButton#tbToolClear:hover,
QToolButton#tbToolImport:hover,
QToolButton#tbToolSettings:hover,
QToolButton#tbProfilDelBtn:hover {
	background: %7;
	border-color: %7;
}

QToolButton#tbToolAdd:pressed,
QToolButton#tbToolClear:pressed,
QToolButton#tbToolImport:pressed,
QToolButton#tbToolSettings:pressed,
QToolButton#tbProfilDelBtn:pressed {
	background: %6;
	border-color: %6;
}

QToolButton#tbStyleToggle {
	color: %13;
}
QToolButton#tbStyleToggle:hover {
	background: %7;
	color: %2;
}

QPushButton#tbToolUndo {
	background-color: transparent;
	border: 1px solid %3;
	border-radius: 4px;
	padding: 4px 8px;
	color: %5;
	font-size: 10pt;
	min-height: 24px;
}
QPushButton#tbToolUndo:hover {
	background-color: %3;
	color: %2;
}

QPushButton#tbBtnNewProfile {
	background-color: %3;
	border: 1px solid %3;
	border-radius: 4px;
	padding: 4px 8px;
	color: %2;
	font-size: 10pt;
	min-height: 24px;
}
QPushButton#tbBtnNewProfile:hover {
	background-color: %8;
}

QPushButton#tbBtnGreen {
	background-color: transparent;
	border: 1px solid %7;
	border-radius: 4px;
	padding: 4px 8px;
	color: %2;
	font-size: 10pt;
	min-height: 24px;
}
QPushButton#tbBtnGreen:hover {
	background-color: %7;
}

QPushButton#tbBtnRed {
	background-color: transparent;
	border: 1px solid %15;
	border-radius: 4px;
	padding: 4px 8px;
	color: %10;
	font-size: 10pt;
	min-height: 24px;
}
QPushButton#tbBtnRed:hover {
	background-color: %15;
	border: 1px solid %15;
	color: %2;
}

QPushButton#tbKopfEditCancel,
QPushButton#tbKopfEditOk {
	background-color: %3;
	border: 1px solid %3;
	border-radius: 4px;
	padding: 6px 12px;
	color: %2;
	min-height: 24px;
}
QPushButton#tbKopfEditCancel:hover,
QPushButton#tbKopfEditOk:hover {
	background-color: %8;
}

)QSS")
	.arg(c.window_bg)       // %1
	.arg(c.text)             // %2
	.arg(c.border)           // %3
	.arg(c.panel_bg)         // %4
	.arg(c.text_muted)       // %5
	.arg(c.highlight)        // %6
	.arg(c.highlight_hover)  // %7
	.arg(c.border_hover)     // %8
	.arg(c.text_placeholder) // %9
	.arg(c.danger)           // %10
	.arg(c.input_bg)         // %11
	.arg(c.border_hover)     // %12
	.arg(c.icon_color)       // %13
	.arg(c.highlight_pressed)// %14
	.arg(c.danger_border)    // %15
	.arg(c.scrollbar_track)  // %16
	.arg(c.scrollbar_thumb)  // %17
	.arg(c.scrollbar_thumb_hover); // %18
}

/*
Ui4: Natives OBS-Icon (aus dem Yami-Theme, in der Plugin-DLL eingebettet und
beim Modulstart nach <config>/icons/ geschrieben — s. textbox-theme-icons.hpp).
icon_dir = leeres QString -> Stylesheet ohne Icon-Regeln (Fallback).
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

} // namespace textbox_theme