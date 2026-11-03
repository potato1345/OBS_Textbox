/*
 * liedplan-textfmt.hpp — Farb- und Textformatierungs-Helfer für Liedplan (v3).
 *
 * Zweck:
 *   - Wandelt Hex-Farbcodes (#RRGGBB) in 32-Bit-Farbwerte (0xRRGGBB) um.
 *   - Prüft, ob eine Karte sichtbare Textinhalte (Zeile 1 bis 3) enthält.
 *
 * Hinweis: Header-only, libobs-frei; wird in Standalone-Tests (test-textfmt) verwendet.
 */

#pragma once

#include "liedplan-state.hpp"

#include <cstdint>
#include <string>

/**
 * Wandelt einen "#RRGGBB"-String in einen 32-Bit-RGB-Wert um.
 *
 * @param hex - Hexadezimaler Farbstring.
 * @param fallback - Ersatzwert bei ungültigem Format.
 * @returns 24-Bit-RGB-Ganzzahlwert.
 */
inline uint32_t parse_hex_color(const std::string &hex, uint32_t fallback)
{
	if (hex.size() != 7 || hex[0] != '#')
		return fallback;

	uint32_t value = 0;
	for (size_t i = 1; i < 7; ++i) {
		const unsigned char ch = static_cast<unsigned char>(hex[i]);
		uint32_t nibble = 0;
		if (ch >= '0' && ch <= '9') {
			nibble = static_cast<uint32_t>(ch - '0');
		} else if (ch >= 'a' && ch <= 'f') {
			nibble = static_cast<uint32_t>(ch - 'a' + 10);
		} else if (ch >= 'A' && ch <= 'F') {
			nibble = static_cast<uint32_t>(ch - 'A' + 10);
		} else {
			return fallback;
		}
		value = (value << 4) | nibble;
	}

	return value & 0xFFFFFFu;
}

// Rechnet Alpha (0–255) gerundet in Prozent (0–100) um.
inline int opacity_pct(int alpha_0_255)
{
	int clamped = alpha_0_255;
	if (clamped < 0)
		clamped = 0;
	else if (clamped > 255)
		clamped = 255;

	return (clamped * 100 + 127) / 255;
}

// Baut den Anzeigetext aus zeile1..zeile3 zusammen (leere Zeilen werden übersprungen).
inline std::string card_text(const LiedplanCard &card)
{
	std::string out;
	const std::string *lines[3] = {&card.zeile1, &card.zeile2, &card.zeile3};
	for (const std::string *line : lines) {
		if (line->empty())
			continue;
		if (!out.empty())
			out += '\n';
		out += *line;
	}
	return out;
}
