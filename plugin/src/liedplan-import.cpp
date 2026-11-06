/*
 * liedplan-import.cpp — Implementierung des Liedplan-Imports (Forms-API, CSV, XLSX, ZIP) (v3).
 *
 * Zweck:
 *   - Parst Antworten aus Google Forms API JSON-Objekten.
 *   - Entpackt und liest ZIP-Archive und XLSX-Dateien über zlib / unz.
 *   - Parst CSV-Dateien mit robuster Erkennung von Trennzeichen, Anführungszeichen und Spalten.
 *   - Führt OAuth-Token-Erneuerung (Refresh Token) über WinHTTP durch (ohne externe Abhängigkeiten).
 *
 * Hinweis: Qt- und libobs-frei; wird auch in Standalone-Tests (test-liedplan-import) verwendet.
 */

#ifndef _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#endif

#include "liedplan-import.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <zlib.h>

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

// Normalisiert deutsche (DD.MM.YYYY ...) und ISO-Zeitstempel für lexikographische Sortierung (neueste zuerst).
std::string sortable_timestamp_key(std::string_view raw_ts)
{
	const std::string ts = trim_str(raw_ts);
	if (ts.empty())
		return {};

	std::size_t p = 0;
	int day = 0;
	int day_digits = 0;
	while (p < ts.size() && std::isdigit(static_cast<unsigned char>(ts[p])) && day_digits < 2) {
		day = day * 10 + (ts[p] - '0');
		++p;
		++day_digits;
	}
	if (day_digits > 0 && p < ts.size() && ts[p] == '.') {
		++p;
		int month = 0;
		int month_digits = 0;
		while (p < ts.size() && std::isdigit(static_cast<unsigned char>(ts[p])) &&
		       month_digits < 2) {
			month = month * 10 + (ts[p] - '0');
			++p;
			++month_digits;
		}
		if (month_digits > 0 && p < ts.size() && ts[p] == '.') {
			++p;
			int year = 0;
			int year_digits = 0;
			while (p < ts.size() && std::isdigit(static_cast<unsigned char>(ts[p])) &&
			       year_digits < 4) {
				year = year * 10 + (ts[p] - '0');
				++p;
				++year_digits;
			}
			if (year_digits == 4) {
				char buf[16]{};
				std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month, day);
				std::string rest = trim_str(std::string_view(ts).substr(p));
				if (rest.size() >= 2 &&
				    std::isdigit(static_cast<unsigned char>(rest[0])) &&
				    rest[1] == ':') {
					rest.insert(rest.begin(), '0');
				}
				return rest.empty() ? std::string(buf)
						    : (std::string(buf) + " " + rest);
			}
		}
	}
	return ts;
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
	// IMP-S: Rate-Limit und Serverfehler unterscheidbar machen.
	if (status == 429)
		return "Google-API-Fehler 429 — Rate Limit erreicht, erneut nach wenigen Sekunden";
	if (status >= 500)
		return "Google-API-Fehler " + std::to_string(status) + " — Serverfehler bei Google, erneut versuchen";
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
					    std::string *out_error,
					    bool require_https = false)
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

	// IMP-S: Token-/OAuth-Endpunkte verlangen HTTPS — ein downgrade auf http
	// wäre Credential-Klartext im Netz.
	if (require_https && uc.nScheme != INTERNET_SCHEME_HTTPS) {
		if (out_error)
			*out_error = "HTTPS für diese Anfrage erforderlich — Anfrage abgebrochen.";
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

// Konvertiert einen einzelnen Record in eine LiedplanService-Struktur (12 Programmpunkte).
LiedplanService to_service(const Record &record)
{
	LiedplanService svc;
	svc.woche = get_record_trimmed(record, "woche");
	svc.tag = get_record_trimmed(record, "tag");
	svc.organist = get_record_trimmed(record, "organist");
	svc.zeitstempel = get_record_trimmed(record, "zeitstempel");
	svc.punkte.resize(PROGRAM_POINTS.size());

	for (std::size_t i = 0; i < PROGRAM_POINTS.size(); ++i) {
		const std::string prefix = std::to_string(i + 1) + ".";
		svc.punkte[i].gl = get_record_trimmed(record, prefix + "gl");
		svc.punkte[i].verse = get_record_trimmed(record, prefix + "verse");
		svc.punkte[i].titel = get_record_trimmed(record, prefix + "titel");
	}
	return svc;
}

// Baut ein LiedplanImportResult aus einem einzelnen Gottesdienst (inkl. service und services).
LiedplanImportResult to_import_result(const LiedplanService &service)
{
	LiedplanImportResult res;
	res.woche = service.woche;
	res.tag = service.tag;
	res.organist = service.organist;
	res.zeitstempel = service.zeitstempel;
	res.punkte = service.punkte;
	res.service = service;
	res.services.push_back(service);
	return res;
}

LiedplanImportResult to_import_result(const Record &record)
{
	return to_import_result(to_service(record));
}

LiedplanImportResult to_import_result(const std::vector<Record> &records)
{
	if (records.empty())
		return to_import_result(Record{});

	// Alle gefundenen Gottesdienste sammeln und neueste zuerst nach Zeitstempel sortieren.
	std::vector<LiedplanService> services;
	services.reserve(records.size());
	for (auto it = records.rbegin(); it != records.rend(); ++it) {
		services.push_back(to_service(*it));
	}
	std::stable_sort(services.begin(), services.end(),
			 [](const LiedplanService &a, const LiedplanService &b) {
				 return sortable_timestamp_key(a.zeitstempel) >
					sortable_timestamp_key(b.zeitstempel);
			 });

	LiedplanImportResult res;
	res.services = std::move(services);
	res.service = res.services.front();
	res.woche = res.service.woche;
	res.tag = res.service.tag;
	res.organist = res.service.organist;
	res.zeitstempel = res.service.zeitstempel;
	res.punkte = res.service.punkte;
	return res;
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
	// IMP-S: harte Size-Cap — sonst OOM bei pathological großen Dateien.
	{
		std::error_code sz_ec;
		const auto sz = std::filesystem::file_size(path, sz_ec);
		if (!sz_ec && sz > 50u * 1024u * 1024u) {
			*out_error = "CSV-Datei zu groß (>50 MB): " + path.u8string();
			return ImportError::File;
		}
	}
	std::ostringstream ss;
	ss << in.rdbuf();
	return parse_csv_string(ss.str(), out_result, out_count, out_error);
}

namespace {

std::uint16_t read_le16(std::string_view data, std::size_t pos)
{
	return static_cast<std::uint16_t>(static_cast<unsigned char>(data[pos])) |
	       (static_cast<std::uint16_t>(static_cast<unsigned char>(data[pos + 1])) << 8);
}

std::uint32_t read_le32(std::string_view data, std::size_t pos)
{
	return static_cast<std::uint32_t>(static_cast<unsigned char>(data[pos])) |
	       (static_cast<std::uint32_t>(static_cast<unsigned char>(data[pos + 1])) << 8) |
	       (static_cast<std::uint32_t>(static_cast<unsigned char>(data[pos + 2])) << 16) |
	       (static_cast<std::uint32_t>(static_cast<unsigned char>(data[pos + 3])) << 24);
}

void write_le16(std::string &out, std::uint16_t val)
{
	out.push_back(static_cast<char>(val & 0xFF));
	out.push_back(static_cast<char>((val >> 8) & 0xFF));
}

void write_le32(std::string &out, std::uint32_t val)
{
	out.push_back(static_cast<char>(val & 0xFF));
	out.push_back(static_cast<char>((val >> 8) & 0xFF));
	out.push_back(static_cast<char>((val >> 16) & 0xFF));
	out.push_back(static_cast<char>((val >> 24) & 0xFF));
}

struct ZipMember {
	std::string name;
	std::uint16_t compression_method = 0;
	std::uint32_t compressed_size = 0;
	std::uint32_t uncompressed_size = 0;
	std::uint32_t local_header_offset = 0;
};

bool parse_zip_directory(std::string_view data, std::vector<ZipMember> &out_members)
{
	out_members.clear();
	if (data.size() < 22)
		return false;

	const std::size_t max_back = std::min<std::size_t>(data.size(), 65557);
	const std::size_t min_pos = data.size() - max_back;
	std::size_t eocd_pos = std::string_view::npos;

	for (std::size_t pos = data.size() - 22;; --pos) {
		if (static_cast<unsigned char>(data[pos]) == 0x50 &&
		    static_cast<unsigned char>(data[pos + 1]) == 0x4B &&
		    static_cast<unsigned char>(data[pos + 2]) == 0x05 &&
		    static_cast<unsigned char>(data[pos + 3]) == 0x06) {
			const std::uint16_t comment_len = read_le16(data, pos + 20);
			if (pos + 22 + comment_len <= data.size()) {
				eocd_pos = pos;
				break;
			}
		}
		if (pos == min_pos)
			break;
	}

	if (eocd_pos == std::string_view::npos)
		return false;

	const std::uint16_t total_entries = read_le16(data, eocd_pos + 10);
	const std::uint32_t cd_size = read_le32(data, eocd_pos + 12);
	const std::uint32_t cd_offset = read_le32(data, eocd_pos + 16);

	if (static_cast<std::uint64_t>(cd_offset) + static_cast<std::uint64_t>(cd_size) > data.size())
		return false;

	std::size_t pos = cd_offset;
	const std::size_t cd_end = cd_offset + cd_size;

	for (std::uint16_t i = 0; i < total_entries && pos < cd_end; ++i) {
		if (pos + 46 > data.size())
			return false;
		if (static_cast<unsigned char>(data[pos]) != 0x50 ||
		    static_cast<unsigned char>(data[pos + 1]) != 0x4B ||
		    static_cast<unsigned char>(data[pos + 2]) != 0x01 ||
		    static_cast<unsigned char>(data[pos + 3]) != 0x02) {
			return false;
		}

		ZipMember m;
		m.compression_method = read_le16(data, pos + 10);
		m.compressed_size = read_le32(data, pos + 20);
		m.uncompressed_size = read_le32(data, pos + 24);
		const std::uint16_t name_len = read_le16(data, pos + 28);
		const std::uint16_t extra_len = read_le16(data, pos + 30);
		const std::uint16_t comment_len = read_le16(data, pos + 32);
		m.local_header_offset = read_le32(data, pos + 42);

		const std::size_t next_pos = pos + 46 + name_len + extra_len + comment_len;
		if (next_pos > data.size())
			return false;

		m.name = std::string(data.substr(pos + 46, name_len));
		out_members.push_back(std::move(m));
		pos = next_pos;
	}

	return true;
}

// Hartes Decompress-Cap (64 MB) gegen Zip-Bomben pro Eintrag und über das gesamte Archiv.
constexpr size_t kMaxInflateBytes = 64 * 1024 * 1024;

std::optional<std::string> inflate_data_impl(std::string_view compressed,
					     std::size_t uncompressed_hint,
					     int window_bits,
					     bool *out_limit_exceeded)
{
	if (out_limit_exceeded)
		*out_limit_exceeded = false;

	z_stream zs{};
	zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.data()));
	zs.avail_in = static_cast<uInt>(compressed.size());

	if (inflateInit2(&zs, window_bits) != Z_OK)
		return std::nullopt;

	std::string out;
	if (uncompressed_hint > 0 && uncompressed_hint <= kMaxInflateBytes)
		out.reserve(uncompressed_hint);

	char buf[16384];
	int ret = Z_OK;
	while (ret == Z_OK) {
		zs.next_out = reinterpret_cast<Bytef *>(buf);
		zs.avail_out = static_cast<uInt>(sizeof(buf));
		ret = inflate(&zs, Z_NO_FLUSH);
		if (ret != Z_OK && ret != Z_STREAM_END) {
			inflateEnd(&zs);
			return std::nullopt;
		}
		const std::size_t have = sizeof(buf) - zs.avail_out;
		if (out.size() + have > kMaxInflateBytes) {
			if (out_limit_exceeded)
				*out_limit_exceeded = true;
			inflateEnd(&zs);
			return std::nullopt;
		}
		out.append(buf, have);
	}
	inflateEnd(&zs);
	return out;
}

} // namespace

std::optional<std::string> inflate_data(std::string_view compressed,
					std::size_t uncompressed_hint,
					int window_bits)
{
	return inflate_data_impl(compressed, uncompressed_hint, window_bits, nullptr);
}

namespace {

std::optional<std::string> extract_zip_member(std::string_view data,
					      const ZipMember &m,
					      bool *out_limit_exceeded = nullptr)
{
	if (out_limit_exceeded)
		*out_limit_exceeded = false;

	const std::uint64_t lhoff = m.local_header_offset;
	if (lhoff + 30 > data.size())
		return std::nullopt;

	if (static_cast<unsigned char>(data[static_cast<std::size_t>(lhoff)]) != 0x50 ||
	    static_cast<unsigned char>(data[static_cast<std::size_t>(lhoff + 1)]) != 0x4B ||
	    static_cast<unsigned char>(data[static_cast<std::size_t>(lhoff + 2)]) != 0x03 ||
	    static_cast<unsigned char>(data[static_cast<std::size_t>(lhoff + 3)]) != 0x04) {
		return std::nullopt;
	}

	const std::uint16_t lf_name_len = read_le16(data, static_cast<std::size_t>(lhoff + 26));
	const std::uint16_t lf_extra_len = read_le16(data, static_cast<std::size_t>(lhoff + 28));
	const std::uint64_t data_offset = lhoff + 30 + lf_name_len + lf_extra_len;

	if (data_offset + m.compressed_size > data.size())
		return std::nullopt;

	const std::string_view comp_slice =
		data.substr(static_cast<std::size_t>(data_offset), m.compressed_size);

	if (m.compression_method == 0) {
		if (comp_slice.size() > kMaxInflateBytes) {
			if (out_limit_exceeded)
				*out_limit_exceeded = true;
			return std::nullopt;
		}
		return std::string(comp_slice);
	}
	if (m.compression_method == 8) {
		if (comp_slice.empty() && m.uncompressed_size == 0)
			return std::string{};
		bool limit_raw = false;
		if (auto res = inflate_data_impl(comp_slice, m.uncompressed_size, -MAX_WBITS,
						 &limit_raw))
			return res;
		if (limit_raw) {
			if (out_limit_exceeded)
				*out_limit_exceeded = true;
			return std::nullopt;
		}
		return inflate_data_impl(comp_slice, m.uncompressed_size, MAX_WBITS,
					 out_limit_exceeded);
	}
	return std::nullopt;
}

// Entpackt alle ZIP-Einträge unter Einhaltung eines gemeinsamen 64-MB-Gesamtbudgets.
bool extract_zip_entries(std::string_view data,
			 const std::vector<ZipMember> &members,
			 std::unordered_map<std::string, std::string> &out_entries,
			 std::string *out_error)
{
	out_entries.clear();
	std::size_t total_uncompressed = 0;
	for (const auto &m : members) {
		if (m.uncompressed_size > kMaxInflateBytes ||
		    total_uncompressed + m.uncompressed_size > kMaxInflateBytes) {
			if (out_error)
				*out_error = "ZIP-Archiv zu groß (Zip-Bomb-Schutz)";
			return false;
		}
		bool limit_exceeded = false;
		auto extracted = extract_zip_member(data, m, &limit_exceeded);
		if (!extracted) {
			if (limit_exceeded && out_error)
				*out_error = "ZIP-Archiv zu groß (Zip-Bomb-Schutz)";
			return false;
		}
		if (total_uncompressed + extracted->size() > kMaxInflateBytes) {
			if (out_error)
				*out_error = "ZIP-Archiv zu groß (Zip-Bomb-Schutz)";
			return false;
		}
		total_uncompressed += extracted->size();
		out_entries[m.name] = std::move(*extracted);
	}
	return true;
}

std::string normalize_zip_entry_name(std::string_view name)
{
	std::string out;
	out.reserve(name.size());
	for (char c : name) {
		out.push_back(c == '\\' ? '/' : c);
	}
	while (!out.empty() && out.front() == '/')
		out.erase(out.begin());
	return out;
}

void append_utf8_codepoint(std::string &out, std::uint32_t cp)
{
	// UTF-16-Surrogate-Codepoints (U+D800..U+DFFF) sind in UTF-8 ungültig -> U+FFFD (EF BF BD)
	if (cp >= 0xD800 && cp <= 0xDFFF) {
		cp = 0xFFFD;
	}
	if (cp <= 0x7F) {
		out.push_back(static_cast<char>(cp));
	} else if (cp <= 0x7FF) {
		out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else if (cp <= 0xFFFF) {
		out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else if (cp <= 0x10FFFF) {
		out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
}

bool is_tag_delim(char c)
{
	return c == '>' || c == '/' || c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::size_t find_xml_tag(std::string_view xml, std::string_view tag_name, std::size_t start_pos)
{
	const std::string needle = "<" + std::string(tag_name);
	std::size_t pos = start_pos;
	while (true) {
		pos = xml.find(needle, pos);
		if (pos == std::string_view::npos)
			return std::string_view::npos;
		const std::size_t after = pos + needle.size();
		if (after < xml.size() && is_tag_delim(xml[after]))
			return pos;
		pos += 1;
	}
}

std::string extract_xml_attr(std::string_view tag_header, std::string_view attr_name)
{
	std::size_t pos = 0;
	while (pos < tag_header.size()) {
		pos = tag_header.find(attr_name, pos);
		if (pos == std::string_view::npos)
			return {};
		const bool valid_before =
			(pos == 0) || is_ascii_space(static_cast<unsigned char>(tag_header[pos - 1]));
		std::size_t eq_pos = pos + attr_name.size();
		while (eq_pos < tag_header.size() &&
		       is_ascii_space(static_cast<unsigned char>(tag_header[eq_pos])))
			++eq_pos;
		if (valid_before && eq_pos < tag_header.size() && tag_header[eq_pos] == '=') {
			std::size_t q_pos = eq_pos + 1;
			while (q_pos < tag_header.size() &&
			       is_ascii_space(static_cast<unsigned char>(tag_header[q_pos])))
				++q_pos;
			if (q_pos < tag_header.size() &&
			    (tag_header[q_pos] == '"' || tag_header[q_pos] == '\'')) {
				const char quote = tag_header[q_pos];
				const std::size_t end_q = tag_header.find(quote, q_pos + 1);
				if (end_q != std::string_view::npos)
					return std::string(tag_header.substr(q_pos + 1, end_q - (q_pos + 1)));
			}
		}
		pos += attr_name.size();
	}
	return {};
}

std::string extract_all_t_elements(std::string_view xml)
{
	std::string result;
	std::size_t pos = 0;
	while (true) {
		const std::size_t t_start = find_xml_tag(xml, "t", pos);
		if (t_start == std::string_view::npos)
			break;
		const std::size_t t_gt = xml.find('>', t_start + 2);
		if (t_gt == std::string_view::npos)
			break;
		if (t_gt > t_start && xml[t_gt - 1] == '/') {
			pos = t_gt + 1;
			continue;
		}
		const std::size_t t_close = xml.find("</t>", t_gt + 1);
		if (t_close == std::string_view::npos)
			break;
		result += decode_xml_entities(xml.substr(t_gt + 1, t_close - (t_gt + 1)));
		pos = t_close + 4;
	}
	return result;
}

std::string extract_first_v_element(std::string_view xml)
{
	const std::size_t v_start = find_xml_tag(xml, "v", 0);
	if (v_start == std::string_view::npos)
		return {};
	const std::size_t v_gt = xml.find('>', v_start + 2);
	if (v_gt == std::string_view::npos || (v_gt > v_start && xml[v_gt - 1] == '/'))
		return {};
	const std::size_t v_close = xml.find("</v>", v_gt + 1);
	if (v_close == std::string_view::npos)
		return {};
	return decode_xml_entities(xml.substr(v_gt + 1, v_close - (v_gt + 1)));
}

std::vector<std::string> parse_shared_strings_xml(std::string_view xml)
{
	std::vector<std::string> strings;
	std::size_t pos = 0;
	while (true) {
		const std::size_t si_start = find_xml_tag(xml, "si", pos);
		if (si_start == std::string_view::npos)
			break;
		const std::size_t si_gt = xml.find('>', si_start + 3);
		if (si_gt == std::string_view::npos)
			break;
		if (si_gt > si_start && xml[si_gt - 1] == '/') {
			strings.emplace_back();
			pos = si_gt + 1;
			continue;
		}
		const std::size_t si_close = xml.find("</si>", si_gt + 1);
		if (si_close == std::string_view::npos)
			break;
		strings.push_back(extract_all_t_elements(xml.substr(si_gt + 1, si_close - (si_gt + 1))));
		pos = si_close + 5;
	}
	return strings;
}

int parse_col_index_from_ref(std::string_view r_attr)
{
	int col = 0;
	bool has_letters = false;
	for (char c : r_attr) {
		if (c >= 'A' && c <= 'Z') {
			col = col * 26 + (c - 'A' + 1);
			has_letters = true;
		} else if (c >= 'a' && c <= 'z') {
			col = col * 26 + (c - 'a' + 1);
			has_letters = true;
		} else {
			break;
		}
	}
	return has_letters ? (col - 1) : -1;
}

std::vector<std::vector<std::string>> parse_sheet_xml(
	std::string_view xml,
	const std::vector<std::string> &shared_strings)
{
	std::vector<std::vector<std::string>> rows;
	std::size_t pos = 0;
	while (true) {
		const std::size_t row_start = find_xml_tag(xml, "row", pos);
		if (row_start == std::string_view::npos)
			break;
		const std::size_t row_gt = xml.find('>', row_start + 4);
		if (row_gt == std::string_view::npos)
			break;
		if (row_gt > row_start && xml[row_gt - 1] == '/') {
			pos = row_gt + 1;
			continue;
		}
		const std::size_t row_close = xml.find("</row>", row_gt + 1);
		if (row_close == std::string_view::npos)
			break;

		const std::string_view row_inner = xml.substr(row_gt + 1, row_close - (row_gt + 1));
		std::vector<std::string> row_cells;
		int next_col = 0;
		std::size_t c_pos = 0;

		while (true) {
			const std::size_t c_start = find_xml_tag(row_inner, "c", c_pos);
			if (c_start == std::string_view::npos)
				break;
			const std::size_t c_gt = row_inner.find('>', c_start + 2);
			if (c_gt == std::string_view::npos)
				break;

			const std::string_view c_header = row_inner.substr(c_start + 2, c_gt - (c_start + 2));
			const std::string r_attr = extract_xml_attr(c_header, "r");
			const std::string t_attr = extract_xml_attr(c_header, "t");

			int col_idx = parse_col_index_from_ref(r_attr);
			if (col_idx < 0)
				col_idx = next_col;
			next_col = col_idx + 1;

			std::string cell_val;
			if (c_gt > c_start && row_inner[c_gt - 1] == '/') {
				c_pos = c_gt + 1;
			} else {
				const std::size_t c_close = row_inner.find("</c>", c_gt + 1);
				if (c_close == std::string_view::npos)
					break;
				const std::string_view c_inner =
					row_inner.substr(c_gt + 1, c_close - (c_gt + 1));

				if (t_attr == "s") {
					const std::string idx_str = trim_str(extract_first_v_element(c_inner));
					if (!idx_str.empty()) {
						try {
							const std::size_t s_idx = std::stoull(idx_str);
							if (s_idx < shared_strings.size())
								cell_val = shared_strings[s_idx];
						} catch (...) {
						}
					}
				} else if (t_attr == "inlineStr") {
					cell_val = extract_all_t_elements(c_inner);
				} else {
					cell_val = extract_first_v_element(c_inner);
					if (cell_val.empty() && find_xml_tag(c_inner, "is", 0) != std::string_view::npos) {
						cell_val = extract_all_t_elements(c_inner);
					}
				}
				c_pos = c_close + 4;
			}

			if (static_cast<std::size_t>(col_idx) >= row_cells.size())
				row_cells.resize(static_cast<std::size_t>(col_idx) + 1);
			row_cells[static_cast<std::size_t>(col_idx)] = std::move(cell_val);
		}

		if (row_has_content(row_cells))
			rows.push_back(std::move(row_cells));

		pos = row_close + 6;
	}
	return rows;
}

} // namespace

std::string decode_xml_entities(std::string_view raw)
{
	std::string out;
	out.reserve(raw.size());

	for (std::size_t i = 0; i < raw.size(); ++i) {
		if (raw[i] == '&') {
			const std::size_t semi = raw.find(';', i + 1);
			if (semi != std::string_view::npos && semi > i + 1 && (semi - i) <= 12) {
				const std::string_view ent = raw.substr(i + 1, semi - (i + 1));
				if (ent == "amp") {
					out.push_back('&');
					i = semi;
					continue;
				}
				if (ent == "lt") {
					out.push_back('<');
					i = semi;
					continue;
				}
				if (ent == "gt") {
					out.push_back('>');
					i = semi;
					continue;
				}
				if (ent == "quot") {
					out.push_back('"');
					i = semi;
					continue;
				}
				if (ent == "apos") {
					out.push_back('\'');
					i = semi;
					continue;
				}
				if (ent.size() >= 2 && ent[0] == '#') {
					std::uint32_t cp = 0;
					bool valid = true;
					if (ent[1] == 'x' || ent[1] == 'X') {
						if (ent.size() < 3)
							valid = false;
						for (std::size_t k = 2; k < ent.size(); ++k) {
							const char hc = ent[k];
							if (hc >= '0' && hc <= '9')
								cp = (cp << 4) | static_cast<std::uint32_t>(hc - '0');
							else if (hc >= 'a' && hc <= 'f')
								cp = (cp << 4) | static_cast<std::uint32_t>(hc - 'a' + 10);
							else if (hc >= 'A' && hc <= 'F')
								cp = (cp << 4) | static_cast<std::uint32_t>(hc - 'A' + 10);
							else {
								valid = false;
								break;
							}
						}
					} else {
						for (std::size_t k = 1; k < ent.size(); ++k) {
							const char dc = ent[k];
							if (dc >= '0' && dc <= '9')
								cp = cp * 10 + static_cast<std::uint32_t>(dc - '0');
							else {
								valid = false;
								break;
							}
						}
					}
					if (valid && cp > 0 && cp <= 0x10FFFF) {
						append_utf8_codepoint(out, cp);
						i = semi;
						continue;
					}
				}
			}
		}
		out.push_back(raw[i]);
	}

	return out;
}

ImportError parse_xlsx_bytes(std::string_view data,
			     LiedplanImportResult &out_result,
			     std::size_t *out_count,
			     std::string *out_error)
{
	if (out_count)
		*out_count = 0;

	std::vector<ZipMember> members;
	if (!parse_zip_directory(data, members)) {
		if (out_error)
			*out_error = "Die XLSX-Datei ist beschädigt oder kein gültiges Archiv.";
		return ImportError::Format;
	}

	const ZipMember *sheet_member = nullptr;
	const ZipMember *sst_member = nullptr;
	for (const auto &m : members) {
		const std::string norm = normalize_zip_entry_name(m.name);
		if (norm == "xl/worksheets/sheet1.xml")
			sheet_member = &m;
		else if (norm == "xl/sharedStrings.xml")
			sst_member = &m;
	}

	if (!sheet_member) {
		if (out_error)
			*out_error = "Die XLSX-Datei enthält kein Arbeitsblatt (xl/worksheets/sheet1.xml).";
		return ImportError::Format;
	}

	std::unordered_map<std::string, std::string> extracted_entries;
	std::string zip_err;
	if (!extract_zip_entries(data, members, extracted_entries, &zip_err)) {
		if (out_error) {
			*out_error = !zip_err.empty()
					     ? zip_err
					     : "Die XLSX-Datei konnte nicht entpackt werden.";
		}
		return ImportError::Format;
	}

	std::vector<std::string> shared_strings;
	if (sst_member) {
		auto sst_it = extracted_entries.find(sst_member->name);
		if (sst_it == extracted_entries.end()) {
			if (out_error)
				*out_error = "sharedStrings.xml in der XLSX-Datei konnte nicht entpackt werden.";
			return ImportError::Format;
		}
		shared_strings = parse_shared_strings_xml(sst_it->second);
	}

	auto sheet_it = extracted_entries.find(sheet_member->name);
	if (sheet_it == extracted_entries.end()) {
		if (out_error)
			*out_error = "sheet1.xml in der XLSX-Datei konnte nicht entpackt werden.";
		return ImportError::Format;
	}

	const auto rows = parse_sheet_xml(sheet_it->second, shared_strings);
	if (rows.empty()) {
		if (out_error)
			*out_error = "Die XLSX-Datei ist leer.";
		return ImportError::Format;
	}

	const std::vector<std::string> &header = rows.front();
	const std::vector<std::vector<std::string>> data_rows(rows.begin() + 1, rows.end());

	std::vector<Record> records;
	const ImportError err = parse_records(header, data_rows, records, out_error);
	if (err != ImportError::None)
		return err;

	if (out_count)
		*out_count = records.size();
	out_result = to_import_result(records);
	return ImportError::None;
}

ImportError parse_xlsx_file(const std::filesystem::path &path,
			    LiedplanImportResult &out_result,
			    std::size_t *out_count,
			    std::string *out_error)
{
	if (out_count)
		*out_count = 0;

	std::ifstream in(path, std::ios::binary);
	if (!in) {
		if (out_error)
			*out_error = "XLSX-Datei konnte nicht gelesen werden: " + path.u8string();
		return ImportError::File;
	}
	// IMP-S: Size-Cap gegen pathological große Dateien (OOM).
	{
		std::error_code sz_ec;
		const auto sz = std::filesystem::file_size(path, sz_ec);
		if (!sz_ec && sz > 50u * 1024u * 1024u) {
			*out_error = "XLSX-Datei zu groß (>50 MB): " + path.u8string();
			return ImportError::File;
		}
	}
	std::ostringstream ss;
	ss << in.rdbuf();
	const std::string bytes = ss.str();
	return parse_xlsx_bytes(bytes, out_result, out_count, out_error);
}

ImportError parse_zip_bytes(std::string_view data,
			    LiedplanImportResult &out_result,
			    std::size_t *out_count,
			    std::string *out_error)
{
	if (out_count)
		*out_count = 0;

	std::vector<ZipMember> members;
	if (!parse_zip_directory(data, members)) {
		if (out_error)
			*out_error = "Die ZIP-Datei ist beschädigt oder kein gültiges Archiv.";
		return ImportError::Format;
	}

	std::vector<std::string> candidate_names;
	for (const auto &m : members) {
		const std::string lower_name = utf8_casefold(m.name);
		if (lower_name.size() < 4 ||
		    lower_name.substr(lower_name.size() - 4) != ".csv") {
			continue;
		}
		std::string basename = m.name;
		const std::size_t slash = basename.find_last_of("/\\");
		if (slash != std::string::npos)
			basename = basename.substr(slash + 1);
		if (basename.empty() || basename.rfind(".", 0) == 0 || basename.rfind("__", 0) == 0)
			continue;
		candidate_names.push_back(m.name);
	}

	std::sort(candidate_names.begin(), candidate_names.end());
	if (candidate_names.empty()) {
		if (out_error)
			*out_error = "Die ZIP-Datei enthält keine CSV-Datei.";
		return ImportError::Format;
	}

	const std::string &chosen_name = candidate_names.front();
	const ZipMember *chosen_member = nullptr;
	for (const auto &m : members) {
		if (m.name == chosen_name) {
			chosen_member = &m;
			break;
		}
	}

	if (!chosen_member) {
		if (out_error)
			*out_error = "Die ZIP-Datei enthält keine CSV-Datei.";
		return ImportError::Format;
	}

	std::unordered_map<std::string, std::string> extracted_entries;
	std::string zip_err;
	if (!extract_zip_entries(data, members, extracted_entries, &zip_err)) {
		if (out_error) {
			*out_error = !zip_err.empty()
					     ? zip_err
					     : "Die CSV-Datei im ZIP-Archiv konnte nicht entpackt werden.";
		}
		return ImportError::Format;
	}

	auto csv_it = extracted_entries.find(chosen_member->name);
	if (csv_it == extracted_entries.end()) {
		if (out_error)
			*out_error = "Die CSV-Datei im ZIP-Archiv konnte nicht entpackt werden.";
		return ImportError::Format;
	}

	return parse_csv_string(csv_it->second, out_result, out_count, out_error);
}

ImportError parse_zip_file(const std::filesystem::path &path,
			   LiedplanImportResult &out_result,
			   std::size_t *out_count,
			   std::string *out_error)
{
	if (out_count)
		*out_count = 0;

	std::ifstream in(path, std::ios::binary);
	if (!in) {
		if (out_error)
			*out_error = "ZIP-Datei konnte nicht gelesen werden: " + path.u8string();
		return ImportError::File;
	}
	// IMP-S: Size-Cap gegen pathological große Dateien (OOM).
	{
		std::error_code sz_ec;
		const auto sz = std::filesystem::file_size(path, sz_ec);
		if (!sz_ec && sz > 100u * 1024u * 1024u) {
			if (out_error)
				*out_error = "ZIP-Datei zu groß (>100 MB): " + path.u8string();
			return ImportError::File;
		}
	}
	std::ostringstream ss;
	ss << in.rdbuf();
	const std::string bytes = ss.str();
	return parse_zip_bytes(bytes, out_result, out_count, out_error);
}

ImportError parse_answers(std::string_view data,
			  std::string_view filename,
			  LiedplanImportResult &out_result,
			  std::size_t *out_count,
			  std::string *out_error)
{
	if (out_count)
		*out_count = 0;

	const std::filesystem::path p = std::filesystem::u8path(std::string(filename));
	const std::string ext = utf8_casefold(p.extension().u8string());

	if (ext == ".csv")
		return parse_csv_string(std::string(data), out_result, out_count, out_error);
	if (ext == ".xlsx")
		return parse_xlsx_bytes(data, out_result, out_count, out_error);
	if (ext == ".zip")
		return parse_zip_bytes(data, out_result, out_count, out_error);

	if (out_error) {
		*out_error = "Nicht unterstütztes Format „" +
			     (ext.empty() ? std::string("?") : ext) +
			     "“. Bitte CSV, XLSX oder ZIP hochladen.";
	}
	return ImportError::Format;
}

ImportError parse_answers_file(const std::filesystem::path &path,
			       LiedplanImportResult &out_result,
			       std::size_t *out_count,
			       std::string *out_error)
{
	if (out_count)
		*out_count = 0;

	const std::string ext = utf8_casefold(path.extension().u8string());
	if (ext == ".csv")
		return parse_csv_file(path, out_result, out_count, out_error);
	if (ext == ".xlsx")
		return parse_xlsx_file(path, out_result, out_count, out_error);
	if (ext == ".zip")
		return parse_zip_file(path, out_result, out_count, out_error);

	if (out_error) {
		*out_error = "Nicht unterstütztes Format „" +
			     (ext.empty() ? std::string("?") : ext) +
			     "“. Bitte CSV, XLSX oder ZIP hochladen.";
	}
	return ImportError::Format;
}

std::string build_zip_archive(const std::vector<std::pair<std::string, std::string>> &entries,
			      bool use_deflate)
{
	struct BuiltEntry {
		std::string name;
		std::uint16_t method = 0;
		std::uint32_t crc = 0;
		std::uint32_t comp_size = 0;
		std::uint32_t uncomp_size = 0;
		std::uint32_t local_offset = 0;
	};

	std::string archive;
	std::vector<BuiltEntry> dir;
	dir.reserve(entries.size());

	for (const auto &kv : entries) {
		const std::string &name = kv.first;
		const std::string &raw = kv.second;

		BuiltEntry be;
		be.name = name;
		be.uncomp_size = static_cast<std::uint32_t>(raw.size());
		be.crc = static_cast<std::uint32_t>(
			crc32(0L, reinterpret_cast<const Bytef *>(raw.data()),
			      static_cast<uInt>(raw.size())));
		be.local_offset = static_cast<std::uint32_t>(archive.size());

		std::string payload;
		if (use_deflate) {
			z_stream zs{};
			if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
					 Z_DEFAULT_STRATEGY) == Z_OK) {
				zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(raw.data()));
				zs.avail_in = static_cast<uInt>(raw.size());
				char buf[16384];
				int ret = Z_OK;
				while (ret == Z_OK) {
					zs.next_out = reinterpret_cast<Bytef *>(buf);
					zs.avail_out = static_cast<uInt>(sizeof(buf));
					ret = deflate(&zs, Z_FINISH);
					if (ret != Z_OK && ret != Z_STREAM_END)
						break;
					payload.append(buf, sizeof(buf) - zs.avail_out);
				}
				deflateEnd(&zs);
				be.method = 8;
			} else {
				payload = raw;
				be.method = 0;
			}
		} else {
			payload = raw;
			be.method = 0;
		}
		be.comp_size = static_cast<std::uint32_t>(payload.size());

		// Local file header (PK\x03\x04)
		archive.append("PK\x03\x04", 4);
		write_le16(archive, 20); // version needed
		write_le16(archive, 0);  // flags
		write_le16(archive, be.method);
		write_le16(archive, 0);  // mod time
		write_le16(archive, 0);  // mod date
		write_le32(archive, be.crc);
		write_le32(archive, be.comp_size);
		write_le32(archive, be.uncomp_size);
		write_le16(archive, static_cast<std::uint16_t>(be.name.size()));
		write_le16(archive, 0);  // extra len
		archive.append(be.name);
		archive.append(payload);

		dir.push_back(std::move(be));
	}

	const std::uint32_t cd_offset = static_cast<std::uint32_t>(archive.size());
	for (const auto &be : dir) {
		// Central directory header (PK\x01\x02)
		archive.append("PK\x01\x02", 4);
		write_le16(archive, 20); // version made by
		write_le16(archive, 20); // version needed
		write_le16(archive, 0);  // flags
		write_le16(archive, be.method);
		write_le16(archive, 0);  // mod time
		write_le16(archive, 0);  // mod date
		write_le32(archive, be.crc);
		write_le32(archive, be.comp_size);
		write_le32(archive, be.uncomp_size);
		write_le16(archive, static_cast<std::uint16_t>(be.name.size()));
		write_le16(archive, 0);  // extra len
		write_le16(archive, 0);  // comment len
		write_le16(archive, 0);  // disk number
		write_le16(archive, 0);  // internal attrs
		write_le32(archive, 0);  // external attrs
		write_le32(archive, be.local_offset);
		archive.append(be.name);
	}
	const std::uint32_t cd_size = static_cast<std::uint32_t>(archive.size()) - cd_offset;

	// EOCD (PK\x05\x06)
	archive.append("PK\x05\x06", 4);
	write_le16(archive, 0); // disk number
	write_le16(archive, 0); // start disk
	write_le16(archive, static_cast<std::uint16_t>(dir.size()));
	write_le16(archive, static_cast<std::uint16_t>(dir.size()));
	write_le32(archive, cd_size);
	write_le32(archive, cd_offset);
	write_le16(archive, 0); // comment len

	return archive;
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
	// IMP-S: Token-Refresh nur über HTTPS — Credential-Klartext im POST-Body.
	auto resp = winhttp_request("POST", token_uri, headers, post_body, out_error, true);
	if (!resp)
		return std::nullopt;

	if (resp->status < 200 || resp->status >= 300) {
		if (out_error) {
			std::string detail;
			const nlohmann::json ej = nlohmann::json::parse(resp->body, nullptr, false);
			if (ej.is_object()) {
				const std::string err = ej.value("error", std::string{});
				const std::string desc = ej.value("error_description", std::string{});
				if (!err.empty())
					detail = " (" + err + (desc.empty() ? "" : ": " + desc) + ")";
			}
			*out_error = format_http_error(resp->status) + detail;
			if (resp->status == 400 && detail.find("invalid_grant") != std::string::npos)
				*out_error += " — Google-Token abgelaufen/revoked, OAuth-Re-Authorisierung nötig.";
		}
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

	auto resp = winhttp_request("GET", url, headers, {}, out_error, true);
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

		auto resp = winhttp_request("GET", url, headers, {}, out_error, true);
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

	// Alle Antworten aus dem Formular in services übernehmen (neueste zuerst).
	LiedplanImportResult res;
	res.services.reserve(responses->size());
	for (const auto &resp_json : *responses) {
		res.services.push_back(to_service(api_response_to_record(resp_json, *title_map)));
	}
	res.service = res.services.front();
	res.woche = res.service.woche;
	res.tag = res.service.tag;
	res.organist = res.service.organist;
	res.zeitstempel = res.service.zeitstempel;
	res.punkte = res.service.punkte;
	if (out_error)
		out_error->clear();
	return res;
}

} // namespace liedplan_import
