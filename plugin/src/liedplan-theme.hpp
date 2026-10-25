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
QWidget#lpCardsContainer {
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

QLineEdit {
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

QLineEdit:focus {
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
QPushButton#lpBtn {
	background-color: #272A33;
	border: 1px solid #3C404D;
	border-radius: 8px;
	padding: 8px 14px;
	color: #EFF0F1;
	font-family: 'Inter', 'Segoe UI', sans-serif;
	font-size: 13px;
	font-weight: 600;
}

QPushButton:hover,
QPushButton#lpBtn:hover {
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
)QSS");
}

} // namespace liedplan_theme
