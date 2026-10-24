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

#ifndef _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#endif

#include "liedplan-state.hpp"
#include "liedplan-profiles.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>

namespace {

std::filesystem::path to_fs_path(const std::string &utf8_path)
{
	return std::filesystem::u8path(utf8_path);
}

} // namespace

std::string LiedplanState::load(const std::string &path)
{
	cards.clear();
	next_id = 1;

	if (path.empty())
		return "";

	std::error_code ec;
	const std::filesystem::path fs_path = to_fs_path(path);
	if (!std::filesystem::exists(fs_path, ec)) {
		if (ec)
			return "Filesystem error: " + ec.message();
		return "";
	}

	std::ifstream file(fs_path, std::ios::binary);
	if (!file.is_open())
		return "Failed to open state file for reading: " + path;

	std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	if (file.bad())
		return "Failed to read state file: " + path;

	if (content.find_first_not_of(" \t\r\n") == std::string::npos)
		return "";

	nlohmann::json root = nlohmann::json::parse(content, nullptr, false);
	if (root.is_discarded() || !root.is_object())
		return "Invalid JSON in state file: " + path;

	int loaded_next_id = 1;
	if (root.contains("next_id")) {
		if (!root["next_id"].is_number_integer())
			return "Invalid 'next_id' field in state file: " + path;
		loaded_next_id = root["next_id"].get<int>();
		if (loaded_next_id < 1)
			loaded_next_id = 1;
	}

	std::vector<LiedplanCard> loaded_cards;
	if (root.contains("cards")) {
		if (!root["cards"].is_array())
			return "Invalid 'cards' field in state file: " + path;

		for (const auto &item : root["cards"]) {
			if (!item.is_object())
				return "Invalid card entry in state file: " + path;

			LiedplanCard c;
			if (item.contains("id") && item["id"].is_string())
				c.id = item["id"].get<std::string>();
			if (item.contains("zeile1") && item["zeile1"].is_string())
				c.zeile1 = item["zeile1"].get<std::string>();
			if (item.contains("zeile2") && item["zeile2"].is_string())
				c.zeile2 = item["zeile2"].get<std::string>();
			if (item.contains("zeile3") && item["zeile3"].is_string())
				c.zeile3 = item["zeile3"].get<std::string>();
			if (item.contains("profile") && item["profile"].is_string())
				c.profile = item["profile"].get<std::string>();
			if (item.contains("text_color") && item["text_color"].is_string())
				c.text_color = item["text_color"].get<std::string>();
			if (item.contains("text_opacity") && item["text_opacity"].is_number_integer())
				c.text_opacity = item["text_opacity"].get<int>();
			if (item.contains("box_color") && item["box_color"].is_string())
				c.box_color = item["box_color"].get<std::string>();
			if (item.contains("box_opacity") && item["box_opacity"].is_number_integer())
				c.box_opacity = item["box_opacity"].get<int>();

			if (c.id.rfind("box", 0) == 0 && c.id.size() > 3) {
				try {
					int num = std::stoi(c.id.substr(3));
					if (num >= loaded_next_id)
						loaded_next_id = num + 1;
				} catch (...) {
				}
			}

			loaded_cards.push_back(std::move(c));
		}
	}

	cards = std::move(loaded_cards);
	next_id = loaded_next_id;
	return "";
}

std::string LiedplanState::save(const std::string &path) const
{
	if (path.empty())
		return "Empty state path";

	const std::filesystem::path fs_path = to_fs_path(path);
	if (fs_path.has_parent_path() && !fs_path.parent_path().empty()) {
		std::error_code ec;
		std::filesystem::create_directories(fs_path.parent_path(), ec);
		if (ec)
			return "Failed to create directory: " + ec.message();
	}

	nlohmann::json cards_json = nlohmann::json::array();
	for (const auto &c : cards) {
		cards_json.push_back({
			{"id", c.id},
			{"zeile1", c.zeile1},
			{"zeile2", c.zeile2},
			{"zeile3", c.zeile3},
			{"profile", c.profile},
			{"text_color", c.text_color},
			{"text_opacity", c.text_opacity},
			{"box_color", c.box_color},
			{"box_opacity", c.box_opacity},
		});
	}

	nlohmann::json root = {
		{"version", 1},
		{"next_id", next_id},
		{"cards", std::move(cards_json)},
	};

	const std::string serialized = root.dump(2);
	std::ofstream file(fs_path, std::ios::binary | std::ios::trunc);
	if (!file.is_open())
		return "Failed to open state file for writing: " + path;

	file.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
	file.flush();
	if (!file.good())
		return "Failed to write state file: " + path;

	return "";
}

const LiedplanCard *LiedplanState::find(const std::string &id) const
{
	for (const auto &c : cards) {
		if (c.id == id)
			return &c;
	}
	return nullptr;
}

LiedplanCard &LiedplanState::add_card()
{
	LiedplanCard c;
	c.id = "box" + std::to_string(next_id++);
	c.profile = "OBS Dark";
	if (const LiedplanProfile *prof = profile_by_name(c.profile)) {
		c.text_color = prof->text_color;
		c.box_color = prof->box_color;
	}
	cards.push_back(std::move(c));
	return cards.back();
}

bool LiedplanState::remove_card(const std::string &id)
{
	auto it = std::find_if(cards.begin(), cards.end(),
			       [&id](const LiedplanCard &c) { return c.id == id; });
	if (it == cards.end())
		return false;
	cards.erase(it);
	return true;
}
