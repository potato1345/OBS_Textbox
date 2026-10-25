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

#include "liedplan-html.hpp"
#include "liedplan-textfmt.hpp"

#include <obs-module.h>
#ifndef LIEDPLAN_TEST_NO_OBS
#include <plugin-support.h>
#endif
#include <util/bmem.h>

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace liedplan_html {

namespace {

constexpr const char *kRepoTextboxPath = "C:/Users/Johann/Desktop/GitHub/OBS_Textbox/main/textbox.html";
constexpr const char *kDefaultTriangleColor = "#fce647";
constexpr int kDefaultTriangleOpacity = 255;

constexpr const char *kListenerScript =
	"<script>\n"
	"// Liedplan-Plugin: Live-Updates ohne Reload (javascript_event → CustomEvent)\n"
	"(function () {\n"
	"    function setVar(name, value) {\n"
	"        document.documentElement.style.setProperty(name, value);\n"
	"    }\n"
	"    window.addEventListener(\"liedplan_card_update\", function (ev) {\n"
	"        var d = ev.detail;\n"
	"        if (!d) return;\n"
	"        if (d.zeile1 != null) setVar(\"--zeile-1\", '\"' + String(d.zeile1) + '\"');\n"
	"        if (d.zeile2 != null) setVar(\"--zeile-2\", '\"' + String(d.zeile2) + '\"');\n"
	"        if (d.zeile3 != null) setVar(\"--zeile-3\", '\"' + String(d.zeile3) + '\"');\n"
	"        if (d.box_bg != null) setVar(\"--box-bg\", d.box_bg);\n"
	"        if (d.text_color != null) setVar(\"--text-color\", d.text_color);\n"
	"        if (d.box_opacity != null) setVar(\"--box-opacity\", String(d.box_opacity));\n"
	"        if (d.anim_file != null) setVar(\"--anim-file\", '\"' + String(d.anim_file) + '\"');\n"
	"        if (d.show_anim != null) setVar(\"--show-anim\", String(d.show_anim));\n"
	"        if (d.anim_scale != null) setVar(\"--anim-scale\", String(d.anim_scale));\n"
	"        if (d.anim_x != null) setVar(\"--anim-x\", String(d.anim_x) + \"px\");\n"
	"        if (d.anim_y != null) setVar(\"--anim-y\", String(d.anim_y) + \"px\");\n"
	"        // Die 150-ms updateLines()-Poll der Seite wendet die Variablen an\n"
	"        // (Zeilen ein/ausblenden, Anim-Wrapper).\n"
	"    });\n"
	"})();\n"
	"</script>\n";

// Escaped HTML-Sonderzeichen (& < > " ').
std::string escape_html(const std::string &input)
{
	std::string out;
	out.reserve(input.size());
	for (char c : input) {
		switch (c) {
		case '&':
			out += "&amp;";
			break;
		case '<':
			out += "&lt;";
			break;
		case '>':
			out += "&gt;";
			break;
		case '"':
			out += "&quot;";
			break;
		case '\'':
			out += "&#39;";
			break;
		default:
			out += c;
			break;
		}
	}
	return out;
}

// Escaped einen String für CSS-Variablen in einem <style>-Block:
// Rückwärtsschrägstrich, doppelte Anführungszeichen und < > & escapen (Steuerzeichen ignorieren).
std::string escape_css_string(const std::string &input)
{
	std::string out;
	out.reserve(input.size());
	for (unsigned char uc : input) {
		if (uc < 0x20u || uc == 0x7Fu)
			continue;
		const char c = static_cast<char>(uc);
		switch (c) {
		case '\\':
			out += "\\\\";
			break;
		case '"':
			out += "\\\"";
			break;
		case '<':
			out += "\\3c ";
			break;
		case '>':
			out += "\\3e ";
			break;
		case '&':
			out += "\\26 ";
			break;
		default:
			out += c;
			break;
		}
	}
	return out;
}

// Formatiert "#RRGGBB" + Opacity (0..255) als "rgba(r,g,b,a)".
std::string format_rgba(const std::string &hex, uint32_t fallback_rgb, int opacity)
{
	const uint32_t rgb = parse_hex_color(hex, fallback_rgb);
	const int r = static_cast<int>((rgb >> 16) & 0xFFu);
	const int g = static_cast<int>((rgb >> 8) & 0xFFu);
	const int b = static_cast<int>(rgb & 0xFFu);
	const int alpha_clamped = std::clamp(opacity, 0, 255);

	std::ostringstream oss;
	oss.imbue(std::locale::classic());
	oss << "rgba(" << r << "," << g << "," << b << ",";
	if (alpha_clamped == 0) {
		oss << "0";
	} else if (alpha_clamped == 255) {
		oss << "1";
	} else {
		oss << (static_cast<double>(alpha_clamped) / 255.0);
	}
	oss << ")";
	return oss.str();
}

bool has_any_text(const LiedplanCard &card)
{
	return !card.zeile1.empty() || !card.zeile2.empty() || !card.zeile3.empty();
}

std::string read_utf8_file(const char *utf8_path)
{
	std::error_code ec;
	const std::filesystem::path p = std::filesystem::u8path(utf8_path);
	if (!std::filesystem::exists(p, ec) || ec)
		return "";

	std::ifstream file(p, std::ios::binary);
	if (!file.is_open())
		return "";

	std::ostringstream ss;
	ss << file.rdbuf();
	if (!file.good() && !file.eof())
		return "";
	return ss.str();
}

std::string render_line_div(const char *id, const std::string &text)
{
	std::string out = "\t\t<div id=\"";
	out += id;
	out += "\"";
	if (text.empty())
		out += " style=\"display:none;\"";
	out += ">";
	out += escape_html(text);
	out += "</div>\n";
	return out;
}

} // namespace

std::string hex_to_rgba(const std::string &hex, int opacity_0_255)
{
	return format_rgba(hex, 0x000000u, opacity_0_255);
}

std::string build_inject_css(const LiedplanCard &card)
{
	const std::string box_bg =
		format_rgba(card.box_color.empty() ? "#ffffff" : card.box_color, 0xFFFFFFu, card.box_opacity);
	const std::string text_col =
		format_rgba(card.text_color.empty() ? "#000000" : card.text_color, 0x000000u, card.text_opacity);
	const std::string tri_col = hex_to_rgba(kDefaultTriangleColor, kDefaultTriangleOpacity);
	const int box_opacity = has_any_text(card) ? 1 : 0;

	std::ostringstream oss;
	oss.imbue(std::locale::classic());
	oss << "/* OBS_TEXTBOX_INJECT_START */\n"
	    << ":root {\n"
	    << "  --zeile-1: \"" << escape_css_string(card.zeile1) << "\";\n"
	    << "  --zeile-2: \"" << escape_css_string(card.zeile2) << "\";\n"
	    << "  --zeile-3: \"" << escape_css_string(card.zeile3) << "\";\n"
	    << "  --anim-file: \"\";\n"
	    << "  --show-anim: 0;\n"
	    << "  --anim-scale: 3;\n"
	    << "  --anim-x: 0px;\n"
	    << "  --anim-y: 0px;\n"
	    << "  --box-bg: " << box_bg << ";\n"
	    << "  --triangle-color-a: " << tri_col << ";\n"
	    << "  --triangle-color-b: " << tri_col << ";\n"
	    << "  --text-color: " << text_col << ";\n"
	    << "  --box-opacity: " << box_opacity << ";\n"
	    << "}\n"
	    << "html, body { background-color: rgba(0, 0, 0, 0) !important; }\n"
	    << "/* OBS_TEXTBOX_INJECT_END */";
	return oss.str();
}

std::string render_card_html(const LiedplanCard &card, const std::string &repo_html)
{
	if (repo_html.empty())
		return card_html(card);

	const std::size_t head_pos = repo_html.rfind("</head>");
	const std::size_t body_pos = repo_html.rfind("</body>");
	if (head_pos == std::string::npos || body_pos == std::string::npos || head_pos >= body_pos)
		return card_html(card);

	const std::string style_block = "<style>\n" + build_inject_css(card) + "\n</style>\n";

	std::string out;
	out.reserve(repo_html.size() + style_block.size() + 1024);
	out.append(repo_html, 0, head_pos);
	out.append(style_block);
	out.append(repo_html, head_pos, body_pos - head_pos);
	out.append(kListenerScript);
	out.append(repo_html, body_pos, repo_html.size() - body_pos);
	return out;
}

std::string cards_dir()
{
#ifdef LIEDPLAN_TEST_NO_OBS
	const std::filesystem::path dir =
		std::filesystem::temp_directory_path() / "liedplan_cards_test";
#else
	char *cfg_path = obs_module_config_path("liedplan-state.json");
	const std::string base = cfg_path ? cfg_path : "";
	bfree(cfg_path);
	if (base.empty())
		return "";

	const std::filesystem::path dir = std::filesystem::u8path(base).parent_path() / "cards";
#endif
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);

	std::string out = dir.generic_u8string();
	while (!out.empty() && (out.back() == '/' || out.back() == '\\'))
		out.pop_back();
	return out;
}

std::string card_html(const LiedplanCard &card)
{
	const int box_alpha = std::clamp(card.box_opacity, 0, 255);
	const std::string box_rgba = format_rgba(card.box_color, 0xFFFFFFu, box_alpha);
	const std::string text_rgba = format_rgba(card.text_color, 0x000000u, card.text_opacity);

	std::string box_style = "background:" + box_rgba + ";";
	if (box_alpha <= 0)
		box_style += "display:none;";

	const std::string lines_style = "color:" + text_rgba + ";";

	std::string html;
	html.reserve(2048);
	html += "<!DOCTYPE html>\n"
		"<html lang=\"de\">\n"
		"<head>\n"
		"<meta charset=\"utf-8\">\n"
		"<style>\n"
		"html, body {\n"
		"\tmargin: 0;\n"
		"\tpadding: 0;\n"
		"\twidth: 800px;\n"
		"\theight: 400px;\n"
		"\tbackground: transparent;\n"
		"\toverflow: hidden;\n"
		"}\n"
		"#card {\n"
		"\tposition: relative;\n"
		"\twidth: 800px;\n"
		"\theight: 400px;\n"
		"}\n"
		"#box {\n"
		"\tposition: absolute;\n"
		"\tinset: 0;\n"
		"\twidth: 800px;\n"
		"\theight: 400px;\n"
		"\tborder-radius:24px;\n"
		"\toverflow: hidden;\n"
		"}\n"
		"#lines {\n"
		"\tposition: relative;\n"
		"\tz-index: 1;\n"
		"\twidth: 800px;\n"
		"\theight: 400px;\n"
		"\tdisplay: flex;\n"
		"\tflex-direction: column;\n"
		"\talign-items: center;\n"
		"\tjustify-content: center;\n"
		"\ttext-align: center;\n"
		"\tfont-family: 'Segoe UI', sans-serif;\n"
		"\tline-height: 1.25;\n"
		"\tbox-sizing: border-box;\n"
		"}\n"
		"#l1, #l2 {\n"
		"\tfont-size: 44px;\n"
		"\twidth: 100%;\n"
		"}\n"
		"#l3 {\n"
		"\tfont-size: 60px;\n"
		"\twidth: 100%;\n"
		"}\n"
		"</style>\n"
		"</head>\n"
		"<body>\n"
		"<div id=\"card\">\n"
		"\t<div id=\"box\" style=\"" +
		box_style +
		"\"></div>\n"
		"\t<div id=\"lines\" style=\"" +
		lines_style + "\">\n";

	html += render_line_div("l1", card.zeile1);
	html += render_line_div("l2", card.zeile2);
	html += render_line_div("l3", card.zeile3);

	html += "\t</div>\n"
		"</div>\n"
		"<script>\n"
		"function hexToRgb(hex) {\n"
		"\tif (typeof hex !== \"string\" || !/^#[0-9a-fA-F]{6}$/.test(hex)) return \"0,0,0\";\n"
		"\tvar v = parseInt(hex.slice(1), 16);\n"
		"\treturn ((v >> 16) & 255) + \",\" + ((v >> 8) & 255) + \",\" + (v & 255);\n"
		"}\n"
		"function setLine(id, val) {\n"
		"\tvar el = document.getElementById(id);\n"
		"\tif (!el) return;\n"
		"\tvar text = val == null ? \"\" : String(val);\n"
		"\tel.textContent = text;\n"
		"\tel.style.display = text.length > 0 ? \"block\" : \"none\";\n"
		"}\n"
		"function applyCard(d) {\n"
		"\tif (!d) return;\n"
		"\tsetLine(\"l1\", d.zeile1); setLine(\"l2\", d.zeile2); setLine(\"l3\", d.zeile3);\n"
		"\tif (d.box_color != null) {\n"
		"\t\tvar box = document.getElementById(\"box\");\n"
		"\t\tvar ba = Math.max(0, Math.min(255, d.box_opacity|0));\n"
		"\t\tbox.style.background = \"rgba(\" + hexToRgb(d.box_color) + \",\" + (ba/255) + \")\";\n"
		"\t\tbox.style.display = ba <= 0 ? \"none\" : \"block\";\n"
		"\t}\n"
		"\tif (d.text_color != null) {\n"
		"\t\tvar ta = Math.max(0, Math.min(255, d.text_opacity|0));\n"
		"\t\tdocument.getElementById(\"lines\").style.color =\n"
		"\t\t\t\"rgba(\" + hexToRgb(d.text_color) + \",\" + (ta/255) + \")\";\n"
		"\t}\n"
		"}\n"
		"window.addEventListener(\"liedplan_card_update\", ev => applyCard(ev.detail));\n"
		"</script>\n"
		"</body>\n"
		"</html>\n";

	return html;
}

bool write_card(const LiedplanCard &card, std::string &path_out)
{
	if (card.id.empty())
		return false;

	const std::string dir = cards_dir();
	if (dir.empty())
		return false;

	const std::string repo_html = read_utf8_file(kRepoTextboxPath);
	if (repo_html.empty()) {
#ifndef LIEDPLAN_TEST_NO_OBS
		obs_log(LOG_INFO, "Liedplan: textbox.html nicht gefunden, nutze Fallback-Template");
#endif
	}

	const std::string full_path = dir + "/" + card.id + ".html";
	const std::string html = render_card_html(card, repo_html);

	std::ofstream file(std::filesystem::u8path(full_path), std::ios::binary | std::ios::trunc);
	if (!file.is_open())
		return false;

	file.write(html.data(), static_cast<std::streamsize>(html.size()));
	file.flush();
	if (!file.good())
		return false;

	path_out = full_path;
	return true;
}

void push_update(obs_source_t *src, const LiedplanCard &card, bool clear)
{
	if (!src)
		return;

	const std::string box_bg =
		format_rgba(card.box_color.empty() ? "#ffffff" : card.box_color, 0xFFFFFFu, card.box_opacity);
	const std::string text_col =
		format_rgba(card.text_color.empty() ? "#000000" : card.text_color, 0x000000u, card.text_opacity);
	const int box_opacity = clear ? 0 : (has_any_text(card) ? 1 : 0);

	QJsonObject obj;
	obj.insert(QStringLiteral("zeile1"),
		   clear ? QStringLiteral("") : QString::fromUtf8(card.zeile1.c_str()));
	obj.insert(QStringLiteral("zeile2"),
		   clear ? QStringLiteral("") : QString::fromUtf8(card.zeile2.c_str()));
	obj.insert(QStringLiteral("zeile3"),
		   clear ? QStringLiteral("") : QString::fromUtf8(card.zeile3.c_str()));
	obj.insert(QStringLiteral("box_bg"), QString::fromUtf8(box_bg.c_str()));
	obj.insert(QStringLiteral("text_color"), QString::fromUtf8(text_col.c_str()));
	obj.insert(QStringLiteral("box_opacity"), box_opacity);
	obj.insert(QStringLiteral("anim_file"), QStringLiteral(""));
	obj.insert(QStringLiteral("show_anim"), 0);
	obj.insert(QStringLiteral("anim_scale"), 3);
	obj.insert(QStringLiteral("anim_x"), 0);
	obj.insert(QStringLiteral("anim_y"), 0);

	const QByteArray json_bytes = QJsonDocument(obj).toJson(QJsonDocument::Compact);

#ifndef LIEDPLAN_TEST_NO_OBS
	proc_handler_t *ph = obs_source_get_proc_handler(src);
	if (!ph)
		return;

	calldata_t cd = {};
	calldata_init(&cd);
	calldata_set_string(&cd, "eventName", "liedplan_card_update");
	calldata_set_string(&cd, "jsonString", json_bytes.constData());
	proc_handler_call(ph, "javascript_event", &cd);
	calldata_free(&cd);
#else
	(void)json_bytes;
#endif
}

} // namespace liedplan_html
