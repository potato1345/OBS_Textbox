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

#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace liedplan_import {

inline constexpr std::array<std::string_view, 12> PROGRAM_POINTS = {
	"Eröffnung",      "Kyrie",  "Gloria",    "Zwischengesang", "Halleluja", "Credo",
	"Gabenbereitung", "Heilig", "Agnus Dei", "Kommunion",      "Danklied",  "Schlusslied",
};

inline constexpr const char *kDefaultTokenPath =
	"C:\\Users\\Johann\\AppData\\Local\\hermes\\google_token.json";
inline constexpr const char *kFallbackClientSecretPath =
	"C:\\Users\\Johann\\AppData\\Local\\hermes\\google_client_secret.json";
inline constexpr const char *kDefaultFormId =
	"1jreAuzd-jt1x48WDmjaP7v0OusdVO2lb55Kt4JrhEgY";

struct LiedplanPoint {
	std::string gl;
	std::string verse;
	std::string titel;
};

struct LiedplanImportResult {
	std::string woche;
	std::string tag;
	std::string organist;
	std::string zeitstempel;
	std::vector<LiedplanPoint> punkte; // exakt 12 Einträge (fehlende leer)
};

enum class ImportError { None, Format, NoResponses, File };

using Record = std::map<std::string, std::string>;

// Spaltenkopf-Normalisierung und Mapping
std::string normalize_header(std::string_view raw);
std::optional<std::string> header_key(std::string_view raw);

// CSV-Parser & Record-Konvertierung
std::vector<std::vector<std::string>> parse_csv(const std::string &text);

ImportError parse_records(const std::vector<std::string> &header,
			  const std::vector<std::vector<std::string>> &rows,
			  std::vector<Record> &out_records,
			  std::string *out_error = nullptr);

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

// Google-Forms-API JSON->Record Mapping (offline testbar)
Record api_response_to_record(const nlohmann::json &response,
			      const nlohmann::json &title_map);

// Google-Forms-API (WinHTTP)
std::optional<std::string> get_access_token(
	const std::filesystem::path &token_path = std::filesystem::u8path(kDefaultTokenPath),
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
	const std::filesystem::path &token_path = std::filesystem::u8path(kDefaultTokenPath),
	const std::string &form_id = kDefaultFormId);

} // namespace liedplan_import
