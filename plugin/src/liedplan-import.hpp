/*
 * liedplan-import.hpp — Import-Funktionen für Google Forms, CSV, XLSX und ZIP (v3).
 *
 * Zweck:
 *   - Definiert LiedplanPoint und LiedplanImportResult für 12 liturgische Programmpunkte.
 *   - Deklariert Parser für Google Forms API JSON-Antworten und CSV-/Tabellenexporte.
 *   - Implementiert OAuth-Token-Refresh und HTTP-Aufrufe via Windows WinINet.
 *
 * Hinweis: Qt- und libobs-frei; wird auch in Standalone-Tests (test-liedplan-import) verwendet.
 */

#pragma once

#include <array>
#include "liedplan-state.hpp"
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace liedplan_import {

/** Die 12 standardmäßigen Programmpunkte des Liedplans in fester liturgischer Reihenfolge. */
inline constexpr std::array<std::string_view, 12> PROGRAM_POINTS = {
	"Eröffnung",      "Kyrie",  "Gloria",    "Zwischengesang", "Halleluja", "Credo",
	"Gabenbereitung", "Heilig", "Agnus Dei", "Kommunion",      "Danklied",  "Schlusslied",
};

// IMP-T: Laufzeit-Defaults statt hartkodierter Johann-Pfade (Portabilität);
// dieselbe Auflösung wie liedplan-state.hpp.
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
inline constexpr const char *kDefaultFormId =
	"1jreAuzd-jt1x48WDmjaP7v0OusdVO2lb55Kt4JrhEgY";

struct LiedplanPoint {
	std::string gl;
	std::string verse;
	std::string titel;

	bool operator==(const LiedplanPoint &other) const
	{
		return gl == other.gl && verse == other.verse && titel == other.titel;
	}
};

// Einzelner eingereichter Gottesdienst mit Kopfdaten und 12 Programmpunkten.
struct LiedplanService {
	std::string woche;
	std::string tag;
	std::string organist;
	std::string zeitstempel;
	std::vector<LiedplanPoint> punkte; // exakt 12 Einträge (fehlende leer)

	bool operator==(const LiedplanService &other) const
	{
		return woche == other.woche && tag == other.tag && organist == other.organist &&
		       zeitstempel == other.zeitstempel && punkte == other.punkte;
	}
};

struct LiedplanImportResult {
	std::string woche;
	std::string tag;
	std::string organist;
	std::string zeitstempel;
	std::vector<LiedplanPoint> punkte; // exakt 12 Einträge (fehlende leer)
	LiedplanService service; // neuester Gottesdienst (Abwärtskompatibilität)
	std::vector<LiedplanService> services; // alle Gottesdienste, neueste zuerst

	bool operator==(const LiedplanImportResult &other) const
	{
		return woche == other.woche && tag == other.tag && organist == other.organist &&
		       zeitstempel == other.zeitstempel && punkte == other.punkte;
	}
};

using ImportResult = LiedplanImportResult;

enum class ImportError { None, Format, NoResponses, File };

using Record = std::map<std::string, std::string>;

// Spaltenkopf-Normalisierung und Mapping
std::string normalize_header(std::string_view raw);
std::optional<std::string> header_key(std::string_view raw);

// XML-Entity-Dekodierung (für XLSX sharedStrings / inlineStr)
std::string decode_xml_entities(std::string_view raw);

// CSV-Parser & Record-Konvertierung
std::vector<std::vector<std::string>> parse_csv(const std::string &text);

ImportError parse_records(const std::vector<std::string> &header,
			  const std::vector<std::vector<std::string>> &rows,
			  std::vector<Record> &out_records,
			  std::string *out_error = nullptr);

LiedplanService to_service(const Record &record);
LiedplanImportResult to_import_result(const LiedplanService &service);
LiedplanImportResult to_import_result(const Record &record);
LiedplanImportResult to_import_result(const std::vector<Record> &records);

ImportError parse_csv_string(const std::string &text,
			     LiedplanImportResult &out_result,
			     std::size_t *out_count = nullptr,
			     std::string *out_error = nullptr);

ImportError parse_csv_file(const std::filesystem::path &path,
			   LiedplanImportResult &out_result,
			   std::size_t *out_count = nullptr,
			   std::string *out_error = nullptr);

// XLSX-Import (Mini-ZIP + Mini-XML)
ImportError parse_xlsx_bytes(std::string_view data,
			     LiedplanImportResult &out_result,
			     std::size_t *out_count = nullptr,
			     std::string *out_error = nullptr);

ImportError parse_xlsx_file(const std::filesystem::path &path,
			    LiedplanImportResult &out_result,
			    std::size_t *out_count = nullptr,
			    std::string *out_error = nullptr);

// ZIP-Import (Mini-ZIP + zlib Store/Deflate)
ImportError parse_zip_bytes(std::string_view data,
			    LiedplanImportResult &out_result,
			    std::size_t *out_count = nullptr,
			    std::string *out_error = nullptr);

ImportError parse_zip_file(const std::filesystem::path &path,
			   LiedplanImportResult &out_result,
			   std::size_t *out_count = nullptr,
			   std::string *out_error = nullptr);

// Datei-Dispatch nach Endung (.csv, .xlsx, .zip)
ImportError parse_answers(std::string_view data,
			  std::string_view filename,
			  LiedplanImportResult &out_result,
			  std::size_t *out_count = nullptr,
			  std::string *out_error = nullptr);

ImportError parse_answers_file(const std::filesystem::path &path,
			       LiedplanImportResult &out_result,
			       std::size_t *out_count = nullptr,
			       std::string *out_error = nullptr);

// Hilfsfunktion zum Erzeugen eines ZIP-Archivs im Speicher (Store oder Deflate, für Tests)
std::string build_zip_archive(const std::vector<std::pair<std::string, std::string>> &entries,
			      bool use_deflate);

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

std::optional<LiedplanImportResult> import_from_api(
	std::string *out_error = nullptr,
	const std::filesystem::path &cache_path = {},
	const std::filesystem::path &token_path = default_token_path(),
	const std::string &form_id = kDefaultFormId);

} // namespace liedplan_import
