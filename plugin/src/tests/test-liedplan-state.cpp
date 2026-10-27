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

#include "../liedplan-profiles.hpp"
#include "../liedplan-state.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

namespace {

int g_failures = 0;

void check(bool cond, const char *name, const std::string &detail = "")
{
	if (cond) {
		std::cout << "[PASS] " << name << "\n";
	} else {
		std::cerr << "[FAIL] " << name;
		if (!detail.empty())
			std::cerr << " — " << detail;
		std::cerr << "\n";
		++g_failures;
	}
}

std::filesystem::path make_temp_dir()
{
	std::filesystem::path dir = std::filesystem::temp_directory_path() / "liedplan_m1_tests";
	std::error_code ec;
	std::filesystem::remove_all(dir, ec);
	std::filesystem::create_directories(dir, ec);
	return dir;
}

} // namespace

int main()
{
	const std::filesystem::path temp_dir = make_temp_dir();

	// 1. save->load Roundtrip (inkl. Umlaut in zeile3, z. B. "Übermorgen 1")
	{
		const std::string path = (temp_dir / "roundtrip.json").u8string();
		LiedplanState s1;
		s1.next_id = 5;

		LiedplanCard c;
		c.id = "box1";
		c.zeile1 = "467";
		c.zeile2 = "1+3";
		c.zeile3 = "Übermorgen 1 — Freu dich sehr, o meine Seele";
		c.profile = "Forest Green";
		c.text_color = "#d4e0d0";
		c.text_opacity = 240;
		c.box_color = "#344530";
		c.box_opacity = 200;
		s1.cards.push_back(c);

		const std::string save_err = s1.save(path);
		LiedplanState s2;
		const std::string load_err = s2.load(path);

		bool ok = save_err.empty() && load_err.empty() && s2.cards.size() == 1 &&
			  s2.next_id == 5 && s2.cards[0].id == c.id &&
			  s2.cards[0].zeile1 == c.zeile1 && s2.cards[0].zeile2 == c.zeile2 &&
			  s2.cards[0].zeile3 == c.zeile3 && s2.cards[0].profile == c.profile &&
			  s2.cards[0].text_color == c.text_color &&
			  s2.cards[0].text_opacity == c.text_opacity &&
			  s2.cards[0].box_color == c.box_color &&
			  s2.cards[0].box_opacity == c.box_opacity;
		check(ok, "1. save->load Roundtrip inkl. Umlaut in zeile3",
		      save_err + " " + load_err);
	}

	// 2. load() auf nicht existierender Datei -> leerer State, Rückgabe ""
	{
		const std::string path = (temp_dir / "does_not_exist.json").u8string();
		LiedplanState s;
		s.add_card();
		const std::string err = s.load(path);
		check(err.empty() && s.cards.empty() && s.next_id == 1,
		      "2. load() auf nicht existierender Datei liefert leeren State und \"\"", err);
	}

	// 3. load() mit kaputtem JSON -> leerer State, Rückgabe != ""
	{
		const std::filesystem::path fs_path = temp_dir / "broken.json";
		{
			std::ofstream out(fs_path, std::ios::binary | std::ios::trunc);
			out << "{ \"version\": 1, \"cards\": [ broken json ";
		}
		LiedplanState s;
		const std::string err = s.load(fs_path.u8string());
		check(!err.empty() && s.cards.empty() && s.next_id == 1,
		      "3. load() mit kaputtem JSON liefert leeren State und Fehler-String", err);
	}

	// 4. unknown Feld im JSON wird ignoriert, bekannte bleiben erhalten
	{
		const std::filesystem::path fs_path = temp_dir / "unknown_fields.json";
		{
			std::ofstream out(fs_path, std::ios::binary | std::ios::trunc);
			out << R"({
  "version": 1,
  "future_root_field": {"nested": true},
  "next_id": 7,
  "cards": [
    {
      "id": "box2",
      "zeile1": "100",
      "zeile2": "1-4",
      "zeile3": "Lobet den Herrn",
      "profile": "OBS Dark",
      "text_color": "#eff0f1",
      "text_opacity": 255,
      "box_color": "#3C404D",
      "box_opacity": 255,
      "future_card_field": 42
    }
  ]
})";
		}
		LiedplanState s;
		const std::string err = s.load(fs_path.u8string());
		bool ok = err.empty() && s.next_id == 7 && s.cards.size() == 1 &&
			  s.cards[0].id == "box2" && s.cards[0].zeile1 == "100" &&
			  s.cards[0].zeile2 == "1-4" && s.cards[0].zeile3 == "Lobet den Herrn";
		check(ok, "4. unknown Felder im JSON werden ignoriert, bekannte bleiben erhalten",
		      err);
	}

	// 5. 3 Karten anlegen (Box-Id-Regel): box1, box2, box3; box1 löschen; neu anlegen -> box4
	{
		LiedplanState s;
		const std::string id1 = s.add_card().id;
		const std::string id2 = s.add_card().id;
		const std::string id3 = s.add_card().id;
		const bool removed = s.remove_card("box1");
		const std::string id4 = s.add_card().id;
		bool ok = (id1 == "box1" && id2 == "box2" && id3 == "box3" && removed &&
			   id4 == "box4" && s.cards.size() == 3 && s.next_id == 5);
		check(ok, "5. Box-Id-Regel: box1..box3, box1 löschen, neu -> box4");
	}

	// 6. next_id überlebt save/load (auch wenn letzte Karte gelöscht wurde)
	{
		const std::string path = (temp_dir / "next_id.json").u8string();
		LiedplanState s1;
		s1.add_card(); // box1
		s1.add_card(); // box2
		s1.add_card(); // box3
		s1.remove_card("box3");
		const std::string save_err = s1.save(path);

		LiedplanState s2;
		const std::string load_err = s2.load(path);
		const std::string new_id = s2.add_card().id;
		bool ok = save_err.empty() && load_err.empty() && new_id == "box4" &&
			  s2.next_id == 5;
		check(ok, "6. next_id überlebt save/load", save_err + " " + load_err);
	}

	// 7. liedplan_profiles(): genau 6 Profile, Namen + 2 Farben stimmen
	{
		const auto &profiles = liedplan_profiles();
		bool ok = (profiles.size() == 6) &&
			  (profiles[0].name == "OBS Dark" && profiles[0].text_color == "#eff0f1" &&
			   profiles[0].box_color == "#3C404D") &&
			  (profiles[1].name == "OBS Light" && profiles[1].text_color == "#1a1a1a" &&
			   profiles[1].box_color == "#d5d5d5") &&
			  (profiles[2].name == "Midnight Blue" &&
			   profiles[2].text_color == "#c9d1d9" &&
			   profiles[2].box_color == "#252d3a") &&
			  (profiles[3].name == "Forest Green" &&
			   profiles[3].text_color == "#d4e0d0" &&
			   profiles[3].box_color == "#344530") &&
			  (profiles[4].name == "Sunset Warm" &&
			   profiles[4].text_color == "#f0ddd0" &&
			   profiles[4].box_color == "#3d2a20") &&
			  (profiles[5].name == "High Contrast" &&
			   profiles[5].text_color == "#ffffff" &&
			   profiles[5].box_color == "#222222");
		check(ok, "7. liedplan_profiles(): genau 6 Profile mit exakten Namen und Farben");
	}

	// 8. profile_by_name("nicht da") -> nullptr (+ bekannter Name wird gefunden)
	{
		const LiedplanProfile *missing = profile_by_name("nicht da");
		const LiedplanProfile *hc = profile_by_name("High Contrast");
		bool ok = (missing == nullptr) && (hc != nullptr) && (hc->text_color == "#ffffff") &&
			  (hc->box_color == "#222222");
		check(ok, "8. profile_by_name(\"nicht da\") -> nullptr");
	}

	// 9. LiedplanState::find() für vorhandene und nicht vorhandene IDs
	{
		LiedplanState s;
		s.add_card().zeile3 = "Erstes Lied";
		s.add_card().zeile3 = "Zweites Lied";
		const LiedplanCard *found = s.find("box2");
		const LiedplanCard *not_found = s.find("box99");
		bool ok = (found != nullptr && found->zeile3 == "Zweites Lied" &&
			   not_found == nullptr);
		check(ok, "9. LiedplanState::find() liefert Zeiger bzw. nullptr");
	}

		// 10. load() auf leerer Datei (0 Bytes) -> leerer State, Rückgabe ""
	{
		const std::filesystem::path fs_path = temp_dir / "empty.json";
		{
			std::ofstream out(fs_path, std::ios::binary | std::ios::trunc);
		}
		LiedplanState s;
		s.add_card();
		const std::string err = s.load(fs_path.u8string());
		check(err.empty() && s.cards.empty() && s.next_id == 1,
		      "10. load() auf leerer Datei liefert leeren State und \"\"", err);
	}

	// 11. Ui2/M2b Roundtrip: Card mit nicht-default Corner-Werten + rainbow_corners=true
	{
		const std::string path = (temp_dir / "roundtrip_m2b.json").u8string();
		LiedplanState s1;
		s1.next_id = 3;

		LiedplanCard c;
		c.id = "box2";
		c.zeile1 = "123";
		c.zeile2 = "1-2";
		c.zeile3 = "Lobe den Herren";
		c.profile = "Midnight Blue";
		c.text_color = "#c9d1d9";
		c.text_opacity = 220;
		c.box_color = "#252d3a";
		c.box_opacity = 210;
		c.corner_color_a = "#123456";
		c.corner_color_b = "#abcdef";
		c.corner_opacity_a = 128;
		c.corner_opacity_b = 17;
		c.rainbow_corners = true;
		s1.cards.push_back(c);

		const std::string save_err = s1.save(path);
		LiedplanState s2;
		const std::string load_err = s2.load(path);

		bool ok = save_err.empty() && load_err.empty() && s2.cards.size() == 1 &&
			  s2.next_id == 3 && s2.cards[0].id == c.id &&
			  s2.cards[0].zeile1 == c.zeile1 && s2.cards[0].zeile2 == c.zeile2 &&
			  s2.cards[0].zeile3 == c.zeile3 && s2.cards[0].profile == c.profile &&
			  s2.cards[0].text_color == c.text_color &&
			  s2.cards[0].text_opacity == c.text_opacity &&
			  s2.cards[0].box_color == c.box_color &&
			  s2.cards[0].box_opacity == c.box_opacity &&
			  s2.cards[0].corner_color_a == "#123456" &&
			  s2.cards[0].corner_color_b == "#abcdef" &&
			  s2.cards[0].corner_opacity_a == 128 &&
			  s2.cards[0].corner_opacity_b == 17 &&
			  s2.cards[0].rainbow_corners == true;
		check(ok, "11. Ui2/M2b Roundtrip: Corner-Colors, Opacities und rainbow_corners bleiben erhalten",
		      save_err + " " + load_err);
	}

	// 12. Ui2/M2b Alte-Datei-Test: JSON ohne die neuen Felder -> Defaults (#fce647, 255/255, false)
	{
		const std::filesystem::path fs_path = temp_dir / "legacy_no_corners.json";
		{
			std::ofstream out(fs_path, std::ios::binary | std::ios::trunc);
			out << R"({
  "version": 1,
  "next_id": 4,
  "cards": [
    {
      "id": "box3",
      "zeile1": "290",
      "zeile2": "1-4",
      "zeile3": "Großer Gott, wir loben dich",
      "profile": "OBS Dark",
      "text_color": "#eff0f1",
      "text_opacity": 255,
      "box_color": "#3C404D",
      "box_opacity": 200
    }
  ]
})";
		}
		LiedplanState s;
		const std::string err = s.load(fs_path.u8string());
		bool ok = err.empty() && s.next_id == 4 && s.cards.size() == 1 &&
			  s.cards[0].id == "box3" && s.cards[0].zeile1 == "290" &&
			  s.cards[0].zeile2 == "1-4" &&
			  s.cards[0].zeile3 == "Großer Gott, wir loben dich" &&
			  s.cards[0].profile == "OBS Dark" &&
			  s.cards[0].text_color == "#eff0f1" &&
			  s.cards[0].text_opacity == 255 &&
			  s.cards[0].box_color == "#3C404D" &&
			  s.cards[0].box_opacity == 200 &&
			  s.cards[0].corner_color_a == "#fce647" &&
			  s.cards[0].corner_color_b == "#fce647" &&
			  s.cards[0].corner_opacity_a == 255 &&
			  s.cards[0].corner_opacity_b == 255 &&
			  s.cards[0].rainbow_corners == false;
		check(ok, "12. Ui2/M2b Alte-Datei-Test: fehlende Ecken-Felder erhalten Defaults (#fce647, 255/255, false)",
		      err);
	}

	// 13. (M4 #8) Backup-Roundtrip: State anlegen (cards + quick_profiles + settings) -> serialisieren -> deserialisieren -> == (nlohmann ==)
	{
		const std::string backup_path = (temp_dir / "backup_m4.json").u8string();
		LiedplanState s1;
		s1.active_profile = "Midnight Blue";
		s1.settings.google_form_id = "custom_form_id_123";
		s1.settings.token_path = "C:\\custom\\path\\token.json";

		LiedplanCard &c1 = s1.add_card();
		c1.zeile1 = "144";
		c1.zeile2 = "1+4+6";
		c1.zeile3 = "Nun jauchzt dem Herren, aller Welt";
		c1.rainbow_corners = true;

		LiedplanCard &c2 = s1.add_card();
		c2.zeile1 = "356";
		c2.zeile3 = "Komm, o Gott, durch den Heiligen Geist";

		s1.save_quick_profile("So 19.10.");

		const std::string save_err = s1.save(backup_path);
		LiedplanState s2;
		const std::string restore_err = s2.restore_backup(backup_path);

		const nlohmann::json j1 = nlohmann::json::parse(s1.serialize_json());
		const nlohmann::json j2 = nlohmann::json::parse(s2.serialize_json());
		const bool ok = save_err.empty() && restore_err.empty() && (j1 == j2) &&
				(s1 == s2) &&
				(s2.settings.google_form_id == "custom_form_id_123") &&
				(s2.settings.token_path == "C:\\custom\\path\\token.json") &&
				(s2.active_profile == "Midnight Blue") &&
				(s2.quick_profiles.size() == 1);
		check(ok, "13. M4 Backup-Roundtrip: Tiefenvergleich (nlohmann == und struct ==)",
		      save_err + " " + restore_err);
	}

	// 14. (M4 #9) Schnellprofil-Roundtrip: quick_profiles +{ name, cards } -> serialisieren -> laden -> apply ersetzt cards komplett
	{
		const std::string path = (temp_dir / "quick_profiles_m4.json").u8string();
		LiedplanState s1;
		LiedplanCard &c1 = s1.add_card();
		c1.zeile1 = "144";
		c1.zeile2 = "1-3";
		c1.zeile3 = "Sonntagslied";
		c1.profile = "Forest Green";
		s1.save_quick_profile("So 19.10.");

		// Jetzt aktuellen State verändern (andere Karten)
		s1.cards[0].zeile1 = "999";
		s1.cards[0].zeile3 = "Geändert";
		s1.add_card().zeile3 = "Zusatzkarte";

		const std::string save_err = s1.save(path);
		LiedplanState s2;
		const std::string load_err = s2.load(path);

		const bool before_apply =
			load_err.empty() && s2.cards.size() == 2 &&
			s2.cards[0].zeile1 == "999" && s2.quick_profiles.size() == 1 &&
			s2.quick_profiles[0].name == "So 19.10." &&
			s2.quick_profiles[0].cards.size() == 1 &&
			s2.quick_profiles[0].cards[0].zeile1 == "144";

		const bool applied = s2.apply_quick_profile(0);
		const bool after_apply =
			applied && s2.cards.size() == 1 && s2.cards[0].id == "box1" &&
			s2.cards[0].zeile1 == "144" && s2.cards[0].zeile2 == "1-3" &&
			s2.cards[0].zeile3 == "Sonntagslied" &&
			s2.cards[0].profile == "Forest Green";

		check( save_err.empty() && before_apply && after_apply,
		      "14. M4 Schnellprofil-Roundtrip: Speichern, Laden und Apply ersetzt cards komplett",
		      save_err + " " + load_err);
	}

	// 15. (M4 #10) Backward-Compat: JSON OHNE die drei neuen Top-Level-Keys -> lädt sauber mit Defaults, alte cards unverändert
	{
		const std::filesystem::path fs_path = temp_dir / "legacy_m3_state.json";
		{
			std::ofstream out(fs_path, std::ios::binary | std::ios::trunc);
			out << R"({
  "version": 1,
  "next_id": 3,
  "cards": [
    {
      "id": "box1",
      "zeile1": "175",
      "zeile2": "1",
      "zeile3": "Halleluja!",
      "profile": "OBS Dark",
      "text_color": "#eff0f1",
      "text_opacity": 255,
      "box_color": "#3C404D",
      "box_opacity": 255,
      "corner_color_a": "#fce647",
      "corner_color_b": "#fce647",
      "corner_opacity_a": 255,
      "corner_opacity_b": 255,
      "rainbow_corners": false
    }
  ]
})";
		}
		LiedplanState s;
		const std::string err = s.load(fs_path.u8string());
		const LiedplanSettings default_settings;
		bool ok = err.empty() && s.next_id == 3 && s.cards.size() == 1 &&
			  s.cards[0].id == "box1" && s.cards[0].zeile1 == "175" &&
			  s.cards[0].zeile3 == "Halleluja!" &&
			  s.active_profile == "OBS Dark" &&
			  s.settings == default_settings &&
			  s.quick_profiles.empty();

		// Zusätzlich: kaputtes Backup bei restore_backup verändert bestehenden State NICHT
		const std::string restore_err =
			s.restore_backup((temp_dir / "broken.json").u8string());
		ok = ok && !restore_err.empty() && s.cards.size() == 1 &&
		     s.cards[0].zeile1 == "175";

		check(ok, "15. M4 Backward-Compat: JSON ohne active_profile/settings/quick_profiles lädt mit Defaults",
		      err);
	}

	// 16. (M5 #11) Anim-Roundtrip + Settings-Roundtrip: Karte mit allen 5 Anim-Feldern + Settings mit 5 neuen Feldern -> save -> load -> ==
	{
		const std::string path = (temp_dir / "roundtrip_m5_anim.json").u8string();
		LiedplanState s1;
		s1.settings.openrouter_key_path = "C:\\custom\\api.env";
		s1.settings.markitdown_python = "C:\\custom\\python.exe";
		s1.settings.animation_dir = "C:\\custom\\anim";
		s1.settings.lieder_path = "C:\\custom\\lieder.md";
		s1.settings.ai_system_prompt = "Custom Prompt 123";

		LiedplanCard &c = s1.add_card();
		c.zeile1 = "Eröffnung";
		c.zeile2 = "144, 1+4+6";
		c.zeile3 = "Nun jauchzt dem Herren";
		c.anim_file = "C:\\Users\\Johann\\Desktop\\GitHub\\OBS_Textbox\\animation\\1_advent.gif";
		c.show_anim = true;
		c.anim_scale = 4;
		c.anim_x = -50;
		c.anim_y = 120;

		s1.save_quick_profile("Advent-Profil");

		const std::string save_err = s1.save(path);
		LiedplanState s2;
		const std::string load_err = s2.load(path);

		const bool ok = save_err.empty() && load_err.empty() && (s1 == s2) &&
				s2.cards.size() == 1 &&
				s2.cards[0].anim_file == c.anim_file &&
				s2.cards[0].show_anim == true &&
				s2.cards[0].anim_scale == 4 &&
				s2.cards[0].anim_x == -50 &&
				s2.cards[0].anim_y == 120 &&
				s2.quick_profiles.size() == 1 &&
				s2.quick_profiles[0].cards[0].anim_file == c.anim_file &&
				s2.settings.ai_system_prompt == "Custom Prompt 123";
		check(ok, "16. M5 Anim- & Settings-Roundtrip: alle 5 Anim-Felder + 5 Settings-Felder identisch nach save/load",
		      save_err + " " + load_err);
	}

	// 17. (M5 #12) M5 Backward-Compat: State-JSON OHNE Anim-Felder und Settings OHNE die 5 neuen Keys -> Defaults
	{
		const std::filesystem::path fs_path = temp_dir / "legacy_m4_no_anim.json";
		{
			std::ofstream out(fs_path, std::ios::binary | std::ios::trunc);
			out << R"({
  "version": 1,
  "next_id": 2,
  "active_profile": "OBS Dark",
  "settings": {
    "google_form_id": "form_abc",
    "token_path": "C:\\token.json"
  },
  "cards": [
    {
      "id": "box1",
      "zeile1": "144",
      "zeile2": "1",
      "zeile3": "Lied",
      "profile": "OBS Dark",
      "text_color": "#eff0f1",
      "text_opacity": 255,
      "box_color": "#3C404D",
      "box_opacity": 255,
      "corner_color_a": "#fce647",
      "corner_color_b": "#fce647",
      "corner_opacity_a": 255,
      "corner_opacity_b": 255,
      "rainbow_corners": false
    }
  ]
})";
		}
		LiedplanState s;
		const std::string err = s.load(fs_path.u8string());
		const LiedplanSettings def_settings;
		const bool ok =
			err.empty() && s.cards.size() == 1 &&
			s.cards[0].anim_file.empty() &&
			s.cards[0].show_anim == false &&
			s.cards[0].anim_scale == 3 &&
			s.cards[0].anim_x == 0 &&
			s.cards[0].anim_y == 0 &&
			s.settings.google_form_id == "form_abc" &&
			s.settings.openrouter_key_path == def_settings.openrouter_key_path &&
			s.settings.markitdown_python == def_settings.markitdown_python &&
			s.settings.animation_dir == def_settings.animation_dir &&
			s.settings.lieder_path == def_settings.lieder_path &&
			s.settings.ai_system_prompt.empty();
		check(ok, "17. M5 Backward-Compat: fehlende Anim-Felder und Settings-Keys erhalten Defaults",
		      err);
	}

	std::error_code ec;
	std::filesystem::remove_all(temp_dir, ec);

	if (g_failures > 0) {
		std::cerr << g_failures << " test(s) failed.\n";
		return 1;
	}
	std::cout << "All 17 checks passed.\n";
	return 0;
}
