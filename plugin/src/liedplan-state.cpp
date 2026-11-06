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

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#endif

#include "liedplan-state.hpp"
#include "liedplan-profiles.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <sstream>

namespace {

constexpr const char *kFallbackTokenPath =
	"C:\\Users\\Johann\\AppData\\Local\\hermes\\google_token.json";
constexpr const char *kFallbackLiederPath =
	"C:\\Users\\Johann\\Desktop\\GitHub\\OBS_Textbox\\rewrite\\dashboard\\lieder.md";

std::string g_module_lieder_path;

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
	if (item.contains("rows") && item["rows"].is_array()) {
		for (const auto &r : item["rows"]) {
			if (c.rows.size() >= kMaxCardRows)
				break;
			LiedplanRow row;
			if (r.is_object()) {
				if (r.contains("label") && r["label"].is_string())
					row.label = r["label"].get<std::string>();
				if (r.contains("value") && r["value"].is_string())
					row.value = r["value"].get<std::string>();
			} else if (r.is_string()) {
				row.value = r.get<std::string>();
			} else {
				continue;
			}
			c.rows.push_back(row);
		}
	}
	if (item.contains("style") && item["style"].is_array()) {
		for (const auto &s : item["style"]) {
			if (!s.is_object())
				continue;
			LiedplanStyleRule rule;
			if (s.contains("name") && s["name"].is_string())
				rule.name = s["name"].get<std::string>();
			if (s.contains("value") && s["value"].is_string())
				rule.value = s["value"].get<std::string>();
			if (s.contains("type") && s["type"].is_string())
				rule.type = s["type"].get<std::string>();
			else
				rule.type = infer_style_type(rule.value);
			if (!rule.name.empty())
				c.style.push_back(rule);
		}
	}
	if (item.contains("profile") && item["profile"].is_string())
		c.profile = profile_name_canonical(item["profile"].get<std::string>());
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
	if (item.contains("template_id") && item["template_id"].is_string())
		c.template_id = item["template_id"].get<std::string>();
	// CSS-Regeln sind die authoritative Quelle für Farben/Deckkraft.
	apply_style_rules(c);
	return c;
}



nlohmann::json card_to_json(const LiedplanCard &c)
{
	// Legacy-Key zeile1..3 = Mirror der ersten drei Zeilen-Werte (Kompatibilität).
	auto legacy_value = [&](size_t i) -> std::string {
		const auto rows = effective_rows(c);
		return i < rows.size() ? rows[i].value : std::string();
	};
	nlohmann::json rows = nlohmann::json::array();
	for (const auto &r : c.rows)
		rows.push_back({{"label", r.label}, {"value", r.value}});
	nlohmann::json style = nlohmann::json::array();
	for (const auto &s : c.style)
		style.push_back({{"name", s.name}, {"value", s.value}, {"type", s.type}});
	return {
		{"id", c.id},
		{"zeile1", legacy_value(0)},
		{"zeile2", legacy_value(1)},
		{"zeile3", legacy_value(2)},
		{"rows", rows},
		{"style", style},
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
		{"template_id", c.template_id},
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
			// IMP-H5: Profil-Stilregeln typisiert persistieren (wie Karten-Regeln).
		nlohmann::json cp_style = nlohmann::json::array();
		for (const auto &sr : cp.style)
			cp_style.push_back({{"name", sr.name}, {"value", sr.value}, {"type", sr.type}});
		cp_json.push_back({
			{"name", cp.name},
			{"text_color", cp.text_color},
			{"box_color", cp.box_color},
			{"corner_a", cp.corner_a},
			{"corner_b", cp.corner_b},
			{"corner_op_a", cp.corner_op_a},
			{"corner_op_b", cp.corner_op_b},
			{"text_opacity", cp.text_opacity},
			{"box_opacity", cp.box_opacity},
			{"rainbow", cp.rainbow},
			{"style", cp_style},
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
			 {"textbox_path", s.settings.textbox_path},
			 {"ai_system_prompt", s.settings.ai_system_prompt},
		 }},
		{"quick_profiles", std::move(qp_json)},
		{"profiles_seeded", s.profiles_seeded},
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

	std::string loaded_active_profile = "Karte Dunkel";
	if (root.contains("active_profile") && root["active_profile"].is_string()) {
		loaded_active_profile =
			profile_name_canonical(root["active_profile"].get<std::string>());
	}

	LiedplanSettings loaded_settings;
	if (root.contains("settings") && root["settings"].is_object()) {
		const auto &st = root["settings"];
		if (st.contains("google_form_id") && st["google_form_id"].is_string())
			loaded_settings.google_form_id = st["google_form_id"].get<std::string>();
		if (st.contains("token_path") && st["token_path"].is_string()) {
			const std::string tp = st["token_path"].get<std::string>();
			if (!tp.empty())
				loaded_settings.token_path = tp;
		}
		if (st.contains("openrouter_key_path") && st["openrouter_key_path"].is_string())
			loaded_settings.openrouter_key_path = st["openrouter_key_path"].get<std::string>();
		if (st.contains("markitdown_python") && st["markitdown_python"].is_string())
			loaded_settings.markitdown_python = st["markitdown_python"].get<std::string>();
		if (st.contains("animation_dir") && st["animation_dir"].is_string())
			loaded_settings.animation_dir = st["animation_dir"].get<std::string>();
		if (st.contains("lieder_path") && st["lieder_path"].is_string()) {
			const std::string lp = st["lieder_path"].get<std::string>();
			if (!lp.empty())
				loaded_settings.lieder_path = lp;
		}
		if (st.contains("textbox_path") && st["textbox_path"].is_string())
			loaded_settings.textbox_path = st["textbox_path"].get<std::string>();
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
			if (cp_item.contains("corner_a") && cp_item["corner_a"].is_string())
				cp.corner_a = cp_item["corner_a"].get<std::string>();
			if (cp_item.contains("corner_b") && cp_item["corner_b"].is_string())
				cp.corner_b = cp_item["corner_b"].get<std::string>();
			if (cp_item.contains("corner_op_a") && cp_item["corner_op_a"].is_number_integer())
				cp.corner_op_a = cp_item["corner_op_a"].get<int>();
			if (cp_item.contains("corner_op_b") && cp_item["corner_op_b"].is_number_integer())
				cp.corner_op_b = cp_item["corner_op_b"].get<int>();
			if (cp_item.contains("text_opacity") && cp_item["text_opacity"].is_number_integer())
				cp.text_opacity = cp_item["text_opacity"].get<int>();
			if (cp_item.contains("box_opacity") && cp_item["box_opacity"].is_number_integer())
				cp.box_opacity = cp_item["box_opacity"].get<int>();
			if (cp_item.contains("rainbow") && cp_item["rainbow"].is_boolean())
				cp.rainbow = cp_item["rainbow"].get<bool>();
			if (cp_item.contains("style") && cp_item["style"].is_array()) {
				for (const auto &s_item : cp_item["style"]) {
					if (!s_item.is_object())
						continue;
					LiedplanStyleRule rule;
					if (s_item.contains("name") && s_item["name"].is_string())
						rule.name = s_item["name"].get<std::string>();
					if (s_item.contains("value") && s_item["value"].is_string())
						rule.value = s_item["value"].get<std::string>();
					if (s_item.contains("type") && s_item["type"].is_string())
						rule.type = s_item["type"].get<std::string>();
					else
						rule.type = infer_style_type(rule.value);
					if (!rule.name.empty())
						cp.style.push_back(rule);
				}
			}
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
		// IMP-R: doppelte IDs machen remove/rename/HTML-Pfade inkonsistent
		// (zwei Karten teilen sich dieselbe <id>.html). Ablehn statt Stillstand.
		for (std::size_t i = 0; i < loaded_cards.size(); ++i) {
			for (std::size_t j = i + 1; j < loaded_cards.size(); ++j) {
				if (loaded_cards[i].id == loaded_cards[j].id)
					return "Duplicate card IDs in state JSON";
			}
		}
	}

	update_next_id_from_cards(loaded_cards, loaded_next_id);

	s.cards = std::move(loaded_cards);
	s.next_id = loaded_next_id;
	s.active_profile = std::move(loaded_active_profile);
	s.settings = std::move(loaded_settings);
	s.quick_profiles = std::move(loaded_quick_profiles);
	s.color_profiles = std::move(loaded_color_profiles);
	if (root.contains("profiles_seeded") && root["profiles_seeded"].is_boolean())
		s.profiles_seeded = root["profiles_seeded"].get<bool>();
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

// Entfernt äußere Anführungszeichen und demaskiert \" sowie \\ im Profilnamen.
std::string unquote(const std::string &s)
{
	const std::string res = strip_quotes(s);
	std::string out;
	out.reserve(res.size());
	for (std::size_t i = 0; i < res.size(); ++i) {
		if (res[i] == '\\' && i + 1 < res.size() &&
		    (res[i + 1] == '"' || res[i + 1] == '\\')) {
			out.push_back(res[i + 1]);
			++i;
		} else {
			out.push_back(res[i]);
		}
	}
	return out;
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

std::string default_google_token_path()
{
	const char *env = std::getenv("LOCALAPPDATA");
	if (env && env[0] != '\0') {
		std::string val(env);
		while (!val.empty() && (val.back() == '\\' || val.back() == '/'))
			val.pop_back();
		if (!val.empty()) {
#ifdef _WIN32
			return val + "\\hermes\\google_token.json";
#else
			return val + "/hermes/google_token.json";
#endif
		}
	}
	return kFallbackTokenPath;
}

std::string default_lieder_path()
{
	if (!g_module_lieder_path.empty())
		return g_module_lieder_path;
	return kFallbackLiederPath;
}

std::string default_animation_dir()
{
	const char *env = std::getenv("APPDATA");
	if (env && env[0] != '\0') {
		std::string val(env);
		while (!val.empty() && (val.back() == '\\' || val.back() == '/'))
			val.pop_back();
		if (!val.empty()) {
#ifdef _WIN32
			return val + "\\obs-studio\\plugin_config\\liedplan-for-obs\\animations";
#else
			return val + "/obs-studio/plugin_config/liedplan-for-obs/animations";
#endif
		}
	}
	return "";
}

void liedplan_set_module_lieder_path(const std::string &path)
{
	if (path.empty()) {
		g_module_lieder_path.clear();
		return;
	}
	std::error_code ec;
	if (std::filesystem::exists(to_fs_path(path), ec) && !ec)
		g_module_lieder_path = path;
}

void LiedplanState::reset()
{
	cards.clear();
	next_id = 1;
	active_profile = "Karte Dunkel";
	settings = LiedplanSettings{};
	quick_profiles.clear();
	color_profiles.clear();
	profiles_seeded = false;
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

	// Atomares Speichern: erst in temporäre Datei schreiben, danach per rename ersetzen.
	const std::filesystem::path tmp = to_fs_path(path + ".tmp");
	const std::string serialized = serialize_json();
	{
		std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
		if (!file.is_open())
			return "Failed to open state file for writing: " + path;

		file.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
		file.flush();
		if (!file.good()) {
			file.close();
			std::error_code rm_ec;
			std::filesystem::remove(tmp, rm_ec);
			return "Failed to write state file: " + path;
		}
	}

	std::error_code ec;
	std::filesystem::rename(tmp, fs_path, ec);
	if (ec) {
		std::error_code rm_ec;
		std::filesystem::remove(tmp, rm_ec);
		return "Failed to rename state file: " + ec.message();
	}

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
	c.profile = "Karte Dunkel";
	if (const LiedplanProfile *prof = profile_by_name(c.profile)) {
		profile_apply(*prof, c);
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
	// Backslashes und Anführungszeichen im Profilnamen maskieren.
	std::string escaped_name;
	escaped_name.reserve(p.name.size());
	for (char ch : p.name) {
		if (ch == '\\' || ch == '"')
			escaped_name.push_back('\\');
		escaped_name.push_back(ch);
	}
	std::string yaml = "# Liedplan Farbprofil\n";
	yaml += "name: \"" + escaped_name + "\"\n";
	yaml += "colors:\n";
	yaml += "  textColor: \"" + tc + "\"\n";
	yaml += "  cardOuterBg: \"" + bc + "\"\n";
	if (!p.corner_a.empty() && p.corner_a != "#fce647")
		yaml += "  cornerA: \"" + p.corner_a + "\"\n";
	if (!p.corner_b.empty() && p.corner_b != "#fce647")
		yaml += "  cornerB: \"" + p.corner_b + "\"\n";
	if (p.corner_op_a != 255)
		yaml += "  cornerOpA: " + std::to_string(p.corner_op_a) + "\n";
	if (p.corner_op_b != 255)
		yaml += "  cornerOpB: " + std::to_string(p.corner_op_b) + "\n";
	if (p.text_opacity != 255)
		yaml += "  textOp: " + std::to_string(p.text_opacity) + "\n";
	if (p.box_opacity != 255)
		yaml += "  boxOp: " + std::to_string(p.box_opacity) + "\n";
	if (p.rainbow)
		yaml += "  rainbow: true\n";
	if (!p.style.empty()) {
		yaml += "styles:\n";
		for (const auto &s : p.style)
			yaml += "  " + s.name + "=" + s.value + "\n";
	}
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
	bool has_corner_a = false;
	bool has_corner_b = false;
	bool has_corner_op_a = false;
	bool has_corner_op_b = false;
	bool has_text_op = false;
	bool has_box_op = false;
	bool has_rainbow = false;
	bool in_styles = false;
	std::vector<LiedplanStyleRule> parsed_style;
	std::string raw_text_color;
	std::string raw_box_color;
	std::string raw_corner_a;
	std::string raw_corner_b;
	int parsed_corner_op_a = 255;
	int parsed_corner_op_b = 255;
	int parsed_text_op = 255;
	int parsed_box_op = 255;
	bool parsed_rainbow = false;

	auto parse_opacity_val = [](const std::string &v, int &out_val) {
		if (v.empty())
			return false;
		try {
			std::size_t idx = 0;
			const int n = std::stoi(v, &idx);
			if (idx != v.size())
				return false;
			out_val = std::clamp(n, 0, 255);
			return true;
		} catch (...) {
			return false;
		}
	};

	while (std::getline(iss, raw_line)) {
		const std::string line = trim_ws(raw_line);
		if (line.empty() || line[0] == '#')
			continue;

		if (line.rfind("name:", 0) == 0) {
			const std::string val = trim_ws(line.substr(5));
			parsed_name = trim_ws(unquote(val));
			continue;
		}
		if (line.rfind("- name:", 0) == 0) {
			const std::string val = trim_ws(line.substr(7));
			parsed_name = trim_ws(unquote(val));
			continue;
		}

		if (line == "styles:") {
			in_styles = true;
			continue;
		}

		if (in_styles) {
			const auto eq_idx = line.find('=');
			if (eq_idx == std::string::npos) {
				in_styles = false;
			} else {
				LiedplanStyleRule rule;
				rule.name = trim_ws(line.substr(0, eq_idx));
				rule.value = trim_ws(line.substr(eq_idx + 1));
				rule.type = infer_style_type(rule.value);
				if (!rule.name.empty())
					parsed_style.push_back(rule);
				continue;
			}
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
				} else if (key == "cornerA") {
					has_corner_a = true;
					raw_corner_a = val;
				} else if (key == "cornerB") {
					has_corner_b = true;
					raw_corner_b = val;
				} else if (key == "cornerOpA") {
					if (parse_opacity_val(val, parsed_corner_op_a))
						has_corner_op_a = true;
				} else if (key == "cornerOpB") {
					if (parse_opacity_val(val, parsed_corner_op_b))
						has_corner_op_b = true;
				} else if (key == "textOp") {
					if (parse_opacity_val(val, parsed_text_op))
						has_text_op = true;
				} else if (key == "boxOp") {
					if (parse_opacity_val(val, parsed_box_op))
						has_box_op = true;
				} else if (key == "rainbow") {
					if (liedplan_iequals(val, "true") || val == "1") {
						has_rainbow = true;
						parsed_rainbow = true;
					} else if (liedplan_iequals(val, "false") || val == "0") {
						has_rainbow = true;
						parsed_rainbow = false;
					}
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

	if (has_corner_a && !is_valid_hex_color(raw_corner_a)) {
		err = "Ungültige Farbformat: " + raw_corner_a;
		return false;
	}

	if (has_corner_b && !is_valid_hex_color(raw_corner_b)) {
		err = "Ungültige Farbformat: " + raw_corner_b;
		return false;
	}

	out = LiedplanProfile{};
	out.name = std::move(parsed_name);
	out.style = std::move(parsed_style);
	out.text_color = has_text_color ? std::move(raw_text_color) : "#000000";
	out.box_color = has_box_color ? std::move(raw_box_color) : "#ffffff";
	if (has_corner_a)
		out.corner_a = std::move(raw_corner_a);
	if (has_corner_b)
		out.corner_b = std::move(raw_corner_b);
	if (has_corner_op_a)
		out.corner_op_a = parsed_corner_op_a;
	if (has_corner_op_b)
		out.corner_op_b = parsed_corner_op_b;
	if (has_text_op)
		out.text_opacity = parsed_text_op;
	if (has_box_op)
		out.box_opacity = parsed_box_op;
	if (has_rainbow)
		out.rainbow = parsed_rainbow;
	err.clear();
	return true;
}

std::vector<LiedplanRow> effective_rows(const LiedplanCard &card)
{
	if (!card.rows.empty())
		return card.rows;
	std::vector<LiedplanRow> rows;
	rows.push_back({kLiedplanRowLabels[0], card.zeile1});
	rows.push_back({kLiedplanRowLabels[1], card.zeile2});
	rows.push_back({kLiedplanRowLabels[2], card.zeile3});
	return rows;
}
