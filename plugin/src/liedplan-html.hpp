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
bool write_card(const LiedplanCard &card, std::string &path_out);

// Reines String-Template (für Testbarkeit), ohne Dateizugriff.
std::string card_html(const LiedplanCard &card);

// Formatiert "#RRGGBB" + Opacity (0..255) als "rgba(r,g,b,a)".
std::string hex_to_rgba(const std::string &hex, int opacity_0_255);

// Erzeugt den CSS-Injektionsblock (:root mit --zeile-1/2/3, --box-bg, --text-color usw.)
// im exakten Production-Format (nicht-Rainbow-Variante aus control.js).
std::string build_inject_css(const LiedplanCard &card);

// Rendert die komplette HTML-Datei der Karte (reines String, testbar):
// 1. repo_html leer → card_html(card) (Fallback-Template).
// 2. sonst (Inhalt von main/textbox.html):
//    a) vor dem LETZTEN "</head>": <style>\n + build_inject_css(card) + "\n</style>\n
//    b) vor dem LETZTEN "</body>": Listener-Script für liedplan_card_update.
// 3. Fehlt ein Marker (</head> oder </body>) im repo_html → Fallback card_html(card).
std::string render_card_html(const LiedplanCard &card, const std::string &repo_html);

// Schickt das Kartenevent an eine BESTEHENDE Browser-Source (ohne Reload):
// proc-Handler "javascript_event" mit eventName="liedplan_card_update",
// jsonString = JSON {zeile1,zeile2,zeile3,box_bg,text_color,box_opacity,anim_file,show_anim,anim_scale,anim_x,anim_y}
// (clear=true → alle Zeilen "" und box_opacity = 0). JSON über Qt (QJsonObject/QJsonDocument::toJson(Compact)).
// src==nullptr → no-op.
void push_update(obs_source_t *src, const LiedplanCard &card, bool clear);

} // namespace liedplan_html
