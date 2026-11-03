/*
 * liedplan-state.cpp — Implementierung von Zustandsspeicherung und JSON-/YAML-Parsing (v3).
 *
 * Zweck:
 *   - Liest und schreibt Zustand (liedplan-state.json) atomar über Dateisystem.
 *   - Deserialisiert und validiert Karten, Schnelleinstellungen und Farbprofile.
 *   - Bietet Backup- und Wiederherstellungsfunktionen für Profilkonfigurationen.
 *
 * Hinweis: Qt- und libobs-frei; wird auch in Standalone-Tests (test-liedplan-state) verwendet.
 */

#ifndef _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#endif

#include "liedplan-state.hpp"
#include "liedplan-profiles.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <sstream>

namespace {

std::filesystem::path to_fs_path(const std::string &utf8_path)
{
	return std::filesystem::u8path(utf8_path);
}

LiedplanCard card_from_json(const nlohmann::json &item)
{
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
	if (item.contains("corner_color_a") && item["corner_color_a"].is_string())
		c.corner_color_a = item["corner_color_a"].get<std::string>();
	if (item.contains("corner_color_b") && item["corner_color_b"].is_string())
		c.corner_color_b = item["corner_color_b"].get<std::string>();
	if (item.contains("corner_opacity_a") && item["corner_opacity_a"].is_number_integer())
		c.corner_opacity_a = item["corner_opacity_a"].get<int>();
	if (item.contains("corner_opacity_b") && item["corner_opacity_b"].is_number_integer())
		c.corner_opacity_b = item["corner_opacity_b"].get<int>();
	if (item.contains("rainbow_corners") && item["rainbow_corners"].is_boolean())
		c.rainbow_corners = item["rainbow_corners"].get<bool>();
	if (item.contains("anim_file") && item["anim_file"].is_string())
		c.anim_file = item["anim_file"].get<std::string>();
	if (item.contains("show_anim") && item["show_anim"].is_boolean())
		c.show_anim = item["show_anim"].get<bool>();
	if (item.contains("anim_scale") && item["anim_scale"].is_number_integer())
		c.anim_scale = item["anim_scale"].get<int>();
	if (item.contains("anim_x") && item["anim_x"].is_number_integer())
		c.anim_x = item["anim_x"].get<int>();
	if (item.contains("anim_y") && item["anim_y"].is_number_integer())
		c.anim_y = item["anim_y"].get<int>();
	return c;
}

nlohmann::json card_to_json(const LiedplanCard &c)
{
	return {
		{"id", c.id},
		{"zeile1", c.zeile1},
		{"zeile2", c.zeile2},
		{"zeile3", c.zeile3},
		{"profile", c.profile},
		{"text_color", c.text_color},
		{"text_opacity", c.text_opacity},
		{"box_color", c.box_color},
		{"box_opacity", c.box_opacity},
		{"corner_color_a", c.corner_color_a},
		{"corner_color_b", c.corner_color_b},
		{"corner_opacity_a", c.corner_opacity_a},
		{"corner_opacity_b", c.corner_opacity_b},
		{"rainbow_corners", c.rainbow_corners},
		{"anim_file", c.anim_file},
		{"show_anim", c.show_anim},
		{"anim_scale", c.anim_scale},
		{"anim_x", c.anim_x},
		{"anim_y", c.anim_y},
	};
}

void update_next_id_from_cards(const std::vector<LiedplanCard> &card_list, int &inout_next_id)
{
	for (const auto &c : card_list) {
		if (c.id.rfind("box", 0) == 0 && c.id.size() > 3) {
			try {
				int num = std::stoi(c.id.substr(3));
				if (num >= inout_next_id)
					inout_next_id = num + 1;
			} catch (...) {
			}
		}
	}
}

nlohmann::json state_to_json(const LiedplanState &s)
{
	nlohmann::json cards_json = nlohmann::json::array();
	for (const auto &c : s.cards) {
		cards_json.push_back(card_to_json(c));
	}

	nlohmann::json qp_json = nlohmann::json::array();
	for (const auto &qp : s.quick_profiles) {
		nlohmann::json qp_cards = nlohmann::json::array();
		for (const auto &c : qp.cards) {
			qp_cards.push_back(card_to_json(c));
		}
		qp_json.push_back({
			{"name", qp.name},
			{"cards", std::move(qp_cards)},
		});
	}

	nlohmann::json cp_json = nlohmann::json::array();
	for (const auto &cp : s.color_profiles) {
		cp_json.push_back({
			{"name", cp.name},
			{"text_color", cp.text_color},
			{"box_color", cp.box_color},
		});
	}

	return {
		{"version", 1},
		{"next_id", s.next_id},
		{"active_profile", s.active_profile},
		{"settings",
		 {
			 {"google_form_id", s.settings.google_form_id},
			 {"token_path", s.settings.token_path},
			 {"openrouter_key_path", s.settings.openrouter_key_path},
			 {"markitdown_python", s.settings.markitdown_python},
			 {"animation_dir", s.settings.animation_dir},
			 {"lieder_path", s.settings.lieder_path},
			 {"ai_system_prompt", s.settings.ai_system_prompt},
		 }},
		{"quick_profiles", std::move(qp_json)},
		{"color_profiles", std::move(cp_json)},
		{"cards", std::move(cards_json)},
	};
}

std::string state_from_json(LiedplanState &s, const nlohmann::json &root)
{
	if (root.is_discarded() || !root.is_object())
		return "Invalid JSON object";

	int loaded_next_id = 1;
	if (root.contains("next_id")) {
		if (!root["next_id"].is_number_integer())
			return "Invalid 'next_id' field in state JSON";
		loaded_next_id = root["next_id"].get<int>();
		if (loaded_next_id < 1)
			loaded_next_id = 1;
	}

	std::string loaded_active_profile = "OBS Dark";
	if (root.contains("active_profile") && root["active_profile"].is_string()) {
		loaded_active_profile = root["active_profile"].get<std::string>();
	}

	LiedplanSettings loaded_settings;
	if (root.contains("settings") && root["settings"].is_object()) {
		const auto &st = root["settings"];
		if (st.contains("google_form_id") && st["google_form_id"].is_string())
			loaded_settings.google_form_id = st["google_form_id"].get<std::string>();
		if (st.contains("token_path") && st["token_path"].is_string())
			loaded_settings.token_path = st["token_path"].get<std::string>();
		if (st.contains("openrouter_key_path") && st["openrouter_key_path"].is_string())
			loaded_settings.openrouter_key_path = st["openrouter_key_path"].get<std::string>();
		if (st.contains("markitdown_python") && st["markitdown_python"].is_string())
			loaded_settings.markitdown_python = st["markitdown_python"].get<std::string>();
		if (st.contains("animation_dir") && st["animation_dir"].is_string())
			loaded_settings.animation_dir = st["animation_dir"].get<std::string>();
		if (st.contains("lieder_path") && st["lieder_path"].is_string())
			loaded_settings.lieder_path = st["lieder_path"].get<std::string>();
		if (st.contains("ai_system_prompt") && st["ai_system_prompt"].is_string())
			loaded_settings.ai_system_prompt = st["ai_system_prompt"].get<std::string>();
	}

	std::vector<LiedplanQuickProfile> loaded_quick_profiles;
	if (root.contains("quick_profiles") && root["quick_profiles"].is_array()) {
		for (const auto &qp_item : root["quick_profiles"]) {
			if (!qp_item.is_object())
				continue;
			LiedplanQuickProfile qp;
			if (qp_item.contains("name") && qp_item["name"].is_string())
				qp.name = qp_item["name"].get<std::string>();
			if (qp_item.contains("cards") && qp_item["cards"].is_array()) {
				for (const auto &c_item : qp_item["cards"]) {
					if (c_item.is_object())
						qp.cards.push_back(card_from_json(c_item));
				}
			}
			loaded_quick_profiles.push_back(std::move(qp));
		}
	}

	std::vector<LiedplanProfile> loaded_color_profiles;
	if (root.contains("color_profiles") && root["color_profiles"].is_array()) {
		for (const auto &cp_item : root["color_profiles"]) {
			if (!cp_item.is_object())
				continue;
			LiedplanProfile cp;
			if (cp_item.contains("name") && cp_item["name"].is_string())
				cp.name = cp_item["name"].get<std::string>();
			if (cp_item.contains("text_color") && cp_item["text_color"].is_string())
				cp.text_color = cp_item["text_color"].get<std::string>();
			if (cp_item.contains("box_color") && cp_item["box_color"].is_string())
				cp.box_color = cp_item["box_color"].get<std::string>();
			loaded_color_profiles.push_back(std::move(cp));
		}
	}

	std::vector<LiedplanCard> loaded_cards;
	if (root.contains("cards")) {
		if (!root["cards"].is_array())
			return "Invalid 'cards' field in state JSON";

		for (const auto &item : root["cards"]) {
			if (!item.is_object())
				return "Invalid card entry in state JSON";

			loaded_cards.push_back(card_from_json(item));
		}
	}

	update_next_id_from_cards(loaded_cards, loaded_next_id);

	s.cards = std::move(loaded_cards);
	s.next_id = loaded_next_id;
	s.active_profile = std::move(loaded_active_profile);
	s.settings = std::move(loaded_settings);
	s.quick_profiles = std::move(loaded_quick_profiles);
	s.color_profiles = std::move(loaded_color_profiles);
	return "";
}

std::string trim_ws(const std::string &s)
{
	const auto b = s.find_first_not_of(" \t\r\n");
	if (b == std::string::npos)
		return "";
	const auto e = s.find_last_not_of(" \t\r\n");
	return s.substr(b, e - b + 1);
}

std::string strip_quotes(const std::string &s)
{
	std::string res = s;
	if (!res.empty() && (res.front() == '"' || res.front() == '\''))
		res.erase(res.begin());
	if (!res.empty() && (res.back() == '"' || res.back() == '\''))
		res.pop_back();
	return res;
}

bool is_valid_hex_color(const std::string &s)
{
	if (s.size() != 4 && s.size() != 7)
		return false;
	if (s[0] != '#')
		return false;
	for (std::size_t i = 1; i < s.size(); ++i) {
		if (!std::isxdigit(static_cast<unsigned char>(s[i])))
			return false;
	}
	return true;
}

} // namespace

void LiedplanState::reset()
{
	cards.clear();
	next_id = 1;
	active_profile = "OBS Dark";
	settings = LiedplanSettings{};
	quick_profiles.clear();
	color_profiles.clear();
}

std::string LiedplanState::serialize_json() const
{
	return state_to_json(*this).dump(2);
}

std::string LiedplanState::deserialize_json(const std::string &json_str)
{
	nlohmann::json root = nlohmann::json::parse(json_str, nullptr, false);
	if (root.is_discarded() || !root.is_object())
		return "Invalid JSON";
	return state_from_json(*this, root);
}

std::string LiedplanState::load(const std::string &path)
{
	reset();

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

	const std::string err = state_from_json(*this, root);
	if (!err.empty())
		return err + ": " + path;

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

	const std::string serialized = serialize_json();
	std::ofstream file(fs_path, std::ios::binary | std::ios::trunc);
	if (!file.is_open())
		return "Failed to open state file for writing: " + path;

	file.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
	file.flush();
	if (!file.good())
		return "Failed to write state file: " + path;

	return "";
}

std::string LiedplanState::restore_backup(const std::string &path)
{
	if (path.empty())
		return "Kein Dateipfad angegeben.";

	std::error_code ec;
	const std::filesystem::path fs_path = to_fs_path(path);
	if (!std::filesystem::exists(fs_path, ec) || ec)
		return "Datei nicht gefunden: " + path;

	std::ifstream file(fs_path, std::ios::binary);
	if (!file.is_open())
		return "Datei konnte nicht geöffnet werden: " + path;

	std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	if (file.bad() || content.find_first_not_of(" \t\r\n") == std::string::npos)
		return "Datei ist leer oder nicht lesbar: " + path;

	nlohmann::json root = nlohmann::json::parse(content, nullptr, false);
	if (root.is_discarded() || !root.is_object())
		return "Ungültiges JSON in Backup-Datei: " + path;

	LiedplanState temp;
	const std::string err = state_from_json(temp, root);
	if (!err.empty())
		return err;

	*this = std::move(temp);
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

void LiedplanState::save_quick_profile(const std::string &name)
{
	const std::string clean_name = name.empty()
					       ? ("Profil " + std::to_string(quick_profiles.size() + 1))
					       : name;
	for (auto &qp : quick_profiles) {
		if (qp.name == clean_name) {
			qp.cards = cards;
			return;
		}
	}
	quick_profiles.push_back(LiedplanQuickProfile{clean_name, cards});
}

bool LiedplanState::apply_quick_profile(std::size_t index)
{
	if (index >= quick_profiles.size())
		return false;
	cards = quick_profiles[index].cards;
	update_next_id_from_cards(cards, next_id);
	return true;
}

bool LiedplanState::apply_quick_profile(const std::string &name)
{
	for (std::size_t i = 0; i < quick_profiles.size(); ++i) {
		if (quick_profiles[i].name == name)
			return apply_quick_profile(i);
	}
	return false;
}

bool LiedplanState::remove_quick_profile(std::size_t index)
{
	if (index >= quick_profiles.size())
		return false;
	quick_profiles.erase(quick_profiles.begin() + static_cast<std::ptrdiff_t>(index));
	return true;
}

std::string liedplan_profile_to_yaml(const LiedplanProfile &p)
{
	const std::string tc = p.text_color.empty() ? "#000000" : p.text_color;
	const std::string bc = p.box_color.empty() ? "#ffffff" : p.box_color;
	std::string yaml = "# Liedplan Farbprofil\n";
	yaml += "name: \"" + p.name + "\"\n";
	yaml += "colors:\n";
	yaml += "  textColor: \"" + tc + "\"\n";
	yaml += "  cardOuterBg: \"" + bc + "\"\n";
	return yaml;
}

bool liedplan_yaml_to_profile(const std::string &text, LiedplanProfile &out, std::string &err)
{
	std::istringstream iss(text);
	std::string raw_line;
	std::string parsed_name;
	bool in_colors = false;
	bool has_text_color = false;
	bool has_box_color = false;
	std::string raw_text_color;
	std::string raw_box_color;

	while (std::getline(iss, raw_line)) {
		const std::string line = trim_ws(raw_line);
		if (line.empty() || line[0] == '#')
			continue;

		if (line.rfind("name:", 0) == 0) {
			const std::string val = trim_ws(line.substr(5));
			parsed_name = trim_ws(strip_quotes(val));
			continue;
		}
		if (line.rfind("- name:", 0) == 0) {
			const std::string val = trim_ws(line.substr(7));
			parsed_name = trim_ws(strip_quotes(val));
			continue;
		}

		if (line == "colors:") {
			in_colors = true;
			continue;
		}

		if (in_colors) {
			const auto colon_idx = line.find(':');
			if (colon_idx != std::string::npos) {
				const std::string key = trim_ws(line.substr(0, colon_idx));
				const std::string val =
					strip_quotes(trim_ws(line.substr(colon_idx + 1)));
				if (key == "textColor") {
					has_text_color = true;
					raw_text_color = val;
				} else if (key == "cardOuterBg") {
					has_box_color = true;
					raw_box_color = val;
				}
			}
		}
	}

	if (parsed_name.empty()) {
		err = "Profilname fehlt.";
		return false;
	}

	if (!has_text_color && !has_box_color) {
		err = "Keine Farben gefunden (textColor / cardOuterBg).";
		return false;
	}

	if (has_text_color && !is_valid_hex_color(raw_text_color)) {
		err = "Ungültige Farbformat: " + raw_text_color;
		return false;
	}

	if (has_box_color && !is_valid_hex_color(raw_box_color)) {
		err = "Ungültige Farbformat: " + raw_box_color;
		return false;
	}

	out.name = std::move(parsed_name);
	out.text_color = has_text_color ? std::move(raw_text_color) : "#000000";
	out.box_color = has_box_color ? std::move(raw_box_color) : "#ffffff";
	err.clear();
	return true;
}
