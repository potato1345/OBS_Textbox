/*
Textbox — Standalone OBS-Plugin (T7)
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

/*
 * test-textbox-render.cpp — Rendering- und HTML-Integrationstests (v3).
 *
 * Diese Suite prüft:
 *   - Qt6-Headless-Rendering (offscreen) mit render_card() und Pixel-Exaktheit
 *     (box_color, box_opacity, text_color, text_opacity, Rundung).
 *   - HTML-Generatoren card_html(), build_inject_css(), render_card_html()
 *     inkl. Template-Integration, Regenbogen-CSS, Live-Update-Listener.
 *   - escape-Tests, write_card()-Output, normalize_anim_src() mit Data-URIs,
 *     MIME-Typen, percent-Encoding und Video-Kopie.
 *   - build_inject_css() mit echten Anim-State-Werten und Defaults.
 *
 * Abhängigkeit: Qt6 wird via FetchContent aus dem Build-System bezogen
 * (CMake find_package(Qt6) + fetch_add_content). Der Render-Test läuft
 * nur, wenn Qt6 aufgelöst werden kann.
 *
 * Layout-Bounds-Checks (Pixel-Positionen wie (600,40)) sind aktiv, weil
 * die OBS-Textbox auf eine feste Leinwand von 800×400 rendert — jede
 * Änderung am CSS-Layout würde die Pixel-Koordinaten verschieben.
 */

#ifndef _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#endif

#include "../../src/textbox-html.hpp"
#include "../../src/textbox-render.hpp"

#include <QColor>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QLibraryInfo>
#include <QString>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

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

// Hilfsfunktion: Zählt exakte Pixel einer bestimmten Farbe (ARGB) im gesamten Bild.
// Wird für Text-Sichtbarkeitstests verwendet: prüft ob Text tatsächlich gerendert wurde.
int count_exact_pixels(const QImage &img, QRgb target)
{
	int count = 0;
	for (int y = 0; y < img.height(); ++y) {
		const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
		for (int x = 0; x < img.width(); ++x) {
			if (line[x] == target)
				++count;
		}
	}
	return count;
}

} // namespace

int main(int argc, char *argv[])
{
	// Qt6-Offscreen-Rendering: QT_QPA_PLATFORM=offscreen erzwingt headlesses Rendern
	// ohne X11/Wayland/Windows-GUI. Falls qoffscreen.dll fehlt (z. B. minimales
	// obs-deps-qt6-Paket), fällt Render auf Windows-QPA zurück, der QImage dennoch
	// headless erzeugen kann.
	// Layout-Bounds-Checks sind aktiv, weil render_card() eine feste 800x400-Leinwand
	// verwendet — feste Pixel-Koordinaten wie (600,40) prüfen die CSS-Korrektheit.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef _WIN32
	// Falls das minimale obs-deps-qt6-Paket kein qoffscreen.dll enthält,
	// auf das vorhandene Windows-QPA-Plugin zurückfallen (rendert QImage ebenfalls headless).
	const QString offscreen_dll =
		QLibraryInfo::path(QLibraryInfo::PluginsPath) + QStringLiteral("/platforms/qoffscreen.dll");
	if (!QFileInfo::exists(offscreen_dll)) {
		qputenv("QT_QPA_PLATFORM", "windows");
	}
#endif

	QGuiApplication app(argc, argv);

	// 1. Volle Karte (box #ffffff/255): Pixel (600,40) == box_color @ box_opacity
	{
		TextboxCard c;
		c.id = "box1";
		c.zeile1 = "467";
		c.zeile2 = "1+3";
		c.zeile3 = "Abschluss";
		c.box_color = "#ffffff";
		c.box_opacity = 255;
		c.text_color = "#000000";
		c.text_opacity = 255;

		const QImage img = render_card(c, 0.0);
		check(img.width() == 800 && img.height() == 400 && img.format() == QImage::Format_ARGB32,
		      "1a. Volle Karte: Format 800x400 ARGB32");
		check(img.pixel(600, 40) == qRgba(255, 255, 255, 255),
		      "1. Volle Karte (box #ffffff/255): Pixel (600,40) == box_color @ box_opacity");

		// 2. Rundung: Pixel (2,2) alpha == 0 (außerhalb des 24-px-Radii)
		check(qAlpha(img.pixel(2, 2)) == 0,
		      "2. Rundung: Pixel (2,2) alpha == 0 (außerhalb des 24-px-Radii)");
	}

	// 3. box_opacity=0: Pixel (600,40) alpha == 0
	{
		TextboxCard c;
		c.zeile1 = "467";
		c.zeile2 = "1+3";
		c.zeile3 = "Abschluss";
		c.box_color = "#ffffff";
		c.box_opacity = 0;
		c.text_color = "#000000";
		c.text_opacity = 255;

		const QImage img = render_card(c, 0.0);
		check(qAlpha(img.pixel(600, 40)) == 0,
		      "3. box_opacity=0: Pixel (600,40) alpha == 0");
	}

	// 4. Leere Karte (alle zeilen ""): Box bleibt sichtbar (Pixel (600,40) == box_color)
	{
		TextboxCard c;
		c.zeile1 = "";
		c.zeile2 = "";
		c.zeile3 = "";
		c.box_color = "#ffffff";
		c.box_opacity = 255;

		const QImage img = render_card(c, 0.0);
		check(img.pixel(600, 40) == qRgba(255, 255, 255, 255),
		      "4. Leere Karte (alle zeilen \"\"): Box bleibt sichtbar (Pixel (600,40) == box_color)");
	}

	// 5. Text vorhanden: zeile3 "Abschluss" -> es existiert >=1 Pixel mit Farbe == text_color
	{
		TextboxCard c;
		c.zeile3 = "Abschluss";
		c.text_color = "#000000";
		c.text_opacity = 255;
		c.box_color = "#ffffff";
		c.box_opacity = 255;

		const QImage img = render_card(c, 0.0);
		const int matches = count_exact_pixels(img, qRgba(0, 0, 0, 255));
		check(matches >= 1,
		      "5. Text vorhanden: zeile3 \"Abschluss\" -> >=1 Pixel mit Farbe == text_color",
		      "matches=" + std::to_string(matches));
	}

	// 6. text_opacity=0: es existiert KEIN Pixel == text_color (Text unsichtbar)
	{
		TextboxCard c;
		c.zeile3 = "Abschluss";
		c.text_color = "#000000";
		c.text_opacity = 0;
		c.box_color = "#ffffff";
		c.box_opacity = 255;

		const QImage img = render_card(c, 0.0);
		int text_matches = 0;
		for (int y = 0; y < img.height(); ++y) {
			const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
			for (int x = 0; x < img.width(); ++x) {
				const QRgb px = line[x];
				if (qAlpha(px) > 0 && qRed(px) == 0 && qGreen(px) == 0 && qBlue(px) == 0)
					++text_matches;
			}
		}
		check(text_matches == 0,
		      "6. text_opacity=0: es existiert KEIN Pixel == text_color (Text unsichtbar)",
		      "text_matches=" + std::to_string(text_matches));
	}

	// 7. text_color rot (#ff0000) -> >=1 roter Text-Pixel
	{
		TextboxCard c;
		c.zeile3 = "Abschluss";
		c.text_color = "#ff0000";
		c.text_opacity = 255;
		c.box_color = "#ffffff";
		c.box_opacity = 255;

		const QImage img = render_card(c, 0.0);
		const int red_matches = count_exact_pixels(img, qRgba(255, 0, 0, 255));
		check(red_matches >= 1,
		      "7. text_color rot (#ff0000) -> >=1 roter Text-Pixel",
		      "red_matches=" + std::to_string(red_matches));
	}

	// 8. HTML-Generator (M2a-Browser): box13 / "GL 290" / "1-4" / "Großer Glaube", #000000 / #ffffff, Opacity 255
	{
		TextboxCard c;
		c.id = "box13";
		c.zeile1 = "GL 290";
		c.zeile2 = "1-4";
		c.zeile3 = "Großer Glaube";
		c.text_color = "#000000";
		c.box_color = "#ffffff";
		c.text_opacity = 255;
		c.box_opacity = 255;

		const std::string html = textbox_html::card_html(c);
		check(html.find("GL 290") != std::string::npos &&
			      html.find("1-4") != std::string::npos &&
			      html.find("Großer Glaube") != std::string::npos,
		      "8a. card_html(): enthält alle drei Texte (GL 290 / 1-4 / Großer Glaube)");
		check(html.find("rgba(0,0,0,1)") != std::string::npos &&
			      html.find("rgba(255,255,255,1)") != std::string::npos,
		      "8b. card_html(): enthält rgba(0,0,0,1) und rgba(255,255,255,1)");
		check(html.find("border-radius:24px") != std::string::npos,
		      "8c. card_html(): enthält border-radius:24px");
		check(html.find("window.addEventListener(\"textbox_card_update\", ev => applyCard(ev.detail));") !=
			      std::string::npos,
		      "8d. card_html(): enthält Event-Listener-Zeile für textbox_card_update");

		// NEU: build_inject_css(card) mit Testkarte (#3C404D / #eff0f1)
		TextboxCard c_css;
		c_css.id = "box13";
		c_css.zeile1 = "GL 290";
		c_css.zeile2 = "1-4";
		c_css.zeile3 = "Großer Glaube";
		c_css.box_color = "#3C404D";
		c_css.box_opacity = 255;
		c_css.text_color = "#eff0f1";
		c_css.text_opacity = 255;

		const std::string css = textbox_html::build_inject_css(c_css);
		check(css.find("--zeile-1: \"GL 290\"") != std::string::npos &&
			      css.find("--box-bg: rgba(60,64,77,1)") != std::string::npos &&
			      css.find("--text-color: rgba(239,240,241,1)") != std::string::npos &&
			      css.find("--box-opacity: 1") != std::string::npos &&
			      css.find("--triangle-color-a: rgba(252,230,71,1)") != std::string::npos &&
			      css.find("--show-anim: 0") != std::string::npos &&
			      css.find("OBS_TEXTBOX_INJECT_START") != std::string::npos &&
			      css.find("OBS_TEXTBOX_INJECT_END") != std::string::npos,
		      "8e. build_inject_css(card): enthält alle erwarteten CSS-Variablen und Marker");

		// NEU: alle Zeilen leer -> --box-opacity: 0
		TextboxCard c_empty = c_css;
		c_empty.zeile1.clear();
		c_empty.zeile2.clear();
		c_empty.zeile3.clear();
		const std::string css_empty = textbox_html::build_inject_css(c_empty);
		check(css_empty.find("--box-opacity: 0") != std::string::npos,
		      "8f. build_inject_css(leere Zeilen): --box-opacity: 0");

		// NEU: render_card_html(card, "") -> Fallback
		const std::string fallback_html = textbox_html::render_card_html(c_css, "");
		check(fallback_html.find("textbox_card_update") != std::string::npos &&
			      fallback_html.find("GL 290") != std::string::npos &&
			      fallback_html.find("1-4") != std::string::npos &&
			      fallback_html.find("Großer Glaube") != std::string::npos,
		      "8g. render_card_html(card, \"\"): Fallback enthält textbox_card_update und alle drei Texte");

		// NEU: render_card_html(card, echterRepoInhalt)
		const char *repo_path = "C:/Users/Johann/Desktop/GitHub/OBS_Textbox/plugin/data/index.html";
		std::error_code ec;
		const std::filesystem::path rp = std::filesystem::u8path(repo_path);
		if (std::filesystem::exists(rp, ec) && !ec) {
			std::ifstream rf(rp, std::ios::binary);
			std::ostringstream rss;
			rss << rf.rdbuf();
			const std::string repo_content = rss.str();

			const std::string rendered = textbox_html::render_card_html(c_css, repo_content);
			const std::size_t pos_inject = rendered.find("OBS_TEXTBOX_INJECT_START");
			const std::size_t pos_head = rendered.rfind("</head>");
			const std::size_t pos_listener = rendered.find("textbox_card_update");
			const std::size_t pos_body = rendered.rfind("</body>");
			const std::size_t pos_anim = rendered.find(".anim-wrapper");

			check(pos_inject != std::string::npos && pos_head != std::string::npos &&
				      pos_inject < pos_head && pos_listener != std::string::npos &&
				      pos_body != std::string::npos && pos_listener < pos_body &&
				      pos_anim != std::string::npos,
			      "8h. render_card_html(card, echterRepoInhalt): Injektion vor </head>, Listener vor </body>, .anim-wrapper erhalten");

			// 8i. render_card_html mit REPO-HTML und rainbow=false: --tb-rainbow: 0, kein --triangle-background: none.
			// Die Prüfung ist auf den INJIZIERTEN Block (ab der Injektion bis Dokumentende)
			// beschränkt: das Repo-HTML enthält statisch einen Kommentar mit genau diesem
			// String (IMP-U-Doku), der darf nicht als Plugin-Injektion zählen.
			const std::size_t pos_rb0 = rendered.find("--tb-rainbow: 0;");
			check(pos_rb0 != std::string::npos && pos_rb0 < pos_head &&
			      rendered.find("--triangle-background: none", pos_rb0) == std::string::npos &&
			      rendered.find("--tb-rainbow: 1", pos_rb0) == std::string::npos,
			      "8i. render_card_html(rainbow=false): --tb-rainbow: 0 vor </head>, keine Triangle-/Rainbow-Injektion danach");

			// 8j. render_card_html mit REPO-HTML und rainbow=true: --tb-rainbow: 1 + --triangle-background: none injiziert,
			//     Listener setzt die Variablen zur Laufzeit via setVar (IMP-U: HTML-Magie via Pseudo-Elemente)
			TextboxCard c_rb = c_css;
			c_rb.rainbow_corners = true;
			const std::string rendered_rb = textbox_html::render_card_html(c_rb, repo_content);
			const std::size_t pos_head_rb = rendered_rb.rfind("</head>");
			const std::size_t pos_body_rb = rendered_rb.rfind("</body>");
			const std::size_t pos_inject_rb = rendered_rb.find("OBS_TEXTBOX_INJECT_START");
			const std::size_t pos_listener_rb = rendered_rb.find("textbox_card_update");
			const std::size_t pos_anim_rb = rendered_rb.find(".anim-wrapper");
			const std::string head_part_rb =
				(pos_head_rb != std::string::npos) ? rendered_rb.substr(0, pos_head_rb) : "";

			check(pos_head_rb != std::string::npos &&
			      head_part_rb.find("--tb-rainbow: 1;") != std::string::npos &&
			      head_part_rb.find("--triangle-background: none;") != std::string::npos &&
			      rendered_rb.find("setVar(\"--tb-rainbow\"") != std::string::npos &&
			      pos_inject_rb < pos_head_rb && pos_listener_rb < pos_body_rb &&
			      pos_anim_rb != std::string::npos,
			      "8j. render_card_html(rainbow=true): --tb-rainbow: 1 + --triangle-background: none vor </head>, Listener setzt --tb-rainbow via setVar");

			// 8m. Template-Token {{rainbow}}: substituiert wird im Token-Pfad (substitute_tokens),
			//     nicht im Classic-Injektionspfad (render_card_html). standard.html erwartet
			//     class="rainbow"/"0" (CSS body.rainbow .corner).
			const std::string tpl_rb = "<html><head></head><body class=\"{{rainbow}}\"><div class=\"corner\"></div></body></html>";
			const std::string tpl_on = textbox_html::substitute_tokens(c_rb, tpl_rb);
			const std::string tpl_off = textbox_html::substitute_tokens(c_css, tpl_rb);
			check(tpl_on.find("class=\"rainbow\"") != std::string::npos &&
			      tpl_off.find("class=\"0\"") != std::string::npos &&
			      tpl_on.find("class=\"1\"") == std::string::npos,
			      "8m. {{rainbow}}-Token: rainbow=true -> class=\"rainbow\", false -> class=\"0\" (body.rainbow-CSS greift)");
		} else {
			std::cout << "[SKIP] 8h. plugin/data/index.html nicht vorhanden, überspringe Repo-Inhalt-Test\n";
		}

		// 8k. build_inject_css: custom Corner-Colors/Opacity + leere Color-Felder -> Default #fce647
		TextboxCard c_corners;
		c_corners.zeile1 = "100";
		c_corners.corner_color_a = "#ff0000";
		c_corners.corner_opacity_a = 128;
		c_corners.corner_color_b = "#00ff00";
		c_corners.corner_opacity_b = 51;
		const std::string css_corners = textbox_html::build_inject_css(c_corners);
		const std::string expected_a = "--triangle-color-a: " + textbox_html::hex_to_rgba("#ff0000", 128) + ";";
		const std::string expected_b = "--triangle-color-b: " + textbox_html::hex_to_rgba("#00ff00", 51) + ";";

		TextboxCard c_corners_empty;
		c_corners_empty.zeile1 = "100";
		c_corners_empty.corner_color_a = "";
		c_corners_empty.corner_opacity_a = 255;
		c_corners_empty.corner_color_b = "";
		c_corners_empty.corner_opacity_b = 255;
		const std::string css_corners_empty = textbox_html::build_inject_css(c_corners_empty);

		check(css_corners.find("rgba(255,0,0,0.501961)") != std::string::npos &&
			      css_corners.find("rgba(0,255,0,0.2)") != std::string::npos &&
			      css_corners.find(expected_a) != std::string::npos &&
			      css_corners.find(expected_b) != std::string::npos &&
			      css_corners_empty.find("--triangle-color-a: rgba(252,230,71,1);") != std::string::npos &&
			      css_corners_empty.find("--triangle-color-b: rgba(252,230,71,1);") != std::string::npos,
		      "8k. build_inject_css: custom Corner-Colors/Opacity -> exakte rgba-Strings; leere Color-Felder -> Default #fce647");

		// 8l. card_html (Fallback): rainbow=true -> #box::before/::after + tb-rainbow-rotate + --tb-rainbow: 1; rainbow=false -> --tb-rainbow: 0; applyCard mit box_bg statt box_color
		TextboxCard c_fb_rb = c;
		c_fb_rb.rainbow_corners = true;
		const std::string fb_rb_html = textbox_html::card_html(c_fb_rb);
		const std::size_t fb_head_pos = fb_rb_html.rfind("</head>");
		const std::string fb_head_part =
			(fb_head_pos != std::string::npos) ? fb_rb_html.substr(0, fb_head_pos) : "";
		check(html.find("--tb-rainbow: 0;") != std::string::npos &&
		      fb_head_part.find("--tb-rainbow: 1;") != std::string::npos &&
		      fb_head_part.find("tb-rainbow-rotate") != std::string::npos &&
		      fb_head_part.find("#box::before") != std::string::npos &&
		      fb_head_part.find("#box::after") != std::string::npos &&
		      fb_rb_html.find("setVar(\"--tb-rainbow\"") != std::string::npos &&
		      fb_rb_html.find("d.box_bg != null") != std::string::npos &&
		      fb_rb_html.find("d.box_color") == std::string::npos,
		      "8l. card_html (Fallback): rainbow=true (#box::before/::after + tb-rainbow-rotate + --tb-rainbow: 1), rainbow=false (--tb-rainbow: 0), box_bg-Schema ohne box_color");
	}

	// 9. HTML/CSS-Escape-Tests + write_card()
	{
		TextboxCard c;
		c.id = "box14";
		c.zeile1 = "&<x>";
		c.zeile2 = "\"quote\" & 'apos'";
		c.zeile3 = "";
		c.text_color = "#000000";
		c.box_color = "#ffffff";
		c.text_opacity = 255;
		c.box_opacity = 0;

		const std::string html = textbox_html::card_html(c);
		check(html.find("&amp;&lt;x&gt;") != std::string::npos &&
			      html.find("&<x>") == std::string::npos &&
			      html.find("<x>") == std::string::npos,
		      "9a. card_html() Escape-Test: \"&<x>\" wird zu \"&amp;&lt;x&gt;\" (kein rohes <)");
		check(html.find("&quot;quote&quot; &amp; &#39;apos&#39;") != std::string::npos,
		      "9b. card_html() Escape-Test: '\"' und '\\'' werden escaped");
		check(html.find("id=\"box\" style=\"background:rgba(255,255,255,0);display:none;\"") !=
			      std::string::npos,
		      "9c. card_html(): box_opacity=0 setzt display:none auf #box");
		check(html.find("id=\"l3\" style=\"display:none;\"") != std::string::npos,
		      "9d. card_html(): leere Zeile l3 hat display:none");

		// NEU: Escape in build_inject_css: Zeile `An & <Bö>"se` -> kein rohes `"` innerhalb des Strings, kein rohes `<`
		TextboxCard c_esc;
		c_esc.id = "box13";
		c_esc.zeile1 = "An & <Bö>\"se";
		c_esc.zeile2 = "1-4";
		c_esc.zeile3 = "Großer Glaube";
		c_esc.box_color = "#3C404D";
		c_esc.box_opacity = 255;
		c_esc.text_color = "#eff0f1";
		c_esc.text_opacity = 255;

		const std::string css_esc = textbox_html::build_inject_css(c_esc);
		const std::size_t z1_start = css_esc.find("--zeile-1: \"");
		const std::size_t z1_end =
			(z1_start != std::string::npos) ? css_esc.find("\";\n", z1_start + 12) : std::string::npos;
		const std::string z1_inner = (z1_start != std::string::npos && z1_end != std::string::npos)
						     ? css_esc.substr(z1_start + 12, z1_end - (z1_start + 12))
						     : "";
		bool has_unescaped_quote = false;
		for (std::size_t i = 0; i < z1_inner.size(); ++i) {
			if (z1_inner[i] == '"' && (i == 0 || z1_inner[i - 1] != '\\')) {
				has_unescaped_quote = true;
				break;
			}
		}
		check(!z1_inner.empty() && !has_unescaped_quote && css_esc.find('<') == std::string::npos &&
			      css_esc.find('>') == std::string::npos && css_esc.find('&') == std::string::npos,
		      "9f. build_inject_css Escape-Test: \"An & <Bö>\\\"se\" enthält kein rohes '\"' oder '<'");

		// 9e: write_card() schreibt die Datei; Inhalt enthält Injektionsblock ODER Fallback-Template + Listener
		std::string written_path;
		const bool wrote = textbox_html::write_card(c_esc, written_path);
		std::string written_content;
		if (wrote) {
			std::ifstream wf(std::filesystem::u8path(written_path), std::ios::binary);
			std::ostringstream wss;
			wss << wf.rdbuf();
			written_content = wss.str();
		}
		const bool has_inject_or_fallback =
			(written_content.find("OBS_TEXTBOX_INJECT_START") != std::string::npos ||
			 written_content.find("applyCard") != std::string::npos) &&
			written_content.find("textbox_card_update") != std::string::npos;
		check(wrote && written_path.find("box13.html") != std::string::npos && has_inject_or_fallback,
		      "9e. write_card(): schreibt box13.html mit Injektionsblock/Fallback + Listener", written_path);
	}

	// 10. (M5 #9) normalize_anim_src
	{
		TextboxSettings st;
		const std::filesystem::path fix_dir =
			std::filesystem::temp_directory_path() / "textbox_anim_fix";
		std::error_code ec;
		std::filesystem::remove_all(fix_dir, ec);
		std::filesystem::create_directories(fix_dir, ec);
		{
			std::ofstream f(fix_dir / "1_advent.gif", std::ios::binary | std::ios::trunc);
			f << "GIF89a";
		}
		st.animation_dir = fix_dir.u8string();

		const std::string res_a =
			textbox_html::normalize_anim_src("C:\\Users\\J\\animation\\1_advent.gif", st);
		const std::string res_b =
			textbox_html::normalize_anim_src("https://example.com/a.webm", st);
		const std::string res_c =
			textbox_html::normalize_anim_src("1_advent.gif", st);
		const std::string res_d =
			textbox_html::normalize_anim_src("does_not_exist_999.gif", st);
		const std::string res_e =
			textbox_html::normalize_anim_src("C:\\a b\\x.gif", st);

		const bool ok =
			(res_a == "file:///C:/Users/J/animation/1_advent.gif") &&
			(res_b == "https://example.com/a.webm") &&
			// M5.1: existierendes Bild → base64-Data-URI (CEF-File-Access-Fix),
			// base64("GIF89a") = "R0lGODlh"
			(res_c == "data:image/gif;base64,R0lGODlh") &&
			(res_d == "does_not_exist_999.gif") &&
			(res_e == "file:///C:/a%20b/x.gif");

		std::filesystem::remove_all(fix_dir, ec);
		check(ok, "10. (M5 #9) normalize_anim_src: Win-Pfad, URL, existierendes Bild->Data-URI, fehlender Bare-Name, Space->%20",
		      res_a + " | " + res_c + " | " + res_e);
	}

	// 12. (M5.1 CEF-File-Access) Data-URIs für Bilder, Video-Kopie neben Card-HTML,
	//     percent-Encoding (M5-1), Fallbacks
	{
		// Mini-Base64-Decoder für den Roundtrip-Check
		auto b64_decode = [](const std::string &in) {
			auto val = [](char c) -> int {
				if (c >= 'A' && c <= 'Z')
					return c - 'A';
				if (c >= 'a' && c <= 'z')
					return c - 'a' + 26;
				if (c >= '0' && c <= '9')
					return c - '0' + 52;
				if (c == '+')
					return 62;
				if (c == '/')
					return 63;
				return -1;
			};
			std::string out;
			int buf = 0, bits = 0;
			for (char c : in) {
				if (c == '=')
					break;
				const int v = val(c);
				if (v < 0)
					continue;
				buf = (buf << 6) | v;
				bits += 6;
				if (bits >= 8) {
					bits -= 8;
					out += static_cast<char>((buf >> bits) & 0xFF);
				}
			}
			return out;
		};
		// 1x1-transparenter GIF (43 Bytes)
		const std::string gif_bytes =
			"\x47\x49\x46\x38\x39\x61" // GIF89a
			"\x01\x00\x01\x00"            // 1x1
			"\x80"                         // GCT 2 Farben
			"\x00\x00"
			"\x00\x00\x00\xFF\xFF\xFF"
			"\x21\xF9\x04\x01\x00\x00\x00\x00"
			"\x2C\x00\x00\x00\x00\x01\x00\x01\x00\x00"
			"\x02"
			"\x02\x4C\x01\x00"
			"\x3B";

		TextboxSettings st;
		const std::filesystem::path m51 =
			std::filesystem::temp_directory_path() / "textbox_m51_fix";
		std::error_code ec;
		std::filesystem::remove_all(m51, ec);
		std::filesystem::create_directories(m51 / "src", ec);
		std::filesystem::create_directories(m51 / "cards", ec);
		auto wr = [](const std::filesystem::path &p, const std::string &b) {
			std::ofstream f(p, std::ios::binary | std::ios::trunc);
			f.write(b.data(), static_cast<std::streamsize>(b.size()));
		};
		const std::filesystem::path gif_path = m51 / "src" / "1_advent.gif";
		wr(gif_path, gif_bytes);

		// 12a: existierendes GIF (absoluter Pfad) → Data-URI + Byte-Identität
		const std::string uri_gif =
			textbox_html::normalize_anim_src(gif_path.u8string(), st);
		const bool ok_12a =
			uri_gif.rfind("data:image/gif;base64,", 0) == 0 &&
			b64_decode(uri_gif.substr(22)) == gif_bytes;

		// 12b: MIME pro Endung
		wr(m51 / "src" / "a.png", "PNGDATA");
		wr(m51 / "src" / "a.jpg", "JPGDATA");
		wr(m51 / "src" / "a.webp", "WEBPDATA");
		const std::string uri_png =
			textbox_html::normalize_anim_src((m51 / "src" / "a.png").u8string(), st);
		const std::string uri_jpg =
			textbox_html::normalize_anim_src((m51 / "src" / "a.jpg").u8string(), st);
		const std::string uri_webp =
			textbox_html::normalize_anim_src((m51 / "src" / "a.webp").u8string(), st);
		const bool ok_12b =
			uri_png.rfind("data:image/png;base64,", 0) == 0 &&
			uri_jpg.rfind("data:image/jpeg;base64,", 0) == 0 &&
			uri_webp.rfind("data:image/webp;base64,", 0) == 0;

		// 12c: fehlendes Bild → file-URL-Fallback (kein data:, kein Crash)
		const std::string uri_missing = textbox_html::normalize_anim_src(
			(m51 / "src" / "nicht_da.gif").u8string(), st);
		const bool ok_12c =
			uri_missing.rfind("file:///", 0) == 0 && uri_missing.find("data:") == std::string::npos;

		// 12d: Video außerhalb von copy_dir → Kopie NEBEN die Card-HTML + file-URL
		const std::filesystem::path vid_path = m51 / "src" / "anim_xmas.webm";
		wr(vid_path, "FAKEWEBM-CONTENT");
		const std::string cards = (m51 / "cards").u8string();
		const std::string uri_vid =
			textbox_html::normalize_anim_src(vid_path.u8string(), st, cards);
		const std::filesystem::path vid_copy = m51 / "cards" / "anim_xmas.webm";
		const bool ok_12d =
			uri_vid.rfind("file:///", 0) == 0 &&
			uri_vid.find("cards/anim_xmas.webm") != std::string::npos &&
			std::filesystem::exists(vid_copy, ec) &&
			[&]() {
				std::string a, b;
				std::ifstream fa(vid_path, std::ios::binary);
				std::ifstream fb(vid_copy, std::ios::binary);
				std::ostringstream oa, ob;
				oa << fa.rdbuf();
				ob << fb.rdbuf();
				return oa.str() == ob.str();
			}();

		// 12e: Video BEREITS in copy_dir → direkte file-URL, keine Doppelkopie
		const std::string uri_vid2 = textbox_html::normalize_anim_src(vid_copy.u8string(), st, cards);
		const bool ok_12e =
			uri_vid2.rfind("file:///", 0) == 0 &&
			uri_vid2.find("cards/anim_xmas.webm") != std::string::npos;

		// 12f: Non-ASCII-Dateiname ohne copy_dir (M5-1): Umlaut wird prozent-encodiert.
		// NUR über u8path() mit expliziten UTF-8-Bytes (narrow-Konkatenation würde
		// unter MSVC die Locale/CP1252 nehmen — genau das, was wir hier testen).
		const std::string umlaut_name = "Gr\xc3\xbc\xc3\x9f" "e.webm"; // "Grüße.webm"
		const std::filesystem::path umlaut =
			m51 / "src" / std::filesystem::u8path(umlaut_name.c_str());
		wr(umlaut, "V");
		const std::string uri_um = textbox_html::normalize_anim_src(umlaut.u8string(), st);
		const bool ok_12f =
			uri_um.rfind("file:///", 0) == 0 &&
			uri_um.find("%C3%BC") != std::string::npos &&
			uri_um.find("\xC3") == std::string::npos;

		// 12g: Pass-through und leer
		const std::string uri_pt1 =
			textbox_html::normalize_anim_src("http://x.test/a.gif", st);
		const std::string uri_pt2 =
			textbox_html::normalize_anim_src("file:///C:/x/a.gif", st);
		const std::string uri_empty = textbox_html::normalize_anim_src("", st);
		const bool ok_12g =
			uri_pt1 == "http://x.test/a.gif" &&
			uri_pt2 == "file:///C:/x/a.gif" &&
			uri_empty.empty();

		std::filesystem::remove_all(m51, ec);
		check(ok_12a, "12a. (M5.1) existierendes GIF → data:image/gif;base64 + Byte-Identität", uri_gif.substr(0, 40));
		check(ok_12b, "12b. (M5.1) MIME pro Endung (png/jpg/webp)");
		check(ok_12c, "12c. (M5.1) fehlendes Bild → file-URL-Fallback", uri_missing);
		check(ok_12d, "12d. (M5.1) Video → Kopie nach cards/ + file-URL, Inhalte identisch", uri_vid);
		check(ok_12e, "12e. (M5.1) Video in copy_dir → direkte file-URL", uri_vid2);
		check(ok_12f, "12f. (M5-1) Non-ASCII-Name → %C3%BC-Encoded, keine rohen High-Bytes", uri_um);
		check(ok_12g, "12g. (M5.1) http/file/empty Pass-through");
	}

	// 11. (M5 #13 & #14) build_inject_css mit echten Anim-Werten, Escape und Defaults
	{
		TextboxSettings st;
		TextboxCard c_anim;
		c_anim.id = "box1";
		c_anim.zeile1 = "Beginn";
		c_anim.anim_file = "C:\\x\\a.gif";
		c_anim.show_anim = true;
		c_anim.anim_scale = 4;
		c_anim.anim_x = -1200;
		c_anim.anim_y = 50;

		const std::string css_anim = textbox_html::build_inject_css(c_anim, st);
		const bool ok_13 =
			css_anim.find("--anim-file: \"file:///C:/x/a.gif\";") != std::string::npos &&
			css_anim.find("--show-anim: 1;") != std::string::npos &&
			css_anim.find("--anim-scale: 4;") != std::string::npos &&
			css_anim.find("--anim-x: -1200px;") != std::string::npos &&
			css_anim.find("--anim-y: 50px;") != std::string::npos;

		TextboxCard c_anim_q = c_anim;
		c_anim_q.anim_file = "https://example.com/a\"b.gif";
		const std::string css_anim_q = textbox_html::build_inject_css(c_anim_q, st);
		const bool ok_13_esc =
			css_anim_q.find("--anim-file: \"https://example.com/a\\\"b.gif\";") !=
			std::string::npos;

		TextboxCard c_def;
		c_def.id = "box2";
		c_def.zeile1 = "Review";
		const std::string css_def = textbox_html::build_inject_css(c_def, st);
		const bool ok_14 =
			css_def.find("--anim-file: \"\";") != std::string::npos &&
			css_def.find("--show-anim: 0;") != std::string::npos &&
			css_def.find("--anim-scale: 3;") != std::string::npos &&
			css_def.find("--anim-x: 0px;") != std::string::npos &&
			css_def.find("--anim-y: 0px;") != std::string::npos;

		check(ok_13 && ok_13_esc,
		      "11a. (M5 #13) build_inject_css mit Anim-State: --anim-file/show/scale/x/y exakt + '\"' escaped");
		check(ok_14,
		      "11b. (M5 #14) build_inject_css ohne Anim (Defaults): --anim-file: \"\"; --show-anim: 0;");
	}

	if (g_failures > 0) {
		std::cerr << g_failures << " test(s) failed.\n";
		return 1;
	}
	std::cout << "All render & HTML checks passed.\n";
	return 0;
}
