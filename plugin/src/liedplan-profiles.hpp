/*
 * liedplan-profiles.hpp — Definition und Verwaltung von Farbprofilen (v3).
 *
 * Zweck:
 *   - Definiert die LiedplanProfile-Struktur (name, text_color, box_color).
 *   - Hält vordefinierte Farbpaletten bereit (OBS Dark, OBS Light, Midnight Blue, etc.).
 *   - Ermöglicht das Auffinden von Profilen nach Namen (Groß-/Kleinschreibung ignorierend).
 *
 * Hinweis: Header-only, libobs-frei; wird in Standalone-Tests verwendet.
 */

#pragma once

#include <cctype>
#include <string>
#include <vector>

/**
 * Farbprofil mit Name, Textfarbe und Box-Hintergrundfarbe.
 */
struct LiedplanProfile {
	std::string name;
	std::string text_color;
	std::string box_color;

	bool operator==(const LiedplanProfile &other) const
	{
		return name == other.name && text_color == other.text_color &&
		       box_color == other.box_color;
	}
};

inline const std::vector<LiedplanProfile> &liedplan_profiles()
{
	static const std::vector<LiedplanProfile> profiles = {
		{"OBS Dark", "#eff0f1", "#3C404D"},
		{"OBS Light", "#1a1a1a", "#d5d5d5"},
		{"Midnight Blue", "#c9d1d9", "#252d3a"},
		{"Forest Green", "#d4e0d0", "#344530"},
		{"Sunset Warm", "#f0ddd0", "#3d2a20"},
		{"High Contrast", "#ffffff", "#222222"},
	};
	return profiles;
}

inline const LiedplanProfile *profile_by_name(const std::string &name)
{
	for (const auto &p : liedplan_profiles()) {
		if (p.name == name)
			return &p;
	}
	return nullptr;
}

inline const LiedplanProfile *
liedplan_profile_lookup(const std::string &name,
			const std::vector<LiedplanProfile> &custom)
{
	if (const LiedplanProfile *std_prof = profile_by_name(name))
		return std_prof;
	for (const auto &p : custom) {
		if (p.name == name)
			return &p;
	}
	return nullptr;
}

inline bool liedplan_is_default_profile(const std::string &name)
{
	for (const auto &p : liedplan_profiles()) {
		if (p.name.size() != name.size())
			continue;
		bool match = true;
		for (std::size_t i = 0; i < name.size(); ++i) {
			if (std::tolower(static_cast<unsigned char>(p.name[i])) !=
			    std::tolower(static_cast<unsigned char>(name[i]))) {
				match = false;
				break;
			}
		}
		if (match)
			return true;
	}
	return false;
}
