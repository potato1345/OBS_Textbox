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

	std::error_code ec;
	std::filesystem::remove_all(temp_dir, ec);

	if (g_failures > 0) {
		std::cerr << g_failures << " test(s) failed.\n";
		return 1;
	}
	std::cout << "All 10 checks passed.\n";
	return 0;
}
