/*
 * liedplan-events.hpp — Zentrales Event-Log für das OBS-Textbox-Plugin (v3).
 *
 * Zweck:
 *   - Protokolliert Benutzer- und Systemereignisse mit UTC-ISO-Zeitstempel in <log_dir>/events.log.
 *   - Begrenzt die Dateigröße bei >1 MB automatisch auf die letzten ~400 Zeilen.
 *
 * Hinweis: Header-only, Qt- und libobs-frei (nur C++-Standardbibliothek); in Tests direkt nutzbar.
 */

#pragma once

#ifndef _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#endif

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

inline std::filesystem::path lp_event_log_dir_from_state_path(const std::string &state_path)
{
	if (state_path.empty())
		return {};
	try {
		return std::filesystem::u8path(state_path).parent_path();
	} catch (...) {
		return {};
	}
}

inline void lp_event_log(const std::filesystem::path &log_dir, const std::string &msg)
{
	if (log_dir.empty())
		return;

	static std::mutex log_mutex;
	std::lock_guard<std::mutex> lock(log_mutex);

	try {
		std::error_code ec;
		std::filesystem::create_directories(log_dir, ec);

		const auto now = std::chrono::system_clock::now();
		const std::time_t now_c = std::chrono::system_clock::to_time_t(now);
		std::tm utc_tm{};
#ifdef _WIN32
		gmtime_s(&utc_tm, &now_c);
#else
		gmtime_r(&now_c, &utc_tm);
#endif
		char ts_buf[32]{};
		std::snprintf(ts_buf, sizeof(ts_buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
			      utc_tm.tm_year + 1900, utc_tm.tm_mon + 1, utc_tm.tm_mday,
			      utc_tm.tm_hour, utc_tm.tm_min, utc_tm.tm_sec);

		std::string clean_msg;
		clean_msg.reserve(msg.size());
		for (char ch : msg) {
			if (ch == '\r' || ch == '\n')
				clean_msg.push_back(' ');
			else
				clean_msg.push_back(ch);
		}

		const std::filesystem::path log_file = log_dir / "events.log";
		{
			std::ofstream out(log_file, std::ios::binary | std::ios::app);
			if (!out.is_open())
				return;
			out << ts_buf << " | " << clean_msg << "\n";
			out.flush();
		}

		constexpr std::uintmax_t kMaxBytes = 1024u * 1024u;
		constexpr std::size_t kKeepLines = 400;
		const auto fsize = std::filesystem::file_size(log_file, ec);
		if (!ec && fsize > kMaxBytes) {
			std::ifstream in(log_file, std::ios::binary);
			if (!in.is_open())
				return;
			std::string content((std::istreambuf_iterator<char>(in)),
					    std::istreambuf_iterator<char>());
			in.close();

			std::vector<std::string> lines;
			std::size_t start = 0;
			while (start < content.size()) {
				std::size_t end = content.find('\n', start);
				if (end == std::string::npos) {
					std::string tail = content.substr(start);
					if (!tail.empty() && tail.back() == '\r')
						tail.pop_back();
					if (!tail.empty())
						lines.push_back(std::move(tail));
					break;
				}
				std::string line = content.substr(start, end - start);
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				lines.push_back(std::move(line));
				start = end + 1;
			}

			const std::size_t first_idx =
				(lines.size() > kKeepLines) ? (lines.size() - kKeepLines) : 0;
			std::ofstream trunc_out(log_file, std::ios::binary | std::ios::trunc);
			if (!trunc_out.is_open())
				return;
			for (std::size_t i = first_idx; i < lines.size(); ++i) {
				trunc_out << lines[i] << "\n";
			}
		}
	} catch (...) {
	}
}
