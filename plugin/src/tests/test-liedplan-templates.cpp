/*
 * test-liedplan-templates.cpp — Unit-Tests für die Liedplan-Template-Engine (Spec F2a / Suite 8).
 *
 * Zweck:
 *   1. substitute_tokens(): vollständige Substitution aller Tokens, rgba-Formatierung, UTF-8, clear=true/false.
 *   2. Unbekannte Tokens ({{unbekannt}}) bleiben unverändert erhalten.
 *   3. Hex-Validierung: ungültige Farbwerte fallen auf Standardfarben zurück.
 *   4. render_template_html(): Lesen aus templates_dir() + Substitution; fehlende/leere Datei -> "".
 *   5. write_card() mit template_id: schreibt substituiertes Template ohne INJECT-Marker;
 *      fehlendes Template -> Fallback auf classic-Weg (OBS_TEXTBOX_INJECT_START vorhanden).
 *   6. write_card() mit leerem template_id: classic-Weg unverändert.
 *   7. State-Roundtrip für template_id + Alt-State-Migration (fehlendes Feld -> "").
 *   8. push_update(): alle 14 bestehenden Keys unverändert + template_id.
 */

#ifndef _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#endif

#include "../liedplan-html.hpp"
#include "../liedplan-state.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

namespace liedplan_html {
std::string last_push_update_json();
} // namespace liedplan_html

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

std::string read_file_utf8(const std::filesystem::path &p)
{
	std::ifstream f(p, std::ios::binary);
	if (!f.is_open())
		return "";
	std::ostringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

void write_file_bytes(const std::filesystem::path &p, const std::string &bytes)
{
	std::ofstream f(p, std::ios::binary | std::ios::trunc);
	f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

int main()
{
	// 1. substitute_tokens: alle Tokens substituiert, rgba exakt, UTF-8 Rohtext, clear=false/true
	{
		LiedplanCard c;
		c.id = "box7";
		c.zeile1 = "Eröffnung";
		c.zeile2 = "Grüße 1+4";
		c.zeile3 = "Großer Gott, wir loben dich";
		c.text_color = "#eff0f1";
		c.text_opacity = 240;
		c.box_color = "#3C404D";
		c.box_opacity = 128;
		c.corner_color_a = "#123456";
		c.corner_opacity_a = 200;
		c.corner_color_b = "#654321";
		c.corner_opacity_b = 100;
		c.rainbow_corners = true;
		c.anim_file = "https://example.org/anim.webm";
		c.show_anim = true;
		c.anim_scale = 6;
		c.anim_x = -40;
		c.anim_y = 75;

		const std::string tpl =
			"id={{card_id}}|z1={{zeile1}}|z2={{zeile2}}|z3={{zeile3}}|"
			"tc={{text_color}}|bc={{box_color}}|ca={{corner_a}}|cb={{corner_b}}|"
			"top={{text_opacity}}|bop={{box_opacity}}|rb={{rainbow}}|"
			"asrc={{anim_src}}|sa={{show_anim}}|asc={{anim_scale}}|ax={{anim_x}}|ay={{anim_y}}|"
			"clr={{clear}}";

		const std::string out_norm = liedplan_html::substitute_tokens(c, tpl, false);
		const std::string expected_norm =
			"id=box7|z1=Eröffnung|z2=Grüße 1+4|z3=Großer Gott, wir loben dich|"
			"tc=" + liedplan_html::hex_to_rgba("#eff0f1", 240) +
			"|bc=" + liedplan_html::hex_to_rgba("#3C404D", 128) +
			"|ca=" + liedplan_html::hex_to_rgba("#123456", 200) +
			"|cb=" + liedplan_html::hex_to_rgba("#654321", 100) +
			"|top=240|bop=128|rb=1|"
			"asrc=https://example.org/anim.webm|sa=1|asc=6|ax=-40|ay=75|"
			"clr=0";
		check(out_norm == expected_norm,
		      "1a. substitute_tokens(clear=false): alle Tokens inkl. UTF-8 und rgba exakt substituiert",
		      out_norm);

		const std::string out_clear = liedplan_html::substitute_tokens(c, tpl, true);
		const std::string expected_clear =
			"id=box7|z1=|z2=|z3=|"
			"tc=" + liedplan_html::hex_to_rgba("#eff0f1", 240) +
			"|bc=" + liedplan_html::hex_to_rgba("#3C404D", 128) +
			"|ca=" + liedplan_html::hex_to_rgba("#123456", 200) +
			"|cb=" + liedplan_html::hex_to_rgba("#654321", 100) +
			"|top=240|bop=128|rb=1|"
			"asrc=https://example.org/anim.webm|sa=1|asc=6|ax=-40|ay=75|"
			"clr=1";
		check(out_clear == expected_clear,
		      "1b. substitute_tokens(clear=true): zeile1..3 leer und {{clear}}=\"1\"",
		      out_clear);
	}

	// 2. Unbekannte Tokens bleiben wörtlich erhalten (No-op)
	{
		LiedplanCard c;
		c.id = "box1";
		c.zeile1 = "Kyrie";
		const std::string tpl = "<div>{{zeile1}} - {{unbekannt}} - {{foo}}</div>";
		const std::string out = liedplan_html::substitute_tokens(c, tpl, false);
		check(out == "<div>Kyrie - {{unbekannt}} - {{foo}}</div>",
		      "2. substitute_tokens: unbekannte Tokens ({{unbekannt}}, {{foo}}) bleiben wörtlich erhalten",
		      out);
	}

	// 3. Hex-Validierung: ungültige/leere Farb-Felder -> Fallback-Farben als gültiges rgba(...)
	{
		LiedplanCard c;
		c.text_color = "#xyz";
		c.text_opacity = 255;
		c.box_color = "not-a-color";
		c.box_opacity = 255;
		c.corner_color_a = "#12";
		c.corner_opacity_a = 255;
		c.corner_color_b = "";
		c.corner_opacity_b = 255;

		const std::string tpl = "tc={{text_color}}|bc={{box_color}}|ca={{corner_a}}|cb={{corner_b}}";
		const std::string out = liedplan_html::substitute_tokens(c, tpl, false);
		check(out.find("tc=rgba(0,0,0,1)") != std::string::npos &&
			      out.find("bc=rgba(255,255,255,1)") != std::string::npos &&
			      out.find("ca=rgba(252,230,71,1)") != std::string::npos &&
			      out.find("cb=rgba(252,230,71,1)") != std::string::npos,
		      "3. Hex-Validierung: ungültige/leere Farben fallen auf #000000 / #ffffff / #fce647 zurück",
		      out);
	}

	// 4. render_template_html mit echter Datei in templates_dir() + fehlende Datei -> ""
	{
		const std::string tdir = liedplan_html::templates_dir();
		check(!tdir.empty(), "4a. templates_dir() liefert gültiges Verzeichnis im Test-Modus", tdir);

		const std::filesystem::path tpl_file =
			std::filesystem::u8path(tdir) / "rounded_test.html";
		write_file_bytes(tpl_file,
				 "<!DOCTYPE html><html><body><h1 style=\"color:{{text_color}}\">{{zeile1}}</h1>"
				 "<p>{{zeile3}}</p></body></html>");

		LiedplanCard c;
		c.id = "box2";
		c.zeile1 = "Eröffnung";
		c.zeile3 = "Lobet den Herren";
		c.text_color = "#112233";
		c.text_opacity = 255;

		const std::string rendered = liedplan_html::render_template_html(c, "rounded_test", false);
		check(rendered.find("<h1 style=\"color:rgba(17,34,51,1)\">Eröffnung</h1>") !=
				      std::string::npos &&
			      rendered.find("<p>Lobet den Herren</p>") != std::string::npos,
		      "4b. render_template_html: liest Datei aus templates_dir() und substituiert Tokens",
		      rendered);

		const std::string missing =
			liedplan_html::render_template_html(c, "does_not_exist_tpl", false);
		const std::string empty_id = liedplan_html::render_template_html(c, "", false);
		check(missing.empty() && empty_id.empty(),
		      "4c. render_template_html: fehlende Datei oder leere template_id liefert \"\"");

		std::error_code ec;
		std::filesystem::remove(tpl_file, ec);
	}

	// 4d/4e (IMP-H7): Vorlage per Pfad — voller Pfad as-is, Name mit ".html"
	// im Vorlagen-Ordner, nicht existierender Pfad -> "" (classic-Fallback)
	{
		const std::string tdir = liedplan_html::templates_dir();
		const std::filesystem::path pf = std::filesystem::u8path(tdir) / "path_test.html";
		write_file_bytes(pf, "<!DOCTYPE html><html><body>{{zeile1}}</body></html>");

		LiedplanCard c;
		c.id = "box_path";
		c.zeile1 = "Pfadvorlage";

		const std::string as_path =
			liedplan_html::render_template_html(c, pf.u8string(), false);
		const std::string with_html =
			liedplan_html::render_template_html(c, "path_test.html", false);
		check(as_path.find("Pfadvorlage") != std::string::npos &&
			      with_html.find("Pfadvorlage") != std::string::npos,
		      "4d. IMP-H7: template_id als voller Pfad und als 'name.html' funktionieren",
		      as_path);

		const std::string bad =
			liedplan_html::render_template_html(c, "C:/no/such/dir/tpl.html", false);
		check(bad.empty(), "4e. IMP-H7: nicht existierender Vorlagen-Pfad liefert \"\"");

		std::error_code ec;
		std::filesystem::remove(pf, ec);
	}

	// 5. write_card mit template_id -> schreibt substituiertes Template (kein INJECT-Marker);
	//    fehlendes Template -> Fallback auf classic-Weg (OBS_TEXTBOX_INJECT_START vorhanden)
	{
		const std::string tdir = liedplan_html::templates_dir();
		const std::filesystem::path tpl_file =
			std::filesystem::u8path(tdir) / "custom_card.html";
		write_file_bytes(tpl_file,
				 "<!DOCTYPE html><html><body><div id=\"tpl-custom\">{{card_id}}:{{zeile1}}:{{zeile2}}</div></body></html>");

		LiedplanCard c;
		c.id = "box_tpl_ok";
		c.zeile1 = "Eröffnung";
		c.zeile2 = "Grüße 1+4";
		c.template_id = "custom_card";

		std::string out_path;
		const bool wrote_tpl = liedplan_html::write_card(c, out_path);
		const std::string html_tpl =
			wrote_tpl ? read_file_utf8(std::filesystem::u8path(out_path)) : "";
		check(wrote_tpl &&
			      html_tpl.find("<div id=\"tpl-custom\">box_tpl_ok:Eröffnung:Grüße 1+4</div>") !=
				      std::string::npos &&
			      html_tpl.find("OBS_TEXTBOX_INJECT_START") == std::string::npos,
		      "5a. write_card mit gültiger template_id: schreibt Template-HTML ohne CSS-Injektionsblock",
		      html_tpl);

		// Fehlendes Template -> Fallback auf classic-Pfad (mit settings.textbox_path Override)
		const std::filesystem::path tmp_dir =
			std::filesystem::temp_directory_path() / "liedplan_tpl_fallback_test";
		std::error_code ec;
		std::filesystem::create_directories(tmp_dir, ec);
		const std::filesystem::path classic_tpl = tmp_dir / "textbox_classic.html";
		write_file_bytes(
			classic_tpl,
			"<!DOCTYPE html><html><head><style>.textbox-container { padding: 15px 60px 20px 50px; }</style></head>"
			"<body><div class=\"textbox-container\"></div></body></html>");

		LiedplanSettings st;
		st.textbox_path = classic_tpl.u8string();

		c.id = "box_tpl_missing";
		c.template_id = "missing_template_xyz";
		std::string out_fallback_path;
		const bool wrote_fb = liedplan_html::write_card(c, out_fallback_path, st);
		const std::string html_fb =
			wrote_fb ? read_file_utf8(std::filesystem::u8path(out_fallback_path)) : "";
		check(wrote_fb && html_fb.find("OBS_TEXTBOX_INJECT_START") != std::string::npos,
		      "5b. write_card mit fehlendem Template: fällt auf classic-Weg (OBS_TEXTBOX_INJECT_START) zurück");

		// 6. write_card mit template_id leer -> klassisches Verhalten unverändert (INJECT-Marker vorhanden)
		c.id = "box_tpl_empty";
		c.template_id = "";
		std::string out_classic_path;
		const bool wrote_cl = liedplan_html::write_card(c, out_classic_path, st);
		const std::string html_cl =
			wrote_cl ? read_file_utf8(std::filesystem::u8path(out_classic_path)) : "";
		check(wrote_cl && html_cl.find("OBS_TEXTBOX_INJECT_START") != std::string::npos &&
			      html_cl.find("padding: 15px 60px 20px 50px;") != std::string::npos,
		      "6. write_card mit template_id=\"\": classic-Verhalten unverändert (INJECT-Marker vorhanden)");

		std::filesystem::remove(tpl_file, ec);
		std::filesystem::remove_all(tmp_dir, ec);
	}

	// 7. State round-trip: serialize->deserialize mit template_id="rounded" -> identical (operator==);
	//    alter State ohne template_id-Key -> "" ohne Fehler
	{
		LiedplanState s1;
		LiedplanCard &c = s1.add_card();
		c.zeile1 = "Eröffnung";
		c.zeile2 = "Grüße 1+4";
		c.template_id = "rounded";

		const std::string dumped = s1.serialize_json();
		LiedplanState s2;
		const std::string err = s2.deserialize_json(dumped);
		check(err.empty() && (s1 == s2) && s2.cards.size() == 1 &&
			      s2.cards[0].template_id == "rounded",
		      "7a. State Round-trip: serialize->deserialize mit template_id=\"rounded\" identisch (operator==)",
		      err);

		const std::string old_json = R"({
  "version": 1,
  "next_id": 2,
  "cards": [
    {
      "id": "box1",
      "zeile1": "100",
      "zeile2": "1-3",
      "zeile3": "Alt-Karte ohne template_id"
    }
  ]
})";
		LiedplanState s_old;
		const std::string err_old = s_old.deserialize_json(old_json);
		check(err_old.empty() && s_old.cards.size() == 1 &&
			      s_old.cards[0].template_id.empty(),
		      "7b. State Migration: alter State ohne template_id-Key lädt mit template_id=\"\" ohne Fehler",
		      err_old);
	}

	// 8. push_update: letztes JSON enthält alle 14 alten Keys UNVERÄNDERT + template_id mit Kartentext
	{
		LiedplanCard c;
		c.id = "box9";
		c.zeile1 = "Sanctus";
		c.zeile2 = "198, 1";
		c.zeile3 = "Heilig, heilig, heilig";
		c.box_color = "#252d3a";
		c.box_opacity = 200;
		c.text_color = "#c9d1d9";
		c.text_opacity = 255;
		c.corner_color_a = "#123456";
		c.corner_opacity_a = 255;
		c.corner_color_b = "#654321";
		c.corner_opacity_b = 128;
		c.rainbow_corners = true;
		c.anim_file = "https://example.com/sanctus.webm";
		c.show_anim = true;
		c.anim_scale = 4;
		c.anim_x = -15;
		c.anim_y = 30;
		c.template_id = "rounded";

		obs_source_t *dummy_src = reinterpret_cast<obs_source_t *>(0x1);
		liedplan_html::push_update(dummy_src, c, false);
		const std::string raw = liedplan_html::last_push_update_json();
		const nlohmann::json j = nlohmann::json::parse(raw, nullptr, false);

		const bool ok =
			!j.is_discarded() && j.is_object() && j.size() == 18 &&
			j.contains("zeilen") && j.contains("style") &&
			j.value("zeile1", "") == "Sanctus" &&
			j.value("zeile2", "") == "198, 1" &&
			j.value("zeile3", "") == "Heilig, heilig, heilig" &&
			j.value("box_bg", "") == liedplan_html::hex_to_rgba("#252d3a", 200) &&
			j.value("text_color", "") == "rgba(201,209,217,1)" &&
			j.value("box_opacity", -1) == 1 &&
			j.value("triangle_color_a", "") == "rgba(18,52,86,1)" &&
			j.value("triangle_color_b", "") == liedplan_html::hex_to_rgba("#654321", 128) &&
			j.value("rainbow", -1) == 1 &&
			j.value("anim_file", "") == "https://example.com/sanctus.webm" &&
			j.value("show_anim", -1) == 1 &&
			j.value("anim_scale", -1) == 4 &&
			j.value("anim_x", 0) == -15 &&
			j.value("anim_y", 0) == 30 &&
			j.value("template_id", "") == "rounded";
		check(ok, "8. push_update: enthält alle 14 alten Keys unverändert + template_id=\"rounded\"",
		      raw);
	}

	// 9. F2b: Beispiel-Vorlagen im builtin-Ordner (plugin/templates/) werden
	//    gefunden, token-substituiert und über write_card ausgeliefert.
	{
		std::error_code ec;
		const std::string user_dir = liedplan_html::templates_dir();
		check(!user_dir.empty(), "9a. templates_dir() liefert gültiges Verzeichnis im Test-Modus", user_dir);

		// User-Ordner leeren, damit die builtin-Examples gefunden werden.
		for (const auto &entry : std::filesystem::directory_iterator(std::filesystem::u8path(user_dir), ec)) {
			if (entry.is_regular_file())
				std::filesystem::remove(entry.path(), ec);
		}

		const std::vector<std::string> ids = liedplan_html::list_template_ids();
		const bool has_standard = std::find(ids.begin(), ids.end(), "standard") != ids.end();
		const bool has_minimal = std::find(ids.begin(), ids.end(), "minimal") != ids.end();
		const bool has_display = std::find(ids.begin(), ids.end(), "display") != ids.end();
		std::string id_list;
		for (const std::string &id : ids)
			id_list += id + ",";
		check(has_standard && has_minimal && has_display,
		      "9b. list_template_ids(): builtin-Vorlagen standard/minimal/display gefunden", id_list);

		LiedplanCard c;
		c.id = "box_builtin";
		c.zeile1 = "Vorgehen";
		c.zeile2 = "501, 1";
		c.zeile3 = "Gemeinde singt";
		c.box_color = "#101820";
		c.box_opacity = 180;
		c.text_color = "#c9d1d9";
		c.text_opacity = 255;
		c.corner_color_a = "#123456";
		c.corner_opacity_a = 255;
		c.corner_color_b = "#654321";
		c.corner_opacity_b = 128;
		c.rainbow_corners = true;

		// standard.html: Marker + Token-Substitution + rainbow-Klasse.
		const std::string html_std = liedplan_html::render_template_html(c, "standard");
		const bool c_marker = html_std.find("LIEDPLAN_TEMPLATE: standard") != std::string::npos;
		const bool c_rainbow = html_std.find("class=\"rainbow\"") != std::string::npos;
		const bool c_rgba = html_std.find("--lp-bg-color: rgba(16,24,32,0.705882)") != std::string::npos;
		const bool c_line = html_std.find("\"Vorgehen\"") != std::string::npos;
		const bool c_clean = html_std.find("{{") == std::string::npos;
		std::string c_detail = std::string(c_marker ? "marker " : "NO-marker ") +
			(c_rainbow ? "rainbow " : "NO-rainbow ") +
			(c_rgba ? "rgba " : "NO-rgba ") +
			(c_line ? "line " : "NO-line ") +
			(c_clean ? "clean" : "TOKEN-REST: " + html_std.substr(html_std.find("{{"), 40));
		check(c_marker && c_rainbow && c_rgba && c_line && c_clean,
		      "9c. standard.html: Marker, rainbow-Klasse, rgba-Tokens + Zeilen substituiert, kein Token-Rest",
		      c_detail);

		// minimal.html: rendert ohne rainbow-Klasse.
		const std::string html_min = liedplan_html::render_template_html(c, "minimal");
		check(html_min.find("LIEDPLAN_TEMPLATE: minimal") != std::string::npos &&
		      html_min.find("class=\"rainbow\"") == std::string::npos,
		      "9d. minimal.html: rendert ohne rainbow-Klasse");

		// Live-Push-Contract: builtin-Templates enthalten den Update-Listener.
		const std::string builtin_dir = liedplan_html::builtin_templates_dir();
		const std::string tpl_raw =
			read_file_utf8(std::filesystem::u8path(builtin_dir) / "standard.html");
		check(tpl_raw.find("liedplan_card_update") != std::string::npos,
		      "9e. standard.html enthält liedplan_card_update-Listener (reload-free push)", builtin_dir);

		// write_card(template_id="display"): frisches Template, keine INJECT-Marker.
		c.template_id = "display";
		std::string out_path;
		const bool wrote = liedplan_html::write_card(c, out_path);
		const std::string html_disp =
			wrote ? read_file_utf8(std::filesystem::u8path(out_path)) : "";
		check(wrote &&
		      html_disp.find("LIEDPLAN_TEMPLATE: display") != std::string::npos &&
		      html_disp.find("OBS_TEXTBOX_INJECT_START") == std::string::npos,
		      "9f. write_card(template_id=\"display\"): Template-HTML ohne CSS-Injektion");

		// Fehlende Vorlage -> classic-Fallback (INJECT-Marker), kein Crash.
		c.template_id = "gibt-es-nicht";
		std::string out_fb;
		const bool wrote_fb = liedplan_html::write_card(c, out_fb);
		const std::string html_fb =
			wrote_fb ? read_file_utf8(std::filesystem::u8path(out_fb)) : "";
		check(wrote_fb && html_fb.find("OBS_TEXTBOX_INJECT_START") != std::string::npos,
		      "9g. write_card mit fehlender Vorlage: fällt auf classic zurück");

		// User-Ordner überschreibt builtin (gleicher id).
		write_file_bytes(std::filesystem::u8path(user_dir) / "standard.html",
				 "<!DOCTYPE html><html><body>{{zeile1}}</body></html>");
		const std::string html_override = liedplan_html::render_template_html(c, "standard");
		check(html_override == "<!DOCTYPE html><html><body>Vorgehen</body></html>",
		      "9h. user-Vorlage überschreibt builtin-Vorlage gleicher id", html_override);
		std::filesystem::remove(std::filesystem::u8path(user_dir) / "standard.html", ec);
	}

	if (g_failures > 0) {
		std::cerr << g_failures << " check(s) failed.\n";
		return 1;
	}
	std::cout << "All test-liedplan-templates checks passed.\n";
	return 0;
}
