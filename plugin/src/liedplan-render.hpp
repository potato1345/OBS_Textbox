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

#include "liedplan-state.hpp"

#include <QImage>
#include <QtGlobal>

inline constexpr int kLiedplanCardWidth = 800;
inline constexpr int kLiedplanCardHeight = 400;

// Rendert eine LiedplanCard als 800x400 ARGB32-QImage (außen transparent).
// time_seconds ist für M2a ungenutzt (0.0) und für M2b (Hue-Animation) vorgesehen.
QImage render_card(const LiedplanCard &card, qreal time_seconds = 0.0);
QImage render_card(const LiedplanCard &card, int width, int height, qreal time_seconds);
