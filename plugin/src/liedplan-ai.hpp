/*
 * liedplan-ai.hpp — LLM-gestützte und deterministische Liedplan-Extraktion (v3).
 *
 * Zweck:
 *   - Definiert AiPoint und AiResult zur Repräsentation strukturierter Liedplan-Daten.
 *   - Deklariert Methoden für Dokument-Textextraktion (via markitdown Python-Subprozess).
 *   - Deklariert OpenRouter-API-Kommunikation und kostenlose Modell-Erkennung (:free).
 *   - Deklariert deterministischen Offline-Fallback-Parser und Gotteslob-Titel-Anreicherung (lieder.md).
 *
 * Hinweis: Qt- und libobs-frei; wird auch in Standalone-Tests (test-liedplan-ai) verwendet.
 */

#pragma once

#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

namespace liedplan_ai {

/** Ein einzelner erkannter Programmpunkt mit bis zu 3 Textzeilen. */
struct AiPoint {
	std::string zeile1;
	std::string zeile2;
	std::string zeile3;

	bool operator==(const AiPoint &other) const
	{
		return zeile1 == other.zeile1 && zeile2 == other.zeile2 && zeile3 == other.zeile3;
	}
};

struct AiResult {
	std::vector<AiPoint> items;
	std::string model_label; // z. B. "google/gemma-…", "Offline-Fallback"
};

constexpr const char *kDefaultSystemPrompt =
	"Du bist ein Parser für Gottesdienstprogramme (Liedpläne).\n"
	"Du erhältst den Text eines Liedplans und erzeugst daraus die Programmpunkte der Feier, in Dokumentreihenfolge.\n"
	"\n"
	"Regeln:\n"
	"- Ignoriere Kopfzeilen: Ort/Titel (z. B. \"Liedplan …\"), Datum/Woche, Tagesangaben (Dienstag/Donnerstag/Sonntag), \"Organist …\", Personen-Namen.\n"
	"- Ein Programmpunkt ist z. B.: Eröffnung/Einzug, Kyrie, Gloria, Credo, Halleluja, Zwischengesang, Gabenbereitung, Heilig, Agnus Dei, Kommunion, Lesung, Predigt, Gebet, Danklied, Schlusslied, Segen.\n"
	"- Pro Programmpunkt genau drei Felder:\n"
	"  zeile1 = Bezeichnung des Programmpunkts (z. B. \"Eröffnung\", \"Kyrie\", \"Schlusslied\").\n"
	"  zeile2 = Gotteslob-Nummer mit Versen, exakt wie im Text (z. B. \"144, 1+4+6\"). Keine Nummer => \"\".\n"
	"  zeile3 = Liedtitel, falls vorhanden (z. B. \"Nun jauchzt dem Herren, aller Welt\"). Kein Lied (z. B. nur Orgel) => \"\".\n"
	"- \"Kommunion Orgel\" => zeile1=\"Kommunion\", zeile2=\"Orgel\", zeile3=\"\".\n"
	"- Zerlege Programmpunkt, Nummer und Titel auch dann korrekt, wenn sie auf einer Zeile stehen.\n"
	"\n"
	"Gib AUSSCHLIESSLICH gültiges JSON zurück, ohne Markdown und ohne zusätzlichen Text:\n"
	"{\"items\":[{\"zeile1\":\"...\",\"zeile2\":\"...\",\"zeile3\":\"...\"}]}";

constexpr const char *kModelPriorities[] = {
	"gemma-4-31b",           "gemma-4-26b", "nemotron-3-super-120b",
	"llama-3.3-70b",         "llama-4",     "deepseek",
	"qwen-2.5",              "qwen3",       "mistral",
	"gemini",                "phi-4",
};

std::string read_env_key(const std::string &env_file_path, std::string &err);

std::string extract_document_text(const std::string &file_path, const std::string &python_exe,
				  std::string &err);

nlohmann::json parse_json_clean(const std::string &raw);

std::vector<std::string> discover_free_models(const nlohmann::json &models_json);

AiResult process_with_llm(const std::string &api_key, const std::string &document_text,
			  const std::string &system_prompt, const std::string &lieder_path,
			  std::function<void(const std::string &)> on_progress = nullptr);

std::map<std::string, std::pair<std::string, std::string>>
parse_song_reference(const std::string &md);

void enrich_titles_from_reference(
	std::vector<AiPoint> &items,
	const std::map<std::string, std::pair<std::string, std::string>> &ref);

std::vector<AiPoint> parse_liedplan_locally(const std::string &text);

} // namespace liedplan_ai
