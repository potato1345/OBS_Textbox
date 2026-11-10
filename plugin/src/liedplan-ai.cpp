/*
 * liedplan-ai.cpp — Implementierung der KI- und Offline-Dokument-Analyse (v3).
 *
 * Zweck:
 *   - Ruft markitdown als Python-Subprozess auf, um Text aus PDF/Word/Bild-Dateien zu extrahieren.
 *   - Fragt OpenRouter nach kostenlosen Modellen ab (:free) und steuert Abfrage-Wiederholungen.
 *   - Säubert und parst JSON-Modellausgaben robust aus Markdown-Umgebungen.
 *   - Bietet einen deterministischen Regex-Offline-Parser bei Ausfall der KI-Schnittstelle.
 *   - Gleicht Nummern mit der Referenzdatei ab und ergänzt offizielle Titel.
 *
 * Hinweis: Qt- und libobs-frei; wird auch in Standalone-Tests (test-liedplan-ai) verwendet.
 */

#ifndef _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#endif

#include "liedplan-ai.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>

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

namespace liedplan_ai {

namespace {

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

std::vector<std::string> split_char(std::string_view sv, char delim)
{
	std::vector<std::string> parts;
	std::size_t start = 0;
	while (true) {
		const std::size_t pos = sv.find(delim, start);
		if (pos == std::string_view::npos) {
			parts.emplace_back(sv.substr(start));
			break;
		}
		parts.emplace_back(sv.substr(start, pos - start));
		start = pos + 1;
	}
	return parts;
}

std::string strip_leading_commas_spaces(std::string_view sv)
{
	std::size_t pos = 0;
	while (pos < sv.size()) {
		const unsigned char c = static_cast<unsigned char>(sv[pos]);
		if (c == ',' || is_ascii_space(c)) {
			++pos;
		} else if (pos + 1 < sv.size() && c == 0xC2 &&
			   static_cast<unsigned char>(sv[pos + 1]) == 0xA0) {
			pos += 2;
		} else {
			break;
		}
	}
	return trim_str(sv.substr(pos));
}

std::string strip_trailing_commas_spaces(std::string_view sv)
{
	std::size_t end = sv.size();
	while (end > 0) {
		const unsigned char c = static_cast<unsigned char>(sv[end - 1]);
		if (c == ',' || is_ascii_space(c)) {
			--end;
		} else if (end >= 2 && static_cast<unsigned char>(sv[end - 2]) == 0xC2 &&
			   c == 0xA0) {
			end -= 2;
		} else {
			break;
		}
	}
	return trim_str(sv.substr(0, end));
}

std::string read_file_utf8(const std::string &path)
{
	if (path.empty())
		return "";
	std::error_code ec;
	const std::filesystem::path fsp = std::filesystem::u8path(path);
	if (!std::filesystem::exists(fsp, ec) || ec)
		return "";
	std::ifstream in(fsp, std::ios::binary);
	if (!in.is_open())
		return "";
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
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

// Maskiert ein Argument für Win32 CreateProcessW (Backslashes vor " und am Ende verdoppeln, " als \").
std::wstring quote_win32_arg(const std::wstring &arg)
{
	std::wstring out;
	out.push_back(L'"');
	std::size_t bs_count = 0;
	for (wchar_t ch : arg) {
		if (ch == L'\\') {
			++bs_count;
		} else if (ch == L'"') {
			out.append(bs_count * 2 + 1, L'\\');
			out.push_back(L'"');
			bs_count = 0;
		} else {
			if (bs_count > 0) {
				out.append(bs_count, L'\\');
				bs_count = 0;
			}
			out.push_back(ch);
		}
	}
	if (bs_count > 0)
		out.append(bs_count * 2, L'\\');
	out.push_back(L'"');
	return out;
}

struct HttpResp {
	DWORD status = 0;
	std::string body;
	std::string net_err;
};

HttpResp winhttp_call(const std::string &method, const std::string &url,
		      const std::wstring &headers, const std::string &req_body, int timeout_ms)
{
	HttpResp result;
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
		result.net_err = "Ungültige URL: " + url;
		return result;
	}

	WinHttpHandle session(WinHttpOpen(L"OBS-Textbox/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
					  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
	if (!session) {
		result.net_err = "WinHttpOpen fehlgeschlagen (" + std::to_string(GetLastError()) + ")";
		return result;
	}

	WinHttpSetTimeouts(session, 10000, 15000, 30000, timeout_ms);

	const std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
	const std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
	WinHttpHandle connect(WinHttpConnect(session, host.c_str(), uc.nPort, 0));
	if (!connect) {
		result.net_err = "WinHttpConnect fehlgeschlagen (" + std::to_string(GetLastError()) + ")";
		return result;
	}

	const DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
	const std::wstring wmethod = utf8_to_wide(method);
	WinHttpHandle request(WinHttpOpenRequest(connect, wmethod.c_str(), path.c_str(), nullptr,
						 WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
						 flags));
	if (!request) {
		result.net_err = "WinHttpOpenRequest fehlgeschlagen (" + std::to_string(GetLastError()) + ")";
		return result;
	}

	LPCWSTR hdr_ptr = headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str();
	DWORD hdr_len = headers.empty() ? 0 : static_cast<DWORD>(headers.size());
	LPVOID body_ptr =
		req_body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char *>(req_body.data());
	DWORD body_len = static_cast<DWORD>(req_body.size());

	if (!WinHttpSendRequest(request, hdr_ptr, hdr_len, body_ptr, body_len, body_len, 0) ||
	    !WinHttpReceiveResponse(request, nullptr)) {
		result.net_err = "Netzwerkfehler/Timeout (" + std::to_string(GetLastError()) + ")";
		return result;
	}

	DWORD status_code = 0;
	DWORD status_size = sizeof(status_code);
	WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			    WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size,
			    WINHTTP_NO_HEADER_INDEX);
	result.status = status_code;

	while (true) {
		DWORD avail = 0;
		if (!WinHttpQueryDataAvailable(request, &avail) || avail == 0)
			break;
		std::string chunk(avail, '\0');
		DWORD read = 0;
		if (!WinHttpReadData(request, chunk.data(), avail, &read))
			break;
		chunk.resize(read);
		result.body.append(chunk);
	}

	return result;
}

#endif // _WIN32

} // namespace

std::string read_env_key(const std::string &env_file_path, std::string &err)
{
	err.clear();
	if (env_file_path.empty()) {
		err = "Kein Pfad zur API-Key-Datei angegeben.";
		return "";
	}

	std::error_code ec;
	const std::filesystem::path fsp = std::filesystem::u8path(env_file_path);
	if (!std::filesystem::exists(fsp, ec) || ec) {
		err = "API-Key-Datei nicht gefunden: " + env_file_path;
		return "";
	}

	std::ifstream in(fsp, std::ios::binary);
	if (!in.is_open()) {
		err = "API-Key-Datei konnte nicht geöffnet werden: " + env_file_path;
		return "";
	}

	std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	std::string_view sv(content);
	if (sv.size() >= 3 && static_cast<unsigned char>(sv[0]) == 0xEF &&
	    static_cast<unsigned char>(sv[1]) == 0xBB &&
	    static_cast<unsigned char>(sv[2]) == 0xBF) {
		sv.remove_prefix(3);
	}

	for (const auto &raw_line : split_char(sv, '\n')) {
		const std::string line = trim_str(raw_line);
		if (line.empty() || line[0] == '#')
			continue;

		const std::size_t eq_pos = line.find('=');
		if (eq_pos == std::string::npos)
			continue;

		const std::string key = trim_str(std::string_view(line).substr(0, eq_pos));
		std::string val = trim_str(std::string_view(line).substr(eq_pos + 1));
		while (val.size() >= 2 &&
		       ((val.front() == '"' && val.back() == '"') ||
			(val.front() == '\'' && val.back() == '\''))) {
			val = trim_str(std::string_view(val).substr(1, val.size() - 2));
		}

		if (key == "OPENROUTER_API_KEY" && !val.empty()) {
			return val;
		}
	}

	err = "Kein OPENROUTER_API_KEY in Datei gefunden.";
	return "";
}

std::string extract_document_text(const std::string &file_path, const std::string &python_exe,
				  std::string &err,
				  std::shared_ptr<std::atomic<bool>> cancel)
{
	err.clear();
	if (cancel && cancel->load())
		return "";

	std::error_code ec;
	const std::filesystem::path py_path = std::filesystem::u8path(python_exe);
	if (python_exe.empty() || !std::filesystem::exists(py_path, ec) || ec) {
		err = "MarkItDown-Python nicht gefunden: " + python_exe;
		return "";
	}

	const std::filesystem::path doc_path = std::filesystem::u8path(file_path);
	if (file_path.empty() || !std::filesystem::exists(doc_path, ec) || ec) {
		err = "Dokument-Extraktion fehlgeschlagen (Exit 1)";
		return "";
	}

#ifdef _WIN32
	SECURITY_ATTRIBUTES sa{};
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;
	sa.lpSecurityDescriptor = nullptr;

	HANDLE stdout_read = nullptr;
	HANDLE stdout_write = nullptr;
	if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0)) {
		err = "Dokument-Extraktion fehlgeschlagen (Pipe-Fehler)";
		return "";
	}
	SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);

	HANDLE stderr_read = nullptr;
	HANDLE stderr_write = nullptr;
	if (CreatePipe(&stderr_read, &stderr_write, &sa, 0)) {
		SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);
	}

	std::wstring cmd = quote_win32_arg(utf8_to_wide(python_exe)) +
			   L" -X utf8 -m markitdown " +
			   quote_win32_arg(utf8_to_wide(file_path));
	std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
	cmd_buf.push_back(L'\0');

	STARTUPINFOW si{};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = nullptr;
	si.hStdOutput = stdout_write;
	si.hStdError = stderr_write ? stderr_write : stdout_write;

	PROCESS_INFORMATION pi{};
	const BOOL created = CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr, TRUE,
					    CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

	CloseHandle(stdout_write);
	if (stderr_write)
		CloseHandle(stderr_write);

	if (!created) {
		CloseHandle(stdout_read);
		if (stderr_read)
			CloseHandle(stderr_read);
		err = "Dokument-Extraktion fehlgeschlagen (Exit -1)";
		return "";
	}

	HANDLE job = CreateJobObjectW(nullptr, nullptr);
	if (job) {
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
		jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
		AssignProcessToJobObject(job, pi.hProcess);
	}

	std::string out_bytes;
	std::thread reader([stdout_read, &out_bytes]() {
		char buf[4096];
		DWORD bytes_read = 0;
		while (ReadFile(stdout_read, buf, sizeof(buf), &bytes_read, nullptr) &&
		       bytes_read > 0) {
			out_bytes.append(buf, bytes_read);
		}
	});

	std::thread err_reader([stderr_read]() {
		if (!stderr_read)
			return;
		char buf[4096];
		DWORD bytes_read = 0;
		while (ReadFile(stderr_read, buf, sizeof(buf), &bytes_read, nullptr) &&
		       bytes_read > 0) {
		}
	});

	// Auf markitdown-Kindprozess in 200-ms-Schritten bis zur 120-s-Deadline warten.
	DWORD wait_res = WAIT_TIMEOUT;
	bool cancelled = false;
	for (DWORD elapsed_ms = 0; elapsed_ms < 120000; elapsed_ms += 200) {
		if (cancel && cancel->load()) {
			cancelled = true;
			break;
		}
		wait_res = WaitForSingleObject(pi.hProcess, 200);
		if (cancel && cancel->load()) {
			cancelled = true;
			break;
		}
		if (wait_res != WAIT_TIMEOUT)
			break;
	}
	if (cancelled || wait_res == WAIT_TIMEOUT) {
		TerminateProcess(pi.hProcess, 1);
	}

	reader.join();
	err_reader.join();

	DWORD exit_code = 1;
	GetExitCodeProcess(pi.hProcess, &exit_code);

	CloseHandle(stdout_read);
	if (stderr_read)
		CloseHandle(stderr_read);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	if (job)
		CloseHandle(job);

	if (cancelled)
		return "";

	if (wait_res == WAIT_TIMEOUT) {
		err = "Dokument-Extraktion fehlgeschlagen (Timeout)";
		return "";
	}

	if (exit_code != 0) {
		err = "Dokument-Extraktion fehlgeschlagen (Exit " + std::to_string(exit_code) + ")";
		return "";
	}

	std::string cleaned = trim_str(out_bytes);
	if (cleaned.empty()) {
		err = "Kein Text im Dokument gefunden.";
		return "";
	}

	return cleaned;
#else
	(void)cancel;
	err = "Dokument-Extraktion auf dieser Plattform nicht unterstützt.";
	return "";
#endif
}

std::string extract_document_text(const std::string &file_path, const std::string &python_exe,
				  std::string &err)
{
	return extract_document_text(file_path, python_exe, err, nullptr);
}

nlohmann::json parse_json_clean(const std::string &raw)
{
	std::string s = trim_str(raw);

	// ^```(json)?\s* (case-insensitive)
	if (s.rfind("```", 0) == 0) {
		std::size_t pos = 3;
		if (s.size() >= 7) {
			const std::string maybe_json = utf8_casefold(std::string_view(s).substr(3, 4));
			if (maybe_json == "json")
				pos = 7;
		}
		while (pos < s.size() && is_ascii_space(static_cast<unsigned char>(s[pos])))
			++pos;
		s.erase(0, pos);
	}

	// ```\s*$
	s = trim_str(s);
	if (s.size() >= 3 && s.compare(s.size() - 3, 3, "```") == 0) {
		s.erase(s.size() - 3);
		s = trim_str(s);
	}

	const std::size_t start = s.find('{');
	const std::size_t end = s.rfind('}');
	if (start == std::string::npos || end == std::string::npos || end < start) {
		throw std::runtime_error("Kein valides JSON im Modell-Ergebnis gefunden.");
	}

	const std::string slice = s.substr(start, end - start + 1);
	nlohmann::json parsed = nlohmann::json::parse(slice, nullptr, false);
	if (parsed.is_discarded()) {
		throw std::runtime_error("Kein valides JSON im Modell-Ergebnis gefunden.");
	}
	return parsed;
}

std::vector<std::string> discover_free_models(const nlohmann::json &models_json)
{
	if (!models_json.is_object() || !models_json.contains("data") ||
	    !models_json["data"].is_array()) {
		return {};
	}

	struct Candidate {
		std::string id;
		int score = 100;
		std::int64_t context_length = 0;
	};

	auto is_excluded = [](const std::string &id) -> bool {
		return id.find("-vl") != std::string::npos ||
		       id.find("content-safety") != std::string::npos ||
		       id.find("2.6b") != std::string::npos;
	};

	auto calc_score = [](const std::string &id) -> int {
		constexpr std::size_t n = sizeof(kModelPriorities) / sizeof(kModelPriorities[0]);
		for (std::size_t i = 0; i < n; ++i) {
			if (id.find(kModelPriorities[i]) != std::string::npos)
				return 1000 - static_cast<int>(i) * 10;
		}
		return 100;
	};

	std::vector<Candidate> list;
	for (const auto &m : models_json["data"]) {
		if (!m.is_object() || !m.contains("id") || !m["id"].is_string())
			continue;
		const std::string id = m["id"].get<std::string>();
		if (id.size() < 5 || id.compare(id.size() - 5, 5, ":free") != 0)
			continue;
		if (is_excluded(id))
			continue;

		std::int64_t ctx = 0;
		if (m.contains("context_length") && m["context_length"].is_number())
			ctx = m["context_length"].get<std::int64_t>();

		list.push_back(Candidate{id, calc_score(id), ctx});
	}

	std::stable_sort(list.begin(), list.end(),
			 [](const Candidate &a, const Candidate &b) {
				 if (a.score != b.score)
					 return a.score > b.score;
				 return a.context_length > b.context_length;
			 });

	std::vector<std::string> ids;
	ids.reserve(list.size());
	for (const auto &c : list)
		ids.push_back(c.id);
	return ids;
}

std::map<std::string, std::pair<std::string, std::string>>
parse_song_reference(const std::string &md)
{
	std::map<std::string, std::pair<std::string, std::string>> song_ref;
	if (md.empty())
		return song_ref;

	const std::vector<std::string> lines = split_char(md, '\n');
	std::size_t start = 0;

	// Header-Zeile suchen: erste Pipe-Zelle lautet "Nr" (case-insensitiv)
	for (std::size_t h = 0; h < lines.size(); ++h) {
		const std::string line = trim_str(lines[h]);
		if (line.empty() || line[0] != '|')
			continue;
		const auto cells = split_char(std::string_view(line).substr(1), '|');
		if (!cells.empty() && utf8_casefold(trim_str(cells[0])) == "nr") {
			start = h + 1;
			break;
		}
	}

	for (std::size_t i = start; i < lines.size(); ++i) {
		const std::string line = trim_str(lines[i]);
		if (line.empty() || line[0] != '|')
			continue;

		bool only_sep = true;
		for (char c : line) {
			if (!is_ascii_space(static_cast<unsigned char>(c)) && c != '-' && c != '|') {
				only_sep = false;
				break;
			}
		}
		if (only_sep)
			continue;

		std::vector<std::string> parts = split_char(line, '|');
		for (auto &p : parts)
			p = trim_str(p);

		if (parts.size() >= 3) {
			const std::string &number = parts[1];
			std::string title = parts[2];

			// Entferne optionales Suffix \s*\([A-Za-z]+\)\s*$
			if (!title.empty() && title.back() == ')') {
				const std::size_t open_pos = title.rfind('(');
				if (open_pos != std::string::npos && open_pos + 1 < title.size() - 1) {
					bool all_alpha = true;
					for (std::size_t k = open_pos + 1; k < title.size() - 1; ++k) {
						const unsigned char ch =
							static_cast<unsigned char>(title[k]);
						if (!((ch >= 'A' && ch <= 'Z') ||
						      (ch >= 'a' && ch <= 'z'))) {
							all_alpha = false;
							break;
						}
					}
					if (all_alpha) {
						title = trim_str(
							std::string_view(title).substr(0, open_pos));
					}
				}
			}

			const std::string rubrik = (parts.size() >= 5) ? parts[4] : "";
			if (!number.empty()) {
				song_ref[number] = {title, rubrik};
			}
		}
	}

	return song_ref;
}

void enrich_titles_from_reference(
	std::vector<AiPoint> &items,
	const std::map<std::string, std::pair<std::string, std::string>> &ref)
{
	if (items.empty() || ref.empty())
		return;

	std::map<std::string, std::string> base_lookup;
	for (const auto &kv : ref) {
		const std::string &key = kv.first;
		const std::string &title = kv.second.first;
		const std::size_t comma = key.find(',');
		const std::string base =
			trim_str(comma == std::string::npos ? std::string_view(key)
							    : std::string_view(key).substr(0, comma));
		if (!base.empty() && !title.empty() && base_lookup.find(base) == base_lookup.end()) {
			base_lookup[base] = title;
		}
	}

	static const std::regex kNumRe(R"(\b(\d{1,3})\b)");
	for (auto &item : items) {
		if (trim_str(item.zeile3).empty()) {
			std::smatch m;
			if (std::regex_search(item.zeile2, m, kNumRe) && m.size() >= 2) {
				auto it = base_lookup.find(m[1].str());
				if (it != base_lookup.end()) {
					item.zeile3 = it->second;
				}
			}
		}
	}
}

AiResult process_with_llm(const std::string &api_key, const std::string &document_text,
			  const std::string &system_prompt, const std::string &lieder_path,
			  std::function<void(const std::string &)> on_progress,
			  std::shared_ptr<std::atomic<bool>> cancel)
{
#ifndef _WIN32
	(void)api_key;
	(void)document_text;
	(void)system_prompt;
	(void)lieder_path;
	(void)on_progress;
	(void)cancel;
	throw std::runtime_error("WinHTTP ist nur unter Windows verfügbar.");
#else
	if (cancel && cancel->load())
		return {};

	if (on_progress)
		on_progress("Ermittle verfügbare freie Modelle...");

	const std::wstring auth_hdr = L"Authorization: Bearer " + utf8_to_wide(api_key) + L"\r\n";
	const HttpResp models_resp = winhttp_call(
		"GET", "https://openrouter.ai/api/v1/models", auth_hdr, "", 30000);
	if (cancel && cancel->load())
		return {};
	if (!models_resp.net_err.empty()) {
		throw std::runtime_error("Konnte Modellliste nicht abrufen (" + models_resp.net_err +
					 ")");
	}
	if (models_resp.status != 200) {
		throw std::runtime_error("Konnte Modellliste nicht abrufen (HTTP " +
					 std::to_string(models_resp.status) + ")");
	}

	const nlohmann::json models_json =
		nlohmann::json::parse(models_resp.body, nullptr, false);
	std::vector<std::string> free_models = discover_free_models(models_json);
	if (free_models.empty()) {
		throw std::runtime_error("Keine kostenlosen Modelle auf OpenRouter verfügbar.");
	}
	if (free_models.size() > 6)
		free_models.resize(6);

	const std::string sys_prompt =
		trim_str(system_prompt).empty() ? std::string(kDefaultSystemPrompt) : system_prompt;
	const std::string user_text = "Daten:\n" + document_text;
	const std::wstring post_hdrs =
		L"Authorization: Bearer " + utf8_to_wide(api_key) +
		L"\r\nContent-Type: application/json\r\nHTTP-Referer: http://localhost\r\nX-Title: OBS-Textbox\r\n";

	std::vector<std::string> errors;

	for (const std::string &model : free_models) {
		for (int attempt = 1; attempt <= 2; ++attempt) {
			// Vor jedem Versuch/Retry auf kooperatives Abbruchsignal prüfen.
			if (cancel && cancel->load())
				return {};

			if (on_progress) {
				on_progress("Modell " + model + " (Versuch " +
					    std::to_string(attempt) + "/2)...");
			}

			bool retryable = false;
			std::string attempt_err;

			try {
				nlohmann::json req_json = {
					{"model", model},
					{"messages",
					 nlohmann::json::array({
						 {{"role", "system"}, {"content", sys_prompt}},
						 {{"role", "user"}, {"content", user_text}},
					 })},
					{"temperature", 0},
					{"max_tokens", 3500},
				};

				const HttpResp resp = winhttp_call(
					"POST", "https://openrouter.ai/api/v1/chat/completions",
					post_hdrs, req_json.dump(), 70000);
				if (cancel && cancel->load())
					return {};

				if (!resp.net_err.empty()) {
					retryable = true;
					throw std::runtime_error(resp.net_err);
				}

				if (resp.status != 200) {
					const DWORD st = resp.status;
					retryable = (st == 429 || st == 502 || st == 503 ||
						     st == 504 || st == 524 || st == 529);
					std::string msg = "HTTP " + std::to_string(st);
					const nlohmann::json err_j =
						nlohmann::json::parse(resp.body, nullptr, false);
					if (err_j.is_object() && err_j.contains("error") &&
					    err_j["error"].is_object() &&
					    err_j["error"].contains("message") &&
					    err_j["error"]["message"].is_string()) {
						const std::string m =
							err_j["error"]["message"].get<std::string>();
						if (!m.empty())
							msg = m;
					}
					throw std::runtime_error(msg);
				}

				const nlohmann::json data =
					nlohmann::json::parse(resp.body, nullptr, false);
				if (!data.is_object()) {
					retryable = true;
					throw std::runtime_error("Ungültige Server-Antwort.");
				}

				std::string content;
				if (data.contains("choices") && data["choices"].is_array() &&
				    !data["choices"].empty()) {
					const auto &c0 = data["choices"][0];
					if (c0.is_object() && c0.contains("message") &&
					    c0["message"].is_object() &&
					    c0["message"].contains("content") &&
					    c0["message"]["content"].is_string()) {
						content = c0["message"]["content"].get<std::string>();
					}
				}

				content = trim_str(content);
				if (content.empty()) {
					retryable = true;
					throw std::runtime_error("Leere Antwort vom Modell erhalten.");
				}

				std::string model_used = model;
				if (data.contains("model") && data["model"].is_string()) {
					const std::string mu = data["model"].get<std::string>();
					if (!mu.empty())
						model_used = mu;
				}

				const nlohmann::json parsed = parse_json_clean(content);
				if (!parsed.is_object() || !parsed.contains("items") ||
				    !parsed["items"].is_array() || parsed["items"].empty()) {
					throw std::runtime_error(
						"Antwort enthielt keine Einträge (items).");
				}

				std::vector<AiPoint> items;
				for (const auto &it : parsed["items"]) {
					if (!it.is_object())
						continue;
					AiPoint pt;
					if (it.contains("zeile1") && it["zeile1"].is_string())
						pt.zeile1 = it["zeile1"].get<std::string>();
					if (it.contains("zeile2") && it["zeile2"].is_string())
						pt.zeile2 = it["zeile2"].get<std::string>();
					if (it.contains("zeile3") && it["zeile3"].is_string())
						pt.zeile3 = it["zeile3"].get<std::string>();
					items.push_back(std::move(pt));
				}

				if (items.empty()) {
					throw std::runtime_error(
						"Antwort enthielt keine Einträge (items).");
				}

				const std::string lieder_md = read_file_utf8(lieder_path);
				if (!lieder_md.empty()) {
					const auto song_ref = parse_song_reference(lieder_md);
					enrich_titles_from_reference(items, song_ref);
				}

				return AiResult{std::move(items), std::move(model_used)};
			} catch (const std::exception &ex) {
				attempt_err = ex.what();
			}

			errors.push_back(model + " [" + std::to_string(attempt) + "]: " +
					 attempt_err);
			if (!retryable)
				break;

			if (attempt == 1) {
				if (cancel && cancel->load())
					return {};
				if (on_progress)
					on_progress("Warte 3s vor neuem Versuch...");
				// Wartezeit vor Retry in kurzen Intervallen mit Abbruchprüfung überbrücken.
				for (int w = 0; w < 30; ++w) {
					if (cancel && cancel->load())
						return {};
					std::this_thread::sleep_for(
						std::chrono::milliseconds(100));
				}
			}
		}
	}

	std::string tail;
	const std::size_t start_idx = errors.size() > 4 ? (errors.size() - 4) : 0;
	for (std::size_t i = start_idx; i < errors.size(); ++i) {
		if (!tail.empty())
			tail += "\n";
		tail += errors[i];
	}
	throw std::runtime_error("Alle AI-Modelle fehlgeschlagen:\n" + tail);
#endif
}

AiResult process_with_llm(const std::string &api_key, const std::string &document_text,
			  const std::string &system_prompt, const std::string &lieder_path,
			  std::function<void(const std::string &)> on_progress)
{
	return process_with_llm(api_key, document_text, system_prompt, lieder_path,
				std::move(on_progress), nullptr);
}

namespace {

constexpr const char *kPpKeywords[] = {
	"eröffnung",          "einführung",     "einzug",         "eintritt",
	"kyrie",              "gott danke",     "gloria",         "zwischengesang",
	"halleluja",          "credo",          "glaubensbekenntnis", "gabenbereitung",
	"opfergabe",          "heilig",         "agnus dei",      "lamm gottes",
	"kommunion",          "kommunionlied",  "lesung",         "predigt",
	"gebet",              "segen",          "danklied",       "schlusslied",
	"auszug",
};

bool is_header_line(std::string_view line)
{
	const std::string l = trim_str(utf8_casefold(line));
	if (l.empty())
		return true;

	static const std::regex kHdrWordsRe(R"(\b(liedplan|woche|organist)\b)");
	if (std::regex_search(l, kHdrWordsRe))
		return true;

	static const std::regex kDayPrefixRe(
		R"(^\s*(dienstag|donnerstag|sonntag|samstag|montag|freitag)\b)");
	if (std::regex_search(l, kDayPrefixRe))
		return true;

	return false;
}

} // namespace

std::vector<AiPoint> parse_liedplan_locally(const std::string &text)
{
	if (text.empty())
		return {};

	std::vector<std::string> lines;
	for (const auto &raw_line : split_char(text, '\n')) {
		std::string s = trim_str(raw_line);
		if (!s.empty())
			lines.push_back(std::move(s));
	}

	std::vector<AiPoint> items;
	static const std::regex kGlNumRe(
		R"(\b\d{1,3}\b(?:(?:[.,;]\s*(?:[\d+\- ]|\xE2\x80\x93)*[\d]))?)");

	for (const std::string &line : lines) {
		if (is_header_line(line))
			continue;

		std::smatch num_match;
		if (std::regex_search(line, num_match, kGlNumRe)) {
			const std::string num = num_match[0].str();
			const std::size_t idx =
				static_cast<std::size_t>(num_match.position(0));
			const std::string before = strip_trailing_commas_spaces(
				std::string_view(line).substr(0, idx));
			const std::string after = strip_leading_commas_spaces(
				std::string_view(line).substr(idx + num.size()));

			if (before.empty() && !items.empty() && items.back().zeile2.empty()) {
				items.back().zeile2 = num;
				if (!after.empty())
					items.back().zeile3 = after;
			} else {
				items.push_back(AiPoint{before, num, after});
			}
			continue;
		}

		const std::string lower = utf8_casefold(line);
		const char *matched_kw = nullptr;
		for (const char *kw : kPpKeywords) {
			const std::string_view kw_sv(kw);
			if (lower == kw_sv ||
			    (lower.size() > kw_sv.size() &&
			     lower.compare(0, kw_sv.size(), kw_sv) == 0 &&
			     lower[kw_sv.size()] == ' ')) {
				matched_kw = kw;
				break;
			}
		}

		if (matched_kw) {
			const std::size_t kw_len = std::strlen(matched_kw);
			const std::string label = line.substr(0, kw_len);
			const std::string rest = strip_leading_commas_spaces(
				std::string_view(line).substr(kw_len));
			items.push_back(AiPoint{label, "", rest});
			continue;
		}

		if (!items.empty()) {
			AiPoint &cur = items.back();
			cur.zeile3 = cur.zeile3.empty() ? line : (cur.zeile3 + " " + line);
		}
	}

	std::vector<AiPoint> result;
	result.reserve(items.size());
	for (auto &it : items) {
		it.zeile1 = trim_str(it.zeile1);
		it.zeile2 = trim_str(it.zeile2);
		it.zeile3 = trim_str(it.zeile3);
		if (!it.zeile1.empty() || !it.zeile2.empty() || !it.zeile3.empty()) {
			result.push_back(std::move(it));
		}
	}
	return result;
}

} // namespace liedplan_ai
