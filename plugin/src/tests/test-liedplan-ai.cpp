/*
 * test-liedplan-ai.cpp — Unit-Tests für Liedplan-AI- und Offline-Parser-Funktionen (v3).
 *
 * Zweck:
 *   - Testet das Bereinigen und Parsen von Modell-JSON-Ausgaben (parse_json_clean).
 *   - Testet den deterministischen Regex-basierten Offline-Parser (parse_liedplan_locally).
 *   - Testet das Einlesen und Anwenden der Gotteslob-Referenztabelle (lieder.md).
 *   - Testet das Lesen von API-Keys aus .env-Dateien.
 */

#ifndef _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#endif

#include "../liedplan-ai.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
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

std::filesystem::path make_temp_dir()
{
	std::filesystem::path dir = std::filesystem::temp_directory_path() / "liedplan_m5_ai_tests";
	std::error_code ec;
	std::filesystem::remove_all(dir, ec);
	std::filesystem::create_directories(dir, ec);
	return dir;
}

} // namespace

int main()
{
	using namespace liedplan_ai;
	const std::filesystem::path temp_dir = make_temp_dir();

	// 1. parse_liedplan_locally(sampleText) (JS-Fixtur 1:1)
	{
		const std::string sampleText = R"(
Liedplan St. Johannes
Woche vom 20.09.2026
Sonntag
Eröffnung 144, 1+4+6 Nun jauchzt dem Herren
Kyrie 437, 1-4 Meine engen Grenzen
Gloria 172
Kommunion Orgel
Schlusslied 868, 1+6 Ave Maria klare
)";
		const std::vector<AiPoint> items = parse_liedplan_locally(sampleText);
		const bool ok = items.size() >= 4 &&
				items[0] == AiPoint{"Eröffnung", "144, 1+4+6",
						    "Nun jauchzt dem Herren"};
		check(ok, "1. parse_liedplan_locally(sampleText): >=4 Einträge und items[0] exakt",
		      "size=" + std::to_string(items.size()));
	}

	// 2. parse_song_reference(liederSample) (JS-Fixtur mit "Nr"-Header)
	std::map<std::string, std::pair<std::string, std::string>> songRef;
	{
		const std::string liederSample = R"(# Gotteslob-Referenztabelle

| Nr    | Titel | altes GL | Rubrik | Unterrubrik | ökumenisch |
| ----- | ----- | -------- | ------ | ----------- | ---------- |
| 31,1  | Selig der Mensch, der seine Freude hat (Kv zu Ps 1) | 710,1 | Psalmen | Kehrverse |  |
| 144   | Nun jauchzt dem Herren, aller Welt mit Freuden |       | Lieder zur Einsegnung |  |  |
)";
		songRef = parse_song_reference(liederSample);
		const bool ok = songRef.size() == 2 && songRef.find("Nr") == songRef.end() &&
				songRef.count("31,1") == 1 && songRef.count("144") == 1 &&
				songRef["31,1"].first ==
					"Selig der Mensch, der seine Freude hat (Kv zu Ps 1)" &&
				songRef["31,1"].second == "Psalmen" &&
				songRef["144"].first ==
					"Nun jauchzt dem Herren, aller Welt mit Freuden";
		check(ok,
		      "2. parse_song_reference(liederSample): exakt 2 Keys (31,1 / 144), kein Key 'Nr'");
	}

	// 3. parse_song_reference(liederNoHeader) (JS-Fixtur ohne Header)
	{
		const std::string liederNoHeader = R"(Kleine Testliste
| 5 | Ein Testlied |  | Lieder |  |  |
| --- | --- | --- | --- | --- | --- |
| 6 | Zweites Lied |  | Lieder |  |  |
)";
		const auto songRef2 = parse_song_reference(liederNoHeader);
		const bool ok = songRef2.size() == 2 && songRef2.count("5") == 1 &&
				songRef2.count("6") == 1 &&
				songRef2.at("5").first == "Ein Testlied" &&
				songRef2.at("6").first == "Zweites Lied";
		check(ok,
		      "3. parse_song_reference(liederNoHeader): 2 Keys (5 / 6), Separator übersprungen");
	}

	// 4. enrich_titles_from_reference
	{
		std::vector<AiPoint> items = {
			{"Eröffnung", "144, 1+4+6", ""},
			{"Kyrie", "999", ""},
			{"Gloria", "144", "Bereits gesetzt"},
		};
		enrich_titles_from_reference(items, songRef);
		const bool ok =
			items[0].zeile3 == "Nun jauchzt dem Herren, aller Welt mit Freuden" &&
			items[1].zeile3.empty() && items[2].zeile3 == "Bereits gesetzt";
		check(ok,
		      "4. enrich_titles_from_reference: bekanntes Lied ergänzt, unbekanntes leer, vorhandenes unverändert",
		      items[0].zeile3);
	}

	// 5. parse_json_clean
	{
		const nlohmann::json j_a = parse_json_clean(
			"```json\n{\"items\":[{\"zeile1\":\"Kyrie\",\"zeile2\":\"150\",\"zeile3\":\"\"}]}\n```");
		const nlohmann::json j_b = parse_json_clean(
			"Hier der Plan: {\"items\":[{\"zeile1\":\"Gloria\",\"zeile2\":\"172\",\"zeile3\":\"\"}]} Ende");
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

	// 6. discover_free_models (statische Fixtur, KEIN Netzwerk)
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

	// 8. parse_liedplan_locally("") und nur Kopfzeilen -> leer
	{
		const auto empty1 = parse_liedplan_locally("");
		const auto empty2 =
			parse_liedplan_locally("Liedplan X\nWoche Y\nSonntag\nOrganist Z");
		check(empty1.empty() && empty2.empty(),
		      "8. parse_liedplan_locally: leerer String und reine Kopfzeilen liefern leere Liste");
	}

	// 9. parse_liedplan_locally: mehrzeilig "Kyrie\n437, 1-4" und einzeilig "Eröffnung 144, 1+4+6 Nun jauchzt"
	{
		const std::vector<AiPoint> multi = parse_liedplan_locally("Kyrie\n437, 1-4");
		const bool ok_multi = multi.size() == 1 && multi[0].zeile1 == "Kyrie" &&
				      multi[0].zeile2 == "437, 1-4" && multi[0].zeile3.empty();
		check(ok_multi,
		      "9a. parse_liedplan_locally(\"Kyrie\\n437, 1-4\"): 1 Punkt zeile1=Kyrie, zeile2=437, 1-4",
		      "size=" + std::to_string(multi.size()));

		const std::vector<AiPoint> single =
			parse_liedplan_locally("Eröffnung 144, 1+4+6 Nun jauchzt");
		const bool ok_single = single.size() == 1 && single[0].zeile1 == "Eröffnung" &&
				       single[0].zeile2 == "144, 1+4+6" &&
				       single[0].zeile3 == "Nun jauchzt";
		check(ok_single,
		      "9b. parse_liedplan_locally(\"Eröffnung 144, 1+4+6 Nun jauchzt\"): zeile3=\"Nun jauchzt\"",
		      "size=" + std::to_string(single.size()));
	}

	// 10. Sidecar extract_document_text
	{
#ifdef LIEDPLAN_REPO_ROOT
		const std::filesystem::path py_exe =
			std::filesystem::u8path(LIEDPLAN_REPO_ROOT) / ".venv" / "Scripts" /
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
			out << "Liedplan Test 144, 1+4+6\n";
		}

		if (venv_present) {
			std::string err_ok;
			const std::string extracted =
				extract_document_text(doc_txt.u8string(), py_exe.u8string(), err_ok);
			check(err_ok.empty() && extracted.find("144") != std::string::npos,
			      "10a. extract_document_text mit Repo-Venv: extrahiert '144' aus test_doc.txt",
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
