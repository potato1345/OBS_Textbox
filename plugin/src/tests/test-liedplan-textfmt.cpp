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

#include "../liedplan-textfmt.hpp"

#include <iostream>
#include <string>

namespace {

int g_failures = 0;

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

} // namespace

int main()
{
	// parse_hex_color-Tests
	check(parse_hex_color("#eff0f1", 0x112233u) == 0xEFF0F1u,
	      "parse_hex_color: \"#eff0f1\" -> 0xEFF0F1");
	check(parse_hex_color("#000000", 0x112233u) == 0u,
	      "parse_hex_color: \"#000000\" -> 0");
	check(parse_hex_color("#ABCDEF", 0x112233u) == 0xABCDEFu,
	      "parse_hex_color: \"#ABCDEF\" (Groß) -> 0xABCDEF");
	check(parse_hex_color("fff", 0x112233u) == 0x112233u,
	      "parse_hex_color: \"fff\" (ohne #) -> fallback");
	check(parse_hex_color("#12345", 0x112233u) == 0x112233u,
	      "parse_hex_color: \"#12345\" -> fallback");
	check(parse_hex_color("#1234567", 0x112233u) == 0x112233u,
	      "parse_hex_color: \"#1234567\" -> fallback");
	check(parse_hex_color("", 0x112233u) == 0x112233u,
	      "parse_hex_color: \"\" -> fallback");
	check(parse_hex_color("#zzzzzz", 0x112233u) == 0x112233u,
	      "parse_hex_color: \"#zzzzzz\" -> fallback");

	// opacity_pct-Tests
	check(opacity_pct(0) == 0, "opacity_pct: 0 -> 0");
	check(opacity_pct(255) == 100, "opacity_pct: 255 -> 100");
	check(opacity_pct(254) == 100, "opacity_pct: 254 -> 100 (99,6 % rundet auf 100)");
	check(opacity_pct(128) == 50, "opacity_pct: 128 -> 50 (gerundet)");
	check(opacity_pct(100) == 39, "opacity_pct: 100 -> 39");
	check(opacity_pct(-5) == 0, "opacity_pct: -5 -> 0");
	check(opacity_pct(300) == 100, "opacity_pct: 300 -> 100");

	// card_text-Tests
	{
		LiedplanCard c;
		c.zeile1 = "467";
		c.zeile2 = "1+3";
		c.zeile3 = "Freu dich sehr";
		check(card_text(c) == "467\n1+3\nFreu dich sehr",
		      "card_text: alle 3 gefüllt -> \"467\\n1+3\\nFreu dich sehr\"");
	}
	{
		LiedplanCard c;
		c.zeile3 = "Freu dich sehr";
		check(card_text(c) == "Freu dich sehr",
		      "card_text: nur zeile3 -> \"Freu dich sehr\"");
	}
	{
		LiedplanCard c;
		c.zeile1 = "467";
		c.zeile3 = "Freu dich sehr";
		check(card_text(c) == "467\nFreu dich sehr",
		      "card_text: zeile1+3 -> \"467\\nFreu dich sehr\"");
	}
	{
		LiedplanCard c;
		check(card_text(c).empty(), "card_text: alle leer -> \"\"");
	}
	{
		LiedplanCard c;
		c.zeile3 = "Übermorgen — 1";
		check(card_text(c) == "Übermorgen — 1",
		      "card_text: Umlaut/Em-Dash bleiben erhalten (\"Übermorgen — 1\")");
	}

	if (g_failures > 0) {
		std::cerr << g_failures << " test(s) failed.\n";
		return 1;
	}
	std::cout << "All 20 checks passed.\n";
	return 0;
}
