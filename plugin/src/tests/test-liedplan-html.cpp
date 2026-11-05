/*
 * test-liedplan-html.cpp — Unit-Tests für liedplan-html.cpp (Suite 7, v3).
 *
 * Zweck:
 *   - Prüft build_inject_css() (CSS-Variablen, Escaping, rgba-Formatierung, Anim-Werte).
 *   - Prüft card_html() und #7-Konsistenz (leere Karte -> opacity:0 statt display:none auf #box;
 *     box_bg-Alpha aus card.box_opacity; .anim-wrapper und Regenbogen-CSS).
 *   - Prüft render_card_html() mit main/textbox.html inkl. Erhalt der geschützten Padding-Regel
 *     (.textbox-container { padding: 15px 60px 20px 50px; }).
 *   - Prüft push_update() JSON-Shape (alle 14 Felder, clear=true/false, nullptr-Guard).
 *   - Prüft normalize_anim_src() (Data-URI für Bilder, relative Pfade + Traversal-Schutz,
 *     http/https/file-URLs, Video-Kopie, portable Absolutpfad-Erkennung inkl. UNC/POSIX).
 *   - Prüft write_card() mit Settings-Override (settings.textbox_path) und Dev-Fallback.
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
	// 1. build_inject_css: Basis-Variablen, rgba-Alpha aus box_opacity, --box-opacity aus Textpräsenz
	{
		LiedplanCard c;
		c.id = "box1";
		c.zeile1 = "Eröffnung";
		c.zeile2 = "144, 1+4+6";
		c.zeile3 = "Nun jauchzt dem Herren";
		c.box_color = "#3C404D";
		c.box_opacity = 128;
		c.text_color = "#eff0f1";
		c.text_opacity = 255;
		c.corner_color_a = "#ff0000";
		c.corner_opacity_a = 255;
		c.corner_color_b = "#00ff00";
		c.corner_opacity_b = 51;

		const std::string css = liedplan_html::build_inject_css(c);
		check(css.find("/* OBS_TEXTBOX_INJECT_START */") != std::string::npos &&
			      css.find("/* OBS_TEXTBOX_INJECT_END */") != std::string::npos,
		      "1a. build_inject_css: enthält INJECT_START und INJECT_END Marker");
		check(css.find("--zeile-1: \"Eröffnung\";") != std::string::npos &&
			      css.find("--zeile-2: \"144, 1+4+6\";") != std::string::npos &&
			      css.find("--zeile-3: \"Nun jauchzt dem Herren\";") != std::string::npos,
		      "1b. build_inject_css: enthält alle 3 Textzeilen");
		check(css.find("--box-bg: rgba(60,64,77,0.501961);") != std::string::npos &&
			      css.find("--box-opacity: 1;") != std::string::npos,
		      "1c. build_inject_css (#7): --box-bg trägt Alpha aus box_opacity (128), --box-opacity ist 1 bei Text");
		check(css.find("--triangle-color-a: rgba(255,0,0,1);") != std::string::npos &&
			      css.find("--triangle-color-b: rgba(0,255,0,0.2);") != std::string::npos &&
			      css.find("--text-color: rgba(239,240,241,1);") != std::string::npos,
		      "1d. build_inject_css: Ecken- und Textfarben exakt als rgba formatiert");

		// Leere Karte -> --box-opacity: 0, aber --box-bg behält Alpha aus card.box_opacity
		LiedplanCard c_empty = c;
		c_empty.zeile1.clear();
		c_empty.zeile2.clear();
		c_empty.zeile3.clear();
		const std::string css_empty = liedplan_html::build_inject_css(c_empty);
		check(css_empty.find("--box-bg: rgba(60,64,77,0.501961);") != std::string::npos &&
			      css_empty.find("--box-opacity: 0;") != std::string::npos,
		      "1e. build_inject_css (#7): leere Karte setzt --box-opacity: 0 bei unverändertem --box-bg");
	}

	// 2. card_html & #7-Konsistenz: leere Karte -> opacity:0 statt display:none auf <div id="box">;
	//    box_bg Alpha aus box_opacity
	{
		LiedplanCard c_empty;
		c_empty.id = "box2";
		c_empty.box_color = "#ffffff";
		c_empty.box_opacity = 191;
		c_empty.text_color = "#000000";
		c_empty.text_opacity = 255;

		const std::string html_empty = liedplan_html::card_html(c_empty);
		const std::string expected_empty_box =
			"<div id=\"box\" style=\"background:" +
			liedplan_html::hex_to_rgba("#ffffff", 191) +
			";opacity:0; pointer-events:none;\"></div>";
		check(html_empty.find(expected_empty_box) != std::string::npos &&
			      html_empty.find("<div id=\"box\" style=\"background:" +
					      liedplan_html::hex_to_rgba("#ffffff", 191) +
					      ";display:none;\">") == std::string::npos,
		      "2a. card_html (#7): leere Karte nutzt opacity:0; pointer-events:none; statt display:none auf #box");

		// Karte MIT Text, aber box_opacity = 0 (transparenter Box-Hintergrund, Text & Ecken sichtbar)
		LiedplanCard c_trans;
		c_trans.id = "box3";
		c_trans.zeile1 = "GL 144";
		c_trans.box_color = "#ffffff";
		c_trans.box_opacity = 0;
		c_trans.text_color = "#112233";
		c_trans.text_opacity = 255;

		const std::string html_trans = liedplan_html::card_html(c_trans);
		check(html_trans.find("<div id=\"box\" style=\"background:rgba(255,255,255,0);\"></div>") !=
				      std::string::npos &&
			      html_trans.find("<div id=\"lines\" style=\"color:rgba(17,34,51,1);\">") !=
				      std::string::npos,
		      "2b. card_html (#7): Karte mit Text und box_opacity=0 hat rgba(...,0) ohne opacity:0/display:none auf <div id=\"box\">");
		check(html_trans.find("class=\"anim-wrapper\" id=\"anim-wrapper\"") != std::string::npos &&
			      html_trans.find("window.addEventListener(\"liedplan_card_update\"") !=
				      std::string::npos,
		      "2c. card_html: enthält .anim-wrapper und liedplan_card_update Event-Listener");
	}

	// 3. render_card_html mit main/textbox.html: Erhalt der geschützten Padding-Zeile
	//    .textbox-container { padding: 15px 60px 20px 50px; }
	{
		const std::filesystem::path repo_root =
#ifdef LIEDPLAN_REPO_ROOT
			std::filesystem::u8path(LIEDPLAN_REPO_ROOT);
#else
			std::filesystem::u8path("..");
#endif
		const std::filesystem::path textbox_file = repo_root / "main" / "textbox.html";
		const std::string repo_html = read_file_utf8(textbox_file);
		check(!repo_html.empty(), "3a. main/textbox.html im Repo lesbar", textbox_file.u8string());

		LiedplanCard c;
		c.id = "box1";
		c.zeile1 = "Kyrie";
		c.zeile2 = "437, 1-4";
		c.zeile3 = "Herr, erbarme dich";
		c.rainbow_corners = true;

		const std::string rendered = liedplan_html::render_card_html(c, repo_html);
		const std::size_t pos_container = rendered.find(".textbox-container {");
		const std::size_t pos_padding = rendered.find("padding: 15px 60px 20px 50px;");
		const std::size_t pos_inject = rendered.find("OBS_TEXTBOX_INJECT_START");
		const std::size_t pos_head = rendered.rfind("</head>");
		const std::size_t pos_body = rendered.rfind("</body>");
		const std::size_t pos_listener = rendered.find("liedplan_card_update");

		check(pos_container != std::string::npos && pos_padding != std::string::npos &&
			      pos_container < pos_padding,
		      "3b. render_card_html: geschützte Zeile '.textbox-container { ... padding: 15px 60px 20px 50px; }' bleibt vollständig erhalten");
		check(pos_inject != std::string::npos && pos_head != std::string::npos &&
			      pos_inject < pos_head && pos_listener != std::string::npos &&
			      pos_body != std::string::npos && pos_listener < pos_body,
		      "3c. render_card_html: CSS-Injektion vor </head>, Live-Update-Listener vor </body>");
		check(rendered.find("@keyframes rainbow-rotate") != std::string::npos,
		      "3d. render_card_html: rainbow_corners=true injiziert @keyframes rainbow-rotate");
	}

	// 4. push_update JSON-Shape & #7-Konsistenz
	{
		LiedplanCard c;
		c.id = "box5";
		c.zeile1 = "Gloria";
		c.zeile2 = "169, 1-3";
		c.zeile3 = "Gloria, Ehre sei Gott";
		c.box_color = "#252d3a";
		c.box_opacity = 200;
		c.text_color = "#c9d1d9";
		c.text_opacity = 255;
		c.corner_color_a = "#123456";
		c.corner_opacity_a = 255;
		c.corner_color_b = "#654321";
		c.corner_opacity_b = 128;
		c.rainbow_corners = true;
		c.anim_file = "https://example.com/gloria.webm";
		c.show_anim = true;
		c.anim_scale = 4;
		c.anim_x = -25;
		c.anim_y = 40;

		obs_source_t *dummy_src = reinterpret_cast<obs_source_t *>(0x1);
		liedplan_html::push_update(dummy_src, c, false);
		const std::string json_normal = liedplan_html::last_push_update_json();
		const nlohmann::json j = nlohmann::json::parse(json_normal, nullptr, false);

		const bool shape_ok =
			!j.is_discarded() && j.is_object() &&
			j.value("zeile1", "") == "Gloria" &&
			j.value("zeile2", "") == "169, 1-3" &&
			j.value("zeile3", "") == "Gloria, Ehre sei Gott" &&
			j.value("box_bg", "") == liedplan_html::hex_to_rgba("#252d3a", 200) &&
			j.value("text_color", "") == "rgba(201,209,217,1)" &&
			j.value("box_opacity", -1) == 1 &&
			j.value("triangle_color_a", "") == "rgba(18,52,86,1)" &&
			j.value("triangle_color_b", "") == liedplan_html::hex_to_rgba("#654321", 128) &&
			j.value("rainbow", -1) == 1 &&
			j.value("anim_file", "") == "https://example.com/gloria.webm" &&
			j.value("show_anim", -1) == 1 &&
			j.value("anim_scale", -1) == 4 &&
			j.value("anim_x", 0) == -25 &&
			j.value("anim_y", 0) == 40;
		check(shape_ok, "4a. push_update(clear=false): JSON-Shape mit allen 14 erwarteten Schlüsseln und Typen",
		      json_normal);

		// clear=true -> zeile1..3 == "" und box_opacity == 0, aber box_bg behält Alpha aus card.box_opacity
		liedplan_html::push_update(dummy_src, c, true);
		const std::string json_clear = liedplan_html::last_push_update_json();
		const nlohmann::json jc = nlohmann::json::parse(json_clear, nullptr, false);
		const bool clear_ok =
			!jc.is_discarded() && jc.is_object() &&
			jc.value("zeile1", "x").empty() &&
			jc.value("zeile2", "x").empty() &&
			jc.value("zeile3", "x").empty() &&
			jc.value("box_opacity", -1) == 0 &&
			jc.value("box_bg", "") == liedplan_html::hex_to_rgba("#252d3a", 200);
		check(clear_ok, "4b. push_update(clear=true): Zeilen geleert, box_opacity=0, box_bg unverändert",
		      json_clear);

		// nullptr-Guard: push_update(nullptr, ...) überschreibt nichts
		liedplan_html::push_update(nullptr, c, false);
		check(liedplan_html::last_push_update_json() == json_clear,
		      "4c. push_update(nullptr): sicherer No-Op");

		// 4d. F2a: template_id im Push-JSON (15 Keys insgesamt; bleibt auch bei clear=true erhalten)
		c.template_id = "rounded";
		liedplan_html::push_update(dummy_src, c, false);
		const nlohmann::json jt1 =
			nlohmann::json::parse(liedplan_html::last_push_update_json(), nullptr, false);
		liedplan_html::push_update(dummy_src, c, true);
		const nlohmann::json jt2 =
			nlohmann::json::parse(liedplan_html::last_push_update_json(), nullptr, false);
		check(jt1.is_object() && jt1.size() == 17 &&
			jt1.value("template_id", "") == "rounded" && jt2.is_object() &&
			jt2.size() == 17 && jt2.value("template_id", "") == "rounded",
			"4d. push_update (IMP-G): JSON hat 17 Schlüssel inkl. template_id + zeilen/style (auch bei clear=true)");
	}

	// 5. normalize_anim_src: Data-URI, relativ + Traversal-Guard, http/https, portable Absolutpfade
	{
		const std::filesystem::path tmp_dir =
			std::filesystem::temp_directory_path() / "liedplan_html_suite7";
		std::error_code ec;
		std::filesystem::remove_all(tmp_dir, ec);
		std::filesystem::create_directories(tmp_dir / "anim", ec);
		std::filesystem::create_directories(tmp_dir / "cards", ec);

		write_file_bytes(tmp_dir / "anim" / "candle.gif", "GIF89a");
		write_file_bytes(tmp_dir / "outside.gif", "GIF89a");

		LiedplanSettings st;
		st.animation_dir = (tmp_dir / "anim").u8string();

		// 5a: Relativer Pfad innerhalb animation_dir -> Data-URI
		const std::string rel_uri = liedplan_html::normalize_anim_src("candle.gif", st);
		check(rel_uri == "data:image/gif;base64,R0lGODlh",
		      "5a. normalize_anim_src: relativer Bildpfad in animation_dir -> base64 Data-URI",
		      rel_uri);

		// 5b: Path-Traversal außerhalb animation_dir -> "" (abgelehnt)
		const std::string trav_uri = liedplan_html::normalize_anim_src("../outside.gif", st);
		check(trav_uri.empty(),
		      "5b. normalize_anim_src: Path-Traversal '../outside.gif' wird abgelehnt",
		      trav_uri);

		// 5c: HTTP/HTTPS/file-URLs unverändert
		check(liedplan_html::normalize_anim_src("https://cdn.example.org/a.gif", st) ==
			      "https://cdn.example.org/a.gif" &&
			      liedplan_html::normalize_anim_src("http://localhost:8080/a.webm", st) ==
				      "http://localhost:8080/a.webm" &&
			      liedplan_html::normalize_anim_src("file:///C:/anim/a.gif", st) ==
				      "file:///C:/anim/a.gif",
		      "5c. normalize_anim_src: http/https/file-URLs bleiben unverändert");

		// 5d: Portable Absolutpfad-Erkennung (Windows-Laufwerk, UNC, POSIX-Pfad)
		const std::string win_abs =
			liedplan_html::normalize_anim_src("C:\\nicht_da\\test.gif", st);
		const std::string unc_abs =
			liedplan_html::normalize_anim_src("\\\\server\\share\\test.gif", st);
		const std::string posix_abs =
			liedplan_html::normalize_anim_src("/opt/obs/anim/test.gif", st);
		check(win_abs == "file:///C:/nicht_da/test.gif" &&
			      unc_abs == "file:///server/share/test.gif" &&
			      posix_abs == "file:///opt/obs/anim/test.gif",
		      "5d. normalize_anim_src (#5): Windows-, UNC- und POSIX-Absolutpfade werden portabel erkannt",
		      win_abs + " | " + unc_abs + " | " + posix_abs);

		// 5e: write_card mit settings.textbox_path Override (#5)
		const std::filesystem::path custom_tpl = tmp_dir / "custom_textbox.html";
		write_file_bytes(
			custom_tpl,
			"<!DOCTYPE html><html><head><style>.textbox-container { padding: 15px 60px 20px 50px; }</style></head>"
			"<body><div id=\"custom-template-marker\">OK</div></body></html>");
		st.textbox_path = custom_tpl.u8string();

		LiedplanCard c_wr;
		c_wr.id = "box_override_test";
		c_wr.zeile1 = "GL 400";
		std::string out_html_path;
		const bool wrote = liedplan_html::write_card(c_wr, out_html_path, st);
		const std::string written_html =
			wrote ? read_file_utf8(std::filesystem::u8path(out_html_path)) : "";
		check(wrote && written_html.find("custom-template-marker") != std::string::npos &&
			      written_html.find("OBS_TEXTBOX_INJECT_START") != std::string::npos,
		      "5e. write_card (#5): nutzt settings.textbox_path Override vor Dev-Fallback");

		std::filesystem::remove_all(tmp_dir, ec);
	}

	if (g_failures > 0) {
		std::cerr << g_failures << " check(s) failed.\n";
		return 1;
	}
	std::cout << "All test-liedplan-html checks passed.\n";
	return 0;
}
