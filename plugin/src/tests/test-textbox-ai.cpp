/*
 * test-textbox-ai.cpp — Unit-Tests für Textbox-AI- und Offline-Parser-Funktionen (v3).
 *
 * Zweck:
 *   - Testet das Bereinigen und Parsen von Modell-JSON-Ausgaben (parse_json_clean).
 *   - Testet den deterministischen Regex-basierten Offline-Parser (parse_textbox_locally).
 *   - Testet das Einlesen und Anwenden einer optionalen Referenztabelle (referenz.md).
 *   - Testet das Lesen von API-Keys aus .env-Dateien.
 */

#ifndef _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#endif

#include "../textbox-ai.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

// Test-Auswerter: zählt Fehler in g_failures, gibt [PASS]/[FAIL] mit optionalem Detail auf stdout/stderr aus.
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

// Hilfsfunktion: Legt ein temporäres Testverzeichnis an (vorher gelöscht).
// Wird für .env-Dateien, extract_document_text-Output und andere Fixtures verwendet.
std::filesystem::path make_temp_dir()
{
	std::filesystem::path dir = std::filesystem::temp_directory_path() / "textbox_m5_ai_tests";
	std::error_code ec;
	std::filesystem::remove_all(dir, ec);
	std::filesystem::create_directories(dir, ec);
	return dir;
}

} // namespace

int main()
{
	using namespace textbox_ai;
	const std::filesystem::path temp_dir = make_temp_dir();

	// 1. parse_textbox_locally(sampleText) (JS-Fixtur 1:1)
	{
		const std::string sampleText = R"(
Textbox Studio KW 42
Woche vom 20.10.2026
Montag
Beginn 101, 1-3 Kickoff
Review 205, 1-2 Workshop
Punkt 310
Abschluss Protokoll
)";
		const std::vector<AiPoint> items = parse_textbox_locally(sampleText);
		const bool ok = items.size() >= 4 &&
				items[0] == AiPoint{"Beginn", "101, 1-3", "Kickoff"};
		check(ok, "1. parse_textbox_locally(sampleText): >=4 Einträge und items[0] exakt",
		      "size=" + std::to_string(items.size()));
	}

	// 2. parse_reference_table(referenzSample) (JS-Fixtur mit "Nr"-Header)
	std::map<std::string, std::pair<std::string, std::string>> refTable;
	{
		const std::string referenzSample = R"(# Referenztabelle

| Nr    | Titel | alt | Kategorie | Unterkategorie | Hinweis |
| ----- | ----- | --- | --------- | -------------- | ------- |
| 31,1  | Eintrag Alpha (Nr 1) | 710,1 | Übersicht | Start |  |
| 101   | Kickoff und Start |       | Planung | Beginn |  |
)";
		refTable = parse_reference_table(referenzSample);
		const bool ok = refTable.size() == 2 && refTable.find("Nr") == refTable.end() &&
				refTable.count("31,1") == 1 && refTable.count("101") == 1 &&
				refTable["31,1"].first == "Eintrag Alpha (Nr 1)" &&
				refTable["31,1"].second == "Übersicht" &&
				refTable["101"].first == "Kickoff und Start";
		check(ok,
		      "2. parse_reference_table(referenzSample): exakt 2 Keys (31,1 / 101), kein Key 'Nr'");
	}

	// 3. parse_reference_table(referenzNoHeader) (JS-Fixtur ohne Header)
	{
		const std::string referenzNoHeader = R"(Kleine Testliste
| 5 | Eintrag Alpha |  | Kategorie A |  |  |
| --- | --- | --- | --- | --- | --- |
| 6 | Eintrag Beta |  | Kategorie A |  |  |
)";
		const auto refTable2 = parse_reference_table(referenzNoHeader);
		const bool ok = refTable2.size() == 2 && refTable2.count("5") == 1 &&
				refTable2.count("6") == 1 &&
				refTable2.at("5").first == "Eintrag Alpha" &&
				refTable2.at("6").first == "Eintrag Beta";
		check(ok,
		      "3. parse_reference_table(referenzNoHeader): 2 Keys (5 / 6), Separator übersprungen");
	}

	// 4. enrich_titles_from_reference
	{
		std::vector<AiPoint> items = {
			{"Beginn", "101, 1-3", ""},
			{"Review", "999", ""},
			{"Punkt", "101", "Bereits gesetzt"},
		};
		enrich_titles_from_reference(items, refTable);
		const bool ok =
			items[0].zeile3 == "Kickoff und Start" &&
			items[1].zeile3.empty() && items[2].zeile3 == "Bereits gesetzt";
		check(ok,
		      "4. enrich_titles_from_reference: bekannter Eintrag ergänzt, unbekannter leer, vorhandener unverändert",
		      items[0].zeile3);
	}

	// 5. parse_json_clean
	{
		const nlohmann::json j_a = parse_json_clean(
			"```json\n{\"items\":[{\"zeile1\":\"Review\",\"zeile2\":\"205\",\"zeile3\":\"\"}]}\n```");
		const nlohmann::json j_b = parse_json_clean(
			"Hier der Plan: {\"items\":[{\"zeile1\":\"Punkt\",\"zeile2\":\"310\",\"zeile3\":\"\"}]} Ende");
		bool threw_c = false;
		try {
			(void)parse_json_clean("kein json ohne klammern");
		} catch (const std::exception &) {
			threw_c = true;
		}
		const bool ok = j_a.is_object() && j_a["items"].is_array() &&
				j_a["items"].size() == 1 && j_b.is_object() &&
				j_b["items"].is_array() && j_b["items"].size() == 1 && threw_c;
		check(ok, "5. parse_json_clean: (a) Code-Fence, (b) Präfix/Suffix, (c) kein JSON -> throw");
	}

	// 6. discover_free_models (statische Fixtur, KEIN Netzwerk — simuliert OpenRouter-API-Antwort)
	// Die Fixture enthält Modelle mit Ausschlusskriterien (-vl, content-safety, 2.6b), die gefiltert werden müssen.
	{
		const nlohmann::json fixture = nlohmann::json::parse(R"({
			"data": [
				{"id": "google/gemma-4-31b-it:free", "context_length": 1000},
				{"id": "nvidia/nemotron-3-super-120b:free", "context_length": 2000},
				{"id": "x/y-vl:free", "context_length": 9000},
				{"id": "a/b-content-safety:free", "context_length": 8000},
				{"id": "c/d-2.6b-instruct:free", "context_length": 7000},
				{"id": "mistralai/m:free", "context_length": 3000}
			]
		})");
		const std::vector<std::string> models = discover_free_models(fixture);
		bool has_excluded = false;
		for (const auto &m : models) {
			if (m.find("-vl") != std::string::npos ||
			    m.find("content-safety") != std::string::npos ||
			    m.find("2.6b") != std::string::npos) {
				has_excluded = true;
			}
		}
		const bool ok = !has_excluded && models.size() == 3 &&
				models[0] == "google/gemma-4-31b-it:free" &&
				models[1] == "nvidia/nemotron-3-super-120b:free" &&
				models[2] == "mistralai/m:free";
		check(ok,
		      "6. discover_free_models: Ausschlüsse (-vl/content-safety/2.6b) und Prioritätsreihung");
	}

	// 7. read_env_key
	{
		const std::filesystem::path env_ok = temp_dir / "dummy_api.env";
		{
			std::ofstream out(env_ok, std::ios::binary | std::ios::trunc);
			out << "# Kommentar\nOPENROUTER_API_KEY=sk-or-dummy-test-123\n";
		}
		std::string err1;
		const std::string key1 = read_env_key(env_ok.u8string(), err1);

		std::string err2;
		const std::string key2 =
			read_env_key((temp_dir / "nicht_vorhanden.env").u8string(), err2);

		const std::filesystem::path env_empty = temp_dir / "no_key.env";
		{
			std::ofstream out(env_empty, std::ios::binary | std::ios::trunc);
			out << "OTHER_VAR=abc\n";
		}
		std::string err3;
		const std::string key3 = read_env_key(env_empty.u8string(), err3);

		const bool ok = (key1 == "sk-or-dummy-test-123" && err1.empty()) &&
				(key2.empty() && !err2.empty()) && key3.empty();
		check(ok,
		      "7. read_env_key: Dummy-Key gelesen, fehlende Datei -> err, Datei ohne Key -> leer");
	}

	// 8. parse_textbox_locally("") und nur Kopfzeilen -> leer
	{
		const auto empty1 = parse_textbox_locally("");
		const auto empty2 =
			parse_textbox_locally("Textbox X\nWoche Y\nMontag\nVerantwortlich Z");
		check(empty1.empty() && empty2.empty(),
		      "8. parse_textbox_locally: leerer String und reine Kopfzeilen liefern leere Liste");
	}

	// 9. parse_textbox_locally: mehrzeilig "Punkt\n310, 1-2" und einzeilig "Beginn 101, 1-3 Kickoff"
	{
		const std::vector<AiPoint> multi = parse_textbox_locally("Punkt\n310, 1-2");
		const bool ok_multi = multi.size() == 1 && multi[0].zeile1 == "Punkt" &&
				      multi[0].zeile2 == "310, 1-2" && multi[0].zeile3.empty();
		check(ok_multi,
		      "9a. parse_textbox_locally(\"Punkt\\n310, 1-2\"): 1 Punkt zeile1=Punkt, zeile2=310, 1-2",
		      "size=" + std::to_string(multi.size()));

		const std::vector<AiPoint> single =
			parse_textbox_locally("Beginn 101, 1-3 Kickoff");
		const bool ok_single = single.size() == 1 && single[0].zeile1 == "Beginn" &&
				       single[0].zeile2 == "101, 1-3" &&
				       single[0].zeile3 == "Kickoff";
		check(ok_single,
		      "9b. parse_textbox_locally(\"Beginn 101, 1-3 Kickoff\"): zeile3=\"Kickoff\"",
		      "size=" + std::to_string(single.size()));
	}

	// 10. Sidecar extract_document_text
	{
#ifdef TEXTBOX_REPO_ROOT
		const std::filesystem::path py_exe =
			std::filesystem::u8path(TEXTBOX_REPO_ROOT) / ".venv" / "Scripts" /
			"python.exe";
#else
		const std::filesystem::path py_exe =
			"C:/Users/Johann/Desktop/GitHub/OBS_Textbox/.venv/Scripts/python.exe";
#endif
		// M5-2: ohne Repo-Venv (markitdown installiert) ist 10a maschinenabhängig
		// -> sauber übersprungen statt rot. 10b (fehlende Exe -> Fehler) läuft immer.
		const bool venv_present =
			std::filesystem::exists(py_exe) && std::filesystem::exists(py_exe.parent_path());
		if (!venv_present)
			std::cout << "[SKIP] 10a. extract_document_text (Repo-Venv nicht vorhanden)\n";
		const std::filesystem::path doc_txt = temp_dir / "test_doc.txt";
		{
			std::ofstream out(doc_txt, std::ios::binary | std::ios::trunc);
			out << "Wochenplan Test 101, 1-3\n";
		}

		if (venv_present) {
			std::string err_ok;
			const std::string extracted =
				extract_document_text(doc_txt.u8string(), py_exe.u8string(), err_ok);
			check(err_ok.empty() && extracted.find("101") != std::string::npos,
			      "10a. extract_document_text mit Repo-Venv: extrahiert '101' aus test_doc.txt",
			      err_ok);
		}

		std::string err_missing;
		const std::string ext_missing = extract_document_text(
			doc_txt.u8string(), (temp_dir / "no_such_python.exe").u8string(),
			err_missing);
		check(ext_missing.empty() &&
			      err_missing.find("MarkItDown-Python nicht gefunden") !=
				      std::string::npos,
		      "10b. extract_document_text mit fehlender Python-Exe liefert deutschen Fehler",
		      err_missing);
	}

	std::error_code ec;
	std::filesystem::remove_all(temp_dir, ec);

	if (g_failures > 0) {
		std::cerr << g_failures << " test(s) failed.\n";
		return 1;
	}
	std::cout << "All AI module checks passed.\n";
	return 0;
}
