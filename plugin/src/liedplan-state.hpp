/*
 * liedplan-state.hpp — Datenmodelle und Zustandshaltung des Liedplan-Plugins (v3).
 *
 * Zweck:
 *   - Definiert LiedplanCard (Zustand einer Textbox/Karte inkl. Texten, Farben, Animation).
 *   - Definiert LiedplanSettings (Google Forms ID, Token-Pfad, OpenRouter-Key, Dateipfade).
 *   - Definiert LiedplanQuickProfile (gespeichertes Set von Karten).
 *   - Implementiert die LiedplanState-Klasse zur Serialisierung/Deserialisierung in JSON.
 *   - Konvertierung von Farbprofilen in YAML und zurück.
 *
 * Hinweis: Qt- und libobs-frei; wird auch in Standalone-Tests (test-liedplan-state) verwendet.
 */

#pragma once

#include "liedplan-profiles.hpp"

#include <cstddef>
#include <string>
#include <vector>

/**
 * Zustand einer einzelnen Liedplan-Karte bzw. Textbox.
 */
struct LiedplanCard {
	std::string id; // z. B. "box1"
	std::string zeile1; // GL-Nummer
	std::string zeile2; // Verse
	std::string zeile3; // Liedtitel
	std::string profile = "Karte Dunkel";
	std::string text_color = "#000000";
	int text_opacity = 255;
	std::string box_color = "#ffffff";
	int box_opacity = 255;
	std::string corner_color_a = "#fce647";
	std::string corner_color_b = "#fce647";
	int corner_opacity_a = 255; // 0..255
	int corner_opacity_b = 255; // 0..255
	bool rainbow_corners = false;
	std::string anim_file = ""; // Dateiname, absoluter Pfad oder URL (roher Wert, wie eingegeben)
	bool show_anim = false;
	int anim_scale = 3; // 1,2,3,4,6,8,10,12 (v2-Optionen); Default 3
	int anim_x = 0; // -1200..1200 px
	int anim_y = 0; // -1200..1200 px
	std::string template_id = ""; // "" = classic (CSS-Injektion in index.html)
	std::vector<LiedplanRow> rows; // dynamische Zeilen (max kMaxCardRows); leer = Legacy zeile1..3
	std::vector<LiedplanStyleRule> style; // generelle CSS-Regeln (--name: value)

	bool operator==(const LiedplanCard &other) const
	{
		return id == other.id && zeile1 == other.zeile1 && zeile2 == other.zeile2 &&
		       zeile3 == other.zeile3 && profile == other.profile &&
		       text_color == other.text_color && text_opacity == other.text_opacity &&
		       box_color == other.box_color && box_opacity == other.box_opacity &&
		       corner_color_a == other.corner_color_a &&
		       corner_color_b == other.corner_color_b &&
		       corner_opacity_a == other.corner_opacity_a &&
		       corner_opacity_b == other.corner_opacity_b &&
		       rainbow_corners == other.rainbow_corners &&
		       anim_file == other.anim_file && show_anim == other.show_anim &&
		       anim_scale == other.anim_scale && anim_x == other.anim_x &&
		       anim_y == other.anim_y && template_id == other.template_id &&
		       rows == other.rows && style == other.style;
	}
};
/**
 * Karten-Zeilen: rows wenn gesetzt, sonst Legacy-Fallback aus zeile1..3 mit
 * Standard-Labels (immer genau 3 Zeilen). Immer nicht-empty für Render/UI.
 */
std::vector<LiedplanRow> effective_rows(const LiedplanCard &card);


// Ermittelt den Standardpfad für google_token.json über %LOCALAPPDATA%/hermes/google_token.json
// mit Fallback auf den bestehenden Entwickler-Pfad.
std::string default_google_token_path();

// Ermittelt den Standardpfad für lieder.md (obs_module_file-Auflösung falls vorhanden, sonst Dev-Fallback).
std::string default_lieder_path();
// IMP-T: runtime-Auflösung statt hartkodierter Benutzerpfad (Animationsordner
// unter dem Plugin-Konfigverzeichnis).
std::string default_animation_dir();
void liedplan_set_module_lieder_path(const std::string &path);

struct LiedplanSettings {
	std::string google_form_id = "1jreAuzd-jt1x48WDmjaP7v0OusdVO2lb55Kt4JrhEgY";
	std::string token_path = default_google_token_path();
	// IMP-T: keine benutzerspezifischen Absolutpfade als Defaults — auf anderen
	// Rechnern waren sie defekt und leakten die Benutzerstruktur. Persistierte
	// Werte aus liedplan-state.json bleiben unverändert übernommen.
	std::string openrouter_key_path = "";
	std::string markitdown_python = "";
	std::string animation_dir = default_animation_dir();
	std::string lieder_path = default_lieder_path();
	std::string textbox_path = ""; // leer = obs_module_file("index.html") -> Dev-Fallback kRepoTextboxPath
	std::string ai_system_prompt = ""; // leer = LiedplanAi::kDefaultSystemPrompt verwenden

	bool operator==(const LiedplanSettings &other) const
	{
		return google_form_id == other.google_form_id && token_path == other.token_path &&
		       openrouter_key_path == other.openrouter_key_path &&
		       markitdown_python == other.markitdown_python &&
		       animation_dir == other.animation_dir && lieder_path == other.lieder_path &&
		       textbox_path == other.textbox_path &&
		       ai_system_prompt == other.ai_system_prompt;
	}
};

struct LiedplanQuickProfile {
	std::string name;
	std::vector<LiedplanCard> cards;

	bool operator==(const LiedplanQuickProfile &other) const
	{
		return name == other.name && cards == other.cards;
	}
};

class LiedplanState {
public:
	std::vector<LiedplanCard> cards;
	int next_id = 1; // Id-Zähler; Ids "box1","box2",... werden NIEMALS wiederverwendet
	std::string active_profile = "Karte Dunkel";
	LiedplanSettings settings;
	std::vector<LiedplanQuickProfile> quick_profiles;
	std::vector<LiedplanProfile> color_profiles;
	// false = Standardprofile noch nicht in color_profiles materialisiert
	// (Seed erfolgt einmalig im Dock; danach bleiben Löschungen/Umbenennungen erhalten).
	bool profiles_seeded = false;
	// Einstufiger Undo-Snapshot für destruktive Karten-Aktionen im Dock
	std::vector<LiedplanCard> undo_snapshot_;
	bool undo_available_ = false;

	void reset();
	std::string load(const std::string &path); // "" = ok; sonst Fehlerbeschreibung
	std::string save(const std::string &path) const; // "" = ok
	std::string restore_backup(const std::string &path); // "" = ok; bei Fehler bleibt State unverändert

	std::string serialize_json() const;
	std::string deserialize_json(const std::string &json_str);

	const LiedplanCard *find(const std::string &id) const;

	LiedplanCard &add_card();
	bool remove_card(const std::string &id);

	void save_quick_profile(const std::string &name);
	bool apply_quick_profile(std::size_t index);
	bool apply_quick_profile(const std::string &name);
	bool remove_quick_profile(std::size_t index);

	bool operator==(const LiedplanState &other) const
	{
		return cards == other.cards && next_id == other.next_id &&
		       active_profile == other.active_profile && settings == other.settings &&
	       profiles_seeded == other.profiles_seeded &&
		       quick_profiles == other.quick_profiles &&
		       color_profiles == other.color_profiles;
	}
};

std::string liedplan_profile_to_yaml(const LiedplanProfile &p);
bool liedplan_yaml_to_profile(const std::string &text, LiedplanProfile &out, std::string &err);
