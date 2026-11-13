/*
 * test-liedplan-import.cpp — Unit-Tests für Liedplan-Import-Funktionen (v3).
 *
 * Zweck:
 *   - Testet das Parsen von Google Forms JSON-Antworten.
 *   - Testet CSV-Parser mit verschiedenen Trennzeichen, Anführungszeichen und Zeilenumbrüchen.
 *   - Testet das Entpacken und Einlesen von XLSX- und ZIP-Dateien aus den Test-Samples.
 */

#ifndef _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#endif

#include "../liedplan-import.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <zlib.h>

namespace liedplan_import {
std::optional<std::string> inflate_data(std::string_view compressed,
					std::size_t uncompressed_hint = 0,
					int window_bits = MAX_WBITS);
} // namespace liedplan_import

namespace {

#ifdef LIEDPLAN_REPO_ROOT
const char *kSamplesDir = LIEDPLAN_REPO_ROOT "/main/tests/samples/";
#else
const char *kSamplesDir = "main/tests/samples/";
#endif

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

// M4-T1: ZIP-Archiv-Builder (Store oder Deflate) — nur für Tests dieser Suite;
// zuvor öffentlich in liedplan-import.hpp deklariert und aus der Produktions-DLL
// exportiert. write_le16/write_le32 sind hier lokal kopiert (Original-Helpers
// waren namespace-lokal in liedplan-import.cpp).
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

} // namespace

int main()
{
	using namespace liedplan_import;

	// 1. normalize_header("  1.  Eröffnung   –   GL-Nummer ") == "1. Eröffnung - GL-Nummer"
	{
		const std::string got =
			normalize_header("  1.  Eröffnung   \xE2\x80\x93   GL-Nummer ");
		check(got == "1. Eröffnung - GL-Nummer",
		      "1. normalize_header mit Spaces und Gedankenstrich", got);
	}

	// 2. normalize_header("Woche\u2013Tag") == "Woche-Tag"
	{
		const std::string got = normalize_header("Woche\xE2\x80\x93Tag");
		check(got == "Woche-Tag", "2. normalize_header Woche\\u2013Tag == Woche-Tag", got);
	}

	// 3. header_key
	{
		const auto k_zs = header_key("Zeitstempel");
		const auto k_ts = header_key("Timestamp");
		const auto k_wo = header_key("Woche");
		const auto k_tg = header_key("Tag");
		const auto k_or = header_key("Organist");
		const auto k_p1_gl = header_key("1. Eröffnung - GL-Nummer");
		const auto k_p1_v = header_key("1. Eröffnung \xE2\x80\x93 Verse");
		const auto k_p1_t = header_key("1. Eröffnung - Liedtitel");
		const auto k_p12_t = header_key("12. Schlusslied \xE2\x80\x93 Liedtitel");
		const auto k_unk = header_key("Unbekannte Spalte");

		const bool ok = (k_zs && *k_zs == "zeitstempel") &&
				(k_ts && *k_ts == "zeitstempel") &&
				(k_wo && *k_wo == "woche") &&
				(k_tg && *k_tg == "tag") &&
				(k_or && *k_or == "organist") &&
				(k_p1_gl && *k_p1_gl == "1.gl") &&
				(k_p1_v && *k_p1_v == "1.verse") &&
				(k_p1_t && *k_p1_t == "1.titel") &&
				(k_p12_t && *k_p12_t == "12.titel") &&
				(!k_unk.has_value());
		check(ok, "3. header_key Simple-Keys, Programmpunkte 1..12 und Unbekannte Spalte");
	}

	// 4. antwort_comma.csv
	{
		const std::filesystem::path p =
			std::filesystem::u8path(std::string(kSamplesDir) + "antwort_comma.csv");
		LiedplanImportResult res;
		std::size_t count = 0;
		std::string err_msg;
		const ImportError err = parse_csv_file(p, res, &count, &err_msg);
		const bool ok = (err == ImportError::None) && (count == 1) &&
				(res.tag == "Sonntag") && (res.organist == "E. Stolper") &&
				(res.punkte.size() == 12) && (res.punkte[0].gl == "144") &&
				(res.punkte[0].verse == "1+4+6") &&
				(res.punkte[0].titel == "Nun jauchzt dem Herren, aller Welt");
		check(ok, "4. antwort_comma.csv inkl. Komma im quoteden Liedtitel", err_msg);
	}

	// 5. antwort_semicolon.csv
	{
		const std::filesystem::path p =
			std::filesystem::u8path(std::string(kSamplesDir) + "antwort_semicolon.csv");
		LiedplanImportResult res;
		std::size_t count = 0;
		std::string err_msg;
		const ImportError err = parse_csv_file(p, res, &count, &err_msg);
		const bool ok = (err == ImportError::None) && (count == 1) &&
				(res.tag == "Sonntag") && (res.punkte.size() == 12) &&
				(res.punkte[0].gl == "144");
		check(ok, "5. antwort_semicolon.csv P1.gl == 144", err_msg);
	}

	// 6. antwort_bom.csv
	{
		const std::filesystem::path p =
			std::filesystem::u8path(std::string(kSamplesDir) + "antwort_bom.csv");
		LiedplanImportResult res;
		std::size_t count = 0;
		std::string err_msg;
		const ImportError err = parse_csv_file(p, res, &count, &err_msg);
		const bool ok = (err == ImportError::None) && (count == 1) &&
				(res.punkte.size() == 12) && (res.punkte[0].gl == "144");
		check(ok, "6. antwort_bom.csv P1.gl == 144", err_msg);
	}

	// 7. antwort_timestamp_en.csv
	{
		const std::filesystem::path p = std::filesystem::u8path(
			std::string(kSamplesDir) + "antwort_timestamp_en.csv");
		LiedplanImportResult res;
		std::size_t count = 0;
		std::string err_msg;
		const ImportError err = parse_csv_file(p, res, &count, &err_msg);
		const bool ok = (err == ImportError::None) && (count == 1) &&
				(res.zeitstempel == "20.10.2026 09:15:32");
		check(ok, "7. antwort_timestamp_en.csv zeitstempel == 20.10.2026 09:15:32",
		      err_msg);
	}

	// 8. Extra: Inline-CSV mit ""-Escapes ("He""llo" -> He"llo) und leeren Zeilen zwischen Daten
	{
		const std::string csv_text =
			"Woche,1. Eröffnung – GL-Nummer,1. Eröffnung – Liedtitel\r\n"
			"\r\n"
			"KW 1,100,Erster Titel\r\n"
			"   ,   ,   \r\n"
			"KW 2,200,\"He\"\"llo\"\r\n"
			"\n";
		LiedplanImportResult res;
		std::size_t count = 0;
		std::string err_msg;
		const ImportError err = parse_csv_string(csv_text, res, &count, &err_msg);
		const bool ok = (err == ImportError::None) && (count == 2) &&
				(res.woche == "KW 2") && (res.punkte.size() == 12) &&
				(res.punkte[0].gl == "200") && (res.punkte[0].titel == "He\"llo");
		check(ok, "8. Inline-CSV mit \"\"-Escapes und leeren Zeilen zwischen Daten",
		      err_msg);
	}

	// 9. Extra: Header ohne Punkt-Spalten -> Fehler Format
	{
		const std::string csv_text =
			"Zeitstempel,Woche,Tag,Organist,Unbekannt\n"
			"20.10.2026,KW 42,Sonntag,M. Muster,Wert\n";
		LiedplanImportResult res;
		std::string err_msg;
		const ImportError err = parse_csv_string(csv_text, res, nullptr, &err_msg);
		const bool ok = (err == ImportError::Format) && !err_msg.empty();
		check(ok, "9. Header ohne Punkt-Spalten liefert ImportError::Format", err_msg);
	}

	// 10. Extra: nur Kopfzeile -> Fehler NoResponses
	{
		const std::string csv_text =
			"Zeitstempel,Woche,Tag,Organist,1. Eröffnung – GL-Nummer\n";
		LiedplanImportResult res;
		std::string err_msg;
		const ImportError err = parse_csv_string(csv_text, res, nullptr, &err_msg);
		const bool ok = (err == ImportError::NoResponses) && !err_msg.empty();
		check(ok, "10. Nur Kopfzeile liefert ImportError::NoResponses", err_msg);
	}

	// 11. API-Fixture (offline)
	{
		const nlohmann::json title_map = {
			{"q_woche", "Woche"},
			{"q_tag", "Tag"},
			{"q_organist", "Organist"},
			{"q_p1_gl", "1. Eröffnung \xE2\x80\x93 GL-Nummer"},
			{"q_p1_verse", "1. Eröffnung \xE2\x80\x93 Verse"},
			{"q_p1_titel", "1. Eröffnung \xE2\x80\x93 Liedtitel"},
			{"q_unbekannt", "Sonstige Bemerkungen"},
		};
		const nlohmann::json response = {
			{"createTime", "2026-10-20T08:00:00Z"},
			{"lastSubmittedTime", "2026-10-20T09:15:32Z"},
			{"answers",
			 {
				 {"q_woche",
				  {{"textAnswers",
				    {{"answers", {{{"value", "19.10. - 25.10.2026"}}}}}}}},
				 {"q_tag",
				  {{"textAnswers", {{"answers", {{{"value", "Sonntag"}}}}}}}},
				 {"q_organist",
				  {{"textAnswers", {{"answers", {{{"value", "E. Stolper"}}}}}}}},
				 {"q_p1_gl",
				  {{"textAnswers", {{"answers", {{{"value", "144"}}}}}}}},
				 {"q_p1_verse",
				  {{"textAnswers", {{"answers", {{{"value", "1+4+6"}}}}}}}},
				 {"q_p1_titel",
				  {{"textAnswers",
				    {{"answers",
				      {{{"value", "Nun jauchzt dem Herren, aller Welt"}}}}}}}},
				 {"q_unbekannt",
				  {{"textAnswers", {{"answers", {{{"value", "Ignorieren"}}}}}}}},
			 }},
		};

		const Record rec = api_response_to_record(response, title_map);
		const bool ok = (rec.at("zeitstempel") == "2026-10-20T09:15:32Z") &&
				(rec.at("woche") == "19.10. - 25.10.2026") &&
				(rec.at("tag") == "Sonntag") &&
				(rec.at("organist") == "E. Stolper") &&
				(rec.at("1.gl") == "144") &&
				(rec.at("1.verse") == "1+4+6") &&
				(rec.at("1.titel") == "Nun jauchzt dem Herren, aller Welt") &&
				(rec.find("q_unbekannt") == rec.end()) && (rec.size() == 7);
		check(ok, "11. API-Fixture (offline): Mapping + q_unbekannt ignoriert");
	}

	// 12. API-Multi: zwei textAnswers.answers[]-Werte -> joined "a, b"
	{
		const nlohmann::json title_map = {
			{"q_p1_verse", "1. Eröffnung \xE2\x80\x93 Verse"},
		};
		const nlohmann::json response = {
			{"lastSubmittedTime", "2026-10-20T09:15:32Z"},
			{"answers",
			 {
				 {"q_p1_verse",
				  {{"textAnswers",
				    {{"answers", {{{"value", "a"}}, {{"value", "b"}}}}}}}},
			 }},
		};

		const Record rec = api_response_to_record(response, title_map);
		const bool ok = (rec.find("1.verse") != rec.end()) && (rec.at("1.verse") == "a, b");
		check(ok, "12. API-Multi: zwei textAnswers.answers[]-Werte -> joined \"a, b\"");
	}

	const std::filesystem::path temp_dir =
		std::filesystem::temp_directory_path() / "liedplan_m4_import_tests";
	{
		std::error_code ec;
		std::filesystem::remove_all(temp_dir, ec);
		std::filesystem::create_directories(temp_dir, ec);
	}

	// Referenz-Ergebnis aus antwort_comma.csv laden
	LiedplanImportResult ref_csv_res;
	std::string ref_csv_bytes;
	{
		const std::filesystem::path csv_path =
			std::filesystem::u8path(std::string(kSamplesDir) + "antwort_comma.csv");
		std::ifstream in(csv_path, std::ios::binary);
		std::ostringstream ss;
		ss << in.rdbuf();
		ref_csv_bytes = ss.str();
		parse_csv_file(csv_path, ref_csv_res);
	}

	// 13. (M4 #1) XLSX: parse_xlsx_file auf antwort.xlsx == antwort_comma.csv
	{
		const std::filesystem::path xlsx_path =
			std::filesystem::u8path(std::string(kSamplesDir) + "antwort.xlsx");
		LiedplanImportResult xlsx_res;
		std::size_t count = 0;
		std::string err_msg;
		const ImportError err = parse_xlsx_file(xlsx_path, xlsx_res, &count, &err_msg);
		const bool ok = (err == ImportError::None) && (count == 1) &&
				(xlsx_res == ref_csv_res) && (xlsx_res.punkte.size() == 12) &&
				(xlsx_res.punkte[0].gl == "144") &&
				(xlsx_res.punkte[0].verse == "1+4+6") &&
				(xlsx_res.punkte[0].titel == "Nun jauchzt dem Herren, aller Welt") &&
				(xlsx_res.punkte[4].gl == "175") &&
				(xlsx_res.punkte[4].titel == "Halleluja!");
		check(ok, "13. XLSX: antwort.xlsx == antwort_comma.csv (inkl. P1 & P5 Halleluja)",
		      err_msg);
	}

	// 14. (M4 #2) ZIP-Deflate: antwort.zip (Deflate) -> parse_zip_file == antwort_comma.csv
	{
		const std::string zip_deflate_bytes = build_zip_archive(
			{{"._ignored.csv", "muss ignoriert werden"},
			 {"__ignored.csv", "muss ebenfalls ignoriert werden"},
			 {"antwort.csv", ref_csv_bytes}},
			true);
		const std::filesystem::path zip_path = temp_dir / "antwort.zip";
		{
			std::ofstream out(zip_path, std::ios::binary | std::ios::trunc);
			out.write(zip_deflate_bytes.data(),
				  static_cast<std::streamsize>(zip_deflate_bytes.size()));
		}
		LiedplanImportResult zip_res;
		std::size_t count = 0;
		std::string err_msg;
		const ImportError err = parse_zip_file(zip_path, zip_res, &count, &err_msg);
		const bool ok = (err == ImportError::None) && (count == 1) &&
				(zip_res == ref_csv_res);
		check(ok, "14. ZIP-Deflate: antwort.zip == antwort_comma.csv", err_msg);
	}

	// 15. (M4 #3) ZIP-Store: antwort_store.zip (Store/unkomprimiert) -> parse_zip_file == antwort_comma.csv
	{
		const std::string zip_store_bytes =
			build_zip_archive({{"antwort.csv", ref_csv_bytes}}, false);
		const std::filesystem::path zip_path = temp_dir / "antwort_store.zip";
		{
			std::ofstream out(zip_path, std::ios::binary | std::ios::trunc);
			out.write(zip_store_bytes.data(),
				  static_cast<std::streamsize>(zip_store_bytes.size()));
		}
		LiedplanImportResult zip_res;
		std::size_t count = 0;
		std::string err_msg;
		const ImportError err = parse_zip_file(zip_path, zip_res, &count, &err_msg);
		const bool ok = (err == ImportError::None) && (count == 1) &&
				(zip_res == ref_csv_res);
		check(ok, "15. ZIP-Store: unkomprimiertes ZIP == antwort_comma.csv", err_msg);
	}

	// 16. (M4 #4) ZIP ohne CSV: ZIP mit nur foo.txt -> ImportError::Format
	{
		const std::string zip_no_csv =
			build_zip_archive({{"foo.txt", "Kein CSV hier"}}, true);
		LiedplanImportResult res;
		std::string err_msg;
		const ImportError err = parse_zip_bytes(zip_no_csv, res, nullptr, &err_msg);
		const bool ok = (err == ImportError::Format) && !err_msg.empty();
		check(ok, "16. ZIP ohne CSV liefert ImportError::Format", err_msg);
	}

	// 17. (M4 #5) XLSX leer (nur Header): -> ImportError::NoResponses
	{
		const std::string sheet_only_header =
			"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
			"<worksheet><sheetData>"
			"<row r=\"1\">"
			"<c r=\"A1\" t=\"inlineStr\"><is><t>Woche</t></is></c>"
			"<c r=\"B1\" t=\"inlineStr\"><is><t>1. Eröffnung – GL-Nummer</t></is></c>"
			"</row>"
			"</sheetData></worksheet>";
		const std::string xlsx_header_only = build_zip_archive(
			{{"xl/worksheets/sheet1.xml", sheet_only_header}}, true);
		LiedplanImportResult res;
		std::string err_msg;
		const ImportError err =
			parse_xlsx_bytes(xlsx_header_only, res, nullptr, &err_msg);
		const bool ok = (err == ImportError::NoResponses) && !err_msg.empty();
		check(ok, "17. XLSX leer (nur Header) liefert ImportError::NoResponses", err_msg);
	}

	// 18. (M4 #6) Falsche Endung: parse_answers-Dispatch auf .txt -> ImportError::Format
	{
		LiedplanImportResult res;
		std::string err_msg;
		const ImportError err1 =
			parse_answers("beliebiger inhalt", "notizen.txt", res, nullptr, &err_msg);
		std::string err_msg2;
		const ImportError err2 = parse_answers_file(
			temp_dir / "notizen.txt", res, nullptr, &err_msg2);
		const bool ok = (err1 == ImportError::Format) && !err_msg.empty() &&
				(err2 == ImportError::Format) && !err_msg2.empty();
		check(ok, "18. Falsche Endung (.txt) bei parse_answers liefert ImportError::Format",
		      err_msg);
	}

	// 19. (M4 #7) Shared-Strings-Entity: hand-konstruiertes XLSX mit sharedStrings.xml (&amp;, &#x41;, &lt;, &gt;, &quot;, &apos;)
	{
		const std::string sst_xml =
			"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
			"<sst count=\"4\" uniqueCount=\"4\">"
			"<si><t>Woche</t></si>"
			"<si><t>1. Er&#246;ffnung &#8211; GL-Nummer</t></si>"
			"<si><t>1. Er&#246;ffnung &#8211; Liedtitel</t></si>"
			"<si><t xml:space=\"preserve\">Lob &amp; &#x41;lleluja &lt;Choral&gt; &quot;1&quot; &apos;A&apos;</t></si>"
			"<si><t>KW 42 &amp; &#x42;</t></si>"
			"</sst>";
		const std::string sheet_xml =
			"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
			"<worksheet><sheetData>"
			"<row r=\"1\">"
			"<c r=\"A1\" t=\"s\"><v>0</v></c>"
			"<c r=\"B1\" t=\"s\"><v>1</v></c>"
			"<c r=\"C1\" t=\"s\"><v>2</v></c>"
			"</row>"
			"<row r=\"2\">"
			"<c r=\"A2\" t=\"s\"><v>4</v></c>"
			"<c r=\"B2\"><v>144</v></c>"
			"<c r=\"C2\" t=\"s\"><v>3</v></c>"
			"</row>"
			"</sheetData></worksheet>";
		const std::string xlsx_bytes = build_zip_archive(
			{{"xl/sharedStrings.xml", sst_xml},
			 {"xl/worksheets/sheet1.xml", sheet_xml}},
			true);
		LiedplanImportResult res;
		std::string err_msg;
		const ImportError err = parse_xlsx_bytes(xlsx_bytes, res, nullptr, &err_msg);
		const bool ok =
			(err == ImportError::None) && (res.woche == "KW 42 & B") &&
			(res.punkte.size() == 12) && (res.punkte[0].gl == "144") &&
			(res.punkte[0].titel == "Lob & Alleluja <Choral> \"1\" 'A'");
		check(ok, "19. Shared-Strings-Entity (&amp;, &#x41;, &lt;, &gt;, &quot;, &apos;) in XLSX korrekt dekodiert",
		      err_msg);
	}

	// 20. Zip-Bomb-Cap (#2): ~100 MB Null-Bytes mit compress2 komprimieren -> inflate_data liefert std::nullopt
	{
		constexpr std::size_t kBombSize = 100ULL * 1024 * 1024;
		std::string raw_zeros(kBombSize, '\0');
		uLongf comp_len = compressBound(static_cast<uLong>(kBombSize));
		std::string compressed(static_cast<std::size_t>(comp_len), '\0');
		const int zrc = compress2(reinterpret_cast<Bytef *>(compressed.data()), &comp_len,
					  reinterpret_cast<const Bytef *>(raw_zeros.data()),
					  static_cast<uLong>(kBombSize), Z_BEST_COMPRESSION);
		compressed.resize(static_cast<std::size_t>(comp_len));
		std::optional<std::string> inflated =
			inflate_data(compressed, kBombSize, MAX_WBITS);
		const bool ok = (zrc == Z_OK) && !inflated.has_value();
		check(ok, "20. Zip-Bomb-Cap: ~100 MB Null-Bytes via compress2 -> inflate_data == std::nullopt");
	}

	// 21. Mehrere Gottesdienste (#1): CSV und ZIP mit 2 Services liefern services.size()==2, newest first, service == neuester
	{
		const std::string multi_csv =
			"Zeitstempel,Woche,Tag,Organist,1. Eröffnung – GL-Nummer,1. Eröffnung – Liedtitel\n"
			"19.10.2026 18:00:00,KW 42,Samstag,A. Organist,140,Vorabend-Lied\n"
			"20.10.2026 09:15:32,KW 42,Sonntag,E. Stolper,144,Sonntags-Lied\n";

		ImportResult csv_res;
		std::size_t csv_count = 0;
		std::string csv_err;
		const ImportError err1 = parse_csv_string(multi_csv, csv_res, &csv_count, &csv_err);

		const std::string zip_bytes =
			build_zip_archive({{"antworten.csv", multi_csv}}, true);
		ImportResult zip_res;
		std::size_t zip_count = 0;
		std::string zip_err;
		const ImportError err2 = parse_zip_bytes(zip_bytes, zip_res, &zip_count, &zip_err);

		const bool ok =
			(err1 == ImportError::None) && (csv_count == 2) &&
			(csv_res.services.size() == 2) &&
			(csv_res.services[0].zeitstempel == "20.10.2026 09:15:32") &&
			(csv_res.services[0].tag == "Sonntag") &&
			(csv_res.services[0].punkte[0].gl == "144") &&
			(csv_res.services[1].zeitstempel == "19.10.2026 18:00:00") &&
			(csv_res.services[1].tag == "Samstag") &&
			(csv_res.services[1].punkte[0].gl == "140") &&
			(csv_res.service == csv_res.services[0]) &&
			(csv_res.tag == "Sonntag") &&
			(err2 == ImportError::None) && (zip_count == 2) &&
			(zip_res.services.size() == 2) &&
			(zip_res.services[0] == csv_res.services[0]) &&
			(zip_res.services[1] == csv_res.services[1]) &&
			(zip_res.service == zip_res.services[0]);
		check(ok, "21. Mehrere Gottesdienste: CSV und ZIP mit 2 Services liefern services.size()==2, newest first, service == neuester",
		      csv_err + " " + zip_err);
	}

	// 22. (IMP-X4) CSV mit generischen Spalten + Kirchen-Schema befüllt fields in Quellreihenfolge (ohne Punkt-Spalten / leere Werte)
	{
		const std::string csv_gen =
			"Zeitstempel,Woche,Tag,Organist,Raum,Technik,LeerSpalte,1. Eröffnung – GL-Nummer,1. Eröffnung – Liedtitel\n"
			"20.10.2026 09:15:32,KW 42,Sonntag,E. Stolper,Halle A,Licht,,144,Sonntags-Lied\n";
		ImportResult gen_res;
		std::size_t gen_count = 0;
		std::string gen_err;
		const ImportError err = parse_csv_string(csv_gen, gen_res, &gen_count, &gen_err);
		const std::vector<std::pair<std::string, std::string>> expected_fields = {
			{"Zeitstempel", "20.10.2026 09:15:32"},
			{"Woche", "KW 42"},
			{"Tag", "Sonntag"},
			{"Organist", "E. Stolper"},
			{"Raum", "Halle A"},
			{"Technik", "Licht"},
		};
		const bool ok = (err == ImportError::None) && (gen_count == 1) &&
				(gen_res.service.fields == expected_fields) &&
				(gen_res.services.size() == 1) &&
				(gen_res.services[0].fields == expected_fields);
		check(ok, "22. IMP-X4 CSV fields: Quellreihenfolge inkl. generischer Spalten, ohne Punkt-/Leerspalten", gen_err);
	}

	// 23. (IMP-X4) API-Response fields: Zeitstempel + Fragetitel inkl. generischer Frage, ohne Punkt-Fragen
	{
		const nlohmann::json title_map = {
			{"q_woche", "Woche"},
			{"q_tag", "Tag"},
			{"q_organist", "Organist"},
			{"q_p1_gl", "1. Eröffnung \xE2\x80\x93 GL-Nummer"},
			{"q_unbekannt", "Sonstige Bemerkungen"},
		};
		const nlohmann::json response = {
			{"lastSubmittedTime", "2026-10-20T09:15:32Z"},
			{"answers",
			 {
				 {"q_woche", {{"textAnswers", {{"answers", {{{"value", "KW 42"}}}}}}}},
				 {"q_tag", {{"textAnswers", {{"answers", {{{"value", "Sonntag"}}}}}}}},
				 {"q_organist", {{"textAnswers", {{"answers", {{{"value", "E. Stolper"}}}}}}}},
				 {"q_p1_gl", {{"textAnswers", {{"answers", {{{"value", "144"}}}}}}}},
				 {"q_unbekannt", {{"textAnswers", {{"answers", {{{"value", "Wichtig"}}}}}}}},
			 }},
		};
		const Record rec = api_response_to_record(response, title_map);
		const LiedplanService svc = to_service(rec);
		bool has_zs = false, has_bem = false, has_p1 = false;
		for (const auto &kv : svc.fields) {
			if (kv.first == "Zeitstempel" && kv.second == "2026-10-20T09:15:32Z")
				has_zs = true;
			if (kv.first == "Sonstige Bemerkungen" && kv.second == "Wichtig")
				has_bem = true;
			if (kv.first.find("Eröffnung") != std::string::npos)
				has_p1 = true;
		}
		const bool ok = (svc.fields.size() == 5) && has_zs && has_bem && !has_p1;
		check(ok, "23. IMP-X4 API fields: Zeitstempel + Fragetitel (inkl. generisch), ohne Punkt-Fragen");
	}

	// 24. (IMP-X4/X2) fields in XLSX & ZIP + import_from_api mit leerer form_id liefert Pflichthinweis
	{
		const std::filesystem::path xlsx_path =
			std::filesystem::u8path(std::string(kSamplesDir) + "antwort.xlsx");
		ImportResult xlsx_res;
		const ImportError xerr = parse_xlsx_file(xlsx_path, xlsx_res);
		const std::string zip_bytes =
			build_zip_archive({{"antwort.csv", ref_csv_bytes}}, true);
		ImportResult zip_res;
		const ImportError zerr = parse_zip_bytes(zip_bytes, zip_res);

		std::string api_err;
		const auto api_opt = import_from_api(&api_err, temp_dir, temp_dir, "");

		const bool ok = (xerr == ImportError::None) && !xlsx_res.service.fields.empty() &&
				(xlsx_res.service.fields == ref_csv_res.service.fields) &&
				(zerr == ImportError::None) &&
				(zip_res.service.fields == ref_csv_res.service.fields) &&
				!api_opt.has_value() &&
				(api_err == "Bitte zuerst eine FORM-ID eintragen");
		check(ok, "24. IMP-X4/X2 fields in XLSX/ZIP identisch + leere FORM-ID Fehlermeldung", api_err);
	}

	{
		std::error_code ec;
		std::filesystem::remove_all(temp_dir, ec);
	}

	if (g_failures > 0) {
		std::cerr << g_failures << " test(s) failed.\n";
		return 1;
	}
	std::cout << "All 24 checks passed.\n";
	return 0;
}
