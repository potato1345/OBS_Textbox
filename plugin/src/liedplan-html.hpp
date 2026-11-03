/*
 * liedplan-html.hpp — HTML- und CSS-Generierung für OBS-Browserquellen (v3).
 *
 * Zweck:
 *   - Erzeugt in sich geschlossene HTML-Dateien für jede Karte in <config>/cards/<card_id>.html.
 *   - Löst Animations-Dateien (GIF/WEBM/MP4) sicher für CEF auf (Base64-Data-URI oder lokale file://-URLs).
 *   - Berechnet CSS-Injektionen und pusht Einstellungen über obs_source_update.
 *
 * Hinweis: Wird auch in Standalone-Tests verwendet.
 */

#pragma once

#include "liedplan-state.hpp"
#include <obs.h>
#include <string>

namespace liedplan_html {

// <obs-data>/plugins/liedplan/cards/ – wird beim ersten Aufruf angelegt (create_directories),
// Basis wie im Dock: obs_module_config_path("liedplan-state.json") ohne Dateinamen.
// Rückgabe: UTF-8-Pfad des Verzeichnisses (ohne abschließenden Separator).
std::string cards_dir();

// Erzeugt das self-contained HTML der Karte (Template unten) und schreibt es nach
// cards_dir() + "/" + card.id + ".html" (UTF-8, via std::filesystem::u8path).
// true bei Erfolg. path_out = voller Pfad der Datei (UTF-8).
bool write_card(const LiedplanCard &card, std::string &path_out,
		const LiedplanSettings &settings = LiedplanSettings());

// Reines String-Template (für Testbarkeit), ohne Dateizugriff.
std::string card_html(const LiedplanCard &card);

// Formatiert "#RRGGBB" + Opacity (0..255) als "rgba(r,g,b,a)".
std::string hex_to_rgba(const std::string &hex, int opacity_0_255);

// Wandelt den rohen anim_file-Wert in einen für CEF ladbaren src-Wert um:
// - "" → ""
// - beginnt mit "http://" / "https://" / "file://" → unverändert
// - lokale Datei (absoluter Win-Pfad oder aufgelöst über settings.animation_dir):
//   * Bilder (gif/png/jpg/jpeg/webp/bmp, ≤16 MB) → base64-DATA-URI
//     ("data:<mime>;base64,...") — CEF 127 lädt file://-Seiten NICHT mit
//     Sub-Ressourcen aus anderen Verzeichnisbäumen ("Not allowed to load
//     local resource", M5.1-Bug); Data-URIs sind immer ladbar.
//     Datei nicht lesbar → Fallback file-URL (best effort).
//   * Videos (webm/mp4/mov) → falls copy_dir gesetzt und die Datei nicht bereits
//     darin: Kopie nach copy_dir/<sanitierter-Name> (selbes Verzeichnis wie die
//     Card-HTML) + file-URL auf die Kopie; sonst file-URL auf den Originalpfad.
//   * sonstige Endungen → file-URL.
// - Datei/Name nicht auflösbar → unverändert (relativ zur Card-HTML, Best-Effort).
// file-URLs: Forward-Slashes, '%' -Encoding für alles außer Unreserved/':'/'/'
// (u. a. Spaces → %20, Umlaute → %C3%BC, M5-1).
std::string normalize_anim_src(const std::string &raw,
			       const LiedplanSettings &settings = LiedplanSettings(),
			       const std::string &copy_dir = "");

// Erzeugt den CSS-Injektionsblock (:root mit --zeile-1/2/3, --box-bg, --triangle-color-a/b,
// --text-color, --box-opacity usw.) aus den State-Feldern der Karte.
// copy_dir wird an normalize_anim_src durchgereicht (Video-Kopien; s. o.).
std::string build_inject_css(const LiedplanCard &card,
			     const LiedplanSettings &settings = LiedplanSettings(),
			     const std::string &copy_dir = "");

// Rendert die komplette HTML-Datei der Karte (reines String, testbar):
// 1. repo_html leer → card_html(card) (Fallback-Template inkl. <style id="lp-rainbow">).
// 2. sonst (Inhalt von main/textbox.html):
//    a) vor dem LETZTEN "</head>": <style>\n + build_inject_css(card, settings) + "\n</style>\n
//       gefolgt vom <style id="lp-rainbow">-Platzhalter (befüllt bei rainbow_corners=true, sonst leer).
//    b) vor dem LETZTEN "</body>": Listener-Script für liedplan_card_update.
// 3. Fehlt ein Marker (</head> oder </body>) im repo_html → Fallback card_html(card).
std::string render_card_html(const LiedplanCard &card, const std::string &repo_html,
			     const LiedplanSettings &settings = LiedplanSettings(),
			     const std::string &copy_dir = "");

// Schickt das Kartenevent an eine BESTEHENDE Browser-Source (ohne Reload):
// proc-Handler "javascript_event" mit eventName="liedplan_card_update",
// jsonString = JSON {zeile1,zeile2,zeile3,box_bg,text_color,box_opacity,
// triangle_color_a,triangle_color_b,rainbow,anim_file,show_anim,anim_scale,anim_x,anim_y}
// (clear=true → alle Zeilen "" und box_opacity = 0). JSON über Qt (QJsonObject/QJsonDocument::toJson(Compact)).
// src==nullptr → no-op. copy_dir s. normalize_anim_src.
void push_update(obs_source_t *src, const LiedplanCard &card, bool clear,
		 const LiedplanSettings &settings = LiedplanSettings(),
		 const std::string &copy_dir = "");

} // namespace liedplan_html
