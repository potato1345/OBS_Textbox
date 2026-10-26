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

#ifndef _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#endif

#include "../liedplan-import.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

const char *kSamplesDir =
	"C:/Users/Johann/Desktop/GitHub/OBS_Textbox/rewrite/tests/samples/";

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
	using namespace liedplan_import;

	// 1. normalize_header("  1.  Eröffnung   –   GL-Nummer ") == "1. Eröffnung - GL-Nummer"
	{
		const std::string got =
			normalize_header("  1.  Eröffnung   \xE2\x80\x93   GL-Nummer ");
		check(got == "1. Eröffnung - GL-Nummer",
		      "1. normalize_header mit Spaces und Gedankenstrich", got);
	}

	// 2. normalize_header("Woche\u2013Tag") == "Woche-Tag"
	{
		const std::string got = normalize_header("Woche\xE2\x80\x93Tag");
		check(got == "Woche-Tag", "2. normalize_header Woche\\u2013Tag == Woche-Tag", got);
	}

	// 3. header_key
	{
		const auto k_zs = header_key("Zeitstempel");
		const auto k_ts = header_key("Timestamp");
		const auto k_wo = header_key("Woche");
		const auto k_tg = header_key("Tag");
		const auto k_or = header_key("Organist");
		const auto k_p1_gl = header_key("1. Eröffnung - GL-Nummer");
		const auto k_p1_v = header_key("1. Eröffnung \xE2\x80\x93 Verse");
		const auto k_p1_t = header_key("1. Eröffnung - Liedtitel");
		const auto k_p12_t = header_key("12. Schlusslied \xE2\x80\x93 Liedtitel");
		const auto k_unk = header_key("Unbekannte Spalte");

		const bool ok = (k_zs && *k_zs == "zeitstempel") &&
				(k_ts && *k_ts == "zeitstempel") &&
				(k_wo && *k_wo == "woche") &&
				(k_tg && *k_tg == "tag") &&
				(k_or && *k_or == "organist") &&
				(k_p1_gl && *k_p1_gl == "1.gl") &&
				(k_p1_v && *k_p1_v == "1.verse") &&
				(k_p1_t && *k_p1_t == "1.titel") &&
				(k_p12_t && *k_p12_t == "12.titel") &&
				(!k_unk.has_value());
		check(ok, "3. header_key Simple-Keys, Programmpunkte 1..12 und Unbekannte Spalte");
	}

	// 4. antwort_comma.csv
	{
		const std::filesystem::path p =
			std::filesystem::u8path(std::string(kSamplesDir) + "antwort_comma.csv");
		LiedplanImportResult res;
		std::size_t count = 0;
		std::string err_msg;
		const ImportError err = parse_csv_file(p, res, &count, &err_msg);
		const bool ok = (err == ImportError::None) && (count == 1) &&
				(res.tag == "Sonntag") && (res.organist == "E. Stolper") &&
				(res.punkte.size() == 12) && (res.punkte[0].gl == "144") &&
				(res.punkte[0].verse == "1+4+6") &&
				(res.punkte[0].titel == "Nun jauchzt dem Herren, aller Welt");
		check(ok, "4. antwort_comma.csv inkl. Komma im quoteden Liedtitel", err_msg);
	}

	// 5. antwort_semicolon.csv
	{
		const std::filesystem::path p =
			std::filesystem::u8path(std::string(kSamplesDir) + "antwort_semicolon.csv");
		LiedplanImportResult res;
		std::size_t count = 0;
		std::string err_msg;
		const ImportError err = parse_csv_file(p, res, &count, &err_msg);
		const bool ok = (err == ImportError::None) && (count == 1) &&
				(res.tag == "Sonntag") && (res.punkte.size() == 12) &&
				(res.punkte[0].gl == "144");
		check(ok, "5. antwort_semicolon.csv P1.gl == 144", err_msg);
	}

	// 6. antwort_bom.csv
	{
		const std::filesystem::path p =
			std::filesystem::u8path(std::string(kSamplesDir) + "antwort_bom.csv");
		LiedplanImportResult res;
		std::size_t count = 0;
		std::string err_msg;
		const ImportError err = parse_csv_file(p, res, &count, &err_msg);
		const bool ok = (err == ImportError::None) && (count == 1) &&
				(res.punkte.size() == 12) && (res.punkte[0].gl == "144");
		check(ok, "6. antwort_bom.csv P1.gl == 144", err_msg);
	}

	// 7. antwort_timestamp_en.csv
	{
		const std::filesystem::path p = std::filesystem::u8path(
			std::string(kSamplesDir) + "antwort_timestamp_en.csv");
		LiedplanImportResult res;
		std::size_t count = 0;
		std::string err_msg;
		const ImportError err = parse_csv_file(p, res, &count, &err_msg);
		const bool ok = (err == ImportError::None) && (count == 1) &&
				(res.zeitstempel == "20.10.2026 09:15:32");
		check(ok, "7. antwort_timestamp_en.csv zeitstempel == 20.10.2026 09:15:32",
		      err_msg);
	}

	// 8. Extra: Inline-CSV mit ""-Escapes ("He""llo" -> He"llo) und leeren Zeilen zwischen Daten
	{
		const std::string csv_text =
			"Woche,1. Eröffnung – GL-Nummer,1. Eröffnung – Liedtitel\r\n"
			"\r\n"
			"KW 1,100,Erster Titel\r\n"
			"   ,   ,   \r\n"
			"KW 2,200,\"He\"\"llo\"\r\n"
			"\n";
		LiedplanImportResult res;
		std::size_t count = 0;
		std::string err_msg;
		const ImportError err = parse_csv_string(csv_text, res, &count, &err_msg);
		const bool ok = (err == ImportError::None) && (count == 2) &&
				(res.woche == "KW 2") && (res.punkte.size() == 12) &&
				(res.punkte[0].gl == "200") && (res.punkte[0].titel == "He\"llo");
		check(ok, "8. Inline-CSV mit \"\"-Escapes und leeren Zeilen zwischen Daten",
		      err_msg);
	}

	// 9. Extra: Header ohne Punkt-Spalten -> Fehler Format
	{
		const std::string csv_text =
			"Zeitstempel,Woche,Tag,Organist,Unbekannt\n"
			"20.10.2026,KW 42,Sonntag,M. Muster,Wert\n";
		LiedplanImportResult res;
		std::string err_msg;
		const ImportError err = parse_csv_string(csv_text, res, nullptr, &err_msg);
		const bool ok = (err == ImportError::Format) && !err_msg.empty();
		check(ok, "9. Header ohne Punkt-Spalten liefert ImportError::Format", err_msg);
	}

	// 10. Extra: nur Kopfzeile -> Fehler NoResponses
	{
		const std::string csv_text =
			"Zeitstempel,Woche,Tag,Organist,1. Eröffnung – GL-Nummer\n";
		LiedplanImportResult res;
		std::string err_msg;
		const ImportError err = parse_csv_string(csv_text, res, nullptr, &err_msg);
		const bool ok = (err == ImportError::NoResponses) && !err_msg.empty();
		check(ok, "10. Nur Kopfzeile liefert ImportError::NoResponses", err_msg);
	}

	// 11. API-Fixture (offline)
	{
		const nlohmann::json title_map = {
			{"q_woche", "Woche"},
			{"q_tag", "Tag"},
			{"q_organist", "Organist"},
			{"q_p1_gl", "1. Eröffnung \xE2\x80\x93 GL-Nummer"},
			{"q_p1_verse", "1. Eröffnung \xE2\x80\x93 Verse"},
			{"q_p1_titel", "1. Eröffnung \xE2\x80\x93 Liedtitel"},
			{"q_unbekannt", "Sonstige Bemerkungen"},
		};
		const nlohmann::json response = {
			{"createTime", "2026-10-20T08:00:00Z"},
			{"lastSubmittedTime", "2026-10-20T09:15:32Z"},
			{"answers",
			 {
				 {"q_woche",
				  {{"textAnswers",
				    {{"answers", {{{"value", "19.10. - 25.10.2026"}}}}}}}},
				 {"q_tag",
				  {{"textAnswers", {{"answers", {{{"value", "Sonntag"}}}}}}}},
				 {"q_organist",
				  {{"textAnswers", {{"answers", {{{"value", "E. Stolper"}}}}}}}},
				 {"q_p1_gl",
				  {{"textAnswers", {{"answers", {{{"value", "144"}}}}}}}},
				 {"q_p1_verse",
				  {{"textAnswers", {{"answers", {{{"value", "1+4+6"}}}}}}}},
				 {"q_p1_titel",
				  {{"textAnswers",
				    {{"answers",
				      {{{"value", "Nun jauchzt dem Herren, aller Welt"}}}}}}}},
				 {"q_unbekannt",
				  {{"textAnswers", {{"answers", {{{"value", "Ignorieren"}}}}}}}},
			 }},
		};

		const Record rec = api_response_to_record(response, title_map);
		const bool ok = (rec.at("zeitstempel") == "2026-10-20T09:15:32Z") &&
				(rec.at("woche") == "19.10. - 25.10.2026") &&
				(rec.at("tag") == "Sonntag") &&
				(rec.at("organist") == "E. Stolper") &&
				(rec.at("1.gl") == "144") &&
				(rec.at("1.verse") == "1+4+6") &&
				(rec.at("1.titel") == "Nun jauchzt dem Herren, aller Welt") &&
				(rec.find("q_unbekannt") == rec.end()) && (rec.size() == 7);
		check(ok, "11. API-Fixture (offline): Mapping + q_unbekannt ignoriert");
	}

	// 12. API-Multi: zwei textAnswers.answers[]-Werte -> joined "a, b"
	{
		const nlohmann::json title_map = {
			{"q_p1_verse", "1. Eröffnung \xE2\x80\x93 Verse"},
		};
		const nlohmann::json response = {
			{"lastSubmittedTime", "2026-10-20T09:15:32Z"},
			{"answers",
			 {
				 {"q_p1_verse",
				  {{"textAnswers",
				    {{"answers", {{{"value", "a"}}, {{"value", "b"}}}}}}}},
			 }},
		};

		const Record rec = api_response_to_record(response, title_map);
		const bool ok = (rec.find("1.verse") != rec.end()) && (rec.at("1.verse") == "a, b");
		check(ok, "12. API-Multi: zwei textAnswers.answers[]-Werte -> joined \"a, b\"");
	}

	if (g_failures > 0) {
		std::cerr << g_failures << " test(s) failed.\n";
		return 1;
	}
	std::cout << "All 12 checks passed.\n";
	return 0;
}
