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

#include <obs.h>
#include <string>

namespace liedplan_scene {

// Quellenname in OBS: "Liedplan " + card_id (z. B. "Liedplan box1").
std::string source_name(const std::string &card_id);

// Erzeugt obs_data_t mit den browser_source-Settings für eine Karte (Caller: obs_data_release).
obs_data_t *make_settings(const LiedplanCard &card);
obs_data_t *make_settings(const LiedplanCard &card, const std::string &local_file);

// Legt die Text-Quelle in der aktuellen Szene an oder aktualisiert sie dort.
void send_to_current_scene(const LiedplanCard &card);

// Aktualisiert alle bereits platzierten Kopien dieser Karte in allen Szenen.
void update_everywhere(const LiedplanCard &card, bool clear);

// Entfernt alle Szenen-Items dieser Karte aus allen Szenen und gibt die Source frei.
void remove_everywhere(const std::string &card_id);

} // namespace liedplan_scene
