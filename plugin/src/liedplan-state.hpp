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

#include <string>
#include <vector>

struct LiedplanCard {
	std::string id; // z. B. "box1"
	std::string zeile1; // GL-Nummer
	std::string zeile2; // Verse
	std::string zeile3; // Liedtitel
	std::string profile = "OBS Dark";
	std::string text_color = "#000000";
	int text_opacity = 255;
	std::string box_color = "#ffffff";
	int box_opacity = 255;
};

class LiedplanState {
public:
	std::vector<LiedplanCard> cards;
	int next_id = 1; // Id-Zähler; Ids "box1","box2",... werden NIEMALS wiederverwendet

	std::string load(const std::string &path); // "" = ok; sonst Fehlerbeschreibung
	std::string save(const std::string &path) const; // "" = ok
	const LiedplanCard *find(const std::string &id) const;

	LiedplanCard &add_card();
	bool remove_card(const std::string &id);
};
