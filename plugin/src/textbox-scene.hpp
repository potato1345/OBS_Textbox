/*
 * textbox-scene.hpp — Verwaltung von OBS-Szenenquellen für Textbox-Karten (v3).
 *
 * Zweck:
 *   - Erzeugt und aktualisiert Videoquellen (textbox_card bzw. browser_source) in OBS-Szenen.
 *   - Synchronisiert Kartendaten (Texte, Farben, Deckkraft) in aktive und inaktive Szenen.
 *   - Entfernt gelöschte Karten sauber aus allen Szenen.
 */

#pragma once

#include "textbox-state.hpp"

#include <obs.h>
#include <string>

namespace textbox_scene {

/** Quellenname in OBS: "Textbox " + card_id (z. B. "Textbox box1"). */
std::string source_name(const std::string &card_id);

/** Erzeugt obs_data_t mit den Einstellungen für eine Browser-Quelle. */
obs_data_t *make_settings(const TextboxCard &card, const std::string &local_file);

/** Legt die Text-Quelle in der aktuellen Szene an oder aktualisiert sie dort. */
void send_to_current_scene(const TextboxCard &card,
			   const TextboxSettings &settings = TextboxSettings());

/** Aktualisiert alle bereits platzierten Kopien dieser Karte in allen Szenen. */
void update_everywhere(const TextboxCard &card, bool clear,
                       const TextboxSettings &settings = TextboxSettings());

/** Pushed Karten-Updates per javascript_event in alle Browser-Quellen aller
 * Szenen (Live-Text/Farbe ohne Reload). */
void push_all_scenes(const TextboxCard &card,
                     const TextboxSettings &settings);

/** Schreibt die Karten-HTML neu und reloadt alle Browser-Quellen dieser Karte
 * in allen Szenen (Template-Wechsel, Spec F2b — Document-Neuerstellung). */
void reload_everywhere(const TextboxCard &card,
		       const TextboxSettings &settings = TextboxSettings());

/** Entfernt alle Szenen-Items dieser Karte aus allen Szenen und gibt die Source frei. */
void remove_everywhere(const std::string &card_id);

/** True, wenn mindestens eine Browser-Quelle "Textbox <card_id>" in irgendeiner Szene existiert. */
bool source_exists(const std::string &card_id);

} // namespace textbox_scene
