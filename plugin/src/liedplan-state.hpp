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

#include <cstddef>
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
	std::string corner_color_a = "#fce647";
	std::string corner_color_b = "#fce647";
	int corner_opacity_a = 255; // 0..255
	int corner_opacity_b = 255; // 0..255
	bool rainbow_corners = false;

	bool operator==(const LiedplanCard &other) const
	{
		return id == other.id && zeile1 == other.zeile1 && zeile2 == other.zeile2 &&
		       zeile3 == other.zeile3 && profile == other.profile &&
		       text_color == other.text_color && text_opacity == other.text_opacity &&
		       box_color == other.box_color && box_opacity == other.box_opacity &&
		       corner_color_a == other.corner_color_a &&
		       corner_color_b == other.corner_color_b &&
		       corner_opacity_a == other.corner_opacity_a &&
		       corner_opacity_b == other.corner_opacity_b &&
		       rainbow_corners == other.rainbow_corners;
	}
};

struct LiedplanSettings {
	std::string google_form_id = "1jreAuzd-jt1x48WDmjaP7v0OusdVO2lb55Kt4JrhEgY";
	std::string token_path = "C:\\Users\\Johann\\AppData\\Local\\hermes\\google_token.json";

	bool operator==(const LiedplanSettings &other) const
	{
		return google_form_id == other.google_form_id && token_path == other.token_path;
	}
};

struct LiedplanQuickProfile {
	std::string name;
	std::vector<LiedplanCard> cards;

	bool operator==(const LiedplanQuickProfile &other) const
	{
		return name == other.name && cards == other.cards;
	}
};

class LiedplanState {
public:
	std::vector<LiedplanCard> cards;
	int next_id = 1; // Id-Zähler; Ids "box1","box2",... werden NIEMALS wiederverwendet
	std::string active_profile = "OBS Dark";
	LiedplanSettings settings;
	std::vector<LiedplanQuickProfile> quick_profiles;

	std::string load(const std::string &path); // "" = ok; sonst Fehlerbeschreibung
	std::string save(const std::string &path) const; // "" = ok
	std::string restore_backup(const std::string &path); // "" = ok; bei Fehler bleibt State unverändert

	std::string serialize_json() const;
	std::string deserialize_json(const std::string &json_str);

	const LiedplanCard *find(const std::string &id) const;

	LiedplanCard &add_card();
	bool remove_card(const std::string &id);

	void save_quick_profile(const std::string &name);
	bool apply_quick_profile(std::size_t index);
	bool apply_quick_profile(const std::string &name);
	bool remove_quick_profile(std::size_t index);

	bool operator==(const LiedplanState &other) const
	{
		return cards == other.cards && next_id == other.next_id &&
		       active_profile == other.active_profile && settings == other.settings &&
		       quick_profiles == other.quick_profiles;
	}
};
