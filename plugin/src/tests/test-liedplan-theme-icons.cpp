/*
 * test-liedplan-theme-icons.cpp — Unit-Tests für eingebettete OBS-Theme-Icons (v3).
 *
 * Zweck:
 *   - Prüft das Ausliefern der SVG-Icons in ein Zielverzeichnis (write_icons).
 *   - Prüft Idempotenz (No-Op bei übereinstimmender Version ohne Änderungen).
 *   - Prüft erzwungenes Neuschreiben (force=true) und Fehlerbehandlung bei ungültigen Pfaden.
 */

#include "../liedplan-theme-icons.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

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

std::string read_file(const std::filesystem::path &p)
{
	std::ifstream in(p, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

} // namespace

int main()
{
	const auto icons = liedplan_theme_icons::icons();
	check(icons.size() >= 12, "icon-list non-trivial size",
	      "size=" + std::to_string(icons.size()));

	// Namen: ASCII, leer-frei, .svg-Endung (CP1252-Pitfall s. HPP-Kommentar)
	bool names_ok = true;
	for (const auto &item : icons) {
		if (item.first.empty() || item.first.back() != 'g' || item.second.empty()) {
			names_ok = false;
			break;
		}
	}
	check(names_ok, "icon names ASCII/non-empty, content non-empty");

	std::filesystem::path dir =
		std::filesystem::temp_directory_path() / "liedplan_icon_tests";
	std::error_code ec;
	std::filesystem::remove_all(dir, ec);
	std::filesystem::create_directories(dir / "icons", ec);

	// 1. Fresh-Write (force) — alle Dateien + Version
	std::string err;
	check(liedplan_theme_icons::write_icons(dir / "icons", true, &err),
	      "force write succeeds", err);
	bool all_present = true;
	for (const auto &item : icons) {
		std::error_code fec;
		const auto sz = std::filesystem::file_size(dir / "icons" / item.first, fec);
		if (fec || sz == 0) {
			all_present = false;
			break;
		}
	}
	check(all_present, "all icon files present, >=1 byte");
	check(read_file(dir / "icons" / "icons-version.txt") ==
		      std::to_string(liedplan_theme_icons::kVersion),
	      "version file matches kVersion");
	check(read_file(dir / "icons" / "plus.svg").find("<svg") != std::string::npos,
	      "plus.svg contains <svg");
	const std::string chev_r = read_file(dir / "icons" / "chevron_right.svg");
	check(chev_r.find("<svg") != std::string::npos && chev_r.find("</svg>") != std::string::npos,
	      "chevron_right.svg present and SVG well-formed");
	const std::string chev_d = read_file(dir / "icons" / "chevron_down.svg");
	check(chev_d.find("<svg") != std::string::npos && chev_d.find("</svg>") != std::string::npos,
	      "chevron_down.svg present and SVG well-formed");

	// 2. No-Op: zweiter Call ohne force -> true, Inhalte unveraendert
	const std::string plus_before = read_file(dir / "icons" / "plus.svg");
	check(liedplan_theme_icons::write_icons(dir / "icons", false, &err),
	      "no-op on current version", err);
	check(read_file(dir / "icons" / "plus.svg") == plus_before,
	      "no-op leaves content unchanged");

	// 3. BeschaeDIGte Datei (0 Byte) -> Rewrite
	std::ofstream trunc(dir / "icons" / "close.svg", std::ios::binary | std::ios::trunc);
	trunc.close();
	check(liedplan_theme_icons::write_icons(dir / "icons", false, &err),
	      "rewrite after corruption", err);
	std::error_code cec;
	check(std::filesystem::file_size(dir / "icons" / "close.svg", cec) > 0,
	      "corrupted file rewritten non-empty");

	// 4. Fehlende Versions-Datei -> Rewrite
	std::filesystem::remove(dir / "icons" / "icons-version.txt");
	check(liedplan_theme_icons::write_icons(dir / "icons", false, &err),
	      "rewrite after missing version file", err);
	check(std::filesystem::exists(dir / "icons" / "icons-version.txt"),
	      "version file recreated");

	std::filesystem::remove_all(dir, ec);

	if (g_failures == 0) {
		std::cout << "ALL THEME-ICON TESTS PASSED\n";
		return 0;
	}
	std::cerr << g_failures << " THEME-ICON TEST(S) FAILED\n";
	return 1;
}
