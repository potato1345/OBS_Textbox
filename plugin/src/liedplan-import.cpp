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

#include "liedplan-import.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace liedplan_import {

namespace {

constexpr const char *kFormatErrorMsg =
	"Unbekanntes Dateiformat: Es wurden keine Programmpunkt-Spalten des "
	"Liedplan-Formulars gefunden (erwartet z. B. „1. Eröffnung – GL-Nummer“).";
constexpr const char *kNoResponsesCsvMsg =
	"Die Datei enthält keine Antworten (nur die Kopfzeile).";
constexpr const char *kEmptyCsvMsg = "Die CSV-Datei ist leer.";
constexpr const char *kNoResponsesApiMsg =
	"Im Google-Formular liegen noch keine Antworten vor.";

bool is_ascii_space(unsigned char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

std::string trim_str(std::string_view sv)
{
	std::size_t start = 0;
	while (start < sv.size()) {
		if (is_ascii_space(static_cast<unsigned char>(sv[start]))) {
			++start;
		} else if (start + 1 < sv.size() &&
			   static_cast<unsigned char>(sv[start]) == 0xC2 &&
			   static_cast<unsigned char>(sv[start + 1]) == 0xA0) {
			start += 2;
		} else {
			break;
		}
	}

	std::size_t end = sv.size();
	while (end > start) {
		if (is_ascii_space(static_cast<unsigned char>(sv[end - 1]))) {
			--end;
		} else if (end >= start + 2 &&
			   static_cast<unsigned char>(sv[end - 2]) == 0xC2 &&
			   static_cast<unsigned char>(sv[end - 1]) == 0xA0) {
			end -= 2;
		} else {
			break;
		}
	}

	return std::string(sv.substr(start, end - start));
}

std::string utf8_casefold(std::string_view sv)
{
	std::string out;
	out.reserve(sv.size());
	for (std::size_t i = 0; i < sv.size(); ++i) {
		const unsigned char c = static_cast<unsigned char>(sv[i]);
		if (c >= 'A' && c <= 'Z') {
			out.push_back(static_cast<char>(c + ('a' - 'A')));
		} else if (c == 0xC3 && i + 1 < sv.size()) {
			const unsigned char next = static_cast<unsigned char>(sv[i + 1]);
			// U+00C4 (Ä) -> U+00E4 (ä), U+00D6 (Ö) -> U+00F6 (ö), U+00DC (Ü) -> U+00FC (ü)
			if (next >= 0x80 && next <= 0x9E && next != 0x97) {
				out.push_back(static_cast<char>(0xC3));
				out.push_back(static_cast<char>(next + 0x20));
				++i;
			} else {
				out.push_back(sv[i]);
			}
		} else {
			out.push_back(sv[i]);
		}
	}
	return out;
}

bool row_has_content(const std::vector<std::string> &row)
{
	for (const auto &cell : row) {
		if (!trim_str(cell).empty())
			return true;
	}
	return false;
}

std::string get_record_trimmed(const Record &rec, const std::string &key)
{
	auto it = rec.find(key);
	if (it == rec.end())
		return {};
	return trim_str(it->second);
}

#ifdef _WIN32

struct WinHttpHandle {
	HINTERNET h = nullptr;
	WinHttpHandle() = default;
	explicit WinHttpHandle(HINTERNET handle) : h(handle) {}
	~WinHttpHandle()
	{
		if (h)
			WinHttpCloseHandle(h);
	}
	WinHttpHandle(const WinHttpHandle &) = delete;
	WinHttpHandle &operator=(const WinHttpHandle &) = delete;
	operator HINTERNET() const { return h; }
	explicit operator bool() const { return h != nullptr; }
};

std::wstring utf8_to_wide(std::string_view sv)
{
	if (sv.empty())
		return {};
	const int len = MultiByteToWideChar(CP_UTF8, 0, sv.data(), static_cast<int>(sv.size()),
					    nullptr, 0);
	if (len <= 0)
		return {};
	std::wstring out(static_cast<std::size_t>(len), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, sv.data(), static_cast<int>(sv.size()), out.data(), len);
	return out;
}

std::string url_encode(std::string_view sv)
{
	std::string out;
	out.reserve(sv.size() * 3);
	constexpr const char *hex = "0123456789ABCDEF";
	for (unsigned char c : sv) {
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
		    c == '-' || c == '_' || c == '.' || c == '~') {
			out.push_back(static_cast<char>(c));
		} else {
			out.push_back('%');
			out.push_back(hex[(c >> 4) & 0x0F]);
			out.push_back(hex[c & 0x0F]);
		}
	}
	return out;
}

std::string format_http_error(DWORD status)
{
	if (status == 401)
		return "Google-API-Fehler 401 — Token abgelaufen oder ungültig";
	if (status == 403)
		return "Google-API-Fehler 403 — Zugriff verweigert";
	if (status == 404)
		return "Google-API-Fehler 404 — Formular nicht gefunden";
	return "Google-API-Fehler " + std::to_string(status) + " — Anfrage fehlgeschlagen";
}

struct HttpResponse {
	DWORD status = 0;
	std::string body;
};

std::optional<HttpResponse> winhttp_request(const std::string &method,
					    const std::string &url,
					    const std::wstring &headers,
					    const std::string &req_body,
					    std::string *out_error)
{
	const std::wstring wurl = utf8_to_wide(url);
	URL_COMPONENTS uc{};
	uc.dwStructSize = sizeof(uc);
	wchar_t host_buf[256]{};
	wchar_t path_buf[2048]{};
	uc.lpszHostName = host_buf;
	uc.dwHostNameLength = static_cast<DWORD>(std::size(host_buf));
	uc.lpszUrlPath = path_buf;
	uc.dwUrlPathLength = static_cast<DWORD>(std::size(path_buf));

	if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &uc)) {
		if (out_error)
			*out_error = "Ungültige Google-API-URL: " + url;
		return std::nullopt;
	}

	WinHttpHandle session(WinHttpOpen(L"Liedplan-OBS/1.0",
					  WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
					  WINHTTP_NO_PROXY_NAME,
					  WINHTTP_NO_PROXY_BYPASS, 0));
	if (!session) {
		if (out_error)
			*out_error = "Netzwerkfehler: WinHttpOpen fehlgeschlagen (Fehler " +
				     std::to_string(GetLastError()) + ").";
		return std::nullopt;
	}

	WinHttpSetTimeouts(session, 10000, 10000, 10000, 30000);

	const std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
	const std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
	WinHttpHandle connect(WinHttpConnect(session, host.c_str(), uc.nPort, 0));
	if (!connect) {
		if (out_error)
			*out_error = "Netzwerkfehler: Verbindung zu " + url +
				     " fehlgeschlagen (Fehler " + std::to_string(GetLastError()) +
				     ").";
		return std::nullopt;
	}

	const DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
	const std::wstring wmethod = utf8_to_wide(method);
	WinHttpHandle request(WinHttpOpenRequest(connect, wmethod.c_str(), path.c_str(), nullptr,
						 WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
						 flags));
	if (!request) {
		if (out_error)
			*out_error = "Netzwerkfehler: WinHttpOpenRequest fehlgeschlagen (Fehler " +
				     std::to_string(GetLastError()) + ").";
		return std::nullopt;
	}

	LPCWSTR hdr_ptr = headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str();
	DWORD hdr_len = headers.empty() ? 0 : static_cast<DWORD>(headers.size());
	LPVOID body_ptr =
		req_body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char *>(req_body.data());
	DWORD body_len = static_cast<DWORD>(req_body.size());

	if (!WinHttpSendRequest(request, hdr_ptr, hdr_len, body_ptr, body_len, body_len, 0) ||
	    !WinHttpReceiveResponse(request, nullptr)) {
		if (out_error)
			*out_error =
				"Netzwerkfehler oder Zeitüberschreitung bei der Google-API-Anfrage (Fehler " +
				std::to_string(GetLastError()) + ").";
		return std::nullopt;
	}

	DWORD status_code = 0;
	DWORD status_size = sizeof(status_code);
	WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			    WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size,
			    WINHTTP_NO_HEADER_INDEX);

	std::string resp_body;
	while (true) {
		DWORD avail = 0;
		if (!WinHttpQueryDataAvailable(request, &avail))
			break;
		if (avail == 0)
			break;
		std::string chunk(avail, '\0');
		DWORD read = 0;
		if (!WinHttpReadData(request, chunk.data(), avail, &read))
			break;
		chunk.resize(read);
		resp_body.append(chunk);
	}

	return HttpResponse{status_code, std::move(resp_body)};
}

#endif // _WIN32

} // namespace

std::string normalize_header(std::string_view raw)
{
	std::string temp;
	temp.reserve(raw.size());

	for (std::size_t i = 0; i < raw.size();) {
		const unsigned char b0 = static_cast<unsigned char>(raw[i]);
		// U+FEFF BOM: EF BB BF -> entfernen
		if (b0 == 0xEF && i + 2 < raw.size() &&
		    static_cast<unsigned char>(raw[i + 1]) == 0xBB &&
		    static_cast<unsigned char>(raw[i + 2]) == 0xBF) {
			i += 3;
			continue;
		}
		// U+2012..U+2015: E2 80 92..95 -> '-'
		if (b0 == 0xE2 && i + 2 < raw.size() &&
		    static_cast<unsigned char>(raw[i + 1]) == 0x80) {
			const unsigned char b2 = static_cast<unsigned char>(raw[i + 2]);
			if (b2 >= 0x92 && b2 <= 0x95) {
				temp.push_back('-');
				i += 3;
				continue;
			}
		}
		// U+2212 (Minus Sign): E2 88 92 -> '-'
		if (b0 == 0xE2 && i + 2 < raw.size() &&
		    static_cast<unsigned char>(raw[i + 1]) == 0x88 &&
		    static_cast<unsigned char>(raw[i + 2]) == 0x92) {
			temp.push_back('-');
			i += 3;
			continue;
		}
		// U+00A0 (NBSP): C2 A0 -> ' '
		if (b0 == 0xC2 && i + 1 < raw.size() &&
		    static_cast<unsigned char>(raw[i + 1]) == 0xA0) {
			temp.push_back(' ');
			i += 2;
			continue;
		}
		temp.push_back(raw[i]);
		++i;
	}

	// Whitespace-Ketten -> ein Leerzeichen + trimmen
	std::string out;
	out.reserve(temp.size());
	bool in_space = false;
	for (std::size_t i = 0; i < temp.size(); ++i) {
		if (is_ascii_space(static_cast<unsigned char>(temp[i]))) {
			if (!out.empty())
				in_space = true;
		} else {
			if (in_space) {
				out.push_back(' ');
				in_space = false;
			}
			out.push_back(temp[i]);
		}
	}
	return out;
}

std::optional<std::string> header_key(std::string_view raw)
{
	const std::string title = normalize_header(raw);
	if (title.empty())
		return std::nullopt;

	const std::string lower = utf8_casefold(title);
	if (lower == "zeitstempel" || lower == "timestamp")
		return std::string("zeitstempel");
	if (lower == "woche")
		return std::string("woche");
	if (lower == "tag")
		return std::string("tag");
	if (lower == "organist")
		return std::string("organist");

	// Punkt-Pattern: ^(\d{1,2})\.\s*(.+?)\s+-\s+(.+)$
	std::size_t pos = 0;
	int index = 0;
	int digits = 0;
	while (pos < title.size() &&
	       std::isdigit(static_cast<unsigned char>(title[pos])) && digits < 2) {
		index = index * 10 + (title[pos] - '0');
		++pos;
		++digits;
	}
	if (digits == 0 || pos >= title.size() || title[pos] != '.')
		return std::nullopt;
	++pos; // '.'

	while (pos < title.size() && title[pos] == ' ')
		++pos;

	// Non-greedy (.+?)\s+-\s+(.+): da normalize_header alle Whitespace-Ketten auf
	// genau ein Leerzeichen normiert hat, ist "\s+-\s+" exakt " - ".
	const std::size_t sep = title.find(" - ", pos);
	if (sep == std::string::npos || sep == pos)
		return std::nullopt;

	const std::string name = trim_str(std::string_view(title).substr(pos, sep - pos));
	const std::string field = trim_str(std::string_view(title).substr(sep + 3));
	if (name.empty() || field.empty())
		return std::nullopt;

	if (index < 1 || index > static_cast<int>(PROGRAM_POINTS.size()))
		return std::nullopt;

	if (utf8_casefold(name) != utf8_casefold(PROGRAM_POINTS[static_cast<std::size_t>(index - 1)]))
		return std::nullopt;

	const std::string field_lower = utf8_casefold(field);
	std::string_view field_key;
	if (field_lower == "gl-nummer") {
		field_key = "gl";
	} else if (field_lower == "verse") {
		field_key = "verse";
	} else if (field_lower == "liedtitel") {
		field_key = "titel";
	} else {
		return std::nullopt;
	}

	return std::to_string(index) + "." + std::string(field_key);
}

std::vector<std::vector<std::string>> parse_csv(const std::string &text)
{
	std::string_view sv(text);
	// Optionaler UTF-8 BOM am Dateianfang entfernen
	if (sv.size() >= 3 &&
	    static_cast<unsigned char>(sv[0]) == 0xEF &&
	    static_cast<unsigned char>(sv[1]) == 0xBB &&
	    static_cast<unsigned char>(sv[2]) == 0xBF) {
		sv.remove_prefix(3);
	}

	// Erste nicht-leere Zeile für Trennzeichen-Erkennung (wie Python text.lstrip().splitlines()[0])
	std::size_t lstrip_pos = 0;
	while (lstrip_pos < sv.size() && is_ascii_space(static_cast<unsigned char>(sv[lstrip_pos])))
		++lstrip_pos;

	if (lstrip_pos >= sv.size())
		return {};

	std::size_t line_end = lstrip_pos;
	while (line_end < sv.size() && sv[line_end] != '\r' && sv[line_end] != '\n')
		++line_end;

	const std::string_view first_line = sv.substr(lstrip_pos, line_end - lstrip_pos);
	std::size_t semi_count = 0;
	std::size_t comma_count = 0;
	for (char c : first_line) {
		if (c == ';')
			++semi_count;
		else if (c == ',')
			++comma_count;
	}
	const char delimiter = (semi_count > comma_count) ? ';' : ',';

	std::vector<std::vector<std::string>> table;
	std::vector<std::string> current_row;
	std::string current_field;
	bool in_quotes = false;
	bool at_field_start = true;

	for (std::size_t i = 0; i < sv.size(); ++i) {
		const char c = sv[i];
		if (in_quotes) {
			if (c == '"') {
				if (i + 1 < sv.size() && sv[i + 1] == '"') {
					current_field.push_back('"');
					++i;
				} else {
					in_quotes = false;
				}
			} else {
				current_field.push_back(c);
			}
		} else if (at_field_start) {
			if (c == '"') {
				in_quotes = true;
				at_field_start = false;
			} else if (c == delimiter) {
				current_row.push_back(std::move(current_field));
				current_field.clear();
				at_field_start = true;
			} else if (c == '\r' || c == '\n') {
				if (c == '\r' && i + 1 < sv.size() && sv[i + 1] == '\n')
					++i;
				current_row.push_back(std::move(current_field));
				current_field.clear();
				if (row_has_content(current_row))
					table.push_back(std::move(current_row));
				current_row.clear();
				at_field_start = true;
			} else {
				current_field.push_back(c);
				at_field_start = false;
			}
		} else {
			if (c == delimiter) {
				current_row.push_back(std::move(current_field));
				current_field.clear();
				at_field_start = true;
			} else if (c == '\r' || c == '\n') {
				if (c == '\r' && i + 1 < sv.size() && sv[i + 1] == '\n')
					++i;
				current_row.push_back(std::move(current_field));
				current_field.clear();
				if (row_has_content(current_row))
					table.push_back(std::move(current_row));
				current_row.clear();
				at_field_start = true;
			} else {
				current_field.push_back(c);
			}
		}
	}

	if (!current_field.empty() || !current_row.empty()) {
		current_row.push_back(std::move(current_field));
		if (row_has_content(current_row))
			table.push_back(std::move(current_row));
	}

	return table;
}

ImportError parse_records(const std::vector<std::string> &header,
			  const std::vector<std::vector<std::string>> &rows,
			  std::vector<Record> &out_records,
			  std::string *out_error)
{
	out_records.clear();
	std::map<std::string, std::size_t> columns;
	for (std::size_t pos = 0; pos < header.size(); ++pos) {
		auto key = header_key(header[pos]);
		if (key && columns.find(*key) == columns.end()) {
			columns[*key] = pos;
		}
	}

	bool has_point_1 = false;
	for (const auto &kv : columns) {
		if (kv.first.rfind("1.", 0) == 0) {
			has_point_1 = true;
			break;
		}
	}

	if (!has_point_1) {
		if (out_error)
			*out_error = kFormatErrorMsg;
		return ImportError::Format;
	}

	for (const auto &row : rows) {
		if (!row_has_content(row))
			continue;
		Record rec;
		for (const auto &kv : columns) {
			const std::string &key = kv.first;
			const std::size_t pos = kv.second;
			rec[key] = (pos < row.size()) ? trim_str(row[pos]) : std::string{};
		}
		out_records.push_back(std::move(rec));
	}

	if (out_records.empty()) {
		if (out_error)
			*out_error = kNoResponsesCsvMsg;
		return ImportError::NoResponses;
	}

	if (out_error)
		out_error->clear();
	return ImportError::None;
}

LiedplanImportResult to_import_result(const Record &record)
{
	LiedplanImportResult res;
	res.woche = get_record_trimmed(record, "woche");
	res.tag = get_record_trimmed(record, "tag");
	res.organist = get_record_trimmed(record, "organist");
	res.zeitstempel = get_record_trimmed(record, "zeitstempel");
	res.punkte.resize(PROGRAM_POINTS.size());

	for (std::size_t i = 0; i < PROGRAM_POINTS.size(); ++i) {
		const std::string prefix = std::to_string(i + 1) + ".";
		res.punkte[i].gl = get_record_trimmed(record, prefix + "gl");
		res.punkte[i].verse = get_record_trimmed(record, prefix + "verse");
		res.punkte[i].titel = get_record_trimmed(record, prefix + "titel");
	}
	return res;
}

LiedplanImportResult to_import_result(const std::vector<Record> &records)
{
	if (records.empty())
		return to_import_result(Record{});
	return to_import_result(records.back());
}

ImportError parse_csv_string(const std::string &text,
			     LiedplanImportResult &out_result,
			     std::size_t *out_count,
			     std::string *out_error)
{
	if (out_count)
		*out_count = 0;

	const auto table = parse_csv(text);
	if (table.empty()) {
		if (out_error)
			*out_error = kEmptyCsvMsg;
		return ImportError::Format;
	}

	const std::vector<std::string> &header = table.front();
	const std::vector<std::vector<std::string>> data_rows(table.begin() + 1, table.end());

	std::vector<Record> records;
	const ImportError err = parse_records(header, data_rows, records, out_error);
	if (err != ImportError::None)
		return err;

	if (out_count)
		*out_count = records.size();
	out_result = to_import_result(records);
	return ImportError::None;
}

ImportError parse_csv_file(const std::filesystem::path &path,
			   LiedplanImportResult &out_result,
			   std::size_t *out_count,
			   std::string *out_error)
{
	if (out_count)
		*out_count = 0;

	std::ifstream in(path, std::ios::binary);
	if (!in) {
		if (out_error)
			*out_error = "CSV-Datei konnte nicht gelesen werden: " + path.u8string();
		return ImportError::File;
	}

	std::ostringstream ss;
	ss << in.rdbuf();
	return parse_csv_string(ss.str(), out_result, out_count, out_error);
}

Record api_response_to_record(const nlohmann::json &response,
			      const nlohmann::json &title_map)
{
	Record record;
	std::string ts;
	if (response.is_object()) {
		if (response.contains("lastSubmittedTime") &&
		    response["lastSubmittedTime"].is_string()) {
			ts = response["lastSubmittedTime"].get<std::string>();
		}
		if (ts.empty() && response.contains("createTime") &&
		    response["createTime"].is_string()) {
			ts = response["createTime"].get<std::string>();
		}
	}
	record["zeitstempel"] = ts;

	if (!response.is_object() || !response.contains("answers") ||
	    !response["answers"].is_object()) {
		return record;
	}

	for (auto it = response["answers"].begin(); it != response["answers"].end(); ++it) {
		const std::string &question_id = it.key();
		const nlohmann::json &answer = it.value();

		std::string raw_title;
		if (title_map.is_object() && title_map.contains(question_id) &&
		    title_map[question_id].is_string()) {
			raw_title = title_map[question_id].get<std::string>();
		}
		const auto key = header_key(raw_title);
		if (!key)
			continue;

		std::string joined;
		if (answer.is_object() && answer.contains("textAnswers") &&
		    answer["textAnswers"].is_object()) {
			const auto &ta = answer["textAnswers"];
			if (ta.contains("answers") && ta["answers"].is_array()) {
				for (const auto &a : ta["answers"]) {
					if (a.is_object() && a.contains("value") &&
					    a["value"].is_string()) {
						const std::string val =
							a["value"].get<std::string>();
						if (!val.empty()) {
							if (!joined.empty())
								joined += ", ";
							joined += val;
						}
					}
				}
			}
		}
		record[*key] = std::move(joined);
	}

	return record;
}

std::optional<std::string> get_access_token(const std::filesystem::path &token_path,
					    const std::filesystem::path &cache_path,
					    std::string *out_error)
{
#ifndef _WIN32
	(void)token_path;
	(void)cache_path;
	if (out_error)
		*out_error = "Google-API wird nur unter Windows (WinHTTP) unterstützt.";
	return std::nullopt;
#else
	const auto now_epoch = std::chrono::duration_cast<std::chrono::seconds>(
				       std::chrono::system_clock::now().time_since_epoch())
				       .count();

	// 1. Access-Token-Cache prüfen (nur wiederverwenden, wenn expiry_unix - 60 > now())
	if (!cache_path.empty()) {
		std::error_code ec;
		if (std::filesystem::exists(cache_path, ec)) {
			std::ifstream cache_in(cache_path, std::ios::binary);
			if (cache_in) {
				nlohmann::json cj = nlohmann::json::parse(cache_in, nullptr, false);
				if (cj.is_object()) {
					const std::string cached_tok =
						cj.value("token", std::string{});
					const long long expiry = cj.value("expiry_unix", 0LL);
					if (!cached_tok.empty() && (expiry - 60 > now_epoch)) {
						return cached_tok;
					}
				}
			}
		}
	}

	// 2. Token-Datei NUR LESEN
	std::error_code ec;
	if (!std::filesystem::exists(token_path, ec)) {
		if (out_error)
			*out_error = "Google-Token-Datei nicht gefunden: " + token_path.u8string();
		return std::nullopt;
	}

	std::ifstream in(token_path, std::ios::binary);
	if (!in) {
		if (out_error)
			*out_error = "Google-Token-Datei konnte nicht gelesen werden: " +
				     token_path.u8string();
		return std::nullopt;
	}

	const nlohmann::json tj = nlohmann::json::parse(in, nullptr, false);
	if (!tj.is_object()) {
		if (out_error)
			*out_error = "Google-Token-Datei enthält ungültiges JSON: " +
				     token_path.u8string();
		return std::nullopt;
	}

	const std::string refresh_token = tj.value("refresh_token", std::string{});
	std::string client_id = tj.value("client_id", std::string{});
	std::string client_secret = tj.value("client_secret", std::string{});
	std::string token_uri = tj.value("token_uri", std::string{});
	if (token_uri.empty())
		token_uri = "https://oauth2.googleapis.com/token";

	// Fallback für client_id / client_secret (NUR LESEN)
	if (client_id.empty() || client_secret.empty()) {
		const std::filesystem::path fb_path =
			std::filesystem::u8path(kFallbackClientSecretPath);
		std::ifstream fb_in(fb_path, std::ios::binary);
		if (fb_in) {
			const nlohmann::json fbj = nlohmann::json::parse(fb_in, nullptr, false);
			if (fbj.is_object() && fbj.contains("installed") &&
			    fbj["installed"].is_object()) {
				const auto &inst = fbj["installed"];
				if (client_id.empty())
					client_id = inst.value("client_id", std::string{});
				if (client_secret.empty())
					client_secret = inst.value("client_secret", std::string{});
			}
		}
	}

	if (refresh_token.empty() || client_id.empty() || client_secret.empty()) {
		if (out_error)
			*out_error =
				"Google-Token unvollständig (refresh_token, client_id oder client_secret fehlt).";
		return std::nullopt;
	}

	const std::string post_body = "grant_type=refresh_token&refresh_token=" +
				      url_encode(refresh_token) +
				      "&client_id=" + url_encode(client_id) +
				      "&client_secret=" + url_encode(client_secret);

	const std::wstring headers = L"Content-Type: application/x-www-form-urlencoded\r\n";
	auto resp = winhttp_request("POST", token_uri, headers, post_body, out_error);
	if (!resp)
		return std::nullopt;

	if (resp->status < 200 || resp->status >= 300) {
		if (out_error)
			*out_error = format_http_error(resp->status);
		return std::nullopt;
	}

	const nlohmann::json rj = nlohmann::json::parse(resp->body, nullptr, false);
	if (!rj.is_object() || !rj.contains("access_token") || !rj["access_token"].is_string()) {
		if (out_error)
			*out_error = "Ungültige OAuth-Antwort vom Google-Token-Server.";
		return std::nullopt;
	}

	const std::string access_token = rj["access_token"].get<std::string>();
	const long long expires_in = rj.value("expires_in", 3600LL);

	// 3. Cache-Datei im Plugin-Datenverzeichnis aktualisieren
	if (!cache_path.empty() && !access_token.empty()) {
		std::error_code dir_ec;
		if (cache_path.has_parent_path())
			std::filesystem::create_directories(cache_path.parent_path(), dir_ec);
		std::ofstream cache_out(cache_path, std::ios::binary | std::ios::trunc);
		if (cache_out) {
			nlohmann::json out_j;
			out_j["token"] = access_token;
			out_j["expiry_unix"] = now_epoch + expires_in;
			cache_out << out_j.dump(2);
		}
	}

	return access_token;
#endif
}

std::optional<nlohmann::json> fetch_titles(const std::string &form_id,
					   const std::string &access_token,
					   std::string *out_error)
{
#ifndef _WIN32
	(void)form_id;
	(void)access_token;
	if (out_error)
		*out_error = "Google-API wird nur unter Windows (WinHTTP) unterstützt.";
	return std::nullopt;
#else
	const std::string url = "https://forms.googleapis.com/v1/forms/" + url_encode(form_id);
	const std::wstring headers =
		L"Authorization: Bearer " + utf8_to_wide(access_token) + L"\r\n";

	auto resp = winhttp_request("GET", url, headers, {}, out_error);
	if (!resp)
		return std::nullopt;

	if (resp->status < 200 || resp->status >= 300) {
		if (out_error)
			*out_error = format_http_error(resp->status);
		return std::nullopt;
	}

	const nlohmann::json form = nlohmann::json::parse(resp->body, nullptr, false);
	if (!form.is_object()) {
		if (out_error)
			*out_error = "Ungültige JSON-Antwort bei forms.get.";
		return std::nullopt;
	}

	nlohmann::json title_map = nlohmann::json::object();
	if (form.contains("items") && form["items"].is_array()) {
		for (const auto &item : form["items"]) {
			if (!item.is_object() || !item.contains("questionItem"))
				continue;
			const auto &qi = item["questionItem"];
			if (!qi.is_object() || !qi.contains("question"))
				continue;
			const auto &q = qi["question"];
			if (!q.is_object() || !q.contains("questionId") ||
			    !q["questionId"].is_string())
				continue;
			const std::string qid = q["questionId"].get<std::string>();
			const std::string title = item.value("title", std::string{});
			title_map[qid] = title;
		}
	}

	return title_map;
#endif
}

std::optional<std::vector<nlohmann::json>> fetch_responses(const std::string &form_id,
							   const std::string &access_token,
							   std::string *out_error)
{
#ifndef _WIN32
	(void)form_id;
	(void)access_token;
	if (out_error)
		*out_error = "Google-API wird nur unter Windows (WinHTTP) unterstützt.";
	return std::nullopt;
#else
	std::vector<nlohmann::json> all_responses;
	std::string page_token;
	const std::wstring headers =
		L"Authorization: Bearer " + utf8_to_wide(access_token) + L"\r\n";

	while (true) {
		std::string url = "https://forms.googleapis.com/v1/forms/" + url_encode(form_id) +
				  "/responses?pageSize=100";
		if (!page_token.empty())
			url += "&pageToken=" + url_encode(page_token);

		auto resp = winhttp_request("GET", url, headers, {}, out_error);
		if (!resp)
			return std::nullopt;

		if (resp->status < 200 || resp->status >= 300) {
			if (out_error)
				*out_error = format_http_error(resp->status);
			return std::nullopt;
		}

		const nlohmann::json body = nlohmann::json::parse(resp->body, nullptr, false);
		if (!body.is_object()) {
			if (out_error)
				*out_error = "Ungültige JSON-Antwort bei responses.list.";
			return std::nullopt;
		}

		if (body.contains("responses") && body["responses"].is_array()) {
			for (const auto &r : body["responses"]) {
				all_responses.push_back(r);
			}
		}

		page_token = body.value("nextPageToken", std::string{});
		if (page_token.empty())
			break;
	}

	return all_responses;
#endif
}

std::optional<LiedplanImportResult> import_from_api(std::string *out_error,
						    const std::filesystem::path &cache_path,
						    const std::filesystem::path &token_path,
						    const std::string &form_id)
{
	auto token = get_access_token(token_path, cache_path, out_error);
	if (!token)
		return std::nullopt;

	auto title_map = fetch_titles(form_id, *token, out_error);
	if (!title_map)
		return std::nullopt;

	auto responses = fetch_responses(form_id, *token, out_error);
	if (!responses)
		return std::nullopt;

	if (responses->empty()) {
		if (out_error)
			*out_error = kNoResponsesApiMsg;
		return std::nullopt;
	}

	auto response_time_key = [](const nlohmann::json &r) -> std::string {
		if (!r.is_object())
			return {};
		if (r.contains("lastSubmittedTime") && r["lastSubmittedTime"].is_string()) {
			const std::string s = r["lastSubmittedTime"].get<std::string>();
			if (!s.empty())
				return s;
		}
		if (r.contains("createTime") && r["createTime"].is_string())
			return r["createTime"].get<std::string>();
		return {};
	};

	std::sort(responses->begin(), responses->end(),
		  [&](const nlohmann::json &a, const nlohmann::json &b) {
			  return response_time_key(a) > response_time_key(b);
		  });

	const Record latest_record = api_response_to_record(responses->front(), *title_map);
	if (out_error)
		out_error->clear();
	return to_import_result(latest_record);
}

} // namespace liedplan_import
