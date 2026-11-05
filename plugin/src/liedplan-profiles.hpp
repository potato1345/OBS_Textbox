/*
 * liedplan-profiles.hpp — Definition und Verwaltung von Farbprofilen (v3).
 *
 * Zweck:
 *   - Definiert die LiedplanProfile-Struktur (Farben, Ecken, Deckkräfte, Regenbogen).
 *   - Hält vordefinierte Karten-Farbpaletten bereit (Karte Dunkel, Karte Hell, etc.).
 *   - Unterstützt Legacy-Aliase (z. B. "OBS Dark" -> "Karte Dunkel") und wendet Profile auf Karten an.
 *
 * Hinweis: Header-only, libobs-frei; wird in Standalone-Tests verwendet.
 */

#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

/**
 * Eine frei benennbare Karten-Zeile (Label nur UI-Metadaten; Value wird gerendert).
 * Max: kMaxCardRows Zeilen pro Karte.
 */
struct LiedplanRow {
	std::string label;
	std::string value;

	bool operator==(const LiedplanRow &other) const
	{
		return label == other.label && value == other.value;
	}
};

/**
 * Generelle CSS-Regel: name (Variable-Name, ohne/ mit '--' Prefix) -> value.
 */
struct LiedplanStyleRule {
	std::string name;
	std::string value;

	bool operator==(const LiedplanStyleRule &other) const
	{
		return name == other.name && value == other.value;
	}
};

inline constexpr size_t kMaxCardRows = 8;

// Standard-Labels der klassischen drei Zeilen.
inline const char *kLiedplanRowLabels[3] = {"GL-Nummer", "Verse", "Liedtitel"};

/**
 * Karten-Farbprofil mit Text-/Box-Farben, Eckfarben, Deckkräften und Regenbogen-Option.
 */
struct LiedplanProfile {
	std::string name;
	std::string text_color;
	std::string box_color;
	std::string corner_a = "#fce647";
	std::string corner_b = "#fce647";
	int corner_op_a = 255;
	int corner_op_b = 255;
	int text_opacity = 255;
	int box_opacity = 255;
	bool rainbow = false;
	std::vector<LiedplanStyleRule> style; // generelle CSS-Regeln des Profils

	bool operator==(const LiedplanProfile &other) const
	{
		return name == other.name && text_color == other.text_color &&
		       box_color == other.box_color && corner_a == other.corner_a &&
		       corner_b == other.corner_b && corner_op_a == other.corner_op_a &&
		       corner_op_b == other.corner_op_b &&
		       text_opacity == other.text_opacity &&
		       box_opacity == other.box_opacity && rainbow == other.rainbow &&
		       style == other.style;
	}

	bool operator!=(const LiedplanProfile &other) const
	{
		return !(*this == other);
	}
};

inline const std::vector<LiedplanProfile> &liedplan_profiles()
{
	static const std::vector<LiedplanProfile> profiles = {
		{"Karte Dunkel", "#eff0f1", "#3C404D"},
		{"Karte Hell", "#1a1a1a", "#d5d5d5"},
		{"Karte Mitternacht", "#c9d1d9", "#252d3a"},
		{"Karte Wald", "#d4e0d0", "#344530"},
		{"Karte Sonne", "#f0ddd0", "#3d2a20"},
		{"Karte Kontrast", "#ffffff", "#222222"},
	};
	return profiles;
}

inline bool liedplan_iequals(const std::string &a, const std::string &b)
{
	if (a.size() != b.size())
		return false;
	for (std::size_t i = 0; i < a.size(); ++i) {
		if (std::tolower(static_cast<unsigned char>(a[i])) !=
		    std::tolower(static_cast<unsigned char>(b[i])))
			return false;
	}
	return true;
}

/**
 * Mappt alte Profilnamen (v3 vor F1, z. B. "OBS Dark") case-insensitiv auf die
 * neuen kanonischen Kartennamen ("Karte Dunkel"). Unbekannte/eigene Namen bleiben unverändert.
 */
inline std::string profile_name_canonical(const std::string &name)
{
	struct LegacyAlias {
		const char *old_name;
		const char *new_name;
	};
	static constexpr LegacyAlias kAliases[] = {
		{"OBS Dark", "Karte Dunkel"},
		{"OBS Light", "Karte Hell"},
		{"Midnight Blue", "Karte Mitternacht"},
		{"Forest Green", "Karte Wald"},
		{"Sunset Warm", "Karte Sonne"},
		{"High Contrast", "Karte Kontrast"},
	};
	for (const auto &a : kAliases) {
		if (liedplan_iequals(name, a.old_name))
			return a.new_name;
	}
	return name;
}

inline const LiedplanProfile *profile_by_name(const std::string &name)
{
	const std::string canonical = profile_name_canonical(name);
	for (const auto &p : liedplan_profiles()) {
		if (p.name == canonical)
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
	const std::string canonical = profile_name_canonical(name);
	for (const auto &p : custom) {
		if (p.name == name || p.name == canonical)
			return &p;
	}
	return nullptr;
}

inline bool liedplan_is_default_profile(const std::string &name)
{
	const std::string canonical = profile_name_canonical(name);
	for (const auto &p : liedplan_profiles()) {
		if (liedplan_iequals(p.name, canonical))
			return true;
	}
	return false;
}

/**
 * Überträgt sämtliche Farb-, Ecken-, Deckkraft- und Regenbogen-Felder eines Profils auf eine Karte.
 * Profil-CSS-Regeln werden ergänzt; Karten-Regeln mit gleichem Namen gewinnen.
 */
template <typename CardT>
inline void profile_apply(const LiedplanProfile &prof, CardT &card)
{
	card.text_color = prof.text_color;
	card.box_color = prof.box_color;
	card.text_opacity = prof.text_opacity;
	card.box_opacity = prof.box_opacity;
	card.corner_color_a = prof.corner_a;
	card.corner_color_b = prof.corner_b;
	card.corner_opacity_a = prof.corner_op_a;
	card.corner_opacity_b = prof.corner_op_b;
	card.rainbow_corners = prof.rainbow;
	for (const auto &rule : prof.style) {
		bool owned = false;
		for (const auto &cr : card.style) {
			if (cr.name == rule.name) {
				owned = true;
				break;
			}
		}
		if (!owned)
			card.style.push_back(rule);
	}
}
