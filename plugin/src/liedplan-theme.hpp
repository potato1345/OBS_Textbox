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

#pragma once

#include <QString>

namespace liedplan_theme {

inline QString stylesheet()
{
	return QStringLiteral(R"QSS(
QWidget#liedplanDock,
QWidget#lpCardsContainer,
QDialog#lpImportDialog,
QDialog#lpQuickProfilesDialog,
QDialog#lpSettingsDialog {
	background-color: #1C1F26;
	color: #EFF0F1;
	font-family: 'Inter', 'Segoe UI', sans-serif;
	font-size: 13px;
}

QScrollArea {
	background-color: transparent;
	border: none;
}

QFrame#lpCardOuter {
	background-color: #3C404D;
	border: none;
	border-radius: 8px;
}

QFrame#lpCardInner {
	background-color: #272A33;
	border: none;
	border-top-left-radius: 0px;
	border-top-right-radius: 0px;
	border-bottom-left-radius: 8px;
	border-bottom-right-radius: 8px;
}

QFrame#lpCardInner:hover {
	background-color: #414852;
}

QFrame#lpIdBar {
	background-color: #3C404D;
	border: none;
	border-radius: 8px;
}

QLabel {
	background-color: transparent;
	border: none;
	color: #EFF0F1;
	font-family: 'Inter', 'Segoe UI', sans-serif;
}

QLabel#lpIdPrefix {
	color: #B0B6BD;
	font-size: 11px;
	font-weight: 600;
	letter-spacing: 0.5px;
}

QLabel#lpIdValue {
	color: #EFF0F1;
	font-size: 14px;
	font-weight: 600;
}

QLabel#lpFieldLabel {
	color: #B0B6BD;
	font-size: 11px;
	font-weight: 600;
	letter-spacing: 0.4px;
}

QLineEdit,
QPlainTextEdit {
	background-color: #323A44;
	border: 1px solid #3C404D;
	border-radius: 8px;
	padding: 7px 10px;
	color: #EFF0F1;
	font-family: 'Inter', 'Segoe UI', sans-serif;
	font-size: 13px;
	selection-background-color: rgba(61, 174, 233, 0.35);
	selection-color: #EFF0F1;
}

QLineEdit:focus,
QPlainTextEdit:focus {
	background-color: #272A33;
	border: 1px solid #3DAEE9;
}

QComboBox {
	background-color: #323A44;
	border: 1px solid #3C404D;
	border-radius: 8px;
	padding: 7px 10px;
	color: #EFF0F1;
	font-family: 'Inter', 'Segoe UI', sans-serif;
	font-size: 13px;
}

QComboBox:hover,
QComboBox:focus {
	border: 1px solid #3DAEE9;
}

QComboBox::drop-down {
	border: none;
	width: 22px;
}

QComboBox QAbstractItemView {
	background-color: #272A33;
	border: 1px solid #3C404D;
	color: #EFF0F1;
	selection-background-color: #3DAEE9;
	selection-color: #1C1F26;
	outline: 0;
}

QPushButton,
QPushButton#lpBtn,
QPushButton#lpColorA,
QPushButton#lpColorB {
	background-color: #272A33;
	border: 1px solid #3C404D;
	border-radius: 8px;
	padding: 8px 14px;
	color: #EFF0F1;
	font-family: 'Inter', 'Segoe UI', sans-serif;
	font-size: 13px;
	font-weight: 600;
}

QPushButton#lpColorA,
QPushButton#lpColorB {
	padding: 4px 10px;
}

QPushButton:hover,
QPushButton#lpBtn:hover,
QPushButton#lpColorA:hover,
QPushButton#lpColorB:hover {
	background-color: #414852;
}

QPushButton#lpBtnAccent {
	background-color: #272A33;
	border: 1px solid #3DAEE9;
	color: #3DAEE9;
}

QPushButton#lpBtnAccent:hover {
	background-color: #414852;
}

QPushButton#lpBtnRed {
	background-color: #272A33;
	border: 1px solid #F87171;
	color: #F87171;
}

QPushButton#lpBtnRed:hover {
	background-color: rgba(248, 113, 113, 0.15);
}

QPushButton#lpBtnGreen {
	background-color: #272A33;
	border: 1px solid rgba(61, 174, 233, 0.14);
	color: #EFF0F1;
}

QPushButton#lpBtnGreen:hover {
	background-color: #414852;
}

QPushButton#lpDelBtn {
	background-color: transparent;
	border: none;
	border-radius: 6px;
	padding: 0px;
	color: #B0B6BD;
	font-size: 14px;
	font-weight: 600;
	min-width: 28px;
	max-width: 28px;
	min-height: 28px;
	max-height: 28px;
}

QPushButton#lpDelBtn:hover {
	background-color: rgba(248, 113, 113, 0.15);
	color: #F87171;
}

QScrollBar:vertical {
	background: #1C1F26;
	width: 8px;
	margin: 0px;
	border: none;
}

QScrollBar::handle:vertical {
	background: #414852;
	min-height: 24px;
	border-radius: 4px;
}

QScrollBar::handle:vertical:hover {
	background: #3DAEE9;
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
	background: #1C1F26;
	height: 8px;
	margin: 0px;
	border: none;
}

QScrollBar::handle:horizontal {
	background: #414852;
	min-width: 24px;
	border-radius: 4px;
}

QScrollBar::handle:horizontal:hover {
	background: #3DAEE9;
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

QSlider:horizontal {
	min-height: 20px;
	background: transparent;
}

QSlider::groove:horizontal {
	background-color: #323A44;
	height: 6px;
	border-radius: 3px;
	border: none;
}

QSlider::sub-page:horizontal,
QSlider::add-page:horizontal {
	background-color: #323A44;
	border-radius: 3px;
	border: none;
}

QSlider::handle:horizontal {
	background-color: #3DAEE9;
	width: 12px;
	height: 12px;
	margin: -3px 0;
	border-radius: 6px;
	border: none;
}

QSlider::handle:horizontal:hover {
	background-color: #5cc0f0;
}

QCheckBox {
	background-color: transparent;
	color: #EFF0F1;
	spacing: 8px;
	font-family: 'Inter', 'Segoe UI', sans-serif;
	font-size: 13px;
}

QCheckBox::indicator {
	width: 16px;
	height: 16px;
	background-color: #323A44;
	border: 1px solid #3C404D;
	border-radius: 4px;
}

QCheckBox::indicator:hover {
	border: 1px solid #3DAEE9;
}

QCheckBox::indicator:checked {
	background-color: #3DAEE9;
	border: 1px solid #3DAEE9;
}

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
	background-color: #272A33;
	border: 1px solid #3DAEE9;
	color: #3DAEE9;
}

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
	background-color: #414852;
}

QPushButton#lpBtnApiImport:disabled,
QPushButton#lpBtnCsvImport:disabled,
QPushButton#lpBtnAiStart:disabled,
QPushButton#lpBtnAiApply:disabled,
QPushButton#lpBtnQuickApply:disabled,
QPushButton#lpBtnQuickDelete:disabled {
	background-color: #1C1F26;
	border: 1px solid #3C404D;
	color: #B0B6BD;
}

QPushButton#lpBtnQuickDelete {
	background-color: #272A33;
	border: 1px solid #F87171;
	color: #F87171;
}

QPushButton#lpBtnQuickDelete:hover {
	background-color: rgba(248, 113, 113, 0.15);
}

QGroupBox {
	background-color: #272A33;
	border: 1px solid #3C404D;
	border-radius: 8px;
	margin-top: 12px;
	padding-top: 12px;
	color: #EFF0F1;
	font-weight: 600;
}

QGroupBox#lpAnimGroup {
	background-color: #272A33;
	border: 1px solid #3C404D;
	border-radius: 8px;
	margin-top: 8px;
	padding-top: 8px;
}

QGroupBox::title {
	subcontrol-origin: margin;
	subcontrol-position: top left;
	left: 12px;
	padding: 0 4px;
	color: #3DAEE9;
}

QGroupBox::indicator {
	width: 14px;
	height: 14px;
	background-color: #323A44;
	border: 1px solid #3C404D;
	border-radius: 4px;
}

QGroupBox::indicator:hover {
	border: 1px solid #3DAEE9;
}

QGroupBox::indicator:checked {
	background-color: #3DAEE9;
	border: 1px solid #3DAEE9;
}

QTableWidget {
	background-color: #272A33;
	border: 1px solid #3C404D;
	border-radius: 8px;
	gridline-color: #3C404D;
	color: #EFF0F1;
	selection-background-color: rgba(61, 174, 233, 0.25);
	selection-color: #EFF0F1;
}

QHeaderView::section {
	background-color: #323A44;
	color: #B0B6BD;
	padding: 6px 8px;
	border: none;
	border-bottom: 1px solid #3C404D;
	font-weight: 600;
	font-size: 11px;
}

QLabel#lpImportStatus,
QLabel#lpDockStatus,
QLabel#lpQuickStatus,
QLabel#lpSettingsStatus,
QLabel#lpAiSourceLabel {
	color: #B0B6BD;
	font-size: 12px;
}

QLabel#lpImportError {
	color: #E85D5D;
	font-size: 12px;
	font-weight: 600;
}

QLabel#lpKopfWochen,
QLabel#lpKopfTag,
QLabel#lpKopfOrganist,
QLabel#lpKopfZeitstempel {
	color: #EFF0F1;
	font-size: 12px;
}
)QSS");
}

} // namespace liedplan_theme
