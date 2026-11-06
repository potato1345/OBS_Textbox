/*
 * liedplan-html.cpp — Implementierung der HTML/CSS-Erzeugung für OBS-Browserquellen (v3).
 *
 * Zweck:
 *   - Baut das vollständige DOM und CSS für jede Liedplan-Karte zusammen.
 *   - Erzeugt CEF-kompatible Animationselemente (<img> für GIFs/Bilder via Data-URI, <video> für Clips).
 *   - Verwaltet CSS-Variablen (--zeile-1..3, --anim-file, --box-bg, etc.) für Live-Injektion.
 *   - Aktualisiert Browserquellen über obs_source_update.
 *
 * Hinweis: Wird mit LIEDPLAN_TEST_NO_OBS auch in Standalone-Tests kompiliert.
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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <tuple>
#include <utility>

namespace liedplan_html {

namespace {

constexpr const char *kRepoTextboxPath = "C:/Users/Johann/Desktop/GitHub/OBS_Textbox/main/index.html";

constexpr const char *kRainbowCssRepo =
	".textbox-container { background-image: none !important; }\n"
	".textbox-container::before {\n"
	"  content: \"\" !important; display: block !important; position: absolute; top: 0; left: 0;\n"
	"  width: 50px; height: 50px; border-top-left-radius: 4px;\n"
	"  background: linear-gradient(135deg, var(--triangle-color-a) 50%, transparent 50%);\n"
	"  pointer-events: none; z-index: 0; animation: rainbow-rotate 12s linear infinite;\n"
	"}\n"
	".textbox-container::after {\n"
	"  content: \"\" !important; display: block !important; position: absolute; bottom: 0; right: 0;\n"
	"  width: 50px; height: 50px; border-bottom-right-radius: 4px;\n"
	"  background: linear-gradient(315deg, var(--triangle-color-b) 50%, transparent 50%);\n"
	"  pointer-events: none; z-index: 0; animation: rainbow-rotate 12s linear infinite;\n"
	"}\n"
	".text-line { position: relative; z-index: 1; }\n"
	"@keyframes rainbow-rotate {\n"
	"  from { filter: hue-rotate(0deg); }\n"
	"  to   { filter: hue-rotate(360deg); }\n"
	"}\n";

constexpr const char *kRainbowCssFallback =
	"#box { background-image: none !important; }\n"
	"#box::before {\n"
	"  content: \"\" !important; display: block !important; position: absolute; top: 0; left: 0;\n"
	"  width: 50px; height: 50px; border-top-left-radius: 24px;\n"
	"  background: linear-gradient(135deg, var(--triangle-color-a) 50%, transparent 50%);\n"
	"  pointer-events: none; z-index: 0; animation: rainbow-rotate 12s linear infinite;\n"
	"}\n"
	"#box::after {\n"
	"  content: \"\" !important; display: block !important; position: absolute; bottom: 0; right: 0;\n"
	"  width: 50px; height: 50px; border-bottom-right-radius: 24px;\n"
	"  background: linear-gradient(315deg, var(--triangle-color-b) 50%, transparent 50%);\n"
	"  pointer-events: none; z-index: 0; animation: rainbow-rotate 12s linear infinite;\n"
	"}\n"
	"#lines { position: relative; z-index: 1; }\n"
	"@keyframes rainbow-rotate {\n"
	"  from { filter: hue-rotate(0deg); }\n"
	"  to   { filter: hue-rotate(360deg); }\n"
	"}\n";

std::string escape_js_string(const char *input)
{
	std::string out;
	if (!input)
		return out;
	for (const char *p = input; *p != '\0'; ++p) {
		switch (*p) {
		case '\\':
			out += "\\\\";
			break;
		case '"':
			out += "\\\"";
			break;
		case '\n':
			out += "\\n";
			break;
		case '\r':
			out += "\\r";
			break;
		case '-':
			out += "\\x2d";
			break;
		default:
			out += *p;
			break;
		}
	}
	return out;
}

std::string build_repo_listener_script()
{
	std::string s =
		"<script>\n"
		"// Liedplan-Plugin: Live-Updates ohne Reload (javascript_event → CustomEvent)\n"
		"(function () {\n"
		"    var RAINBOW_CSS = \"";
	s += escape_js_string(kRainbowCssRepo);
	s += "\";\n"
	     "    function setVar(name, value) {\n"
	     "        document.documentElement.style.setProperty(name, value);\n"
	     "    }\n"
	     "    function applyRainbow(v) {\n"
	     "        var el = document.getElementById(\"lp-rainbow\");\n"
	     "        if (el) el.textContent = v ? RAINBOW_CSS : \"\";\n"
	     "    }\n"
	     "    window.addEventListener(\"liedplan_card_update\", function (ev) {\n"
	     "        var d = ev.detail;\n"
	     "        if (!d) return;\n"
	     "        if (d.zeilen != null) {\n"
	     "            var n = 0;\n"
	     "            for (var i = 0; i < d.zeilen.length && i < 8; i++) {\n"
	     "                setVar(\"--zeile-\" + (i + 1), '\"' + String(d.zeilen[i]) + '\"');\n"
     "                if (d.vn && d.vn[i]) setVar(d.vn[i], '\"' + String(d.zeilen[i]) + '\"');\n"
	     "                n = i + 1;\n"
	     "            }\n"
	     "            setVar(\"--zeile-count\", String(n));\n"
	     "        } else {\n"
	     "            if (d.zeile1 != null) setVar(\"--zeile-1\", '\"' + String(d.zeile1) + '\"');\n"
	     "            if (d.zeile2 != null) setVar(\"--zeile-2\", '\"' + String(d.zeile2) + '\"');\n"
	     "            if (d.zeile3 != null) setVar(\"--zeile-3\", '\"' + String(d.zeile3) + '\"');\n"
	     "        }\n"
	     "        if (d.style != null) {\n"
	     "            for (var j = 0; j < d.style.length; j++) {\n"
	     "                var r = d.style[j];\n"
	     "                if (r && r.name && r.value != null) setVar(r.name, String(r.value));\n"
	     "            }\n"
	     "        }\n"
	     "        if (d.box_bg != null) setVar(\"--box-bg\", d.box_bg);\n"
	     "        if (d.text_color != null) setVar(\"--text-color\", d.text_color);\n"
	     "        if (d.box_opacity != null) setVar(\"--box-opacity\", String(d.box_opacity));\n"
	     "        if (d.triangle_color_a != null) setVar(\"--triangle-color-a\", d.triangle_color_a);\n"
	     "        if (d.triangle_color_b != null) setVar(\"--triangle-color-b\", d.triangle_color_b);\n"
	     "        if (d.rainbow != null) applyRainbow(d.rainbow);\n"
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
	return s;
}

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
/**
 * Zeilen-Labels sind CSS-Variablen-Namen: Label mit "--" Präfix (sanisiert auf
 * [A-Za-z0-9_-]) wird als Variablenname verwendet; sonst Fallback "--zeile-N".
 */
std::string row_var_name(const std::string &label, std::size_t index)
{
	if (label.size() > 2 && label[0] == '-' && label[1] == '-') {
		std::string out;
		for (char c : label) {
			if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			    (c >= '0' && c <= '9') || c == '-' || c == '_')
				out += c;
		}
		if (out.size() > 2)
			return out;
	}
	return "--zeile-" + std::to_string(index + 1);
}

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
	if (!card.zeile1.empty() || !card.zeile2.empty() || !card.zeile3.empty())
		return true;
	for (const auto &r : card.rows) {
		if (!r.value.empty())
			return true;
	}
	return false;
}

// CSS-Var-Name aus frei eingegebenem Text: behält [A-Za-z0-9-], prefixt '--'. "" wenn leer.


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

bool starts_with_icase(const std::string &s, const char *prefix)
{
	std::size_t i = 0;
	for (; prefix[i] != '\0'; ++i) {
		if (i >= s.size())
			return false;
		if (std::tolower(static_cast<unsigned char>(s[i])) !=
		    std::tolower(static_cast<unsigned char>(prefix[i])))
			return false;
	}
	return true;
}

bool is_abs_win_path(const std::string &s)
{
	if (s.empty())
		return false;
	if (std::filesystem::path(std::filesystem::u8path(s)).is_absolute())
		return true;
	// Auch UNC-Pfade ("\\...") und POSIX-Absolutpfade ("/...") plattformübergreifend als absolut behandeln.
	if (s[0] == '/')
		return true;
	if (s.size() >= 2 && s[0] == '\\' && s[1] == '\\')
		return true;
	return false;
}

// Löst den Pfad zu index.html (Standard-Vorlage „classic") auf:
// 1. settings.textbox_path (Settings-Override, falls gesetzt und Datei existiert)
// 2. obs_module_file("index.html") (OBS Plugin-Data-Verzeichnis, falls vorhanden)
// 3. kRepoTextboxPath (Dev-Fallback auf diesem PC)
// anon-Impl; exportierter Wrapper am Namespace-Ende (für Dock "Vorlage aus index.html kopieren").
std::string resolve_textbox_path_impl(const LiedplanSettings &settings)
{
	std::error_code ec;
	if (!settings.textbox_path.empty()) {
		const std::filesystem::path p = std::filesystem::u8path(settings.textbox_path);
		if (std::filesystem::exists(p, ec) && !ec)
			return settings.textbox_path;
	}
#ifndef LIEDPLAN_TEST_NO_OBS
	char *mod_file = obs_module_file("index.html");
	if (mod_file) {
		const std::string mod_path(mod_file);
		bfree(mod_file);
		if (!mod_path.empty()) {
			ec.clear();
			const std::filesystem::path p = std::filesystem::u8path(mod_path);
			if (std::filesystem::exists(p, ec) && !ec)
				return mod_path;
		}
	}
#endif
	return kRepoTextboxPath;
}

std::string g_last_push_update_json;

std::string path_to_file_url(const std::string &path_utf8)
{
	std::string out = "file:///";
	out.reserve(out.size() + path_utf8.size() + 8);
	std::size_t start = 0;
	while (start < path_utf8.size() && (path_utf8[start] == '/' || path_utf8[start] == '\\'))
		++start;
	static const char hexd[] = "0123456789ABCDEF";
	for (std::size_t i = start; i < path_utf8.size(); ++i) {
		const unsigned char c = static_cast<unsigned char>(path_utf8[i]);
		if (c == '\\') {
			out += '/';
		} else if (std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.' || c == '~' || c == ':' ||
		           c == '/') {
			// Unreserved (plus ':' für Laufwerksbuchstaben und '/' als Separator) bleibt roh.
			out += static_cast<char>(c);
		} else {
			// M5-1: alles andere (Spaces, Umlaute/Non-ASCII-Bytes, …) prozent-encodiert,
			// sonst weigert sich CEF, die Datei zu laden.
			out += '%';
			out += hexd[(c >> 4) & 0xF];
			out += hexd[c & 0xF];
		}
	}
	return out;
}

std::string base64_encode(const std::string &data)
{
	static const char tbl[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve((data.size() + 2) / 3 * 4);
	for (std::size_t i = 0; i < data.size(); i += 3) {
		const unsigned b0 = static_cast<unsigned char>(data[i]);
		const unsigned b1 = (i + 1 < data.size()) ? static_cast<unsigned char>(data[i + 1]) : 0;
		const unsigned b2 = (i + 2 < data.size()) ? static_cast<unsigned char>(data[i + 2]) : 0;
		const unsigned n = (b0 << 16) | (b1 << 8) | b2;
		out += tbl[(n >> 18) & 0x3F];
		out += tbl[(n >> 12) & 0x3F];
		out += (i + 1 < data.size()) ? tbl[(n >> 6) & 0x3F] : '=';
		out += (i + 2 < data.size()) ? tbl[n & 0x3F] : '=';
	}
	return out;
}

// Liest eine Datei (max. max_bytes) als rohe Bytes; ok=false bei Fehler.
bool read_file_bytes(const std::string &path, std::size_t max_bytes, std::string &out)
{
	std::ifstream f(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
	if (!f.is_open())
		return false;
	const std::size_t size = static_cast<std::size_t>(f.tellg());
	if (size > max_bytes)
		return false;
	f.seekg(0, std::ios::beg);
	out.resize(size);
	if (size > 0 && !f.read(&out[0], static_cast<std::streamsize>(size)))
		return false;
	return true;
}

std::string ext_lowercase(const std::string &p)
{
	const std::size_t dot = p.find_last_of('.');
	const std::size_t slash = p.find_last_of("/\\");
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
		return "";
	std::string e = p.substr(dot + 1);
	for (char &c : e)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return e;
}

bool is_image_ext(const std::string &e)
{
	return e == "gif" || e == "png" || e == "jpg" || e == "jpeg" || e == "webp" || e == "bmp";
}

bool is_video_ext(const std::string &e)
{
	return e == "webm" || e == "mp4" || e == "mov";
}

std::string mime_for_ext(const std::string &e)
{
	if (e == "gif")
		return "image/gif";
	if (e == "png")
		return "image/png";
	if (e == "jpg" || e == "jpeg")
		return "image/jpeg";
	if (e == "webp")
		return "image/webp";
	if (e == "bmp")
		return "image/bmp";
	return "application/octet-stream";
}

// Nur [A-Za-z0-9._-], sonst '_' — für Video-Kopien neben der Card-HTML (ASCII,
// damit der file://-URL nie Non-ASCII enthält).
std::string sanitize_basename(const std::string &name)
{
	std::string out;
	out.reserve(name.size());
	for (const char c : name)
		out += (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.' || c == '_' || c == '-')
				? c
				: '_';
	while (!out.empty() && out.back() == '_')
		out.pop_back();
	return out.empty() ? "anim" : out;
}

// true, wenn `p` (absolut) innerhalb des Verzeichnisses `dir` liegt (oder gleich).
bool path_inside_dir(const std::string &p, const std::string &dir)
{
	std::error_code ec1, ec2;
	const std::filesystem::path a =
		std::filesystem::weakly_canonical(std::filesystem::u8path(p), ec1);
	const std::filesystem::path b =
		std::filesystem::weakly_canonical(std::filesystem::u8path(dir), ec2);
	if (ec1 || ec2)
		return false;
	auto it = a.begin();
	auto ite = a.end();
	for (const auto &seg : b) {
		if (it == ite || !(*it == seg))
			return false;
		++it;
	}
	return it != ite;
}

} // namespace

// Exportierter Wrapper (Header-Deklaration): classic-Vorlagen-Pfad für Dock & interne Aufrufer.
std::string resolve_textbox_path(const LiedplanSettings &settings)
{
	return resolve_textbox_path_impl(settings);
}

std::string hex_to_rgba(const std::string &hex, int opacity_0_255)
{
	return format_rgba(hex, 0x000000u, opacity_0_255);
}

std::string normalize_anim_src(const std::string &raw, const LiedplanSettings &settings,
                               const std::string &copy_dir)
{
	if (raw.empty())
		return "";

	if (starts_with_icase(raw, "http://") || starts_with_icase(raw, "https://") ||
	    starts_with_icase(raw, "file://")) {
		return raw;
	}

	// Lokalen Pfad auflösen (absolut oder relativ zu settings.animation_dir).
	std::string resolved;
	if (is_abs_win_path(raw)) {
		resolved = std::filesystem::u8path(raw).generic_u8string();
	} else {
		const std::string base_dir_str =
			!settings.animation_dir.empty()
				? settings.animation_dir
				: (std::filesystem::u8path(resolve_textbox_path(settings)).parent_path() / "animation")
					  .u8string();
		std::error_code ec;
		const std::filesystem::path anim_dir = std::filesystem::u8path(base_dir_str);
		const std::filesystem::path candidate = anim_dir / std::filesystem::u8path(raw);
		const bool cand_inside = path_inside_dir(candidate.u8string(), anim_dir.u8string());
		bool parent_inside = false;

		if (cand_inside && !settings.animation_dir.empty() &&
		    std::filesystem::exists(candidate, ec) && !ec) {
			const std::filesystem::path abs_cand =
				std::filesystem::absolute(candidate, ec);
			resolved = (!ec && !abs_cand.empty()) ? abs_cand.generic_u8string()
			                                      : candidate.generic_u8string();
		}
		// Fallback, falls "animation/..." oder "animation\..." relativ zum Repo-Root angegeben wurde
		if (resolved.empty() && anim_dir.has_parent_path()) {
			const std::filesystem::path parent_cand =
				anim_dir.parent_path() / std::filesystem::u8path(raw);
			parent_inside =
				path_inside_dir(parent_cand.u8string(), anim_dir.u8string());
			ec.clear();
			if (parent_inside && !settings.animation_dir.empty() &&
			    std::filesystem::exists(parent_cand, ec) && !ec) {
				const std::filesystem::path abs_cand =
					std::filesystem::absolute(parent_cand, ec);
				resolved = (!ec && !abs_cand.empty()) ? abs_cand.generic_u8string()
				                                     : parent_cand.generic_u8string();
			}
		}
		// Pfad-Guard: relative Pfade müssen nach weakly_canonical im Animation-Base-Ordner bleiben.
		if (!cand_inside && !parent_inside)
			return "";
	}
	if (resolved.empty())
		return raw;

	const std::string ext = ext_lowercase(resolved);

	// M5.1 (CEF-File-Access): Die Card-HTML lädt per file://; CEF 127 weigert sich,
	// Sub-Ressourcen aus ANDEREN Verzeichnisbäumen zu laden ("Not allowed to load
	// local resource"). Bilder daher als base64-data-URI inline (immer ladbar,
	// keine Kopie, bleibt frisch bei jedem push_update),
	if (is_image_ext(ext)) {
		static std::mutex s_cache_mtx;
		static std::map<std::string,
				std::pair<std::pair<std::filesystem::file_time_type, std::size_t>,
					  std::string>>
			s_anim_cache;

		std::error_code ec;
		const std::filesystem::path img_path = std::filesystem::u8path(resolved);
		const auto mtime = std::filesystem::last_write_time(img_path, ec);
		if (!ec) {
			const auto fsize = std::filesystem::file_size(img_path, ec);
			if (!ec && fsize <= 16 * 1024 * 1024) {
				const std::size_t sz = static_cast<std::size_t>(fsize);
				const auto key_meta = std::make_pair(mtime, sz);
				{
					std::lock_guard<std::mutex> lk(s_cache_mtx);
					const auto it = s_anim_cache.find(resolved);
					if (it != s_anim_cache.end() &&
					    it->second.first == key_meta) {
						return it->second.second;
					}
				}
				std::string bytes;
				if (read_file_bytes(resolved, 16 * 1024 * 1024, bytes)) {
					std::string uri = "data:" + mime_for_ext(ext) +
							  ";base64," + base64_encode(bytes);
					std::lock_guard<std::mutex> lk(s_cache_mtx);
					s_anim_cache[resolved] =
						std::make_pair(key_meta, uri);
					return uri;
				}
			}
		} else {
			std::lock_guard<std::mutex> lk(s_cache_mtx);
			s_anim_cache.erase(resolved);
		}
		// Lesefehler → Fallback wie vorher (file-URL, best effort).
	}

	// Videos (webm/mp4/mov) sind zu groß für data-URIs → Kopie NEBEN die Card-HTML
	// (selbes Verzeichnis = selbst bei striktester CEF-Policy erlaubt) + file-URL.
	if (is_video_ext(ext)) {
		if (!copy_dir.empty() && !path_inside_dir(resolved, copy_dir)) {
			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::u8path(copy_dir), ec);
			const std::filesystem::path src = std::filesystem::u8path(resolved);
			const std::filesystem::path dest =
				std::filesystem::u8path(copy_dir) /
				std::filesystem::u8path(sanitize_basename(src.filename().u8string()));
			std::filesystem::copy_file(src, dest,
			                           std::filesystem::copy_options::overwrite_existing, ec);
			if (!ec)
				return path_to_file_url(dest.generic_u8string());
		}
		return path_to_file_url(resolved);
	}

	return path_to_file_url(resolved);
}

std::string build_inject_css(const LiedplanCard &card, const LiedplanSettings &settings,
                             const std::string &copy_dir)
{
	const std::string box_bg =
		format_rgba(card.box_color.empty() ? "#ffffff" : card.box_color, 0xFFFFFFu, card.box_opacity);
	const std::string text_col =
		format_rgba(card.text_color.empty() ? "#000000" : card.text_color, 0x000000u, card.text_opacity);
	const std::string tri_col_a =
		format_rgba(card.corner_color_a.empty() ? "#fce647" : card.corner_color_a, 0xFCE647u, card.corner_opacity_a);
	const std::string tri_col_b =
		format_rgba(card.corner_color_b.empty() ? "#fce647" : card.corner_color_b, 0xFCE647u, card.corner_opacity_b);
	const int box_opacity = has_any_text(card) ? 1 : 0;
	const std::string norm_anim = normalize_anim_src(card.anim_file, settings, copy_dir);
	const int show_anim = card.show_anim ? 1 : 0;

	std::ostringstream oss;
	oss.imbue(std::locale::classic());
	oss << "/* OBS_TEXTBOX_INJECT_START */\n"
	    << ":root {\n";
	const auto rows = effective_rows(card);
	for (std::size_t i = 0; i < rows.size(); ++i)
	oss << "  " << row_var_name(rows[i].label, i) << ": \"" << escape_css_string(rows[i].value) << "\";\n";
	oss << "  --zeile-count: " << rows.size() << ";\n";
	oss << "  --anim-file: \"" << escape_css_string(norm_anim) << "\";\n"
	    << "  --show-anim: " << show_anim << ";\n"
	    << "  --anim-scale: " << card.anim_scale << ";\n"
	    << "  --anim-x: " << card.anim_x << "px;\n"
	    << "  --anim-y: " << card.anim_y << "px;\n"
	    << "  --box-bg: " << box_bg << ";\n"
	    << "  --triangle-color-a: " << tri_col_a << ";\n"
	    << "  --triangle-color-b: " << tri_col_b << ";\n"
	    << "  --text-color: " << text_col << ";\n"
	    << "  --box-opacity: " << box_opacity << ";\n";
	// Generelle CSS-Regeln (--name: value); ungültige Namen/Values werden gedroppt.
	for (const auto &rule : card.style) {
		const std::string var_name = sanitize_css_var_name(rule.name);
		const std::string var_value = sanitize_css_value(rule.value);
		if (var_name.empty() || var_value.empty())
			continue;
		oss << "  " << var_name << ": " << var_value << ";\n";
	}
	oss << "}\n"
	    << "html, body { background-color: rgba(0, 0, 0, 0) !important; }\n"
	    << "/* OBS_TEXTBOX_INJECT_END */";
	return oss.str();
}

std::string render_card_html(const LiedplanCard &card, const std::string &repo_html,
			     const LiedplanSettings &settings, const std::string &copy_dir)
{
	if (repo_html.empty())
		return card_html(card);

	const std::size_t head_pos = repo_html.rfind("</head>");
	const std::size_t body_pos = repo_html.rfind("</body>");
	if (head_pos == std::string::npos || body_pos == std::string::npos || head_pos >= body_pos)
		return card_html(card);

	const std::string style_block =
		"<style>\n" + build_inject_css(card, settings, copy_dir) + "\n</style>\n";
	const std::string rainbow_block = std::string("<style id=\"lp-rainbow\">") +
					  (card.rainbow_corners ? kRainbowCssRepo : "") + "</style>\n";
	const std::string listener_script = build_repo_listener_script();

	std::string out;
	out.reserve(repo_html.size() + style_block.size() + rainbow_block.size() + listener_script.size());
	out.append(repo_html, 0, head_pos);
	out.append(style_block);
	out.append(rainbow_block);
	out.append(repo_html, head_pos, body_pos - head_pos);
	out.append(listener_script);
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

std::size_t prune_orphan_card_html(const std::vector<std::string> &live_ids)
{
	const std::string dir = cards_dir();
	if (dir.empty())
		return 0;

	std::error_code ec;
	std::filesystem::directory_iterator it(std::filesystem::u8path(dir), ec);
	if (ec)
		return 0;

	std::size_t removed = 0;
	for (const auto &entry : it) {
		if (!entry.is_regular_file(ec) || ec)
			continue;
		const std::string name = entry.path().filename().u8string();
		if (name.size() < 6 || name.compare(name.size() - 5, 5, ".html") != 0)
			continue;
		const std::string stem = name.substr(0, name.size() - 5);
		if (std::find(live_ids.begin(), live_ids.end(), stem) != live_ids.end())
			continue;
		std::error_code rm;
		std::filesystem::remove(entry.path(), rm);
		if (!rm)
			++removed;
	}
	return removed;
}

std::string templates_dir()
{
#ifdef LIEDPLAN_TEST_NO_OBS
	const std::filesystem::path dir =
		std::filesystem::temp_directory_path() / "liedplan_templates_test";
#else
	char *cfg_path = obs_module_config_path("liedplan-state.json");
	const std::string base = cfg_path ? cfg_path : "";
	bfree(cfg_path);
	if (base.empty())
		return "";

	const std::filesystem::path dir = std::filesystem::u8path(base).parent_path() / "templates";
#endif
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	if (ec)
		return "";

	std::string out = dir.generic_u8string();
	while (!out.empty() && (out.back() == '/' || out.back() == '\\'))
		out.pop_back();
	return out;
}

std::string substitute_tokens(const LiedplanCard &card, const std::string &template_html,
			      bool clear, const LiedplanSettings &settings,
			      const std::string &copy_dir)
{
	if (template_html.empty())
		return "";

	const std::string text_col = format_rgba(
		card.text_color.empty() ? "#000000" : card.text_color, 0x000000u, card.text_opacity);
	const std::string box_col = format_rgba(
		card.box_color.empty() ? "#ffffff" : card.box_color, 0xFFFFFFu, card.box_opacity);
	const std::string corner_a = format_rgba(
		card.corner_color_a.empty() ? "#fce647" : card.corner_color_a, 0xFCE647u,
		card.corner_opacity_a);
	const std::string corner_b = format_rgba(
		card.corner_color_b.empty() ? "#fce647" : card.corner_color_b, 0xFCE647u,
		card.corner_opacity_b);
	const std::string text_op = std::to_string(std::clamp(card.text_opacity, 0, 255));
	const std::string box_op = std::to_string(std::clamp(card.box_opacity, 0, 255));
	const std::string corner_op_a = std::to_string(std::clamp(card.corner_opacity_a, 0, 255));
	const std::string corner_op_b = std::to_string(std::clamp(card.corner_opacity_b, 0, 255));
	const std::string rainbow = card.rainbow_corners ? "1" : "0";
	const std::string anim_src =
		(template_html.find("{{anim_src}}") != std::string::npos)
			? normalize_anim_src(card.anim_file, settings, copy_dir)
			: std::string();
	const std::string show_anim = card.show_anim ? "1" : "0";
	const std::string anim_scale = std::to_string(card.anim_scale);
	const std::string anim_x = std::to_string(card.anim_x);
	const std::string anim_y = std::to_string(card.anim_y);
	const std::string clear_str = clear ? "1" : "0";

	const std::pair<const char *, std::string> tokens[] = {
		{"{{zeile1}}", clear ? std::string() : card.zeile1},
		{"{{zeile2}}", clear ? std::string() : card.zeile2},
		{"{{zeile3}}", clear ? std::string() : card.zeile3},
		{"{{text_color}}", text_col},
		{"{{box_color}}", box_col},
		{"{{corner_a}}", corner_a},
		{"{{corner_b}}", corner_b},
		{"{{corner_op_a}}", corner_op_a},
		{"{{corner_op_b}}", corner_op_b},
		{"{{text_opacity}}", text_op},
		{"{{box_opacity}}", box_op},
		{"{{rainbow}}", rainbow},
		{"{{anim_src}}", anim_src},
		{"{{show_anim}}", show_anim},
		{"{{anim_scale}}", anim_scale},
		{"{{anim_x}}", anim_x},
		{"{{anim_y}}", anim_y},
		{"{{card_id}}", card.id},
		{"{{clear}}", clear_str},
	};

	std::string out = template_html;
	for (const auto &kv : tokens) {
		const std::string key(kv.first);
		const std::string &val = kv.second;
		std::size_t pos = 0;
		while ((pos = out.find(key, pos)) != std::string::npos) {
			out.replace(pos, key.size(), val);
			pos += val.size();
		}
	}
	// Dynamische Zeilen-Tokens {{zeile-1}}..{{zeile-8}} + {{zeile-count}}.
	const auto rows = effective_rows(card);
	for (std::size_t i = 0; i < kMaxCardRows; ++i) {
		const std::string key = "{{zeile-" + std::to_string(i + 1) + "}}";
		const std::string val = (i < rows.size() && !clear) ? rows[i].value : std::string();
		std::size_t pos = 0;
		while ((pos = out.find(key, pos)) != std::string::npos) {
			out.replace(pos, key.size(), val);
			pos += val.size();
		}
	}
	{
		const std::string key = "{{zeile-count}}";
		const std::string val = std::to_string(rows.size());
		std::size_t pos = 0;
		while ((pos = out.find(key, pos)) != std::string::npos) {
			out.replace(pos, key.size(), val);
			pos += val.size();
		}
	}
	return out;
}

std::string builtin_templates_dir()
{
#ifdef LIEDPLAN_TEST_NO_OBS
#if defined(LIEDPLAN_REPO_ROOT)
	const std::filesystem::path dir =
		std::filesystem::u8path(LIEDPLAN_REPO_ROOT) / "plugin" / "templates";
	if (std::filesystem::is_directory(dir))
		return dir.generic_u8string();
#endif
	return "";
#else
	// OBS liefert hier oft NULL für obs_module_file (Data-Pfad-Ableitung hängt von der
	// Installation ab). Analog zu resolve_textbox_path: erst Plugin-Data-Verzeichnis
	// (falls vorhanden), dann Repo-Fallback neben dem Dev-Textbox-Pfad.
	char *mod_file = obs_module_file("templates/standard.html");
	const std::string base = mod_file ? mod_file : "";
	bfree(mod_file);
	if (!base.empty()) {
		const std::filesystem::path dir = std::filesystem::u8path(base).parent_path();
		if (std::filesystem::is_directory(dir)) {
			std::string out = dir.generic_u8string();
			while (!out.empty() && (out.back() == '/' || out.back() == '\\'))
				out.pop_back();
			obs_log(LOG_INFO, "Liedplan: builtin-Vorlagenordner (Data): %s", out.c_str());
			return out;
		}
	}
	const std::filesystem::path repo_dir =
		std::filesystem::u8path(kRepoTextboxPath).parent_path().parent_path() /
		"plugin" / "templates";
	if (std::filesystem::is_directory(repo_dir)) {
		std::string out = repo_dir.generic_u8string();
		while (!out.empty() && (out.back() == '/' || out.back() == '\\'))
			out.pop_back();
		obs_log(LOG_INFO, "Liedplan: builtin-Vorlagenordner (Repo): %s", out.c_str());
		return out;
	}
	obs_log(LOG_INFO, "Liedplan: kein builtin-Vorlagenordner gefunden");
	return "";
#endif
}

std::string render_template_html(const LiedplanCard &card, const std::string &template_id,
				 bool clear, const LiedplanSettings &settings,
				 const std::string &copy_dir)
{
	if (template_id.empty())
		return "";

	// IMP-H7: template_id ist ein Vorlagen-Pfad (User-Eingabe). Kandidaten:
	// 1) Pfad as-is (absolut oder relativ), 2) Legacy-ID-Schema <dir>/<id>.html
	// (user-Dir zuerst, überschreibt builtin), 3) <dir>/<id> für Namen mit
	// ".html"-Endung. Fehlender Pfad -> "" (write_card fällt auf classic).
	const std::string user_dir = templates_dir();
	const std::string builtin_dir = builtin_templates_dir();
	const bool ends_html = template_id.size() >= 5 &&
		template_id.compare(template_id.size() - 5, 5, ".html") == 0;
	std::vector<std::string> candidates;
	candidates.push_back(template_id);
	const std::string dirs[] = {user_dir, builtin_dir};
	for (const std::string &dir : dirs) {
		if (dir.empty())
			continue;
		if (!ends_html)
			candidates.push_back(dir + "/" + template_id + ".html");
		candidates.push_back(dir + "/" + template_id);
	}
	for (const std::string &tpl_path : candidates) {
		const std::string tpl_html = read_utf8_file(tpl_path.c_str());
		if (!tpl_html.empty()) {
			std::string out = substitute_tokens(card, tpl_html, clear, settings, copy_dir);
			// IMP-L6: Templates bekamen nur Token-baked Werte — generelle
			// CSS-Regeln (--name) und Zeilen-Custom-Variablen fehlten im
			// written HTML und wirkten erst nach einem Live-Push. Derselbe
			// :root-Inject-Block wie im Classic-Pfad, direkt hinter dem
			// Template-Style (letzte Definition gewinnt).
			if (!clear) {
				// Insertion vor </head>; minimalistische Templates ohne
				// </head> bekommen den Block vor </body>.
				std::size_t insert_pos = out.rfind("</head>");
				if (insert_pos == std::string::npos)
					insert_pos = out.rfind("</body>");
				if (insert_pos != std::string::npos) {
					const std::string style_block =
						"<style>\n" + build_inject_css(card, settings, copy_dir) + "\n</style>\n";
					const std::string rainbow_block = std::string("<style id=\"lp-rainbow\">") +
						(card.rainbow_corners ? kRainbowCssRepo : "") + "</style>\n";
					out.insert(insert_pos, style_block + rainbow_block);
				}
			}
			return out;
		}
	}
	return "";
}

std::vector<std::string> list_template_ids()
{
	std::vector<std::string> ids;
	const std::string dirs[] = {templates_dir(), builtin_templates_dir()};
	for (const std::string &dir : dirs) {
		if (dir.empty())
			continue;
		std::error_code ec;
		if (!std::filesystem::is_directory(dir, ec))
			continue;
		for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
			if (!entry.is_regular_file())
				continue;
			const std::string name = entry.path().filename().u8string();
			if (name.size() < 6 || name.compare(name.size() - 5, 5, ".html") != 0)
				continue;
			std::string id = name.substr(0, name.size() - 5);
			if (id.empty())
				continue;
			if (std::find(ids.begin(), ids.end(), id) == ids.end())
				ids.push_back(id);
		}
	}
	std::sort(ids.begin(), ids.end());
	return ids;
}

bool reload_browser_source(obs_source_t *source)
{
	if (!source)
		return false;
#ifdef LIEDPLAN_TEST_NO_OBS
	(void)source;
	return false;
#else
	// Browser-Source hat die Proc "reload" (argumentlos); proc_handler_call aus
	// callback/proc.h ruft sie mit leerem calldata auf. false, wenn Proc fehlt.
	proc_handler_t *ph = obs_source_get_proc_handler(source);
	if (!ph)
		return false;
	calldata_t cd;
	calldata_init(&cd);
	const bool ok = proc_handler_call(ph, "reload", &cd);
	calldata_free(&cd);
	return ok;
#endif
}

std::string card_html(const LiedplanCard &card)
{
	const int box_alpha = std::clamp(card.box_opacity, 0, 255);
	const std::string box_rgba = format_rgba(card.box_color, 0xFFFFFFu, box_alpha);
	const std::string text_rgba = format_rgba(card.text_color, 0x000000u, card.text_opacity);
	const std::string tri_a_rgba = format_rgba(
		card.corner_color_a.empty() ? "#fce647" : card.corner_color_a, 0xFCE647u, card.corner_opacity_a);
	const std::string tri_b_rgba = format_rgba(
		card.corner_color_b.empty() ? "#fce647" : card.corner_color_b, 0xFCE647u, card.corner_opacity_b);

	const bool visible = has_any_text(card);
	std::string box_style = "background:" + box_rgba + ";";
	if (!visible)
		box_style += "opacity:0; pointer-events:none;";

	const std::string lines_style =
		"color:" + text_rgba + ";" + (visible ? "" : "opacity:0; pointer-events:none;");

	const std::string norm_anim =
		card.anim_file.empty() ? std::string() : normalize_anim_src(card.anim_file);
	const int show_anim = card.show_anim ? 1 : 0;

	std::string html;
	html.reserve(4096);
	html += "<!DOCTYPE html>\n"
		"<html lang=\"de\">\n"
		"<head>\n"
		"<meta charset=\"utf-8\">\n"
		"<style>\n"
		":root {\n"
		"\t--triangle-color-a: " +
		tri_a_rgba +
		";\n"
		"\t--triangle-color-b: " +
		tri_b_rgba +
		";\n"
		"\t--anim-file: \"" +
		escape_css_string(norm_anim) +
		"\";\n"
		"\t--show-anim: " +
		std::to_string(show_anim) +
		";\n"
		"\t--anim-scale: " +
		std::to_string(card.anim_scale) +
		";\n"
		"\t--anim-x: " +
		std::to_string(card.anim_x) +
		"px;\n"
		"\t--anim-y: " +
		std::to_string(card.anim_y) +
		"px;\n"
		"}\n"
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
		"\tbackground-image: linear-gradient(135deg, var(--triangle-color-a) 50%, transparent 50%), linear-gradient(315deg, var(--triangle-color-b) 50%, transparent 50%);\n"
		"\tbackground-size: 50px 50px, 50px 50px;\n"
		"\tbackground-position: top left, bottom right;\n"
		"\tbackground-repeat: no-repeat;\n"
		"\ttransition: opacity 0.3s ease;\n"
		"}\n"
		"#lines {\n"
		"	position: relative;\n"
		"	z-index: 1;\n"
		"	width: 800px;\n"
		"	height: 400px;\n"
		"	display: flex;\n"
		"	flex-direction: column;\n"
		"	align-items: center;\n"
		"	justify-content: center;\n"
		"	text-align: center;\n"
		"	font-family: 'Segoe UI', sans-serif;\n"
		"	line-height: 1.25;\n"
		"	box-sizing: border-box;\n"
		"}\n"
		".lp-row {\n"
		"	font-size: 44px;\n"
		"	width: 100%;\n"
		"}\n"
		".lp-row.lp-last {\n"
		"	font-size: 60px;\n"
		"}\n"
		".anim-wrapper {\n"
		"\tposition: absolute;\n"
		"\tbottom: 100%;\n"
		"\tright: 0;\n"
		"\tmargin-bottom: 15px;\n"
		"\tz-index: 10;\n"
		"\ttransform: translate(var(--anim-x, 0px), var(--anim-y, 0px)) scale(var(--anim-scale, 3));\n"
		"\ttransform-origin: bottom right;\n"
		"}\n"
		".anim-wrapper img, .anim-wrapper video {\n"
		"\tdisplay: block;\n"
		"\timage-rendering: pixelated;\n"
		"}\n"
		"</style>\n"
		"<style id=\"lp-rainbow\">";
	if (card.rainbow_corners)
		html += kRainbowCssFallback;
	html += "</style>\n"
		"</head>\n"
		"<body>\n";
	if (box_alpha <= 0) {
		html += "<!-- id=\"box\" style=\"background:" + box_rgba + ";display:none;\" -->\n";
	}
	html += "<div id=\"card\">\n"
		"\t<div class=\"anim-wrapper\" id=\"anim-wrapper\" style=\"display:none;\"></div>\n"
		"\t<div id=\"box\" style=\"" +
		box_style +
		"\"></div>\n"
		"\t<div id=\"lines\" style=\"" +
		lines_style + "\">\n";

	// Zeilen dynamisch: Anzahl = effective_rows(card) (1..8); letzte Zeile größer.
	const auto rows = effective_rows(card);
	for (std::size_t i = 0; i < rows.size(); ++i) {
		std::string div = "		<div id=\"l" + std::to_string(i + 1) + "\"";
		if (rows[i].value.empty())
			div += " style=\"display:none;\"";
		div += " class=\"lp-row";
		if (i + 1 == rows.size())
			div += " lp-last";
		div += "\">" + escape_html(rows[i].value) + "</div>\n";
		html += div;
	}

	html += "\t</div>\n"
		"</div>\n"
		"<script>\n"
		"var RAINBOW_CSS = \"" +
		escape_js_string(kRainbowCssFallback) +
		"\";\n"
		"var currentAnimFile = \"\";\n"
		"function applyRainbow(v) {\n"
		"\tvar el = document.getElementById(\"lp-rainbow\");\n"
		"\tif (el) el.textContent = v ? RAINBOW_CSS : \"\";\n"
		"}\n"
		"function setVar(name, value) {\n"
		"\tdocument.documentElement.style.setProperty(name, value);\n"
		"\tvar box = document.getElementById(\"box\");\n"
		"\tif (box) box.style.setProperty(name, value);\n"
		"}\n"
		"function setLine(id, val) {\n"
		"\tvar el = document.getElementById(id);\n"
		"\tif (!el) return;\n"
		"\tvar text = val == null ? \"\" : String(val);\n"
		"\tel.textContent = text;\n"
		"\tel.style.display = text.length > 0 ? \"block\" : \"none\";\n"
		"}\n"
		"function updateAnim() {\n"
		"\tvar cs = getComputedStyle(document.documentElement);\n"
		"\tvar showRaw = (cs.getPropertyValue(\"--show-anim\") || \"\").trim();\n"
		"\tvar fileRaw = (cs.getPropertyValue(\"--anim-file\") || \"\").trim();\n"
		"\tvar animFile = fileRaw.replace(/^[\"']|[\"']$/g, \"\");\n"
		"\tvar animWrapper = document.getElementById(\"anim-wrapper\");\n"
		"\tif (!animWrapper) return;\n"
		"\tif (showRaw === \"1\" && animFile) {\n"
		"\t\tanimWrapper.style.display = \"block\";\n"
		"\t\tif (animFile !== currentAnimFile) {\n"
		"\t\t\tcurrentAnimFile = animFile;\n"
		"\t\t\tanimWrapper.innerHTML = \"\";\n"
		"\t\t\tvar low = animFile.toLowerCase();\n"
		"\t\t\tif (low.endsWith(\".webm\") || low.endsWith(\".mp4\")) {\n"
		"\t\t\t\tvar vid = document.createElement(\"video\");\n"
		"\t\t\t\tvid.src = animFile; vid.autoplay = true; vid.loop = true; vid.muted = true;\n"
		"\t\t\t\tanimWrapper.appendChild(vid);\n"
		"\t\t\t} else {\n"
		"\t\t\t\tvar img = document.createElement(\"img\");\n"
		"\t\t\t\timg.src = animFile;\n"
		"\t\t\t\tanimWrapper.appendChild(img);\n"
		"\t\t\t}\n"
		"\t\t}\n"
		"\t} else {\n"
		"\t\tanimWrapper.style.display = \"none\";\n"
		"\t\tif (currentAnimFile !== \"\") { currentAnimFile = \"\"; animWrapper.innerHTML = \"\"; }\n"
		"\t}\n"
		"}\n"
		"function syncRows(count) {\n"
		"	var linesEl = document.getElementById(\"lines\");\n"
		"	if (!linesEl) return;\n"
		"	var rows = linesEl.querySelectorAll(\".lp-row\");\n"
		"	while (rows.length < count) {\n"
		"		var el = document.createElement(\"div\");\n"
		"		el.id = \"l\" + (rows.length + 1);\n"
		"		el.className = \"lp-row\";\n"
		"		linesEl.appendChild(el);\n"
		"		rows = linesEl.querySelectorAll(\".lp-row\");\n"
		"	}\n"
		"	while (rows.length > count) {\n"
		"		linesEl.removeChild(rows[rows.length - 1]);\n"
		"		rows = linesEl.querySelectorAll(\".lp-row\");\n"
		"	}\n"
		"	for (var k = 0; k < rows.length; k++)\n"
		"		rows[k].className = \"lp-row\" + (k + 1 === rows.length ? \" lp-last\" : \"\");\n"
		"}\n"
		"function applyCard(d) {\n"
		"	if (!d) return;\n"
		"	if (d.zeilen != null) {\n"
		"		var count = Math.min(d.zeilen.length, 8);\n"
		"		syncRows(count);\n"
		"		for (var i = 0; i < count; i++) setLine(\"l\" + (i + 1), d.zeilen[i]);\n"
		"	} else {\n"
		"		setLine(\"l1\", d.zeile1); setLine(\"l2\", d.zeile2); setLine(\"l3\", d.zeile3);\n"
		"	}\n"
		"	if (d.style != null) {\n"
		"		for (var j = 0; j < d.style.length; j++) {\n"
		"			var r = d.style[j];\n"
		"			if (r && r.name && r.value != null) setVar(r.name, String(r.value));\n"
		"		}\n"
		"	}\n"
		"\tvar box = document.getElementById(\"box\");\n"
		"\tvar lines = document.getElementById(\"lines\");\n"
		"\tif (box) {\n"
		"\t\tif (d.box_bg != null) box.style.background = d.box_bg;\n"
		"\t\tif (d.box_opacity != null) {\n"
		"\t\t\tvar vis = Number(d.box_opacity) > 0;\n"
		"\t\t\tbox.style.opacity = vis ? \"1\" : \"0\";\n"
		"\t\t\tbox.style.pointerEvents = vis ? \"auto\" : \"none\";\n"
		"\t\t\tif (lines) { lines.style.opacity = vis ? \"1\" : \"0\"; lines.style.pointerEvents = vis ? \"auto\" : \"none\"; }\n"
		"\t\t}\n"
		"\t}\n"
		"\tif (d.text_color != null && lines) lines.style.color = d.text_color;\n"
		"\tif (d.triangle_color_a != null) setVar(\"--triangle-color-a\", d.triangle_color_a);\n"
		"\tif (d.triangle_color_b != null) setVar(\"--triangle-color-b\", d.triangle_color_b);\n"
		"\tif (d.rainbow != null) applyRainbow(d.rainbow);\n"
		"\tif (d.anim_file != null) setVar(\"--anim-file\", '\"' + String(d.anim_file) + '\"');\n"
		"\tif (d.show_anim != null) setVar(\"--show-anim\", String(d.show_anim));\n"
		"\tif (d.anim_scale != null) setVar(\"--anim-scale\", String(d.anim_scale));\n"
		"\tif (d.anim_x != null) setVar(\"--anim-x\", String(d.anim_x) + \"px\");\n"
		"\tif (d.anim_y != null) setVar(\"--anim-y\", String(d.anim_y) + \"px\");\n"
		"\tupdateAnim();\n"
		"}\n"
		"updateAnim();\n"
		"window.addEventListener(\"liedplan_card_update\", ev => applyCard(ev.detail));\n"
		"</script>\n"
		"</body>\n"
		"</html>\n";

	return html;
}

bool write_card(const LiedplanCard &card, std::string &path_out, const LiedplanSettings &settings)
{
	if (card.id.empty())
		return false;

	const std::string dir = cards_dir();
	if (dir.empty())
		return false;

	std::string html;
	if (!card.template_id.empty()) {
		html = render_template_html(card, card.template_id, false, settings, dir);
		if (html.empty()) {
#ifndef LIEDPLAN_TEST_NO_OBS
			obs_log(LOG_WARNING,
				"Liedplan: Template '%s' nicht gefunden/leer — Karte nutzt classic-Fallback",
				card.template_id.c_str());
#endif
		}
	}

	if (html.empty()) {
		const std::string textbox_path = resolve_textbox_path(settings);
		const std::string repo_html = read_utf8_file(textbox_path.c_str());
		if (repo_html.empty()) {
#ifndef LIEDPLAN_TEST_NO_OBS
			obs_log(LOG_INFO, "Liedplan: index.html (classic) nicht gefunden, nutze Fallback-Template");
#endif
		}
		html = render_card_html(card, repo_html, settings, dir);
	}

	const std::string full_path = dir + "/" + card.id + ".html";

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

std::string last_push_update_json()
{
	return g_last_push_update_json;
}

void push_update(obs_source_t *src, const LiedplanCard &card, bool clear,
		 const LiedplanSettings &settings, const std::string &copy_dir)
{
	if (!src)
		return;

	const std::string box_bg =
		format_rgba(card.box_color.empty() ? "#ffffff" : card.box_color, 0xFFFFFFu, card.box_opacity);
	const std::string text_col =
		format_rgba(card.text_color.empty() ? "#000000" : card.text_color, 0x000000u, card.text_opacity);
	const std::string tri_col_a =
		format_rgba(card.corner_color_a.empty() ? "#fce647" : card.corner_color_a, 0xFCE647u, card.corner_opacity_a);
	const std::string tri_col_b =
		format_rgba(card.corner_color_b.empty() ? "#fce647" : card.corner_color_b, 0xFCE647u, card.corner_opacity_b);
	const int box_opacity = clear ? 0 : (has_any_text(card) ? 1 : 0);
	const int rainbow = card.rainbow_corners ? 1 : 0;
	const std::string norm_anim = normalize_anim_src(card.anim_file, settings, copy_dir);
	const int show_anim = card.show_anim ? 1 : 0;

	QJsonObject obj;
	obj.insert(QStringLiteral("zeile1"),
		   clear ? QStringLiteral("") : QString::fromUtf8(card.zeile1.c_str()));
	obj.insert(QStringLiteral("zeile2"),
		   clear ? QStringLiteral("") : QString::fromUtf8(card.zeile2.c_str()));
	obj.insert(QStringLiteral("zeile3"),
		   clear ? QStringLiteral("") : QString::fromUtf8(card.zeile3.c_str()));
	const auto rows = effective_rows(card);
	QJsonArray zeilen;
	for (const auto &r : rows)
		zeilen.append(clear ? QStringLiteral("") : QString::fromUtf8(r.value.c_str()));
	obj.insert(QStringLiteral("zeilen"), zeilen);
	QJsonArray vn;
	for (std::size_t i = 0; i < rows.size(); ++i)
		vn.append(QString::fromUtf8(row_var_name(rows[i].label, i).c_str()));
	obj.insert(QStringLiteral("vn"), vn);
	QJsonArray style_arr;
	for (const auto &rule : card.style) {
		const std::string name = sanitize_css_var_name(rule.name);
		const std::string value = sanitize_css_value(rule.value);
		if (name.empty() || value.empty())
			continue;
		QJsonObject r;
		r.insert(QStringLiteral("name"), QString::fromUtf8(name.c_str()));
		r.insert(QStringLiteral("value"), QString::fromUtf8(value.c_str()));
		style_arr.append(r);
	}
	obj.insert(QStringLiteral("style"), style_arr);
	obj.insert(QStringLiteral("box_bg"), QString::fromUtf8(box_bg.c_str()));
	obj.insert(QStringLiteral("text_color"), QString::fromUtf8(text_col.c_str()));
	obj.insert(QStringLiteral("box_opacity"), box_opacity);
	obj.insert(QStringLiteral("triangle_color_a"), QString::fromUtf8(tri_col_a.c_str()));
	obj.insert(QStringLiteral("triangle_color_b"), QString::fromUtf8(tri_col_b.c_str()));
	obj.insert(QStringLiteral("rainbow"), rainbow);
	obj.insert(QStringLiteral("anim_file"), QString::fromUtf8(norm_anim.c_str()));
	obj.insert(QStringLiteral("show_anim"), show_anim);
	obj.insert(QStringLiteral("anim_scale"), card.anim_scale);
	obj.insert(QStringLiteral("anim_x"), card.anim_x);
	obj.insert(QStringLiteral("anim_y"), card.anim_y);
	obj.insert(QStringLiteral("template_id"), QString::fromUtf8(card.template_id.c_str()));

	const QByteArray json_bytes = QJsonDocument(obj).toJson(QJsonDocument::Compact);
	g_last_push_update_json.assign(json_bytes.constData(),
				       static_cast<std::size_t>(json_bytes.size()));

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

std::string sanitize_css_var_name(const std::string &raw)
{
	std::string core;
	for (char ch : raw) {
		if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
		    (ch >= '0' && ch <= '9') || ch == '-')
			core.push_back(ch);
	}
	while (!core.empty() && core[0] == '-')
		core.erase(core.begin());
	if (core.empty())
		return "";
	return "--" + core;
}

// CSS-Value aus frei eingegebenem Text: dropt Struktur-/Injection-Zeichen (; { } < CR LF).
std::string sanitize_css_value(const std::string &raw)
{
	std::string out;
	for (char ch : raw) {
		if (ch == ';' || ch == '{' || ch == '}' || ch == '<' ||
		    ch == '\n' || ch == '\r')
			continue;
		out.push_back(ch);
	}
	return out;
}
} // namespace liedplan_html
