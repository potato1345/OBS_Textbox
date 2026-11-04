/*
 * test-liedplan-state.cpp — Unit-Tests für LiedplanState und YAML-Profil-Konvertierung (v3).
 *
 * Zweck:
 *   - Verifiziert Laden, Speichern, Backup-Wiederherstellung und JSON-Serialisierung von Zuständen.
 *   - Prüft Kartenoperationen (Hinzufügen, Entfernen, ID-Eindeutigkeit).
 *   - Prüft YAML-Export und -Import von Farbprofilen auf Korrektheit und Fehlertoleranz.
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
		c.profile = "Karte Wald";
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
		// F1-Migration: Legacy-String "OBS Dark" im alten JSON -> kanonischer neuer Name
		check(s.cards.size() == 1 && s.cards[0].profile == "Karte Dunkel",
		      "4b. F1 Legacy-Migration: Karten-Profil -> kanonischer Name", s.cards[0].profile);
		check(s.active_profile == "Karte Dunkel",
		      "4c. F1 Legacy-Migration: active_profile Default -> Karte Dunkel");
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
			  (profiles[0].name == "Karte Dunkel" && profiles[0].text_color == "#eff0f1" &&
			   profiles[0].box_color == "#3C404D") &&
			  (profiles[1].name == "Karte Hell" && profiles[1].text_color == "#1a1a1a" &&
			   profiles[1].box_color == "#d5d5d5") &&
			  (profiles[2].name == "Karte Mitternacht" &&
			   profiles[2].text_color == "#c9d1d9" &&
			   profiles[2].box_color == "#252d3a") &&
			  (profiles[3].name == "Karte Wald" &&
			   profiles[3].text_color == "#d4e0d0" &&
			   profiles[3].box_color == "#344530") &&
			  (profiles[4].name == "Karte Sonne" &&
			   profiles[4].text_color == "#f0ddd0" &&
			   profiles[4].box_color == "#3d2a20") &&
			  (profiles[5].name == "Karte Kontrast" &&
			   profiles[5].text_color == "#ffffff" &&
			   profiles[5].box_color == "#222222");
		// F1: erweiterte Felder haben Karten-Defaults
		for (const auto &p : profiles)
			ok = ok && p.corner_a == "#fce647" && p.corner_b == "#fce647" &&
			       p.corner_op_a == 255 && p.corner_op_b == 255 &&
			       p.text_opacity == 255 && p.box_opacity == 255 && p.rainbow == false;
		check(ok, "7. liedplan_profiles(): genau 6 Profile \"Karte …\" mit exakten Farben und Ecken-Defaults");
	}

	// 8. profile_by_name("nicht da") -> nullptr (+ bekannter Name wird gefunden)
	{
		const LiedplanProfile *missing = profile_by_name("nicht da");
		const LiedplanProfile *hc = profile_by_name("Karte Kontrast");
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
		c.profile = "Karte Mitternacht";
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
			  s.cards[0].profile == "Karte Dunkel" &&
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
		s1.active_profile = "Karte Mitternacht";
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
				(s2.active_profile == "Karte Mitternacht") &&
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
		c1.profile = "Karte Wald";
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
			s2.cards[0].profile == "Karte Wald";

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
			  s.active_profile == "Karte Dunkel" &&
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
			s.active_profile == "Karte Dunkel" &&
			s.cards[0].profile == "Karte Dunkel" &&
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

	// 18. (M6-B3 #1) YAML-Roundtrip: Profil {Mein Profil, #aabbcc, #ddeeff} -> to_yaml -> yaml_to_profile -> identisch
	{
		const LiedplanProfile orig{"Mein Profil", "#aabbcc", "#ddeeff"};
		const std::string yaml = liedplan_profile_to_yaml(orig);
		LiedplanProfile parsed;
		std::string err;
		const bool ok = liedplan_yaml_to_profile(yaml, parsed, err) && err.empty() &&
				(parsed == orig);
		check(ok, "18. M6-B3 YAML-Roundtrip: liedplan_profile_to_yaml -> liedplan_yaml_to_profile identisch",
		      err);
	}

	// 19. (M6-B3 #2) v2-18-Key-YAML-Import: alle 18 COLOR_KEYS + Kommentare -> extrahiert exakt textColor + cardOuterBg
	{
		const std::string v2_yaml = R"(# ============================================================
# OBS Textbox Farbprofil (v2 18-Key Export)
# ============================================================
name: "Karte Mitternacht"
colors:
  bgPrimary: "#0d1117"
  bgPrimaryOpacity: 255
  bgSecondary: "#161b22"
  bgSecondaryOpacity: 255
  panelBg: "#1c2333"
  panelBgOpacity: 255
  border: "#30363d"
  borderOpacity: 255
  cardOuterBg: "#252d3a"
  cardOuterBgOpacity: 255
  panelBgAlt: "#2a3342"
  panelBgAltOpacity: 255
  btnHoverBg: "#2d3748"
  btnHoverBgOpacity: 255
  textColor: "#c9d1d9"
  textColorOpacity: 255
  accentBlue: "#58a6ff"
  accentBlueOpacity: 255
)";
		LiedplanProfile parsed;
		std::string err;
		const bool ok = liedplan_yaml_to_profile(v2_yaml, parsed, err) && err.empty() &&
				parsed.name == "Karte Mitternacht" &&
				parsed.text_color == "#c9d1d9" &&
				parsed.box_color == "#252d3a";
		check(ok, "19. M6-B3 v2-18-Key-YAML-Import: extrahiert exakt textColor + cardOuterBg, ignoriert übrige 16 Keys",
		      err);
	}

	// 20. (M6-B3 #3) YAML-Fehlerpfade: leerer Name, keine Color-Keys, ungültiges Hex-Format
	{
		LiedplanProfile p1, p2, p3;
		std::string err1, err2, err3;
		const bool res1 = liedplan_yaml_to_profile(
			"name: \"\"\ncolors:\n  textColor: \"#112233\"\n  cardOuterBg: \"#445566\"\n",
			p1, err1);
		const bool res2 = liedplan_yaml_to_profile(
			"name: \"Ohne Farben\"\ncolors:\n  bgPrimary: \"#112233\"\n",
			p2, err2);
		const bool res3 = liedplan_yaml_to_profile(
			"name: \"Defekt\"\ncolors:\n  textColor: \"zzz\"\n  cardOuterBg: \"#445566\"\n",
			p3, err3);

		const bool ok = !res1 && err1 == "Profilname fehlt." && !res2 &&
				err2.rfind("Keine Farben", 0) == 0 && !res3 &&
				err3.rfind("Ungültige Farbformat", 0) == 0;
		check(ok, "20. M6-B3 YAML-Fehlerpfade: leerer Name, keine Farben, ungültiges Farbformat",
		      err1 + " | " + err2 + " | " + err3);
	}

	// 21. (M6-B3 #4) YAML-Defaults/Quoting: 3-Digit-Hex, '-Quotes, fehlendes box_color -> #ffffff-Default
	{
		const std::string yaml_single =
			"name: 'Single Quoted'\ncolors:\n  textColor: '#AbC'\n";
		LiedplanProfile parsed;
		std::string err;
		const bool ok = liedplan_yaml_to_profile(yaml_single, parsed, err) &&
				err.empty() && parsed.name == "Single Quoted" &&
				parsed.text_color == "#AbC" && parsed.box_color == "#ffffff";
		check(ok, "21. M6-B3 YAML-Defaults/Quoting: 3-Digit-Hex, '-Quotes und #ffffff-Default bei fehlendem cardOuterBg",
		      err);
	}

	// 22. (M6-B2 #5) Lookup: Standard vor Custom, Custom gefunden, unbekannter Name -> nullptr
	{
		const std::vector<LiedplanProfile> custom = {
			{"Karte Dunkel", "#111111", "#222222"},
			{"Kirchen-Gold", "#fce647", "#2a1f0d"},
		};
		const LiedplanProfile *std_hit = liedplan_profile_lookup("Karte Dunkel", custom);
		const LiedplanProfile *cust_hit = liedplan_profile_lookup("Kirchen-Gold", custom);
		const LiedplanProfile *miss = liedplan_profile_lookup("Unbekannt", custom);

		const bool ok = (std_hit != nullptr && std_hit->text_color == "#eff0f1" &&
					 std_hit->box_color == "#3C404D") &&
					(cust_hit != nullptr && cust_hit->text_color == "#fce647" &&
					 cust_hit->box_color == "#2a1f0d") &&
					(miss == nullptr);
		check(ok, "22. M6-B2 Lookup: Standard vor Custom, Custom wird gefunden, Unbekannt -> nullptr");

		// F1: Legacy-Aliase funktionieren in Lookup und profile_by_name (case-insensitive)
		const LiedplanProfile *alias1 = profile_by_name("OBS Dark");
		const LiedplanProfile *alias2 = profile_by_name("high contrast");
		const LiedplanProfile *alias3 = liedplan_profile_lookup("Forest Green", custom);
		check(alias1 != nullptr && alias1->name == "Karte Dunkel" && alias1->box_color == "#3C404D",
		      "22b. F1 Alias profile_by_name(\"OBS Dark\") -> Karte Dunkel");
		check(alias2 != nullptr && alias2->name == "Karte Kontrast",
		      "22c. F1 Alias case-insensitive -> Karte Kontrast");
		check(alias3 != nullptr && alias3->name == "Karte Wald",
		      "22d. F1 Alias lookup(\"Forest Green\") -> Karte Wald");
	}

	// 23. (M6-B2 #6) State-Seriale: 2 Custom-Profile -> save -> load -> identisch; JSON ohne Key -> leer; reset() -> leer
	{
		const std::string path = (temp_dir / "roundtrip_m6_profiles.json").u8string();
		LiedplanState s1;
		s1.color_profiles.push_back({"Profil A", "#112233", "#445566"});
		s1.color_profiles.push_back({"Profil B", "#aabbcc", "#ddeeff"});

		const std::string save_err = s1.save(path);
		LiedplanState s2;
		const std::string load_err = s2.load(path);

		const bool roundtrip_ok =
			save_err.empty() && load_err.empty() && (s1 == s2) &&
			s2.color_profiles.size() == 2 &&
			s2.color_profiles[0] == LiedplanProfile{"Profil A", "#112233", "#445566"} &&
			s2.color_profiles[1] == LiedplanProfile{"Profil B", "#aabbcc", "#ddeeff"};

		// JSON ohne "color_profiles"-Key -> leer (Backward-Compat)
		LiedplanState s3;
		s3.color_profiles.push_back({"Alt", "#000000", "#ffffff"});
		const std::string back_err =
			s3.load((temp_dir / "legacy_m4_no_anim.json").u8string());
		const bool back_ok = back_err.empty() && s3.color_profiles.empty();

		// reset() -> leer
		s2.reset();
		const bool reset_ok = s2.color_profiles.empty() && s2.cards.empty() &&
				      s2.next_id == 1;

		check(roundtrip_ok && back_ok && reset_ok,
		      "23. M6-B2 State-Seriale: save->load mit 2 Custom-Profilen, JSON ohne Key -> leer, reset() -> leer",
		      save_err + " " + load_err + " " + back_err);
	}

	// 24. (F1) YAML-Roundtrip mit erweiterten Feldern + 2-Feld-Profil ohne Extra-Keys -> Defaults
	{
		const LiedplanProfile ext{"Goldprofil", "#fce647", "#2a1f0d", "#ff0000", "#00ff00",
		                          128, 64, 220, 200, true};
		const std::string yaml_ext = liedplan_profile_to_yaml(ext);
		LiedplanProfile parsed_ext;
		std::string err_ext;
		const bool ext_ok = liedplan_yaml_to_profile(yaml_ext, parsed_ext, err_ext) &&
		                    err_ext.empty() && (parsed_ext == ext);

		const LiedplanProfile duo{"Zweifelder", "#aabbcc", "#ddeeff"};
		const std::string yaml_duo = liedplan_profile_to_yaml(duo);
		LiedplanProfile parsed_duo;
		std::string err_duo;
		const bool duo_ok = liedplan_yaml_to_profile(yaml_duo, parsed_duo, err_duo) &&
		                    err_duo.empty() && (parsed_duo == duo) &&
		                    parsed_duo.corner_a == "#fce647" && parsed_duo.rainbow == false;
		check(ext_ok && duo_ok, "24. F1 YAML-Roundtrip: erweiterte Felder + 2-Feld-Profil -> Defaults",
		      err_ext + " | " + err_duo);
	}

	// 25. (F1) profile_apply schreibt alle Kartenfelder; 2-Feld-Profil -> Defaults; operator== vergleicht Ecken
	{
		LiedplanCard card;
		const LiedplanProfile ext{"P", "#112233", "#445566", "#ff0000", "#00ff00", 10, 20, 200, 150, true};
		profile_apply(ext, card);
		card.profile = ext.name; // wie im Dock-Handler: Profilname separat gesetzt
		const bool applied = card.profile == "P" && card.text_color == "#112233" &&
		                     card.box_color == "#445566" && card.corner_color_a == "#ff0000" &&
		                     card.corner_color_b == "#00ff00" && card.corner_opacity_a == 10 &&
		                     card.corner_opacity_b == 20 && card.text_opacity == 200 &&
		                     card.box_opacity == 150 && card.rainbow_corners == true;

		LiedplanCard card2;
		card2.corner_color_a = "#abcdef";
		profile_apply({"Q", "#aabbcc", "#ddeeff"}, card2);
		const bool legacy_reset = card2.corner_color_a == "#fce647" &&
		                          card2.corner_opacity_a == 255 && card2.text_opacity == 255 &&
		                          card2.rainbow_corners == false;

		const LiedplanProfile base{"R", "#112233", "#445566"};
		LiedplanProfile other = base;
		other.corner_a = "#000000";
		const bool neq = !(base == other);

		check(applied && legacy_reset && neq,
		      "25. F1 profile_apply: alle Felder, Legacy-Reset auf Defaults, operator== incl. Ecken");
	}

	// 26. (F1) State-JSON-Seriale der erweiterten color_profiles
	{
		const std::string path = (temp_dir / "roundtrip_f1_profiles.json").u8string();
		LiedplanState s1;
		s1.color_profiles.push_back({"Ext", "#112233", "#445566", "#ff0000", "#00ff00",
		                            10, 20, 30, 40, true});
		const std::string save_err = s1.save(path);
		LiedplanState s2;
		const std::string load_err = s2.load(path);
		const bool ok = save_err.empty() && load_err.empty() && (s1 == s2) &&
		                s2.color_profiles.size() == 1 &&
		                s2.color_profiles[0].corner_a == "#ff0000" &&
		                s2.color_profiles[0].rainbow == true;
		check(ok, "26. F1 State-JSON: erweiterte color_profiles save->load identisch",
		      save_err + " " + load_err);
	}

	// 27. (F1) Alias-Migration im Dock-State: aktives Profil "Karte Dunkel" nach Load eines Alt-States
	{
		const std::filesystem::path fs_path = temp_dir / "legacy_f1_active.json";
		{
			std::ofstream out(fs_path, std::ios::binary | std::ios::trunc);
			out << R"({
  "version": 1,
  "next_id": 2,
  "active_profile": "Sunset Warm",
  "cards": []
})";
		}
		LiedplanState s;
		const std::string err = s.load(fs_path.u8string());
		check(err.empty() && s.active_profile == "Karte Sonne",
		      "27. F1 Migration active_profile \"Sunset Warm\" -> Karte Sonne", err);
	}

	// 28. (F2a) template_id Round-trip + Alt-State-Migration (fehlendes template_id -> "")
	{
		LiedplanState s1;
		LiedplanCard &c = s1.add_card();
		c.zeile1 = "Eröffnung";
		c.template_id = "rounded";
		s1.save_quick_profile("Template-Quick");

		const std::string json_str = s1.serialize_json();
		LiedplanState s2;
		const std::string err = s2.deserialize_json(json_str);

		LiedplanCard c_diff = c;
		c_diff.template_id = "frame";
		const bool roundtrip_ok = err.empty() && (s1 == s2) && s2.cards.size() == 1 &&
					  s2.cards[0].template_id == "rounded" &&
					  s2.quick_profiles.size() == 1 &&
					  s2.quick_profiles[0].cards[0].template_id == "rounded" &&
					  !(c == c_diff);

		// Alt-State ohne template_id -> "" (classic)
		LiedplanState s_legacy;
		const std::string leg_err =
			s_legacy.load((temp_dir / "legacy_m4_no_anim.json").u8string());
		const bool legacy_ok = leg_err.empty() && s_legacy.cards.size() == 1 &&
				       s_legacy.cards[0].template_id.empty();

		check(roundtrip_ok && legacy_ok,
		      "28. F2a template_id Round-trip (Cards + QuickProfile + operator==) und Alt-State-Migration -> \"\"",
		      err + " " + leg_err);
	}

	std::error_code ec;
	std::filesystem::remove_all(temp_dir, ec);

	if (g_failures > 0) {
		std::cerr << g_failures << " test(s) failed.\n";
		return 1;
	}
	std::cout << "All 28 checks passed.\n";
	return 0;
}
