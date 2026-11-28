/*
 * test-textbox-html.cpp — Unit-Tests für textbox-html.cpp (Suite 7, v3).
 *
 * Zweck:
 *   - Prüft build_inject_css() (CSS-Variablen, Escaping, rgba-Formatierung, Anim-Werte).
 *   - Prüft card_html() und #7-Konsistenz (leere Karte -> opacity:0 statt display:none auf #box;
 *     box_bg-Alpha aus card.box_opacity; .anim-wrapper und Regenbogen-CSS).
 *   - Prüft render_card_html() mit plugin/data/index.html inkl. Erhalt der geschützten Padding-Regel
 *     (.textbox-container { padding: 15px 60px 20px 50px; }).
 *   - Prüft push_update() JSON-Shape (alle 14 Felder, clear=true/false, nullptr-Guard).
 *   - Prüft normalize_anim_src() (Data-URI für Bilder, relative Pfade + Traversal-Schutz,
 *     http/https/file-URLs, Video-Kopie, portable Absolutpfad-Erkennung inkl. UNC/POSIX).
 *   - Prüft write_card() mit Settings-Override (settings.textbox_path) und Dev-Fallback.
 */

#ifndef _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#endif

#include "../textbox-html.hpp"
#include "../textbox-state.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

namespace textbox_html {
std::string last_push_update_json();
} // namespace textbox_html

namespace {

int g_failures = 0;

// Test-Auswerter: zählt Fehler in g_failures, gibt [PASS]/[FAIL] mit optionalem Detail auf stdout/stderr aus.
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

// Hilfsfunktion: Liest eine Datei im Binärmodus und gibt den Inhalt als String zurück.
// Für Test-Fixtures (anim-GIFs, Referenz-HTML) und write_card-Output verwendet.
std::string read_file_utf8(const std::filesystem::path &p)
{
	std::ifstream f(p, std::ios::binary);
	if (!f.is_open())
		return "";
	std::ostringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

// Hilfsfunktion: Schreibt Byte-Daten binär in eine Datei (überschreibt).
// Für temporäre Test-Fixtures (anim-GIFs, ZIP-Archive, custom_tpl) verwendet.
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
		TextboxCard c;
		c.id = "box1";
		c.zeile1 = "Beginn";
		c.zeile2 = "144, 1+4+6";
		c.zeile3 = "Kickoff";
		c.box_color = "#3C404D";
		c.box_opacity = 128;
		c.text_color = "#eff0f1";
		c.text_opacity = 255;
		c.corner_color_a = "#ff0000";
		c.corner_opacity_a = 255;
		c.corner_color_b = "#00ff00";
		c.corner_opacity_b = 51;

		const std::string css = textbox_html::build_inject_css(c);
		check(css.find("/* OBS_TEXTBOX_INJECT_START */") != std::string::npos &&
			      css.find("/* OBS_TEXTBOX_INJECT_END */") != std::string::npos,
		      "1a. build_inject_css: enthält INJECT_START und INJECT_END Marker");
		check(css.find("--zeile-1: \"Beginn\";") != std::string::npos &&
			css.find("--zeile-2: \"144, 1+4+6\";") != std::string::npos &&
			css.find("--zeile-3: \"Kickoff\";") != std::string::npos,
		    "1b. build_inject_css: enthält alle 3 Textzeilen");
		// 1b2. Zeilen-Labels als CSS-Variablen-Namen: "--"-Label wird Variablenname,
		// sonst Fallback --zeile-N.
		TextboxCard rc;
		rc.id = "varrows";
		rc.rows = {{"--tb-gl", "GL 12"}, {"Alt", "text alt"}, {"--sauber 1", "clean"}};
		const std::string css2 = textbox_html::build_inject_css(rc);
		check(css2.find("--tb-gl: \"GL 12\";") != std::string::npos &&
			css2.find("--zeile-2: \"text alt\";") != std::string::npos &&
			css2.find("--sauber1: \"clean\";") != std::string::npos,
		    "1b2. Zeilen-Labels: -- Präfix => Variablenname (sanisiert), sonst --zeile-N");
		check(css.find("--box-bg: rgba(60,64,77,0.501961);") != std::string::npos &&
			      css.find("--box-opacity: 1;") != std::string::npos,
		      "1c. build_inject_css (#7): --box-bg trägt Alpha aus box_opacity (128), --box-opacity ist 1 bei Text");
		check(css.find("--triangle-color-a: rgba(255,0,0,1);") != std::string::npos &&
			      css.find("--triangle-color-b: rgba(0,255,0,0.2);") != std::string::npos &&
			      css.find("--text-color: rgba(239,240,241,1);") != std::string::npos,
		      "1d. build_inject_css: Ecken- und Textfarben exakt als rgba formatiert");

		// Leere Karte -> --box-opacity: 0, aber --box-bg behält Alpha aus card.box_opacity
		TextboxCard c_empty = c;
		c_empty.zeile1.clear();
		c_empty.zeile2.clear();
		c_empty.zeile3.clear();
		const std::string css_empty = textbox_html::build_inject_css(c_empty);
		check(css_empty.find("--box-bg: rgba(60,64,77,0.501961);") != std::string::npos &&
			      css_empty.find("--box-opacity: 0;") != std::string::npos,
		      "1e. build_inject_css (#7): leere Karte setzt --box-opacity: 0 bei unverändertem --box-bg");
	}

	// 2. card_html & #7-Konsistenz: leere Karte -> opacity:0 statt display:none auf <div id="box">;
	//    box_bg Alpha aus box_opacity
	{
		TextboxCard c_empty;
		c_empty.id = "box2";
		c_empty.box_color = "#ffffff";
		c_empty.box_opacity = 191;
		c_empty.text_color = "#000000";
		c_empty.text_opacity = 255;

		const std::string html_empty = textbox_html::card_html(c_empty);
		const std::string expected_empty_box =
			"<div id=\"box\" style=\"background:" +
			textbox_html::hex_to_rgba("#ffffff", 191) +
			";opacity:0; pointer-events:none;\"></div>";
		check(html_empty.find(expected_empty_box) != std::string::npos &&
			      html_empty.find("<div id=\"box\" style=\"background:" +
					      textbox_html::hex_to_rgba("#ffffff", 191) +
					      ";display:none;\">") == std::string::npos,
		      "2a. card_html (#7): leere Karte nutzt opacity:0; pointer-events:none; statt display:none auf #box");

		// Karte MIT Text, aber box_opacity = 0 (transparenter Box-Hintergrund, Text & Ecken sichtbar)
		TextboxCard c_trans;
		c_trans.id = "box3";
		c_trans.zeile1 = "GL 144";
		c_trans.box_color = "#ffffff";
		c_trans.box_opacity = 0;
		c_trans.text_color = "#112233";
		c_trans.text_opacity = 255;

		const std::string html_trans = textbox_html::card_html(c_trans);
		check(html_trans.find("<div id=\"box\" style=\"background:rgba(255,255,255,0);\"></div>") !=
				      std::string::npos &&
			      html_trans.find("<div id=\"lines\" style=\"color:rgba(17,34,51,1);\">") !=
				      std::string::npos,
		      "2b. card_html (#7): Karte mit Text und box_opacity=0 hat rgba(...,0) ohne opacity:0/display:none auf <div id=\"box\">");
		check(html_trans.find("class=\"anim-wrapper\" id=\"anim-wrapper\"") != std::string::npos &&
			      html_trans.find("window.addEventListener(\"textbox_card_update\"") !=
				      std::string::npos,
		      "2c. card_html: enthält .anim-wrapper und textbox_card_update Event-Listener");
	}

	// 3. render_card_html mit plugin/data/index.html: Erhalt der geschützten Padding-Zeile
	//    .textbox-container { padding: 15px 60px 20px 50px; }
	{
		const std::filesystem::path repo_root =
#ifdef TEXTBOX_REPO_ROOT
			std::filesystem::u8path(TEXTBOX_REPO_ROOT);
#else
			std::filesystem::u8path("..");
#endif
		const std::filesystem::path textbox_file = repo_root / "plugin" / "data" / "index.html";
		const std::string repo_html = read_file_utf8(textbox_file);
		check(!repo_html.empty(), "3a. plugin/data/index.html im Repo lesbar", textbox_file.u8string());

		TextboxCard c;
		c.id = "box1";
		c.zeile1 = "Review";
		c.zeile2 = "437, 1-4";
		c.zeile3 = "Herr, erbarme dich";
		c.rainbow_corners = true;

		const std::string rendered = textbox_html::render_card_html(c, repo_html);
		const std::size_t pos_container = rendered.find(".textbox-container {");
		const std::size_t pos_padding = rendered.find("padding: 15px 60px 20px 50px;");
		const std::size_t pos_inject = rendered.find("OBS_TEXTBOX_INJECT_START");
		const std::size_t pos_head = rendered.rfind("</head>");
		const std::size_t pos_body = rendered.rfind("</body>");
		const std::size_t pos_listener = rendered.find("textbox_card_update");

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
		TextboxCard c;
		c.id = "box5";
		c.zeile1 = "Punkt";
		c.zeile2 = "169, 1-3";
		c.zeile3 = "Review, Sprint 1";
		c.box_color = "#252d3a";
		c.box_opacity = 200;
		c.text_color = "#c9d1d9";
		c.text_opacity = 255;
		c.corner_color_a = "#123456";
		c.corner_opacity_a = 255;
		c.corner_color_b = "#654321";
		c.corner_opacity_b = 128;
		c.rainbow_corners = true;
		c.anim_file = "https://example.com/loop.webm";
		c.show_anim = true;
		c.anim_scale = 4;
		c.anim_x = -25;
		c.anim_y = 40;

		obs_source_t *dummy_src = reinterpret_cast<obs_source_t *>(0x1);
		textbox_html::push_update(dummy_src, c, false);
		const std::string json_normal = textbox_html::last_push_update_json();
		const nlohmann::json j = nlohmann::json::parse(json_normal, nullptr, false);

		const bool shape_ok =
			!j.is_discarded() && j.is_object() &&
			j.value("zeile1", "") == "Punkt" &&
			j.value("zeile2", "") == "169, 1-3" &&
			j.value("zeile3", "") == "Review, Sprint 1" &&
			j.value("box_bg", "") == textbox_html::hex_to_rgba("#252d3a", 200) &&
			j.value("text_color", "") == "rgba(201,209,217,1)" &&
			j.value("box_opacity", -1) == 1 &&
			j.value("triangle_color_a", "") == "rgba(18,52,86,1)" &&
			j.value("triangle_color_b", "") == textbox_html::hex_to_rgba("#654321", 128) &&
			j.value("rainbow", -1) == 1 &&
			j.value("anim_file", "") == "https://example.com/loop.webm" &&
			j.value("show_anim", -1) == 1 &&
			j.value("anim_scale", -1) == 4 &&
			j.value("anim_x", 0) == -25 &&
			j.value("anim_y", 0) == 40;
		check(shape_ok, "4a. push_update(clear=false): JSON-Shape mit allen 14 erwarteten Schlüsseln und Typen",
		      json_normal);

		// clear=true -> zeile1..3 == "" und box_opacity == 0, aber box_bg behält Alpha aus card.box_opacity
		textbox_html::push_update(dummy_src, c, true);
		const std::string json_clear = textbox_html::last_push_update_json();
		const nlohmann::json jc = nlohmann::json::parse(json_clear, nullptr, false);
		const bool clear_ok =
			!jc.is_discarded() && jc.is_object() &&
			jc.value("zeile1", "x").empty() &&
			jc.value("zeile2", "x").empty() &&
			jc.value("zeile3", "x").empty() &&
			jc.value("box_opacity", -1) == 0 &&
			jc.value("box_bg", "") == textbox_html::hex_to_rgba("#252d3a", 200);
		check(clear_ok, "4b. push_update(clear=true): Zeilen geleert, box_opacity=0, box_bg unverändert",
		      json_clear);

		// nullptr-Guard: push_update(nullptr, ...) überschreibt nichts
		textbox_html::push_update(nullptr, c, false);
		check(textbox_html::last_push_update_json() == json_clear,
		      "4c. push_update(nullptr): sicherer No-Op");

		// 4d. F2a: template_id im Push-JSON (15 Keys insgesamt; bleibt auch bei clear=true erhalten)
		c.template_id = "rounded";
		textbox_html::push_update(dummy_src, c, false);
		const nlohmann::json jt1 =
			nlohmann::json::parse(textbox_html::last_push_update_json(), nullptr, false);
		textbox_html::push_update(dummy_src, c, true);
		const nlohmann::json jt2 =
			nlohmann::json::parse(textbox_html::last_push_update_json(), nullptr, false);
		check(jt1.is_object() && jt1.size() == 21 &&
			jt1.value("template_id", "") == "rounded" &&
			jt1.contains("vn") &&
			jt1.value("vn", nlohmann::json::array()).size() >= 1 &&
			jt1.value("vn", nlohmann::json::array())[0].get<std::string>() == "--zeile-1" && jt2.is_object() &&
			jt2.size() == 21 && jt2.value("template_id", "") == "rounded",
			"4d. push_update (IMP-G): JSON hat 21 Schlüssel (inkl. box_x/box_y/box_scale) + template_id + zeilen/style/vn (auch bei clear=true)");
	}

	// 5. normalize_anim_src: Data-URI, relativ + Traversal-Guard, http/https, portable Absolutpfade
	{
		// Temporäres Verzeichnis für normalize_anim_src-Tests: Unterverzeichnisse anim/ und cards/
	// dienen als Fixture für relative Pfade, Path-Traversal und write_card-Override.
	const std::filesystem::path tmp_dir =
			std::filesystem::temp_directory_path() / "textbox_html_suite7";
		std::error_code ec;
		std::filesystem::remove_all(tmp_dir, ec);
		std::filesystem::create_directories(tmp_dir / "anim", ec);
		std::filesystem::create_directories(tmp_dir / "cards", ec);

		write_file_bytes(tmp_dir / "anim" / "candle.gif", "GIF89a");
		write_file_bytes(tmp_dir / "outside.gif", "GIF89a");

		TextboxSettings st;
		st.animation_dir = (tmp_dir / "anim").u8string();

		// 5a: Relativer Pfad innerhalb animation_dir -> Data-URI
		const std::string rel_uri = textbox_html::normalize_anim_src("candle.gif", st);
		check(rel_uri == "data:image/gif;base64,R0lGODlh",
		      "5a. normalize_anim_src: relativer Bildpfad in animation_dir -> base64 Data-URI",
		      rel_uri);

		// 5b: Path-Traversal außerhalb animation_dir -> "" (abgelehnt)
		const std::string trav_uri = textbox_html::normalize_anim_src("../outside.gif", st);
		check(trav_uri.empty(),
		      "5b. normalize_anim_src: Path-Traversal '../outside.gif' wird abgelehnt",
		      trav_uri);

		// 5c: HTTP/HTTPS/file-URLs unverändert
		check(textbox_html::normalize_anim_src("https://cdn.example.org/a.gif", st) ==
			      "https://cdn.example.org/a.gif" &&
			      textbox_html::normalize_anim_src("http://localhost:8080/a.webm", st) ==
				      "http://localhost:8080/a.webm" &&
			      textbox_html::normalize_anim_src("file:///C:/anim/a.gif", st) ==
				      "file:///C:/anim/a.gif",
		      "5c. normalize_anim_src: http/https/file-URLs bleiben unverändert");

		// 5d: Portable Absolutpfad-Erkennung (Windows-Laufwerk, UNC, POSIX-Pfad)
		const std::string win_abs =
			textbox_html::normalize_anim_src("C:\\nicht_da\\test.gif", st);
		const std::string unc_abs =
			textbox_html::normalize_anim_src("\\\\server\\share\\test.gif", st);
		const std::string posix_abs =
			textbox_html::normalize_anim_src("/opt/obs/anim/test.gif", st);
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

		TextboxCard c_wr;
		c_wr.id = "box_override_test";
		c_wr.zeile1 = "GL 400";
		std::string out_html_path;
		const bool wrote = textbox_html::write_card(c_wr, out_html_path, st);
		const std::string written_html =
			wrote ? read_file_utf8(std::filesystem::u8path(out_html_path)) : "";
		check(wrote && written_html.find("custom-template-marker") != std::string::npos &&
		      written_html.find("OBS_TEXTBOX_INJECT_START") != std::string::npos,
		      "5e. write_card (#5): nutzt settings.textbox_path Override vor Dev-Fallback");

		// 5f: IMP-P — sanitize_card_id + write_card gegen Pfad-Traversierung
		{
			const std::string clean = textbox_html::sanitize_card_id("../evil");
			const std::string keep = textbox_html::sanitize_card_id("box-6_x");
			const std::string long_id = textbox_html::sanitize_card_id(std::string(60, 'a'));
			bool ok = clean == "___evil" && keep == "box-6_x" && long_id.size() == 32;

			TextboxCard c_evil;
			c_evil.id = "../../outside";
			c_evil.zeile1 = "X";
			std::string evil_path;
			const bool wrote_evil = textbox_html::write_card(c_evil, evil_path, st);
			const std::filesystem::path cards =
				std::filesystem::u8path(textbox_html::cards_dir());
			const std::string evil_rel = "______outside.html";
			const bool inside = wrote_evil &&
				std::filesystem::exists(cards / evil_rel) &&
				evil_path.find("..") == std::string::npos;
			check(ok && inside,
			      "5f. IMP-P: sanitize_card_id reinigt ID; write_card schreibt nur in cards/");

			std::filesystem::remove(cards / evil_rel, ec);
		}

		std::filesystem::remove_all(tmp_dir, ec);
	}

	// 6. build_inject_css: card.style mit anim_file (type "file") -> data-URI statt Rohpfad
	{
		const std::filesystem::path tmp_dir =
			std::filesystem::temp_directory_path() / "textbox_html_suite7";
		std::error_code ec;
		std::filesystem::remove_all(tmp_dir, ec);
		std::filesystem::create_directories(tmp_dir / "anim", ec);

		// Minimales 1x1 transparentes GIF
		// (GIF89a + Logical Screen Descriptor mit 1x1 + Image Descriptor + terminator)
		const std::string tiny_gif =
			"GIF89a\x01\x00\x01\x00\x80\x00\x00\xff\xff\xff\x00\x00\x00"
			"\x21\xf9\x04\x00\x00\x00\x00\x00"
			"\x2c\x00\x00\x00\x00\x01\x00\x01\x00\x00\x02\x02\x44\x01\x00"
			"\x3b";
		write_file_bytes(tmp_dir / "anim" / "anim_test.gif", tiny_gif);

		TextboxSettings st;
		st.animation_dir = (tmp_dir / "anim").u8string();

		TextboxCard c;
		c.id = "style_anim";
		c.zeile1 = "Test";
		c.box_opacity = 255;
		c.text_opacity = 255;
		// Card style-Regel mit file-Typ, die den Wert überschreibt
		c.style.push_back({"--anim-file", (tmp_dir / "anim" / "anim_test.gif").u8string(), "file"});

		const std::string css = textbox_html::build_inject_css(c, st);
		// Der Rohpfad darf NICHT als letzter --anim-file-Wert vorkommen (User-Werte überschreiben
		// die feste Zeile). Stattdessen muss ein data:image/gif;base64,... erscheinen.
		const std::string raw_path = (tmp_dir / "anim" / "anim_test.gif").generic_u8string();
		// Letztes Vorkommen von --anim-file im CSS extrahieren (User-Regeln stehen nach den festen)
		const std::size_t last_pos = css.rfind("--anim-file:");
		const bool last_is_data = last_pos != std::string::npos &&
			css.find("data:image/gif;base64,", last_pos) != std::string::npos;
		const bool raw_not_last = last_pos == std::string::npos ||
			css.find(raw_path, last_pos) == std::string::npos;
		check(last_is_data && raw_not_last,
		      "6a. build_inject_css mit card.style --anim-file/file: letzter Wert ist data:image/gif;base64, nicht Rohpfad",
		      css.substr(last_pos != std::string::npos ? last_pos : 0, 120));

		// Regel mit type="file" (nicht name="--anim-file") -> ebenfalls normalisiert
		TextboxCard c2;
		c2.id = "style_other";
		c2.zeile1 = "Test2";
		c2.box_opacity = 255;
		c2.text_opacity = 255;
		c2.style.push_back({"--custom-anim", (tmp_dir / "anim" / "anim_test.gif").u8string(), "file"});
		const std::string css2 = textbox_html::build_inject_css(c2, st);
		check(css2.find("data:image/gif;base64,") != std::string::npos,
		      "6b. build_inject_css mit type=file (anderer Name): Wert ist data-URI",
		      css2.substr(css2.find("--custom-anim:"), 120));

		// Pfad-Traversal außerhalb animation_dir -> normalize_anim_src gibt "" zurück
		TextboxCard c3;
		c3.id = "style_trav";
		c3.zeile1 = "Test3";
		c3.box_opacity = 255;
		c3.text_opacity = 255;
		c3.style.push_back({"--anim-file", "../outside.gif", "file"});
		const std::string css3 = textbox_html::build_inject_css(c3, st);
		// Die style-Regel sollte übersprungen sein (normalize_anim_src für "../outside.gif"
		// gibt "" zurück wegen Path-Traversal-Guard);
		// es bleibt nur die feste Zeile --anim-file: ""; (card.anim_file ist leer).
		// Zähle wie oft --anim-file vorkommt: genau 1x (feste Zeile) = die style-Regel wurde übersprungen.
		int count_anim_file = 0;
		for (std::size_t p = 0; (p = css3.find("--anim-file:", p)) != std::string::npos; ++p)
			++count_anim_file;
		check(count_anim_file == 1,
		      "6c. build_inject_css mit Path-Traversal file: Regel übersprungen (nur 1x --anim-file aus fester Zeile)",
		      "count=" + std::to_string(count_anim_file));

		std::filesystem::remove_all(tmp_dir, ec);
	}

	// 7. repo_listener_two_pass: build_repo_listener_script() hat zwei getrennte Loops
	{
		const std::string script = textbox_html::build_repo_listener_script();
		bool has_old_bug = script.find("if (d.vn && d.vn[i]) setVar(d.vn[i]") != std::string::npos;
		bool has_separate_vn_loop = script.find("for (var i = 0; i < d.vn.length && i < 8; i++)") != std::string::npos;
		// Find index of first positional setVar and vn-loop
		std::size_t pos_first_setvar = script.find("setVar(\"--zeile-\" + (i + 1)");
		std::size_t pos_vn_loop = script.find("for (var i = 0; i < d.vn.length && i < 8; i++)");
		bool positional_before_vn = pos_first_setvar != std::string::npos &&
			pos_vn_loop != std::string::npos && pos_first_setvar < pos_vn_loop;
		check(!has_old_bug,
		      "7a. repo_listener_two_pass: alter Bug-Anker 'if (d.vn && d.vn[i]) setVar(d.vn[i]' NICHT enthalten");
		check(has_separate_vn_loop,
		      "7b. repo_listener_two_pass: separater vn-Loop 'for (var i = 0; i < d.vn.length && i < 8; i++)' ist enthalten");
		check(positional_before_vn,
		      "7c. repo_listener_two_pass: positionelles setVar vor vn-Loop (erster Durchlauf vor zweitem)",
		      "pos_first_setvar=" + std::to_string(pos_first_setvar) +
		      " pos_vn_loop=" + std::to_string(pos_vn_loop));
	}

	if (g_failures > 0) {
		std::cerr << g_failures << " check(s) failed.\n";
		return 1;
	}
	std::cout << "All test-textbox-html checks passed.\n";
	return 0;
}
