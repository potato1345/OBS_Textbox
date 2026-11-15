/*
 * textbox-import.hpp — Import-Funktionen für Google Forms, CSV, XLSX und ZIP (v3).
 *
 * Zweck:
 *   - Definiert TextboxPoint und TextboxImportResult für 12 strukturierte Einträge.
 *   - Deklariert Parser für Google Forms API JSON-Antworten und CSV-/Tabellenexporte.
 *   - Implementiert OAuth-Token-Refresh und HTTP-Aufrufe via Windows WinINet.
 *
 * Hinweis: Qt- und libobs-frei; wird auch in Standalone-Tests (test-textbox-import) verwendet.
 */

#pragma once

#include <array>
#include "textbox-state.hpp"
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace textbox_import {

/** Die 12 standardmäßigen Blöcke in fester Reihenfolge. */
inline constexpr std::array<std::string_view, 12> PROGRAM_POINTS = {
	"Block 1",  "Block 2",  "Block 3",  "Block 4",  "Block 5",  "Block 6",
	"Block 7",  "Block 8",  "Block 9",  "Block 10", "Block 11", "Block 12",
};

// IMP-T: Laufzeit-Defaults statt hartkodierter Johann-Pfade (Portabilität);
// dieselbe Auflösung wie textbox-state.hpp.
inline std::filesystem::path default_token_path()
{
	return std::filesystem::u8path(default_google_token_path());
}
inline std::string default_token_path_str()
{
	return default_google_token_path();
}
inline std::filesystem::path default_client_secret_path()
{
	const char *env = std::getenv("LOCALAPPDATA");
	if (env && env[0] != '\0') {
		std::string val(env);
		while (!val.empty() && (val.back() == '\\' || val.back() == '/'))
			val.pop_back();
		if (!val.empty()) {
#ifdef _WIN32
			return std::filesystem::u8path(val + "\\hermes\\google_client_secret.json");
#else
			return std::filesystem::u8path(val + "/hermes/google_client_secret.json");
#endif
		}
	}
	return std::filesystem::path();
}

struct TextboxPoint {
	std::string nr;    // Nummer/Kürzel des Blocks (z. B. "101")
	std::string zeilen; // Zeilenangabe (z. B. "1-3"), optional
	std::string titel; // Titel/Bezeichnung des Blocks

	bool operator==(const TextboxPoint &other) const
	{
		return nr == other.nr && zeilen == other.zeilen && titel == other.titel;
	}
};

// Einzelne eingereichte Antwort mit Kopfdaten, generischen Feldern und 12 Blöcken.
struct TextboxService {
	std::string woche;
	std::string tag;
	std::string verantwortlich;
	std::string zeitstempel;
	std::vector<TextboxPoint> punkte; // exakt 12 Einträge (fehlende leer)
	std::vector<std::pair<std::string, std::string>> fields; // geordnete Nicht-Punkt-Spalten

	bool operator==(const TextboxService &other) const
	{
		return woche == other.woche && tag == other.tag && verantwortlich == other.verantwortlich &&
		       zeitstempel == other.zeitstempel && punkte == other.punkte &&
		       fields == other.fields;
	}
};

struct TextboxImportResult {
	std::string woche;
	std::string tag;
	std::string verantwortlich;
	std::string zeitstempel;
	std::vector<TextboxPoint> punkte; // exakt 12 Einträge (fehlende leer)
	TextboxService service; // neueste Einreichung (Abwärtskompatibilität)
	std::vector<TextboxService> services; // alle Einreichungen, neueste zuerst

	bool operator==(const TextboxImportResult &other) const
	{
		return woche == other.woche && tag == other.tag && verantwortlich == other.verantwortlich &&
		       zeitstempel == other.zeitstempel && punkte == other.punkte;
	}
};

using ImportResult = TextboxImportResult;

enum class ImportError { None, Format, NoResponses, File };

struct Record : std::map<std::string, std::string> {
	using std::map<std::string, std::string>::map;
	std::vector<std::pair<std::string, std::string>> fields;
};

// Spaltenkopf-Normalisierung und Mapping
std::string normalize_header(std::string_view raw);
std::optional<std::string> header_key(std::string_view raw);

// XML-Entity-Dekodierung (für XLSX sharedStrings / inlineStr)
std::string decode_xml_entities(std::string_view raw);

// CSV-Parser: zerlegt Text in Zeilen/Felder mit automatischer Trennzeichen-Erkennung
// (; vs ,) und gequoteten Feldern (""-Escaping). Gibt Tabelle als vector<vector<string>>.
std::vector<std::vector<std::string>> parse_csv(const std::string &text);

// Parst Header-Zeilen + Datenzeilen in Records: mapped Spalten via header_key().
// out_records ist leer bei Fehler (ImportError != None).
ImportError parse_records(const std::vector<std::string> &header,
			  const std::vector<std::vector<std::string>> &rows,
			  std::vector<Record> &out_records,
			  std::string *out_error = nullptr);

// Konvertiert einen Record in eine TextboxService-Struktur (12 standardisierte Blöcke).
TextboxService to_service(const Record &record);

// Konvertiert (einzelner) Service → ImportResult (füllt woche, tag, verantwortlich, punkte).
TextboxImportResult to_import_result(const TextboxService &service);
// Konvertiert einen einzelnen Record → ImportResult (via to_service).
TextboxImportResult to_import_result(const Record &record);
// Konvertiert mehrere Records → ImportResult (neueste zuerst nach Zeitstempel sortiert).
TextboxImportResult to_import_result(const std::vector<Record> &records);

// Parst CSV-Text direkt: ruft parse_csv + parse_records + to_import_result auf.
// out_count zeigt die Anzahl gefundener Antwort-Datensätze.
ImportError parse_csv_string(const std::string &text,
			     TextboxImportResult &out_result,
			     std::size_t *out_count = nullptr,
			     std::string *out_error = nullptr);

// Liest CSV-Datei (≤50 MB) und ruft parse_csv_string auf.
ImportError parse_csv_file(const std::filesystem::path &path,
			   TextboxImportResult &out_result,
			   std::size_t *out_count = nullptr,
			   std::string *out_error = nullptr);

// XLSX-Import (Mini-ZIP + Mini-XML): parst das Arbeitsblatt sheet1.xml + sharedStrings.xml.
// data = rohe XLSX-Bytes. out_count = Anzahl Antwort-Datensätze.
ImportError parse_xlsx_bytes(std::string_view data,
			     TextboxImportResult &out_result,
			     std::size_t *out_count = nullptr,
			     std::string *out_error = nullptr);

// Liest XLSX-Datei (≤50 MB) und ruft parse_xlsx_bytes auf.
ImportError parse_xlsx_file(const std::filesystem::path &path,
			    TextboxImportResult &out_result,
			    std::size_t *out_count = nullptr,
			    std::string *out_error = nullptr);

// ZIP-Import (Mini-ZIP + zlib Store/Deflate): sucht erste CSV im Archiv und parst sie.
// data = rohe ZIP-Bytes. out_count = Anzahl Antwort-Datensätze.
ImportError parse_zip_bytes(std::string_view data,
			    TextboxImportResult &out_result,
			    std::size_t *out_count = nullptr,
			    std::string *out_error = nullptr);

// Liest ZIP-Datei (≤100 MB) und ruft parse_zip_bytes auf.
ImportError parse_zip_file(const std::filesystem::path &path,
			   TextboxImportResult &out_result,
			   std::size_t *out_count = nullptr,
			   std::string *out_error = nullptr);

// Dispatch: erkennt Format (.csv, .xlsx, .zip) an filename und ruft entsprechenden Parser.
// data = rohe Bytes der Datei.
ImportError parse_answers(std::string_view data,
			  std::string_view filename,
			  TextboxImportResult &out_result,
			  std::size_t *out_count = nullptr,
			  std::string *out_error = nullptr);

// Dispatch: öffnet Datei an path, liest Bytes, ruft parse_answers auf.
ImportError parse_answers_file(const std::filesystem::path &path,
			       TextboxImportResult &out_result,
			       std::size_t *out_count = nullptr,
			       std::string *out_error = nullptr);

// Google-Forms-API JSON->Record Mapping (offline testbar)
Record api_response_to_record(const nlohmann::json &response,
			      const nlohmann::json &title_map);

// Google-Forms-API (WinHTTP)
std::optional<std::string> get_access_token(
	const std::filesystem::path &token_path = default_token_path(),
	const std::filesystem::path &cache_path = {},
	std::string *out_error = nullptr);

std::optional<nlohmann::json> fetch_titles(const std::string &form_id,
					   const std::string &access_token,
					   std::string *out_error = nullptr);

std::optional<std::vector<nlohmann::json>> fetch_responses(const std::string &form_id,
							   const std::string &access_token,
							   std::string *out_error = nullptr);

std::optional<TextboxImportResult> import_from_api(
	std::string *out_error = nullptr,
	const std::filesystem::path &cache_path = {},
	const std::filesystem::path &token_path = default_token_path(),
	const std::string &form_id = "");

} // namespace textbox_import
