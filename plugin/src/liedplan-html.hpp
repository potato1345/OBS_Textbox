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
#include <vector>

namespace liedplan_html {

// <obs-data>/plugins/liedplan/cards/ – wird beim ersten Aufruf angelegt (create_directories),
// Basis wie im Dock: obs_module_config_path("liedplan-state.json") ohne Dateinamen.
// Rückgabe: UTF-8-Pfad des Verzeichnisses (ohne abschließenden Separator).
std::string cards_dir();

// Verzeichnis der user-Templates: <plugin_config>/liedplan-for-obs/templates/
// Basis wie cards_dir() (obs_module_config_path("liedplan-state.json").parent_path()),
// im Test-Modus (LIEDPLAN_TEST_NO_OBS) temp_directory_path()/liedplan_templates_test.
// create_directories wie cards_dir(); UTF-8-Pfad ohne abschließenden Separator; "" bei Fehler.
std::string templates_dir();

// Substituiert {{Token}} in einem Template-String (bytes, UTF-8-safe; kein UTF-8-Parsing).
// Kartenwerte werden wie in build_inject_css/push_update formatiert:
//   {{zeile1}} {{zeile2}} {{zeile3}}   -> Rohtext der Zeile (clear=false) bzw. "" (clear=true)
//   {{text_color}}   -> rgba(...) aus text_color+text_opacity (empty -> #000000-Fallback)
//   {{box_color}}    -> rgba(...) aus box_color+box_opacity   (empty -> #ffffff-Fallback)
//   {{corner_a}} {{corner_b}} -> rgba(...) aus corner_color_a/b + corner_opacity_a/b (empty -> #fce647)
//   {{text_opacity}} {{box_opacity}} -> "1" wenn >0 (transparent 0), sonst String des 0..255-Werts —
//     HALTE EXAKT die Formatierung, die build_inject_css heute für --box-opacity nutzt:
//     --box-opacity ist 1 wenn die Karte Text hat, sonst 0. text_opacity/box_opacity-Tokens:
//     Zahlenwert 0..255 (geklemmt) als String.
//   {{rainbow}}    -> "1"/"0"
//   {{anim_src}}   -> normalize_anim_src(card.anim_file, settings, copy_dir)
//   {{show_anim}}  -> "1"/"0"
//   {{anim_scale}} {{anim_x}} {{anim_y}} -> Zahlen als String
//   {{card_id}}    -> card.id
//   {{clear}}      -> "1" wenn clear=true sonst "0"
// Unbekannte Tokens ({{foo}}) bleiben unersetzt im Output (No-op).
// Ein Token, das im Template gar nicht vorkommt, ist irrelevant.
// Hex-Validierung: Farb-Felder laufen über parse_hex_color mit denselben Fallbacks wie
// heute (invalid/leer -> Default-Farbe), Output immer gültiges rgba(...).
std::string substitute_tokens(const LiedplanCard &card, const std::string &template_html,
			      bool clear = false,
			      const LiedplanSettings &settings = LiedplanSettings(),
			      const std::string &copy_dir = "");

// Verzeichnis der mitinstallierten Beispiel-Vorlagen (Plugin-Data-Verzeichnis,
// Basis obs_module_file("templates/standard.html")). Im Test-Modus: repo
// plugin/templates via LIEDPLAN_REPO_ROOT; sonst "".
std::string builtin_templates_dir();

// Liest <template_id>.html aus dem Suchpfad — zuerst templates_dir()
// (user-editierbar, überschreibt builtin), dann builtin_templates_dir() —
// und substituiert.
// Rückgabe "" wenn: id empty, Datei fehlt/leer/unlesbar. Kein Log hier (Caller loggt).
std::string render_template_html(const LiedplanCard &card, const std::string &template_id,
				 bool clear = false,
				 const LiedplanSettings &settings = LiedplanSettings(),
				 const std::string &copy_dir = "");

// Vorlagen-IDs beider Suchverzeichnisse (user-Dir + builtin), Dedup, sortiert.
// "classic" ist hier nicht enthalten (leere template_id = klassischer Weg).
std::vector<std::string> list_template_ids();

// Löst den Pfad der classic-Vorlage (textbox.html) auf: settings.textbox_path →
// obs_module_file("textbox.html") → Repo-Fallback. Für "Vorlage aus classic
// kopieren" (Spec F2b).
std::string resolve_textbox_path(const LiedplanSettings &settings = LiedplanSettings());

// Browser-Source-Reload über proc_handler "reload" (spec F2b: Template-Wechsel
// -> Reload). false bei nullptr / ohne Reload-Prozedur / im Test-Modus.
bool reload_browser_source(obs_source_t *source);

// Erzeugt das self-contained HTML der Karte (Template unten) und schreibt es nach
// cards_dir() + "/" + card.id + ".html" (UTF-8, via std::filesystem::u8path).
// true bei Erfolg. path_out = voller Pfad der Datei (UTF-8).
bool write_card(const LiedplanCard &card, std::string &path_out,
		const LiedplanSettings &settings = LiedplanSettings());

// Reines String-Template (für Testbarkeit), ohne Dateizugriff.
std::string card_html(const LiedplanCard &card);

// Formatiert "#RRGGBB" + Opacity (0..255) als "rgba(r,g,b,a)".
std::string hex_to_rgba(const std::string &hex, int opacity_0_255);

// Normalisiert frei eingegebenen CSS-Var-Namen: behält [A-Za-z0-9-], prefixt '--'; "" wenn leer.
std::string sanitize_css_var_name(const std::string &raw);

// Sanisiert einen CSS-Value: entfernt Struktur-/Injection-Zeichen ; { } < CR LF.
std::string sanitize_css_value(const std::string &raw);

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
