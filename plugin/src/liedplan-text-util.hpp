/*
 * liedplan-text-util.hpp — Geteilte Text- und WinHTTP-Helfer.
 *
 * Zweck:
 *   - trim_str / utf8_casefold / is_ascii_space / split_char waren identisch in
 *     liedplan-import.cpp und liedplan-ai.cpp kopiert — hier zentral als inline.
 *   - WinHttpHandle (RAII) und utf8_to_wide waren ebenfalls doppelt; nur unter
 *     _WIN32 verfügbar (windows.h + winhttp.h).
 *
 * Hinweis: Header-only, libobs- und Qt-frei; von liedplan-import, liedplan-ai
 * und deren Standalone-Tests verwendet.
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

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

namespace liedplan_text {

inline bool is_ascii_space(unsigned char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

// Trimmt ASCII-Whitespace sowie UTF-8-encoded NBSP (0xC2 0xA0) beidseitig.
inline std::string trim_str(std::string_view sv)
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

// ASCII-Casefold plus Umlaut-Mapping (Ä/Ö/Ü -> ä/ö/ü); U+00D7 (×) bleibt.
inline std::string utf8_casefold(std::string_view sv)
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

// Splitet bei delim; Semantik identisch zum früheren ai.cpp-Original
// (leerer Input -> ein leeres Element).
inline std::vector<std::string> split_char(std::string_view sv, char delim)
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

#ifdef _WIN32
// RAII-Helfer für WinHTTP-Handles (Session, Connect, Request).
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

inline std::wstring utf8_to_wide(std::string_view sv)
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
#endif

} // namespace liedplan_text
